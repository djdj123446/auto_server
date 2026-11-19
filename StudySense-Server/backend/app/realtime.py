"""实时状态中心：保存设备最新状态，转发给 WebSocket 客户端，写入数据库"""
import asyncio
import logging
import time
from datetime import datetime, timedelta

from fastapi import WebSocket

from app.config import settings
from app.database import SessionLocal
from app.models import SensorData, Threshold

log = logging.getLogger("realtime")

# 设备实时状态（内存中，重启后由下一次上报刷新）
device_state: dict = {
    "online": False,
    "last_seen": None,
    "data": None,        # 最新传感器数据
    "threshold": None,   # 最新阈值
}

# 已连接的 WebSocket 客户端
ws_clients: set[WebSocket] = set()


async def broadcast(message: dict):
    """把消息推给所有在线网页，并清理已断开的连接"""
    dead = []
    for ws in list(ws_clients):
        try:
            await ws.send_json(message)
        except Exception:
            dead.append(ws)
    for ws in dead:
        ws_clients.discard(ws)


async def on_state(payload: dict):
    """收到 studysense/+/state/up"""
    # 打上服务器接收时刻（毫秒）：focus_ms 是设备生成上报时的计时值，
    # 前端用它做插值锚点可补偿 MQTT->WS 的传输延迟，消除 1~2 秒计时差
    payload["_ts"] = int(time.time() * 1000)

    device_state["online"] = True
    device_state["last_seen"] = datetime.now()
    device_state["data"] = payload

    # 写入数据库
    async with SessionLocal() as session:
        session.add(SensorData(
            ts=datetime.now(),
            temperature=payload.get("temperature", 0) or 0,
            humidity=payload.get("humidity", 0) or 0,
            light=int(payload.get("light", 0) or 0),
            people=1 if payload.get("people") else 0,
            led=int(payload.get("led", 0) or 0),
            fan=int(payload.get("fan", 0) or 0),
            mode=int(payload.get("mode", 1) if payload.get("mode") is not None else 1),
            focus=int(payload.get("focus", 0) or 0),
        ))
        await session.commit()

    # 推送给网页
    await broadcast({"type": "state", "data": payload})

    # ===== 扩展服务：专注会话采集 / 自动化规则 =====
    from app import focus_service, rule_engine
    await focus_service.on_state(payload)
    await rule_engine.evaluate(payload)


async def on_command_applied(cmd: str, value: int):
    """指令已成功下发 MQTT 后立即调用：把目标值合并进内存最新状态并广播。

    让网页端毫秒级看到开关切换，不必等设备下一轮 10 秒上报；
    设备执行后的真实上报会随后覆盖该值。只改内存，不写库、不触发规则引擎。"""
    if cmd not in ("led", "fan", "mode"):
        return
    data = device_state["data"]
    if data is None:
        return
    data[cmd] = value
    # 刷新接收时刻，前端乱序保护以最新时刻为准
    data["_ts"] = int(time.time() * 1000)
    await broadcast({"type": "state", "data": data})


def _finalize_state_on_offline():
    """设备离线后把内存状态收尾：专注视为正常结束，切回手动并清零计时。
    快照 / latest / 轮询从此返回结束态，前端不会残留专注显示。"""
    data = device_state["data"]
    if data is not None:
        data["mode"] = 0
        data["focus"] = 0
        data["focus_ms"] = 0
        data["focus_paused"] = 0
        data["focus_rest"] = 0


async def on_status(payload):
    """收到 studysense/+/status（ESP32 上线 / 遗嘱消息下线）"""
    online = str(payload).lower() in ('online', '"online"', '1', 'true')
    device_state["online"] = online
    device_state["last_seen"] = datetime.now() if online else device_state["last_seen"]
    log.info("设备状态变更: online=%s", online)
    if not online:
        # 设备离线：专注会话直接结束（等同正常结束：落库 + 状态切回手动）
        from app import focus_service
        await focus_service.end_on_offline(end_ts=device_state["last_seen"])
        _finalize_state_on_offline()
    await broadcast({"type": "status", "online": online})


async def on_threshold(payload: dict):
    """收到 studysense/+/threshold/up（ESP32 按键改了阈值，同步到数据库）"""
    device_state["threshold"] = payload
    async with SessionLocal() as session:
        from sqlalchemy import select
        row = (await session.execute(select(Threshold).where(Threshold.id == 1))).scalar_one_or_none()
        if row:
            row.content = payload
            row.updated_at = datetime.now()
        else:
            session.add(Threshold(id=1, content=payload, updated_at=datetime.now()))
        await session.commit()

    await broadcast({"type": "threshold", "data": payload})


async def on_ack(payload: dict):
    """收到 studysense/+/cmd/ack（ESP32 执行指令的回执）"""
    log.info("指令回执: %s", payload)
    await broadcast({"type": "ack", "data": payload})

    # 审计闭环：回执核销对应的下发记录
    from app import audit_service
    await audit_service.on_ack(payload)


async def warm_up():
    """启动时从数据库恢复最近状态：设备离线/后端重启时，网页仍显示最后一次数据"""
    from sqlalchemy import select
    async with SessionLocal() as session:
        row = (await session.execute(
            select(SensorData).order_by(SensorData.id.desc()).limit(1)
        )).scalar_one_or_none()
        if row:
            device_state["data"] = {
                "temperature": row.temperature,
                "humidity": row.humidity,
                "light": row.light,
                "people": bool(row.people),
                "led": row.led,
                "fan": row.fan,
                "mode": row.mode,
                "focus": row.focus or 0,
            }
            device_state["last_seen"] = row.ts
        th = (await session.execute(
            select(Threshold).where(Threshold.id == 1)
        )).scalar_one_or_none()
        if th:
            device_state["threshold"] = th.content
    if device_state["data"]:
        log.info("已从数据库恢复最近一次设备数据")


async def offline_checker():
    """定时检查：超时未收到上报则判定离线（兜底，防止错过遗嘱消息）"""
    while True:
        await asyncio.sleep(10)
        last_seen = device_state["last_seen"]
        if last_seen is None:
            continue
        if (datetime.now() - last_seen).total_seconds() > settings.DEVICE_OFFLINE_TIMEOUT:
            if device_state["online"]:
                device_state["online"] = False
                log.warning("设备超时未上报，判定离线")
                # 超时判定离线：同样直接结束专注会话（遗嘱消息丢失时的兜底）
                from app import focus_service
                await focus_service.end_on_offline(end_ts=last_seen)
                _finalize_state_on_offline()
                await broadcast({"type": "status", "online": False})


async def retention_cleanup():
    """定时清理过期数据（每 6 小时一次 + 启动时一次）"""
    while True:
        try:
            async with SessionLocal() as session:
                expire = datetime.now() - timedelta(days=settings.DATA_RETENTION_DAYS)
                await session.execute(SensorData.__table__.delete().where(SensorData.ts < expire))
                await session.commit()
                log.info("已清理过期数据（保留 %d 天）", settings.DATA_RETENTION_DAYS)
        except Exception:
            log.exception("清理任务失败")
        await asyncio.sleep(6 * 3600)


def current_snapshot() -> dict:
    """给前端的一次性完整快照"""
    return {
        "device_id": settings.DEVICE_ID,
        "online": device_state["online"],
        "last_seen": device_state["last_seen"].isoformat() if device_state["last_seen"] else None,
        "data": device_state["data"],
        "threshold": device_state["threshold"],
    }
