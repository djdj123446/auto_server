"""启动入口：Windows 下必须用 SelectorEventLoop，否则 aiomqtt 不工作"""
import asyncio
import sys

if sys.platform == "win32":
    asyncio.set_event_loop_policy(asyncio.WindowsSelectorEventLoopPolicy())

import uvicorn

from app.config import settings

if __name__ == "__main__":
    # loop="none" 让 uvicorn 尊重上面设置的 SelectorEventLoop 策略
    # 端口从 .env 的 APP_PORT 读取（默认 8800，避开 Windows 保留端口段）
    uvicorn.run(
        "app.main:app",
        host=settings.APP_HOST,
        port=settings.APP_PORT,
        loop="none",
    )