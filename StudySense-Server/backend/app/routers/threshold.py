"""阈值：查询 / 网页端修改并下发到 ESP32"""
from datetime import datetime

from fastapi import APIRouter, Depends, HTTPException
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.database import get_db
from app.mqtt_service import mqtt_service
from app.models import Threshold
from app.routers.auth import require_admin
from app.schemas import ThresholdBody

router = APIRouter(prefix="/api/threshold", tags=["threshold"], dependencies=[Depends(require_admin)])


@router.get("")
async def get_threshold(db: AsyncSession = Depends(get_db)):
    row = (await db.execute(select(Threshold).where(Threshold.id == 1))).scalar_one_or_none()
    if row:
        return {"content": row.content, "updated_at": str(row.updated_at)}
    return {"content": {}, "updated_at": None}


@router.put("")
async def set_threshold(body: ThresholdBody, db: AsyncSession = Depends(get_db)):
    """保存到数据库并实时下发 ESP32（retain，设备重启也能拿到）"""
    content = body.content

    # 下发
    try:
        await mqtt_service.publish_threshold(content)
    except RuntimeError as e:
        raise HTTPException(503, str(e))

    # 更新数据库（upsert）
    row = (await db.execute(select(Threshold).where(Threshold.id == 1))).scalar_one_or_none()
    if row:
        row.content = content
        row.updated_at = datetime.now()
    else:
        db.add(Threshold(id=1, content=content, updated_at=datetime.now()))
    await db.commit()
    return {"ok": True, "content": content}
