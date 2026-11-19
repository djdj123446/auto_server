"""专注会话采集：从设备上报流中提取专注学习的开始/结束/时长

状态机（由 state/up 的 mode + focus 字段驱动）：
  mode 0/1 -> mode 2      开始会话（start_ts 用 focus 秒数反推，消除上报延迟）
  mode 2   持续           更新时长
  mode 2   -> mode 0/1    结束会话（时长取会话内最后一次非零 focus，设备结束上报可能已归零）

休息提醒次数：focus_rest 字段 0->1 边沿累计。
"""
import logging
from datetime import datetime, timedelta

from sqlalchemy import select

from app.database import SessionLocal
from app.models import FocusSession

log = logging.getLogger("focus")

# 进行中的会话（内存态；后端重启期间设备仍在专注时，会从下一条上报重建，仅损失 start 时刻精度）
_current: dict | None = None     # {"id": int, "duration": int}
_rest_open: bool = False         # 休息提醒弹框当前是否打开（用于边沿计数）


async def on_state(payload: dict):
    """每收到一条 state/up 调用一次"""
    global _current, _rest_open

    mode = payload.get("mode")
    focus = int(payload.get("focus", 0) or 0)
    if mode is None:
        return

    now = datetime.now()

    # ---- 休息提醒边沿计数（仅会话进行中有意义）----
    rest_now = int(payload.get("focus_rest", 0) or 0) == 1
    if _current and rest_now and not _rest_open:
        async with SessionLocal() as session:
            row = await session.get(FocusSession, _current["id"])
            if row:
                row.rest_count = (row.rest_count or 0) + 1
                await session.commit()
    _rest_open = rest_now

    # ---- 会话状态机 ----
    if mode == 2 and _current is None:
        # 开始：focus 秒数反推开始时刻
        start_ts = now - timedelta(seconds=max(0, focus))
        async with SessionLocal() as session:
            obj = FocusSession(
                device_id=payload.get("_device", "esp32_01"),
                start_ts=start_ts,
                end_ts=None,
                duration_sec=focus,
                rest_count=0,
            )
            session.add(obj)
            await session.commit()
            await session.refresh(obj)
            _current = {"id": obj.id, "duration": focus}
        log.info("专注会话开始 #%s start=%s", _current["id"], start_ts)

    elif mode == 2 and _current is not None:
        # 进行中：只增不减（弹框暂停期间设备冻结计时，上报值不变）
        if focus > _current["duration"]:
            _current["duration"] = focus
            async with SessionLocal() as session:
                row = await session.get(FocusSession, _current["id"])
                if row:
                    row.duration_sec = focus
                    await session.commit()

    elif mode != 2 and _current is not None:
        # 结束：时长取会话内记录的最大值（设备结束后的上报 focus 可能已归零）
        duration = max(_current["duration"], 0)
        async with SessionLocal() as session:
            row = await session.get(FocusSession, _current["id"])
            if row:
                row.end_ts = now
                row.duration_sec = duration
                await session.commit()
        log.info("专注会话结束 #%s 时长 %d 秒", _current["id"], duration)
        _current = None


async def end_on_offline(end_ts: datetime | None = None):
    """设备离线：直接结束进行中的专注会话（不再等设备上报 mode 切换）

    时长取会话内最后一次上报值（离线期间设备无法上报，不补计时长）；
    end_ts 用最后一次上报时刻，与会话时长保持一致。
    幂等：没有进行中的会话时直接返回。
    """
    global _current, _rest_open
    if _current is None:
        return
    session_id = _current["id"]
    duration = max(_current["duration"], 0)
    async with SessionLocal() as session:
        row = await session.get(FocusSession, session_id)
        if row:
            row.end_ts = end_ts or datetime.now()
            row.duration_sec = duration
            await session.commit()
    log.info("设备离线，专注会话结束 #%s 时长 %d 秒", session_id, duration)
    _current = None
    _rest_open = False


async def warm_up():
    """后端重启恢复：把库里未结束的会话按最后状态收尾，避免悬挂"""
    global _current
    async with SessionLocal() as session:
        rows = (await session.execute(
            select(FocusSession).where(FocusSession.end_ts.is_(None))
        )).scalars().all()
        now = datetime.now()
        for row in rows:
            row.end_ts = now
            # 重启丢失了实时 focus，按 start 推算一个下限并截断到当前
            if row.duration_sec <= 0 and row.start_ts:
                row.duration_sec = max(0, int((now - row.start_ts).total_seconds()))
        if rows:
            await session.commit()
            log.info("已收尾 %d 条悬挂的专注会话", len(rows))
