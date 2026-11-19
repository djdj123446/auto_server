"""自动化规则 CRUD + 启停"""
from fastapi import APIRouter, Depends, HTTPException
from pydantic import BaseModel, Field
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.database import get_db
from app.models import AutomationRule
from app.routers.auth import require_admin
from app import rule_engine

router = APIRouter(prefix="/api/rules", tags=["rules"], dependencies=[Depends(require_admin)])

METRICS = ("temperature", "humidity", "light", "people")
OPS = ("gt", "lt")
CMDS = ("led", "fan", "mode")


class RuleBody(BaseModel):
    name: str = Field(min_length=1, max_length=64)
    trigger_metric: str
    trigger_op: str
    trigger_value: float
    action_cmd: str
    action_value: int = Field(ge=0, le=3)
    cooldown_sec: int = Field(default=120, ge=10, le=3600)


def _check(body: RuleBody):
    if body.trigger_metric not in METRICS:
        raise HTTPException(400, f"触发指标非法: {body.trigger_metric}")
    if body.trigger_op not in OPS:
        raise HTTPException(400, f"比较符非法: {body.trigger_op}")
    if body.action_cmd not in CMDS:
        raise HTTPException(400, f"动作指令非法: {body.action_cmd}")


def _dump(r: AutomationRule) -> dict:
    return {
        "id": r.id,
        "name": r.name,
        "trigger_metric": r.trigger_metric,
        "trigger_op": r.trigger_op,
        "trigger_value": r.trigger_value,
        "action_cmd": r.action_cmd,
        "action_value": r.action_value,
        "enabled": bool(r.enabled),
        "cooldown_sec": r.cooldown_sec,
        "fire_count": r.fire_count,
        "last_fired_at": r.last_fired_at.isoformat() if r.last_fired_at else None,
        "created_at": r.created_at.isoformat() if r.created_at else None,
    }


@router.get("")
async def list_rules(db: AsyncSession = Depends(get_db)):
    rows = (await db.execute(
        select(AutomationRule).order_by(AutomationRule.id.desc())
    )).scalars().all()
    return {"rules": [_dump(r) for r in rows]}


@router.post("")
async def create_rule(body: RuleBody, db: AsyncSession = Depends(get_db)):
    _check(body)
    rule = AutomationRule(
        name=body.name,
        trigger_metric=body.trigger_metric,
        trigger_op=body.trigger_op,
        trigger_value=body.trigger_value,
        action_cmd=body.action_cmd,
        action_value=body.action_value,
        cooldown_sec=body.cooldown_sec,
        enabled=1,
    )
    db.add(rule)
    await db.commit()
    await db.refresh(rule)
    rule_engine.invalidate()
    return _dump(rule)


@router.put("/{rule_id}")
async def update_rule(rule_id: int, body: RuleBody, db: AsyncSession = Depends(get_db)):
    _check(body)
    rule = await db.get(AutomationRule, rule_id)
    if not rule:
        raise HTTPException(404, "规则不存在")
    rule.name = body.name
    rule.trigger_metric = body.trigger_metric
    rule.trigger_op = body.trigger_op
    rule.trigger_value = body.trigger_value
    rule.action_cmd = body.action_cmd
    rule.action_value = body.action_value
    rule.cooldown_sec = body.cooldown_sec
    # 条件变了，边沿状态重置，避免新旧条件混淆
    rule.last_satisfied = 0
    await db.commit()
    rule_engine.invalidate()
    return _dump(rule)


@router.post("/{rule_id}/toggle")
async def toggle_rule(rule_id: int, db: AsyncSession = Depends(get_db)):
    rule = await db.get(AutomationRule, rule_id)
    if not rule:
        raise HTTPException(404, "规则不存在")
    rule.enabled = 0 if rule.enabled else 1
    rule.last_satisfied = 0
    await db.commit()
    rule_engine.invalidate()
    return _dump(rule)


@router.delete("/{rule_id}")
async def delete_rule(rule_id: int, db: AsyncSession = Depends(get_db)):
    rule = await db.get(AutomationRule, rule_id)
    if not rule:
        raise HTTPException(404, "规则不存在")
    await db.delete(rule)
    await db.commit()
    rule_engine.invalidate()
    return {"ok": True}
