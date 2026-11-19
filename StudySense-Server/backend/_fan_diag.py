"""端到端诊断：分别下发 fan 和 led 命令，观察 WS 消息时序与设备上报。
用完即删的临时脚本。"""
import asyncio
import json
import time
import urllib.request

BASE = "http://127.0.0.1:8800"
WS_URL = "ws://127.0.0.1:8800/ws/realtime"


def http_json(method: str, path: str, token: str = "", body: dict | None = None):
    req = urllib.request.Request(
        BASE + path, method=method,
        data=json.dumps(body).encode() if body is not None else None,
        headers={"Content-Type": "application/json",
                 **({"Authorization": f"Bearer {token}"} if token else {})},
    )
    with urllib.request.urlopen(req, timeout=8) as r:
        return json.loads(r.read())


async def watch(ws, seconds: float, tag: str):
    t_end = time.perf_counter() + seconds
    while time.perf_counter() < t_end:
        try:
            msg = json.loads(await asyncio.wait_for(ws.recv(), timeout=t_end - time.perf_counter()))
        except asyncio.TimeoutError:
            break
        dt = time.perf_counter() - t_start
        d = msg.get("data") or {}
        if msg.get("type") == "state":
            print(f"  [{tag}] +{dt*1000:6.0f}ms  state  led={d.get('led')} fan={d.get('fan')} mode={d.get('mode')}")


token = http_json("POST", "/api/auth/login",
                  body={"username": "admin", "password": "admin123"})["access_token"]


async def main():
    import websockets
    snap = http_json("GET", "/api/data/latest", token)
    d0 = snap.get("data") or {}
    print(f"初始: led={d0.get('led')} fan={d0.get('fan')} online={snap.get('online')}")

    async with asyncio.timeout(60):
        async with websockets.connect(f"{WS_URL}?token={token}", open_timeout=5) as ws:
            # 等 snapshot
            while True:
                msg = json.loads(await ws.recv())
                if msg.get("type") == "snapshot":
                    break

            global t_start
            # ---- 测 fan ----
            t_start = time.perf_counter()
            print("\n== 下发 fan=2 ==")
            resp = await asyncio.to_thread(http_json, "POST", "/api/control/fan", token, {"value": 2})
            print(f"  POST 返回 {resp}")
            await watch(ws, 14, "fan")

            # ---- 测 led 对照 ----
            t_start = time.perf_counter()
            print("\n== 下发 led=1 ==")
            resp = await asyncio.to_thread(http_json, "POST", "/api/control/led", token, {"value": 1})
            print(f"  POST 返回 {resp}")
            await watch(ws, 14, "led")

    # 恢复
    await asyncio.to_thread(http_json, "POST", "/api/control/fan", token, {"value": d0.get("fan", 0)})
    await asyncio.to_thread(http_json, "POST", "/api/control/led", token, {"value": d0.get("led", 0)})
    print("\n已恢复初始值")


asyncio.run(main())
