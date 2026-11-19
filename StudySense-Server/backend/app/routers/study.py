"""学习报告：专注会话的统计分析

数据来源 focus_session 表（由 focus_service 从设备上报流中提取，永久保留，
不受 sensor_data 7 天滚动清理影响）。
"""
from datetime import datetime, timedelta

from fastapi import APIRouter, Depends, Query
from sqlalchemy import text
from sqlalchemy.ext.asyncio import AsyncSession

from app.database import get_db
from app.routers.auth import require_admin

router = APIRouter(prefix="/api/study", tags=["study"], dependencies=[Depends(require_admin)])


@router.get("/summary")
async def summary(days: int = Query(default=7, ge=1, le=365), db: AsyncSession = Depends(get_db)):
    """概览：今日 / 区间专注时长、连续打卡天数、累计总时长"""
    now = datetime.now()
    today_start = now.replace(hour=0, minute=0, second=0, microsecond=0)
    range_start = today_start - timedelta(days=days - 1)

    # 区间内每日时长（含今日），用于计算连续打卡
    sql = text("""
        SELECT DATE(start_ts) AS d, SUM(duration_sec) AS secs, COUNT(*) AS cnt
        FROM focus_session
        WHERE start_ts >= :range_start
        GROUP BY DATE(start_ts)
        ORDER BY d
    """)
    rows = (await db.execute(sql, {"range_start": range_start})).all()
    by_date = {str(r.d): int(r.secs or 0) for r in rows}

    today_secs = by_date.get(str(today_start.date()), 0)
    today_cnt = next((int(r.cnt or 0) for r in rows if str(r.d) == str(today_start.date())), 0)

    # 连续打卡：从今天往前数每天都有会话（今天还没有则从昨天开始数）
    streak = 0
    cursor = today_start if today_secs > 0 else today_start - timedelta(days=1)
    while str(cursor.date()) in by_date and by_date[str(cursor.date())] > 0:
        streak += 1
        cursor -= timedelta(days=1)

    # 累计总时长（全部历史）
    # 注意：列别名不能用 t/c（Row.t 是 SQLAlchemy 2.0 内置属性，会撞名）
    total = (await db.execute(text(
        "SELECT COALESCE(SUM(duration_sec), 0) AS total_secs, COUNT(*) AS total_cnt FROM focus_session"
    ))).one()

    # 区间合计
    range_secs = sum(by_date.values())
    range_cnt = sum(int(r.cnt or 0) for r in rows)

    return {
        "today_seconds": today_secs,
        "today_sessions": today_cnt,
        "range_seconds": range_secs,
        "range_sessions": range_cnt,
        "range_days": days,
        "streak_days": streak,
        "total_seconds": int(total.total_secs or 0),
        "total_sessions": int(total.total_cnt or 0),
    }


@router.get("/daily")
async def daily(days: int = Query(default=30, ge=1, le=371), db: AsyncSession = Depends(get_db)):
    """每日专注时长（柱状图）：补齐无学习日为 0

    上限 371 天：前端年度贡献图需要 53 个完整周（52*7 + 本周已过天数），
    以周日为起点对齐。
    """
    today_start = datetime.now().replace(hour=0, minute=0, second=0, microsecond=0)
    range_start = today_start - timedelta(days=days - 1)

    sql = text("""
        SELECT DATE(start_ts) AS d, SUM(duration_sec) AS secs, COUNT(*) AS cnt
        FROM focus_session
        WHERE start_ts >= :range_start
        GROUP BY DATE(start_ts)
    """)
    rows = (await db.execute(sql, {"range_start": range_start})).all()
    by_date = {str(r.d): (int(r.secs or 0), int(r.cnt or 0)) for r in rows}

    result = []
    for i in range(days):
        d = (today_start - timedelta(days=days - 1 - i)).date()
        secs, cnt = by_date.get(str(d), (0, 0))
        result.append({"date": str(d), "seconds": secs, "sessions": cnt})
    return {"days": result}


@router.get("/hours")
async def hours(days: int = Query(default=30, ge=1, le=365), db: AsyncSession = Depends(get_db)):
    """24 小时时段分布：找出一天中最容易专注的时段

    会话可能跨小时，按会话覆盖的时间段把时长切分到各小时桶。
    """
    start = datetime.now().replace(hour=0, minute=0, second=0, microsecond=0) - timedelta(days=days - 1)
    sql = text("""
        SELECT start_ts, COALESCE(end_ts, start_ts + INTERVAL duration_sec SECOND) AS end_ts,
               duration_sec
        FROM focus_session
        WHERE start_ts >= :start
    """)
    rows = (await db.execute(sql, {"start": start})).all()

    buckets = [0] * 24
    for r in rows:
        # duration_sec 是权威值；end_ts 供跨小时切分参考
        cur = r.start_ts
        remaining = int(r.duration_sec or 0)
        while remaining > 0 and cur is not None:
            hour_end = (cur.replace(minute=0, second=0, microsecond=0)) + timedelta(hours=1)
            span = min(remaining, int((hour_end - cur).total_seconds()))
            if span <= 0:  # 会话恰好跨越整点时兜底
                span = remaining
                hour_end = cur + timedelta(seconds=remaining)
            buckets[cur.hour] += span
            remaining -= span
            cur = hour_end

    return {
        "hours": [
            {"hour": h, "seconds": buckets[h], "label": f"{h:02d}:00-{(h + 1) % 24:02d}:00"}
            for h in range(24)
        ]
    }


@router.get("/sessions")
async def sessions(
    page: int = Query(default=1, ge=1),
    page_size: int = Query(default=15, ge=1, le=100),
    db: AsyncSession = Depends(get_db),
):
    """专注会话记录（分页，最新的在前）"""
    from sqlalchemy import func, select
    from app.models import FocusSession

    total = (await db.execute(select(func.count(FocusSession.id)))).scalar() or 0
    rows = (await db.execute(
        select(FocusSession)
        .order_by(FocusSession.start_ts.desc())
        .offset((page - 1) * page_size)
        .limit(page_size)
    )).scalars().all()

    return {
        "total": total,
        "page": page,
        "page_size": page_size,
        "rows": [
            {
                "id": r.id,
                "device_id": r.device_id,
                "start_ts": r.start_ts.isoformat() if r.start_ts else None,
                "end_ts": r.end_ts.isoformat() if r.end_ts else None,
                "duration_sec": r.duration_sec,
                "rest_count": r.rest_count,
                "ongoing": r.end_ts is None,
            }
            for r in rows
        ],
    }
