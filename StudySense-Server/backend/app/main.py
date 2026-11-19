"""StudySense 后端入口"""
import asyncio
import logging
from contextlib import asynccontextmanager

from fastapi import FastAPI, Query, WebSocket, WebSocketDisconnect
from fastapi.middleware.cors import CORSMiddleware
from sqlalchemy import select, text

from app import realtime
from app.config import settings
from app.database import Base, SessionLocal, engine
from app.models import AdminUser, Device
from app.mqtt_service import mqtt_service
from app.routers import auth, control, data, threshold
from app.routers import study, devices, audit, rules
from app.security import decode_token, hash_password

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s %(levelname)s %(name)s: %(message)s",
)
log = logging.getLogger("main")


async def _init_db():
    """建表 + 轻量迁移 + 初始化管理员账号"""
    async with engine.begin() as conn:
        await conn.run_sync(Base.metadata.create_all)
        # 轻量迁移：旧库缺 focus 列时自动补齐（create_all 不会给已有表加列）
        try:
            await conn.execute(text(
                "ALTER TABLE sensor_data ADD COLUMN focus INT NOT NULL DEFAULT 0"
            ))
            log.info("已为 sensor_data 补充 focus 列")
        except Exception as e:
            # 1060 = Duplicate column name，列已存在属正常情况
            if "1060" not in str(e):
                raise
    async with SessionLocal() as session:
        user = (await session.execute(
            select(AdminUser).where(AdminUser.username == settings.ADMIN_USERNAME)
        )).scalar_one_or_none()
        if not user:
            session.add(AdminUser(
                username=settings.ADMIN_USERNAME,
                password_hash=hash_password(settings.ADMIN_PASSWORD),
            ))
            await session.commit()
            log.info("已创建管理员账号: %s", settings.ADMIN_USERNAME)

        # 默认设备注册（多设备架构：其余设备收到上报后同样自动注册）
        dev = (await session.execute(
            select(Device).where(Device.device_id == settings.DEVICE_ID)
        )).scalar_one_or_none()
        if not dev:
            session.add(Device(
                device_id=settings.DEVICE_ID,
                name="书房环境终端",
                model="ESP32-DevKit",
                firmware="v1.0.0",
                location="书房",
                note="温湿度 / 光照 / 人体感应 + LED / 风扇 / 蜂鸣器",
            ))
            await session.commit()
            log.info("已注册默认设备: %s", settings.DEVICE_ID)


@asynccontextmanager
async def lifespan(app: FastAPI):
    await _init_db()
    await realtime.warm_up()   # 从数据库恢复最后一次数据（离线重启也有数据可看）
    from app import focus_service
    await focus_service.warm_up()   # 收尾重启前悬挂的专注会话
    await mqtt_service.start()
    realtime_tasks = [
        asyncio.create_task(realtime.offline_checker()),
        asyncio.create_task(realtime.retention_cleanup()),
    ]
    log.info("StudySense 后端启动完成")
    yield
    for t in realtime_tasks:
        t.cancel()


app = FastAPI(title="StudySense 智能书房环境调节系统", version="1.0.0", lifespan=lifespan)

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],  # 本地部署，前端开发服务器跨域放开
    allow_methods=["*"],
    allow_headers=["*"],
)

app.include_router(auth.router)
app.include_router(data.router)
app.include_router(control.router)
app.include_router(threshold.router)
app.include_router(study.router)
app.include_router(devices.router)
app.include_router(audit.router)
app.include_router(rules.router)


@app.get("/")
async def root():
    return {"name": "StudySense API", "docs": "/docs"}


@app.websocket("/ws/realtime")
async def ws_realtime(ws: WebSocket, token: str = Query(default="")):
    """实时推送：传感器数据 / 在线状态 / 阈值 / 指令回执"""
    if not decode_token(token):
        await ws.close(code=4401)
        return
    await ws.accept()
    realtime.ws_clients.add(ws)
    # 连上立即推一次当前快照
    try:
        await ws.send_json({"type": "snapshot", "data": realtime.current_snapshot()})
    except Exception:
        pass
    try:
        while True:
            await ws.receive_text()  # 忽略客户端心跳内容
    except WebSocketDisconnect:
        pass
    finally:
        realtime.ws_clients.discard(ws)
