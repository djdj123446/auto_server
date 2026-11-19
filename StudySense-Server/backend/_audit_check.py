"""查询最近审计日志 + 当前模式/阈值/规则，判断风扇被谁改回。用完即删。"""
import json
import urllib.request

BASE = "http://127.0.0.1:8800"


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

snap = http_json("GET", "/api/data/latest", token)
print("当前:", json.dumps({"online": snap["online"],
                           "led": (snap["data"] or {}).get("led"),
                           "fan": (snap["data"] or {}).get("fan"),
                           "mode": (snap["data"] or {}).get("mode"),
                           "temp": (snap["data"] or {}).get("temperature"),
                           "threshold": snap.get("threshold")}, ensure_ascii=False))

# 最近 30 条审计
audit = http_json("GET", "/api/audit?page=1&page_size=30", token)
rows = audit.get("items") or audit.get("rows") or audit.get("data") or audit
print("\n最近命令审计:")
if isinstance(rows, list):
    for it in rows:
        print(" ", it.get("ts", ""), it.get("source"), it.get("cmd"),
              "=", it.get("value"), it.get("result"), it.get("detail", ""))
else:
    print(json.dumps(audit, ensure_ascii=False)[:2000])

# 规则列表
try:
    rules = http_json("GET", "/api/rules", token)
    print("\n自动化规则:")
    if isinstance(rules, list):
        for r in rules:
            print(" ", r.get("name"), r.get("trigger_metric"), r.get("trigger_op"),
                  r.get("trigger_value"), "->", r.get("action_cmd"), "=", r.get("action_value"),
                  "enabled=", r.get("enabled"))
    else:
        print(json.dumps(rules, ensure_ascii=False)[:1500])
except Exception as e:
    print("规则读取失败:", e)
