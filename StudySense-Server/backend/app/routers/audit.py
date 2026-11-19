"""操作审计查询"""
from fastapi import APIRouter, Depends, Query
from sqlalchemy import func, select
from sqlalchemy.ext.asyncio import AsyncSession

from app.database import get_db
from app.models import AuditLog
from app.routers.auth import require_admin

router = APIRouter(prefix="/api/audit", tags=["audit"], dependencies=[Depends(require_admin)])


@router.get("")
async def list_audit(
    cmd: str | None = Query(default=None, description="按指令筛选，如 led/fan/mode"),
    source: str | None = Query(default=None, description="按来源筛选 web/rule"),
    result: str | None = Query(default=None, description="按结果筛选 pending/ok/fail"),
    page: int = Query(default=1, ge=1),
    page_size: int = Query(default=15, ge=1, le=100),
    db: AsyncSession = Depends(get_db),
):
    q = select(AuditLog)
    if cmd:
        q = q.where(AuditLog.cmd == cmd)
    if source:
        q = q.where(AuditLog.source == source)
    if result:
        q = q.where(AuditLog.result == result)

    total = (await db.execute(select(func.count()).select_from(q.subquery()))).scalar() or 0
    rows = (await db.execute(
        q.order_by(AuditLog.ts.desc()).offset((page - 1) * page_size).limit(page_size)
    )).scalars().all()

    return {
        "total": total,
        "page": page,
        "page_size": page_size,
        "rows": [
            {
                "id": r.id,
                "ts": r.ts.isoformat() if r.ts else None,
                "source": r.source,
                "operator": r.operator,
                "cmd": r.cmd,
                "value": r.value,
                "result": r.result,
                "detail": r.detail,
            }
            for r in rows
        ],
    }
