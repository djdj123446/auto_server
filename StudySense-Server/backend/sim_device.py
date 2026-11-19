"""ESP32 模拟器：模拟设备上报数据并监听云端指令（联调用）

用法: .venv/Scripts/python.exe sim_device.py
停止: Ctrl+C
"""
import asyncio
import json
import random
import sys

import aiomqtt

if sys.platform == "win32":
    asyncio.set_event_loop_policy(asyncio.WindowsSelectorEventLoopPolicy())

BROKER = "127.0.0.1"
PORT = 1883
DEVICE_ID = "esp32_01"

TOPIC_STATE = f"studysense/{DEVICE_ID}/state/up"
TOPIC_STATUS = f"studysense/{DEVICE_ID}/status"
TOPIC_CMD_DOWN = f"studysense/{DEVICE_ID}/cmd/down"
TOPIC_CMD_ACK = f"studysense/{DEVICE_ID}/cmd/ack"
TOPIC_THRESHOLD_DOWN = f"studysense/{DEVICE_ID}/threshold/down"
TOPIC_THRESHOLD_UP = f"studysense/{DEVICE_ID}/threshold/up"


async def main():
    # last-will 遗嘱：模拟设备掉线时通知服务器
    async with aiomqtt.Client(identifier="sim-esp32", hostname=BROKER, port=PORT) as client:
        await client.publish(TOPIC_STATUS, "online", qos=1, retain=True)
        print(f"[模拟器] 已连接 broker，上线消息已发")

        await client.subscribe(TOPIC_CMD_DOWN, qos=1)
        await client.subscribe(TOPIC_THRESHOLD_DOWN, qos=1)

        state = {
            "temperature": 26.5,
            "humidity": 55,
            "light": 300,
            "people": False,
            "led": 0,
            "fan": 0,
            "mode": 1,
        }
        print("[模拟器] 开始每 2 秒上报一次传感器数据（温度会缓慢波动）...")

        async def publish_state():
            n = 0
            while True:
                state["temperature"] = round(state["temperature"] + random.uniform(-0.3, 0.35), 1)
                state["humidity"] = round(max(20, min(90, state["humidity"] + random.uniform(-1, 1))), 1)
                state["light"] = max(0, int(state["light"] + random.uniform(-40, 40)))
                state["people"] = random.random() < 0.6
                await client.publish(TOPIC_STATE, json.dumps(state), qos=1)
                n += 1
                if n % 5 == 0:
                    print(f"[模拟器] 已上报 {n} 条: {state}")
                await asyncio.sleep(2)

        async def listen_commands():
            async for message in client.messages:
                topic = str(message.topic)
                payload = message.payload.decode()
                print(f"\n[模拟器] 收到下发 topic={topic} payload={payload}")
                try:
                    msg = json.loads(payload)
                except Exception:
                    continue

                if topic == TOPIC_CMD_DOWN:
                    cmd = msg.get("cmd")
                    if cmd in ("led", "fan", "mode"):
                        # 真实执行：改到状态里，下一次上报就是新值
                        state[cmd] = int(msg.get("value", 0))
                        print(f"[模拟器] 已执行 {cmd} = {state[cmd]}")
                    # 模拟执行成功后的回执
                    await client.publish(
                        TOPIC_CMD_ACK,
                        json.dumps({"echo": msg, "ok": True}),
                        qos=1,
                    )
                elif topic == TOPIC_THRESHOLD_DOWN:
                    print(f"[模拟器] 已应用新阈值: {msg}")
                    # 回报 threshold/up，让网页同步刷新
                    await client.publish(TOPIC_THRESHOLD_UP, payload, qos=1)

        # 用 asyncio.wait 组合两个任务 —— pub_state 每 2 秒一轮
        listener = asyncio.create_task(listen_commands())
        try:
            await publish_state()
        finally:
            listener.cancel()
            await client.publish(TOPIC_STATUS, "offline", qos=1, retain=True)


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\n[模拟器] 已停止")
        sys.exit(0)
