"""控制中心：LED / 风扇 / 模式 / 专注交互下发（含操作审计）"""
import time

from fastapi import APIRouter, Depends, HTTPException

from app.mqtt_service import mqtt_service
from app.realtime import device_state
from app.routers.auth import require_admin
from app.schemas import ControlBody, FocusEndBody

router = APIRouter(prefix="/api/control", tags=["control"])

CMD_META = {
    "led": {"desc": "LED", "check": lambda v: v in (0, 1)},
    "fan": {"desc": "风扇", "check": lambda v: 0 <= v <= 3},
    "mode": {"desc": "模式", "check": lambda v: v in (0, 1, 2)},  # 0=手动 1=自动 2=专注
}


async def _publish(command: dict, desc: str, operator: str = "-"):
    """设备离线时指令无法送达（ESP32 为临时会话，离线消息会被丢弃），
    直接拒绝，避免客户端"已下发"但设备状态永远不变的假同步"""
    if not device_state.get("online"):
        raise HTTPException(503, f"设备离线，{desc}指令无法送达")

    try:
        await mqtt_service.publish_command(command)
    except RuntimeError as e:
        raise HTTPException(503, str(e))

    # 立即把目标值广播给网页客户端，
    # 开关切换毫秒级可见，不等设备下一轮上报
    from app import realtime
    await realtime.on_command_applied(command.get("cmd", ""), command.get("value") or 0)

    # 操作审计：下发即记 pending，设备 ack 后闭环为 ok/fail
    from app import audit_service
    await audit_service.log_command(
        source="web", operator=operator,
        cmd=command.get("cmd", ""), value=command.get("value"), detail=desc,
    )

    # ts：服务器应答时刻（毫秒）。客户端用它判断"设备上报是否发生在本条命令之后"，
    # 从而在设备拒绝/覆盖命令（如自动模式）时立即采纳真实上报，不把界面钉在旧值
    return {"ok": True, "ts": int(time.time() * 1000), **command}


async def _do_control(action: str, value: int, operator: str):
    meta = CMD_META[action]
    if not meta["check"](value):
        raise HTTPException(400, f"非法的 {meta['desc']} 参数: {value}")

    return await _publish({"cmd": action, "value": value}, meta["desc"], operator)


@router.post("/led")
async def control_led(body: ControlBody, username: str = Depends(require_admin)):
    """LED 开关 0=关 1=开"""
    return await _do_control("led", body.value, username)


@router.post("/fan")
async def control_fan(body: ControlBody, username: str = Depends(require_admin)):
    """风扇 0=关 1/2/3=三档风速"""
    return await _do_control("fan", body.value, username)


@router.post("/mode")
async def control_mode(body: ControlBody, username: str = Depends(require_admin)):
    """模式 1=自动 0=手动 2=专注（进入专注后设备开始计时）"""
    return await _do_control("mode", body.value, username)


# ======================================================
# 专注模式弹框交互（与 ESP32 OLED 弹框一一对应）
# 设备执行后会立即上报 focus_paused / focus_rest，
# 网页据此弹框/收框，保证两端显示一致
# ======================================================


@router.post("/focus/pause")
async def focus_pause(username: str = Depends(require_admin)):
    """专注中打开"是否结束专注"弹框并暂停计时（等价设备端 K3/K4 短按）"""
    return await _publish({"cmd": "focus_pause"}, "专注暂停", username)


@router.post("/focus/continue")
async def focus_continue(username: str = Depends(require_admin)):
    """弹框选择"继续"：恢复计时并关闭弹框（等价设备端 K3）"""
    return await _publish({"cmd": "focus_continue"}, "专注继续", username)


@router.post("/focus/end")
async def focus_end(body: FocusEndBody, username: str = Depends(require_admin)):
    """弹框选择"结束计时"：冻结本次时长并切到目标模式（等价设备端 K4）"""
    return await _publish({"cmd": "focus_end", "value": body.value}, "结束专注", username)
