"""操作审计：记录云端指令下发 + 用设备 ack 回执闭环结果

下发指令时写一条 result=pending 的审计记录，收到 ack 后按
echo 内容匹配回填 ok / fail。同一条指令连续下发多次时按队列
顺序逐条核销（先进先出）。
"""
import logging
from datetime import datetime

from app.database import SessionLocal
from app.models import AuditLog

log = logging.getLogger("audit")

# cmd_key -> [audit_id, ...] 等待 ack 核销（内存态，重启丢失仅影响未核销记录的状态显示）
_pending: dict[str, list[int]] = {}


def _key(cmd: str, value) -> str:
    return f"{cmd}:{value if value is not None else ''}"


async def log_command(source: str, operator: str, cmd: str, value, detail: str = "") -> int:
    """记录一次指令下发，返回审计 ID"""
    async with SessionLocal() as session:
        row = AuditLog(
            ts=datetime.now(),
            source=source,
            operator=operator,
            cmd=cmd,
            value=value,
            result="pending",
            detail=detail,
        )
        session.add(row)
        await session.commit()
        await session.refresh(row)
        _pending.setdefault(_key(cmd, value), []).append(row.id)
        return row.id


async def on_ack(payload: dict):
    """收到设备回执 {"echo": {"cmd":..., "value":...}, "ok": true}，核销对应审计记录"""
    echo = payload.get("echo") or {}
    cmd = str(echo.get("cmd", ""))
    value = echo.get("value")
    ok = bool(payload.get("ok"))

    ids = _pending.get(_key(cmd, value))
    if not ids:
        return
    audit_id = ids.pop(0)
    if not ids:
        _pending.pop(_key(cmd, value), None)

    async with SessionLocal() as session:
        row = await session.get(AuditLog, audit_id)
        if row:
            row.result = "ok" if ok else "fail"
            await session.commit()
            log.info("审计 #%s 核销: %s %s -> %s", audit_id, cmd, value, row.result)
