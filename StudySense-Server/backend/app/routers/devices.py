"""设备管理：注册设备列表 + 实时状态 + 设备详情"""
from datetime import datetime, timedelta

from fastapi import APIRouter, Depends, HTTPException, Query
from sqlalchemy import func, select
from sqlalchemy.ext.asyncio import AsyncSession

from app.config import settings
from app.database import get_db
from app.models import Device, SensorData
from app.realtime import current_snapshot, device_state
from app.routers.auth import require_admin

router = APIRouter(prefix="/api/devices", tags=["devices"], dependencies=[Depends(require_admin)])


@router.get("")
async def list_devices(db: AsyncSession = Depends(get_db)):
    """设备列表（含实时状态；当前仅主设备有完整实时链路，其余设备预留扩展）"""
    rows = (await db.execute(select(Device).order_by(Device.id))).scalars().all()
    snap = current_snapshot()

    devices = []
    for r in rows:
        is_primary = r.device_id == settings.DEVICE_ID
        devices.append({
            "id": r.id,
            "device_id": r.device_id,
            "name": r.name,
            "model": r.model,
            "firmware": r.firmware,
            "location": r.location,
            "note": r.note,
            "created_at": r.created_at.isoformat() if r.created_at else None,
            "last_seen_at": r.last_seen_at.isoformat() if r.last_seen_at else None,
            # 实时状态仅主设备具备（多设备实时链路为预留扩展点）
            "online": snap["online"] if is_primary else False,
            "is_primary": is_primary,
            "latest": snap["data"] if is_primary else None,
        })
    return {"devices": devices}


@router.get("/{device_id}")
async def device_detail(
    device_id: str,
    db: AsyncSession = Depends(get_db),
):
    """设备详情：基本信息 + 24h 样本数 / 在线率 + 今日告警数"""
    r = (await db.execute(
        select(Device).where(Device.device_id == device_id)
    )).scalar_one_or_none()
    if not r:
        raise HTTPException(404, "设备不存在")

    now = datetime.now()
    day_ago = now - timedelta(hours=24)

    # 24h 样本数与有数据的小时数（在线率近似：有上报的小时占比）
    # 注：sensor_data 为单设备时期建表，暂无 device_id 列，样本数按全表统计
    stats = (await db.execute(
        select(
            func.count(SensorData.id),
            func.count(func.distinct(func.hour(SensorData.ts))),
        ).where(SensorData.ts >= day_ago)
    )).one()

    samples = int(stats[0] or 0)
    hours_with_data = min(24, int(stats[1] or 0))
    online_rate = round(hours_with_data / 24 * 100)

    snap = current_snapshot()
    is_primary = device_id == settings.DEVICE_ID

    return {
        "device_id": r.device_id,
        "name": r.name,
        "model": r.model,
        "firmware": r.firmware,
        "location": r.location,
        "note": r.note,
        "created_at": r.created_at.isoformat() if r.created_at else None,
        "online": snap["online"] if is_primary else False,
        "last_seen": snap["last_seen"] if is_primary else None,
        "latest": snap["data"] if is_primary else None,
        "threshold": snap["threshold"] if is_primary else None,
        "samples_24h": samples,
        "online_rate_24h": online_rate,
    }
