"""数据表模型"""
from datetime import datetime

from sqlalchemy import JSON, BigInteger, DateTime, Float, Integer, String
from sqlalchemy.orm import Mapped, mapped_column

from app.database import Base


class SensorData(Base):
    """传感器上报数据（每 10 秒一条，只保留 N 天）"""
    __tablename__ = "sensor_data"

    id: Mapped[int] = mapped_column(BigInteger, primary_key=True, autoincrement=True)
    ts: Mapped[datetime] = mapped_column(DateTime, index=True)
    temperature: Mapped[float] = mapped_column(Float)          # 温度 °C
    humidity: Mapped[float] = mapped_column(Float)            # 湿度 %
    light: Mapped[int] = mapped_column(Integer)               # 光照 lx
    people: Mapped[int] = mapped_column(Integer)               # 1=有人 0=无人
    led: Mapped[int] = mapped_column(Integer)                 # 0=关 1=开
    fan: Mapped[int] = mapped_column(Integer)                 # 0=关 1/2/3=档位
    mode: Mapped[int] = mapped_column(Integer)                # 1=自动 0=手动 2=专注
    focus: Mapped[int] = mapped_column(Integer, default=0)    # 专注已计时秒数（非专注=0）


class Threshold(Base):
    """阈值配置（单行，content 为 JSON，字段与 ESP32 固件对齐）"""
    __tablename__ = "threshold"

    id: Mapped[int] = mapped_column(Integer, primary_key=True)
    content: Mapped[dict] = mapped_column(JSON)
    updated_at: Mapped[datetime] = mapped_column(DateTime)


class AdminUser(Base):
    """管理员账号（首次启动按 .env 自动创建）"""
    __tablename__ = "admin_user"

    id: Mapped[int] = mapped_column(Integer, primary_key=True)
    username: Mapped[str] = mapped_column(String(64), unique=True)
    password_hash: Mapped[str] = mapped_column(String(256))
    created_at: Mapped[datetime] = mapped_column(DateTime, default=datetime.now)


class Device(Base):
    """设备注册表（多设备架构，收到上报自动注册）"""
    __tablename__ = "device"

    id: Mapped[int] = mapped_column(Integer, primary_key=True)
    device_id: Mapped[str] = mapped_column(String(64), unique=True, index=True)
    name: Mapped[str] = mapped_column(String(64), default="")
    model: Mapped[str] = mapped_column(String(64), default="ESP32-DevKit")
    firmware: Mapped[str] = mapped_column(String(32), default="v1.0.0")
    location: Mapped[str] = mapped_column(String(64), default="书房")
    note: Mapped[str] = mapped_column(String(255), default="")
    created_at: Mapped[datetime] = mapped_column(DateTime, default=datetime.now)
    last_seen_at: Mapped[datetime] = mapped_column(DateTime, default=None, nullable=True)


class FocusSession(Base):
    """专注学习会话（从设备上报流中提取，永久保留）"""
    __tablename__ = "focus_session"

    id: Mapped[int] = mapped_column(BigInteger, primary_key=True, autoincrement=True)
    device_id: Mapped[str] = mapped_column(String(64), index=True)
    start_ts: Mapped[datetime] = mapped_column(DateTime, index=True)
    end_ts: Mapped[datetime] = mapped_column(DateTime, nullable=True)      # 进行中为 NULL
    duration_sec: Mapped[int] = mapped_column(Integer, default=0)         # 已专注秒数
    rest_count: Mapped[int] = mapped_column(Integer, default=0)           # 休息提醒次数


class AuditLog(Base):
    """操作审计（云端指令下发 + 设备 ack 回执）"""
    __tablename__ = "audit_log"

    id: Mapped[int] = mapped_column(BigInteger, primary_key=True, autoincrement=True)
    ts: Mapped[datetime] = mapped_column(DateTime, index=True)
    source: Mapped[str] = mapped_column(String(16))       # web / rule / system
    operator: Mapped[str] = mapped_column(String(64))     # 用户名或 rule_engine
    cmd: Mapped[str] = mapped_column(String(32))
    value: Mapped[int] = mapped_column(JSON, nullable=True)
    result: Mapped[str] = mapped_column(String(16), default="pending")  # pending / ok / fail
    detail: Mapped[str] = mapped_column(String(255), default="")


class AutomationRule(Base):
    """自动化规则：条件满足（边沿触发）时云端下发控制指令"""
    __tablename__ = "automation_rule"

    id: Mapped[int] = mapped_column(Integer, primary_key=True)
    name: Mapped[str] = mapped_column(String(64))
    trigger_metric: Mapped[str] = mapped_column(String(16))   # temperature / humidity / light / people
    trigger_op: Mapped[str] = mapped_column(String(4))        # gt / lt
    trigger_value: Mapped[float] = mapped_column(Float)
    action_cmd: Mapped[str] = mapped_column(String(16))       # led / fan / mode
    action_value: Mapped[int] = mapped_column(Integer)
    enabled: Mapped[int] = mapped_column(Integer, default=1)
    cooldown_sec: Mapped[int] = mapped_column(Integer, default=120)
    fire_count: Mapped[int] = mapped_column(Integer, default=0)
    last_fired_at: Mapped[datetime] = mapped_column(DateTime, nullable=True)
    last_satisfied: Mapped[int] = mapped_column(Integer, default=0)  # 上次评估条件是否满足（边沿用）
    created_at: Mapped[datetime] = mapped_column(DateTime, default=datetime.now)
