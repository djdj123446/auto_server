"""数据查询：最新状态 / 历史曲线 / 统计"""
from datetime import datetime, timedelta

from fastapi import APIRouter, Depends, Query
from sqlalchemy import text
from sqlalchemy.ext.asyncio import AsyncSession

from app.database import get_db
from app.realtime import current_snapshot
from app.routers.auth import require_admin

router = APIRouter(prefix="/api/data", tags=["data"], dependencies=[Depends(require_admin)])


@router.get("/latest")
async def latest():
    """当前最新状态（含在线状态）"""
    return current_snapshot()


@router.get("/history")
async def history(
    start: datetime = Query(default=None),
    end: datetime = Query(default=None),
    points: int = Query(default=500, ge=1, le=2000),
    db: AsyncSession = Depends(get_db),
):
    """历史曲线：按时间桶聚合（自动降采样到 points 个点）"""
    if end is None:
        end = datetime.now()
    if start is None:
        start = end - timedelta(days=1)
    total_seconds = max(1.0, (end - start).total_seconds())
    bucket = max(1, int(total_seconds // points))

    sql = text("""
        SELECT
            DATE_FORMAT(FROM_UNIXTIME(b.bucket_ts), '%Y-%m-%d %H:%i:%s') AS ts,
            ROUND(AVG(b.temperature), 1)     AS temperature,
            ROUND(AVG(b.humidity), 1)        AS humidity,
            ROUND(AVG(b.light))              AS light,
            MAX(b.people)                    AS people,
            MAX(b.led)                       AS led,
            MAX(b.fan)                       AS fan,
            MAX(b.mode)                      AS mode,
            MAX(b.focus)                     AS focus
        FROM (
            SELECT FLOOR(UNIX_TIMESTAMP(ts) / :bucket) * :bucket AS bucket_ts,
                   temperature, humidity, light, people, led, fan, mode, focus
            FROM sensor_data
            WHERE ts BETWEEN :start AND :end
        ) b
        GROUP BY b.bucket_ts
        ORDER BY b.bucket_ts
    """)
    result = await db.execute(sql, {"bucket": bucket, "start": start, "end": end})
    rows = [
        {
            "ts": r.ts,
            "temperature": r.temperature,
            "humidity": r.humidity,
            "light": r.light,
            "people": bool(r.people),
            "led": r.led,
            "fan": r.fan,
            "mode": r.mode,
            "focus": r.focus,
        }
        for r in result
    ]
    return {"count": len(rows), "bucket_seconds": bucket, "rows": rows}


@router.get("/stats")
async def stats(
    start: datetime = Query(default=None),
    end: datetime = Query(default=None),
    db: AsyncSession = Depends(get_db),
):
    """区间统计：各指标最大/最小/平均"""
    if end is None:
        end = datetime.now()
    if start is None:
        start = end - timedelta(days=1)
    sql = text("""
        SELECT
            COUNT(*)                            AS samples,
            ROUND(MIN(temperature), 1)          AS temp_min,
            ROUND(MAX(temperature), 1)          AS temp_max,
            ROUND(AVG(temperature), 1)          AS temp_avg,
            ROUND(MIN(humidity), 1)             AS humi_min,
            ROUND(MAX(humidity), 1)             AS humi_max,
            ROUND(AVG(humidity), 1)             AS humi_avg,
            MIN(light)                          AS light_min,
            MAX(light)                          AS light_max,
            ROUND(AVG(light))                   AS light_avg,
            ROUND(SUM(people) / COUNT(*) * 100) AS people_pct
        FROM sensor_data
        WHERE ts BETWEEN :start AND :end
    """)
    r = (await db.execute(sql, {"start": start, "end": end})).one()
    return {
        "samples": r.samples,
        "temperature": {"min": r.temp_min, "max": r.temp_max, "avg": r.temp_avg},
        "humidity": {"min": r.humi_min, "max": r.humi_max, "avg": r.humi_avg},
        "light": {"min": r.light_min, "max": r.light_max, "avg": r.light_avg},
        "people_pct": r.people_pct,
    }
