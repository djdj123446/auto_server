"""验证假设：自动模式下设备会覆盖手动风扇指令，手动模式下不会。
分别测 manual / auto 两种模式下发 fan=2 后设备上报的 fan 值。用完即删。"""
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


token = http_json("POST", "/api/auth/login",
                  body={"username": "admin", "password": "admin123"})["access_token"]

t_start = time.perf_counter()


async def watch(ws, seconds: float, tag: str):
    t_end = time.perf_counter() + seconds
    while time.perf_counter() < t_end:
        try:
            msg = json.loads(await asyncio.wait_for(ws.recv(), timeout=t_end - time.perf_counter()))
        except asyncio.TimeoutError:
            break
        d = msg.get("data") or {}
        if msg.get("type") == "state":
            dt = time.perf_counter() - t_start
            print(f"  [{tag}] +{dt*1000:6.0f}ms  led={d.get('led')} fan={d.get('fan')} mode={d.get('mode')}")


async def run_case(ws, mode: int, label: str):
    global t_start
    await asyncio.to_thread(http_json, "POST", "/api/control/mode", token, {"value": mode})
    await asyncio.sleep(1.2)
    await ws.recv() if False else None
    t_start = time.perf_counter()
    print(f"\n== {label}: 下发 mode={mode}，再下发 fan=2，观察 12 秒 ==")
    await asyncio.to_thread(http_json, "POST", "/api/control/fan", token, {"value": 2})
    await watch(ws, 12, label)


async def main():
    import websockets
    snap = http_json("GET", "/api/data/latest", token)
    d0 = snap.get("data") or {}
    orig_mode = d0.get("mode", 0)
    orig_fan = d0.get("fan", 0)
    print(f"初始: mode={orig_mode} fan={orig_fan} temp={(d0 or {}).get('temperature')}")

    async with asyncio.timeout(90):
        async with websockets.connect(f"{WS_URL}?token={token}", open_timeout=5) as ws:
            while True:
                msg = json.loads(await ws.recv())
                if msg.get("type") == "snapshot":
                    break
            await run_case(ws, 0, "手动")
            await run_case(ws, 1, "自动")
            # 恢复
            await asyncio.to_thread(http_json, "POST", "/api/control/mode", token, {"value": orig_mode})
            await asyncio.to_thread(http_json, "POST", "/api/control/fan", token, {"value": orig_fan})
            print(f"\n已恢复 mode={orig_mode} fan={orig_fan}")


asyncio.run(main())
