"""端到端验证：下发 led 命令后，测 WebSocket state 广播的到达延迟。
用完即删的临时脚本（仅用标准库 + websockets）。"""
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


async def main(token: str):
    snap = http_json("GET", "/api/data/latest", token)
    cur_led = int((snap.get("data") or {}).get("led") or 0)
    print(f"当前 led={cur_led} online={snap.get('online')}")

    import websockets
    async with asyncio.timeout(20):
        async with websockets.connect(f"{WS_URL}?token={token}", open_timeout=5) as ws:
            t0 = time.perf_counter()
            while True:
                msg = json.loads(await ws.recv())
                if msg.get("type") == "snapshot":
                    break
            print(f"WS 已连接，snapshot 收到（{int((time.perf_counter()-t0)*1000)}ms）")

            new_val = 1 - cur_led
            t_post = time.perf_counter()
            resp = await asyncio.to_thread(
                http_json, "POST", "/api/control/led", token, {"value": new_val})
            print(f"POST /control/led value={new_val} -> {resp}（HTTP 往返 {(time.perf_counter()-t_post)*1000:.0f}ms）")

            while True:
                msg = json.loads(await ws.recv())
                if msg.get("type") == "state":
                    led = (msg.get("data") or {}).get("led")
                    delay = (time.perf_counter() - t_post) * 1000
                    print(f"收到 state 广播: led={led}，距发起下发 {delay:.0f}ms")
                    if led == new_val:
                        print("OK: 服务端即时广播正常")
                        break

    await asyncio.to_thread(http_json, "POST", "/api/control/led", token, {"value": cur_led})
    print(f"已恢复 led={cur_led}")


token = http_json("POST", "/api/auth/login",
                  body={"username": "admin", "password": "admin123"})["access_token"]
asyncio.run(main(token))
