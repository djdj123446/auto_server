"""MQTT 服务：订阅设备上报，提供指令下发接口"""
import asyncio
import json
import logging

import aiomqtt

from app.config import settings
from app import realtime

log = logging.getLogger("mqtt")


def _topic(name: str) -> str:
    return f"studysense/{settings.DEVICE_ID}/{name}"


class MQTTService:
    def __init__(self):
        self.client: aiomqtt.Client | None = None

    async def start(self):
        asyncio.create_task(self._run())

    async def _run(self):
        """自动重连循环"""
        while True:
            try:
                async with aiomqtt.Client(
                    identifier="studysense-backend",
                    hostname=settings.EMQX_HOST,
                    port=settings.EMQX_PORT,
                ) as client:
                    self.client = client
                    log.info("MQTT 已连接 %s:%s", settings.EMQX_HOST, settings.EMQX_PORT)
                    await client.subscribe("studysense/+/state/up", qos=1)
                    await client.subscribe("studysense/+/status", qos=1)
                    await client.subscribe("studysense/+/threshold/up", qos=1)
                    await client.subscribe("studysense/+/cmd/ack", qos=1)
                    async for message in client.messages:
                        await self._handle(message)
            except asyncio.CancelledError:
                raise
            except Exception as e:
                log.warning("MQQT 连接断开，5 秒后重连: %s", e)
                await asyncio.sleep(5)

    async def _handle(self, message):
        topic = str(message.topic)
        # studysense/{device_id}/...：取出设备 ID，供多设备架构使用
        parts = topic.split("/")
        device_id = parts[1] if len(parts) >= 3 else settings.DEVICE_ID

        try:
            payload = json.loads(message.payload)
        except Exception:
            payload = message.payload.decode(errors="replace")

        if isinstance(payload, dict):
            payload["_device"] = device_id

        try:
            if topic.endswith("/state/up"):
                await realtime.on_state(payload)
            elif topic.endswith("/status"):
                await realtime.on_status(payload)
            elif topic.endswith("/threshold/up"):
                await realtime.on_threshold(payload)
            elif topic.endswith("/cmd/ack"):
                await realtime.on_ack(payload)
        except Exception:
            log.exception("处理消息失败 topic=%s", topic)

    async def publish_command(self, command: dict):
        """下发控制指令到 ESP32"""
        if not self.client:
            raise RuntimeError("MQTT 尚未连接")
        await self.client.publish(_topic("cmd/down"), json.dumps(command), qos=1)
        log.info("下发指令: %s", command)

    async def publish_threshold(self, content: dict):
        """下发阈值（保留消息，ESP32 重启后能立即读取）"""
        if not self.client:
            raise RuntimeError("MQTT 尚未连接")
        await self.client.publish(_topic("threshold/down"), json.dumps(content), qos=1, retain=True)
        log.info("下发阈值: %s", content)


mqtt_service = MQTTService()
