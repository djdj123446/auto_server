"""自动化规则引擎：条件满足（边沿触发 + 冷却时间）时云端下发控制指令

由 realtime.on_state 在每条传感器上报后调用 evaluate()：
  - 边沿触发：条件从不满足变为满足才执行，避免每 10 秒重复下发
  - 冷却时间：同一条规则两次执行之间的最小间隔（默认 120 秒）
  - 仅设备在线时执行（离线时指令无法送达）
  - 执行动作会写入操作审计（source=rule），在审计页可追溯
"""
import logging
from datetime import datetime

from sqlalchemy import select

from app.database import SessionLocal
from app.models import AutomationRule
from app import realtime, audit_service

log = logging.getLogger("rules")

# 缓存的规则列表（规则 CRUD 时调用 invalidate() 失效）
_rules_cache: list | None = None


def invalidate():
    """规则变更后失效缓存，下次评估重新加载"""
    global _rules_cache
    _rules_cache = None


def _compare(value, op: str, target: float) -> bool:
    try:
        v = float(value)
    except (TypeError, ValueError):
        return False
    return v > target if op == "gt" else v < target


async def evaluate(payload: dict):
    """对每条启用的规则做边沿评估，满足则下发指令"""
    global _rules_cache

    if not realtime.device_state.get("online"):
        return

    if _rules_cache is None:
        async with SessionLocal() as session:
            _rules_cache = (await session.execute(
                select(AutomationRule).where(AutomationRule.enabled == 1)
            )).scalars().all()

    changed = False
    fired = False
    now = datetime.now()

    for rule in _rules_cache:
        value = payload.get(rule.trigger_metric)
        satisfied = _compare(value, rule.trigger_op, rule.trigger_value)

        should_fire = (
            satisfied
            and not rule.last_satisfied          # 边沿：上轮不满足
            and (
                rule.last_fired_at is None
                or (now - rule.last_fired_at).total_seconds() >= rule.cooldown_sec
            )
        )

        if should_fire:
            try:
                from app.mqtt_service import mqtt_service  # 函数内导入避免循环依赖
                await mqtt_service.publish_command(
                    {"cmd": rule.action_cmd, "value": rule.action_value}
                )
                await audit_service.log_command(
                    source="rule", operator=f"rule:{rule.name}",
                    cmd=rule.action_cmd, value=rule.action_value,
                    detail=f"触发条件 {rule.trigger_metric} "
                           f"{'>' if rule.trigger_op == 'gt' else '<'} {rule.trigger_value}",
                )
                rule.fire_count += 1
                rule.last_fired_at = now
                fired = True
                log.info("规则「%s」触发: %s=%s -> %s=%s",
                         rule.name, rule.trigger_metric, value,
                         rule.action_cmd, rule.action_value)
                await realtime.broadcast({
                    "type": "rule_fired",
                    "data": {"rule_id": rule.id, "name": rule.name,
                             "cmd": rule.action_cmd, "value": rule.action_value},
                })
            except Exception:
                log.exception("规则「%s」执行失败", rule.name)

        if satisfied != bool(rule.last_satisfied):
            rule.last_satisfied = 1 if satisfied else 0
            changed = True

    if changed or fired:
        async with SessionLocal() as session:
            for rule in _rules_cache:
                await session.merge(rule)
            await session.commit()
