"""请求/响应模型"""
from pydantic import BaseModel, Field


class LoginBody(BaseModel):
    username: str
    password: str


class TokenOut(BaseModel):
    access_token: str
    token_type: str = "bearer"


class ControlBody(BaseModel):
    value: int = Field(ge=0, le=3, description="led:0/1 fan:0-3 mode:0/1")


class FocusEndBody(BaseModel):
    """结束专注后切换到的模式：0=手动 1=自动（专注模式下结束统一回手动）"""
    value: int = Field(default=0, ge=0, le=1, description="0=手动 1=自动")


class ThresholdBody(BaseModel):
    """阈值内容不固定字段（与固件对齐），如 temp_max / light_min 等"""
    content: dict


class StateOut(BaseModel):
    temperature: float | None = None
    humidity: float | None = None
    light: int | None = None
    people: bool | None = None
    led: int | None = None
    fan: int | None = None
    mode: int | None = None
    focus: int | None = None
