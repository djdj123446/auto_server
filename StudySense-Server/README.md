# StudySense-Server — 智能书房服务端与网页控制台

毕业设计项目 **StudySense** 的服务端：以 **FastAPI + MySQL + EMQX** 接收 ESP32 通过 MQTT 上报的环境数据，提供 JWT 鉴权、REST API、WebSocket 实时推送、历史数据、学习报告、自动化规则与操作审计，并附带一个 **Vue 3 网页控制台**。

项目早期使用 ThingsCloud，现已迁移为完全自建的本地化架构。

## 相关项目

| 部分 | 目录 | 说明 |
| --- | --- | --- |
| 固件端 | `../StudySense/` | ESP32 传感器采集、执行器控制、OLED 交互、MQTT 通信 |
| 服务端（本文档） | `StudySense-Server/` | FastAPI 后端 + Vue 网页控制台 + MySQL + EMQX |

```
ESP32 ──MQTT──> EMQX ──> FastAPI（aiomqtt）
                              │  ├── MySQL（传感器数据 / 专注会话 / 阈值 / 审计 / 规则）
                              │  ├── WebSocket ──> 网页
                              │  └── REST API（JWT）──> 网页
                              └── 下发 cmd/down、threshold/down
```

## 功能模块

### 实时仪表盘（`/dashboard`）
- 设备在线状态（遗嘱消息 + 30 秒超时双保险）
- 温度、湿度、光照、人体感应实时指标卡
- 最近 5 分钟实时曲线（ECharts）
- 专注模式横幅与实时计时；弹框状态与 ESP32 OLED 同步

### 控制中心（`/control`）
- LED 开关、风扇 0~3 档、手动 / 自动 / 专注模式切换
- 阈值设置：温度上限、光照下限、专注休息提醒间隔，滑块调整后保存并实时下发
- 专注弹框交互：暂停 / 继续 / 结束，与设备端按 K3/K4 的行为等价
- 设备离线时后端直接拒绝下发（503），避免"界面已下发、设备没动作"的假同步

### 历史数据（`/history`）
- 区间统计：温度/湿度/光照的最小、最大、平均，以及有人比例
- 温湿度曲线（固定查询最近 24 小时，服务端降采样到 200 点）
- 传感器数据默认保留 7 天，可由清理任务滚动删除

### 学习报告（`/study`）
- 今日专注时长、连续打卡天数、近 7 天合计、累计专注时长
- 近 30 天每日专注时长柱状图
- 24 小时专注时段分布
- 近 90 天打卡日历热力图
- 专注会话记录分页表（开始/结束时间、时长、休息提醒次数、设备）

### 设备总览与审计（`/devices`）
- 设备列表：名称、型号、固件、位置、在线状态、主设备实时数据
- 设备详情：24 小时上报样本数、在线率（近似值）、当前阈值、最新状态
- 操作审计：云端指令下发记录（网页手动 / 自动化规则），按指令、来源、结果筛选；设备 ack 回执后自动闭环为成功/失败

### 自动化规则（`/rules`）
- 规则 CRUD 与启停
- 触发指标：温度 / 湿度 / 光照 / 人体；比较符：`>` / `<`
- 执行动作：LED / 风扇 / 模式
- **边沿触发**：条件从"不满足"变为"满足"时才执行，避免每 10 秒重复下发
- **冷却时间**：同一规则两次执行的最小间隔（10~3600 秒，默认 120 秒）
- 仅设备在线时执行；执行记录写入审计（来源 `rule`），并通过 WebSocket 推送 `rule_fired` 通知

### 账号与数据
- 单管理员账号，JWT 鉴权（默认 72 小时有效）
- 密码使用 PBKDF2-SHA256 + 随机盐哈希存储
- 专注会话从设备上报流中提取，永久保留，不受传感器数据清理影响
- 阈值单行存储，设备端按键修改与网页修改双向同步

## 技术栈

| 层级 | 技术 |
| --- | --- |
| 后端框架 | FastAPI、Uvicorn、Pydantic v2 |
| 数据库 | MySQL 8.0、SQLAlchemy 2（异步）、aiomysql |
| 消息 | EMQX 5.8.3、MQTT、aiomqtt |
| 鉴权 | PyJWT、PBKDF2-SHA256 |
| 前端 | Vue 3、Vite、Vue Router、Element Plus、ECharts、Axios |
| 部署 | Docker Compose（MySQL + EMQX）、Windows 一键脚本 |

## 目录结构

```
StudySense-Server/
├── docker-compose.yml          # MySQL + EMQX
├── start.bat                   # Windows 一键启动（容器 + 后端 + 前端）
├── ESP32固件对接规范.md         # MQTT 主题与 JSON 契约
├── backend/
│   ├── .env                    # 数据库、MQTT、管理员、JWT 等配置
│   ├── requirements.txt
│   ├── run.py                  # 启动入口（Windows 下设置 SelectorEventLoop）
│   ├── sim_device.py           # ESP32 模拟器（无硬件联调）
│   ├── _audit_check.py         # 临时诊断脚本：查询审计与当前状态
│   ├── _fan_auto_test.py       # 临时诊断脚本：验证自动/手动模式下风扇指令
│   ├── _fan_diag.py            # 临时诊断脚本：风扇状态排查
│   ├── _ws_delay_test.py       # 临时诊断脚本：WebSocket 推送延迟测量
│   └── app/
│       ├── main.py             # FastAPI 入口、路由注册、WebSocket、启动初始化
│       ├── config.py           # 从 .env 读取配置
│       ├── database.py         # 异步引擎与会话
│       ├── models.py           # 7 张表的数据模型
│       ├── schemas.py          # 请求/响应模型
│       ├── security.py         # 密码哈希、JWT 签发与校验
│       ├── mqtt_service.py     # MQTT 连接、订阅、指令/阈值下发
│       ├── realtime.py         # 实时状态中心、广播、离线检测、数据清理
│       ├── focus_service.py    # 专注会话状态机（从上报流提取）
│       ├── rule_engine.py      # 自动化规则评估与执行
│       ├── audit_service.py    # 指令审计与 ack 闭环
│       └── routers/
│           ├── auth.py         # 登录、当前用户、鉴权依赖
│           ├── data.py         # 最新状态、历史曲线、区间统计
│           ├── control.py      # LED / 风扇 / 模式 / 专注交互
│           ├── threshold.py    # 阈值查询与下发
│           ├── study.py        # 学习报告接口
│           ├── devices.py      # 设备列表与详情
│           ├── audit.py        # 审计查询
│           └── rules.py        # 自动化规则 CRUD
└── frontend/
    ├── package.json
    ├── vite.config.js          # /api、/ws 代理到 8800
    └── src/
        ├── api.js              # Axios 实例、JWT 注入、401 跳转
        ├── ws.js               # WebSocket 单例与 3 秒重连
        ├── focus.js            # 专注计时插值与弹框状态（全局共享）
        ├── router.js           # 路由与登录守卫
        ├── layout/AppLayout.vue# 侧边栏、顶栏、全局专注弹框
        └── views/              # Login / Dashboard / Control / History / Study / Devices / Rules
```

## 快速开始

### 环境要求

- **Docker Desktop**（MySQL 与 EMQX）
- **Python 3.11**（`backend/.venv` 使用 3.11；其他 3.x 版本需自行验证 aiomqtt）
- **Node.js 18+**（建议 18 或 20 LTS）

### 方式一：一键启动（已完成首次安装后）

双击根目录 `start.bat`，脚本会：

1. 检查并启动 `studysense-mysql` 容器（3306）
2. 检查并启动 `studysense-emqx` 容器（1883 / 18083）
3. 在新窗口启动后端：`backend\.venv\Scripts\python.exe run.py`
4. 在新窗口启动前端：`frontend` 下 `npm run dev`

> `start.bat` 未映射 8083 端口，因为 Windows Hyper-V / WinNAT 可能保留该端口；网页不依赖 MQTT over WebSocket，1883 足够。

### 方式二：手动启动

#### 1. 启动 MySQL 与 EMQX

```bash
docker compose up -d
docker compose ps
```

| 服务 | 端口 | 说明 |
| --- | --- | --- |
| MySQL | 3306 | 库名 `studysense` |
| EMQX | 1883 | MQTT TCP（ESP32 连接） |
| EMQX | 8083 | MQTT over WebSocket（docker-compose 中映射，可选） |
| EMQX | 18083 | 管理界面 http://localhost:18083（默认 admin/public） |

如 Docker 不可用，也可自行安装 MySQL 8 与 EMQX 5.8，并修改 `backend/.env`。

#### 2. 启动后端

```bash
cd backend
python -m venv .venv
.venv\Scripts\activate          # PowerShell: .venv\Scripts\Activate.ps1
pip install -r requirements.txt
python run.py
```

- 后端监听 `0.0.0.0:8800`（可在 `backend/.env` 的 `APP_HOST` / `APP_PORT` 修改），接口文档：http://localhost:8800/docs
- 端口不用 8000 是因为 Windows 的 Hyper-V/WinNAT 可能将其划入保留段，绑定会报 `WinError 10013`；改端口后需同步修改 `frontend/vite.config.js` 的代理
- Windows 上必须使用 `run.py` 启动：它设置 `WindowsSelectorEventLoopPolicy`，否则 aiomqtt 无法正常工作
- 首次启动会自动建表、补齐 `sensor_data.focus` 列、创建管理员账号与默认设备

#### 3. 启动前端

```bash
cd frontend
npm install
npm run dev
```

浏览器打开终端输出的地址（默认 http://localhost:5173），使用 `backend/.env` 中的管理员账号登录（默认 `admin` / `admin123`）。

#### 4. 接入设备

有硬件：按 `../StudySense/README.md` 配置固件，`MQTT_HOST` 填本机局域网 IP 后烧录。

无硬件：另开终端运行模拟器：

```bash
cd backend
.venv\Scripts\python.exe sim_device.py
```

模拟器连接 `127.0.0.1:1883`，每 2 秒上报一次数据，并响应云端指令与阈值下发。

## 配置说明（`backend/.env`）

| 配置项 | 默认值 | 说明 |
| --- | --- | --- |
| `MYSQL_HOST` / `MYSQL_PORT` | `127.0.0.1` / `3306` | MySQL 地址 |
| `MYSQL_USER` / `MYSQL_PASSWORD` | `studysense` / `studysense123` | 与 docker-compose 一致 |
| `MYSQL_DB` | `studysense` | 数据库名 |
| `EMQX_HOST` / `EMQX_PORT` | `127.0.0.1` / `1883` | MQTT Broker |
| `ADMIN_USERNAME` / `ADMIN_PASSWORD` | `admin` / `admin123` | 管理员账号，**仅首次建库时创建** |
| `JWT_SECRET` | 示例值 | 令牌签名密钥，部署前必须修改 |
| `JWT_EXPIRE_HOURS` | `72` | 令牌有效期 |
| `DATA_RETENTION_DAYS` | `7` | 传感器数据保留天数 |
| `DEVICE_ID` | `esp32_01` | 主设备 ID，需与固件 ClientID / 主题一致 |
| `DEVICE_OFFLINE_TIMEOUT` | `30` | 超过该秒数未上报判定离线 |

> `.env` 含数据库密码与 JWT 密钥，请勿提交到公开仓库或分享给他人。

## 数据库表

| 表 | 用途 | 生命周期 |
| --- | --- | --- |
| `sensor_data` | 每次上报一条传感器与设备状态 | 默认保留 7 天 |
| `threshold` | 单行 JSON 阈值配置 | 永久 |
| `admin_user` | 管理员账号 | 永久 |
| `device` | 设备注册表（首次启动自动创建默认设备） | 永久 |
| `focus_session` | 专注会话（开始/结束/时长/休息次数） | 永久 |
| `audit_log` | 指令下发与 ack 回执审计 | 永久 |
| `automation_rule` | 自动化规则 | 永久 |

- 建表使用 SQLAlchemy `create_all`，不含迁移框架；`main.py` 中有一处轻量迁移用于补齐旧库的 `focus` 列
- 数据清理任务每 6 小时执行一次，并在后端启动时执行一次
- 设备离线时，进行中的专注会话会自动收尾，状态切回手动并清零计时

## MQTT 对接

完整契约见 [`ESP32固件对接规范.md`](./ESP32固件对接规范.md)。

后端订阅：

| Topic | 用途 |
| --- | --- |
| `studysense/+/state/up` | 传感器与设备状态上报 |
| `studysense/+/status` | 在线状态（`online` / 遗嘱 `offline`，retain） |
| `studysense/+/threshold/up` | 设备端修改阈值 |
| `studysense/+/cmd/ack` | 指令回执 |

后端下发：

| Topic | 用途 |
| --- | --- |
| `studysense/{DEVICE_ID}/cmd/down` | 控制指令与专注交互 |
| `studysense/{DEVICE_ID}/threshold/down` | 阈值（retain） |

关键行为：

- 收到 `state/up` 后写入 `sensor_data`、更新内存快照、广播 WebSocket，并触发专注会话采集与规则引擎
- 数据库不可用时 `state/up` 处理会抛异常，但 MQTT 服务会记录日志并继续运行
- 下发指令成功后，后端会立即把目标值合并进内存快照并广播，网页毫秒级看到开关变化，不必等下一轮 10 秒上报
- 设备 ack 到达后，审计记录由 `pending` 闭环为 `ok` / `fail`

## REST API

所有 `/api` 业务接口除登录外都需要 `Authorization: Bearer <token>`；WebSocket 通过查询参数 `token` 鉴权。完整参数与响应可在 `/docs` 查看。

| 方法 | 路径 | 说明 |
| --- | --- | --- |
| POST | `/api/auth/login` | 登录，返回 JWT |
| GET | `/api/auth/me` | 当前用户 |
| GET | `/api/data/latest` | 最新快照（在线状态、最新数据、阈值） |
| GET | `/api/data/history` | 历史曲线（`start`、`end`、`points`，自动降采样） |
| GET | `/api/data/stats` | 区间统计（样本数、温湿度/光照 min/max/avg、有人比例） |
| POST | `/api/control/led` | LED 开关（`value`: 0/1） |
| POST | `/api/control/fan` | 风扇档位（`value`: 0~3） |
| POST | `/api/control/mode` | 模式（`value`: 0 手动 / 1 自动 / 2 专注） |
| POST | `/api/control/focus/pause` | 专注中打开结束弹框并暂停计时 |
| POST | `/api/control/focus/continue` | 弹框选择继续 |
| POST | `/api/control/focus/end` | 弹框选择结束（`value`: 0 回手动 / 1 回自动） |
| GET / PUT | `/api/threshold` | 查询 / 保存并下发阈值 |
| GET | `/api/study/summary` | 学习概览（今日、区间、连续打卡、累计） |
| GET | `/api/study/daily` | 每日专注时长（补齐无学习日为 0） |
| GET | `/api/study/hours` | 24 小时时段分布 |
| GET | `/api/study/sessions` | 专注会话分页 |
| GET | `/api/devices` | 设备列表与实时状态 |
| GET | `/api/devices/{device_id}` | 设备详情与 24 小时统计 |
| GET | `/api/audit` | 审计查询（按指令/来源/结果筛选、分页） |
| GET / POST | `/api/rules` | 规则列表 / 新建规则 |
| PUT / DELETE | `/api/rules/{id}` | 修改 / 删除规则 |
| POST | `/api/rules/{id}/toggle` | 启用 / 停用规则 |
| WS | `/ws/realtime?token=<JWT>` | 实时推送 |

WebSocket 消息类型：`snapshot`（连接快照）、`state`（设备上报）、`status`（上线/下线）、`threshold`（阈值同步）、`ack`（指令回执）、`rule_fired`（规则触发）。

## 前端说明

- 开发环境通过 Vite 代理转发：`/api` → `http://127.0.0.1:8800`，`/ws` → `ws://127.0.0.1:8800`
- 生产构建：

  ```bash
  cd frontend
  npm run build
  ```

  产物在 `frontend/dist/`。部署时需用 Nginx 等静态服务器托管，并把 `/api` 与 `/ws` 反向代理到后端 8800 端口；`ws.js` 使用当前页面 host 建立 WebSocket，不能直接双击 `index.html` 打开
- 登录态存 `localStorage`；任何接口 401 会清除令牌并跳转 `/login`
- 专注计时由 `focus.js` 统一插值，仪表盘、控制中心与全局弹框共享同一状态，避免页面间计时不一致

## 一键脚本与联调脚本

| 文件 | 用途 |
| --- | --- |
| `start.bat` | 启动/复用 Docker 容器，并在新窗口启动后端与前端；需先完成 `.venv` 与 `npm install` |
| `backend/sim_device.py` | 无硬件模拟设备，按主题收发，便于联调 |
| `backend/_audit_check.py` | 查询最近审计记录与当前模式/阈值/规则 |
| `backend/_fan_auto_test.py` | 验证自动/手动模式下风扇指令是否被设备覆盖 |
| `backend/_fan_diag.py` | 风扇状态诊断 |
| `backend/_ws_delay_test.py` | 测量指令下发到 WebSocket 广播的端到端延迟 |

`_` 开头的脚本是开发期临时工具，默认账号密码写死为 `admin/admin123`，仅限本地使用。

## 常见问题

- **后端启动报 aiomqtt / event loop 错误**：Windows 上请用 `python run.py` 启动，不要直接 `uvicorn app.main:app`
- **后端连不上 MySQL / EMQX**：确认容器已启动、`.env` 地址与端口正确；`docker compose ps` 查看健康状态
- **EMQX 18083 打不开**：等待健康检查通过；默认账号 `admin` / `public`，首次登录可能要求修改密码
- **8083 端口被占用**：Windows Hyper-V 会保留一段动态端口；本项目网页只用 1883，可不映射 8083
- **网页登录后立刻回到登录页**：JWT 过期或 `JWT_SECRET` 变更；重新登录。若修改过 `.env` 需重启后端
- **设备一直显示离线**：检查 ESP32 是否连上同一 WiFi、`MQTT_HOST` 是否为电脑局域网 IP、`DEVICE_ID` 是否与固件 ClientID 一致、1883 端口是否被防火墙拦截
- **控制提示 503**：设备离线或后端 MQTT 未连接，指令无法送达；先恢复设备在线
- **自动化规则不触发**：规则需启用、设备在线，且条件必须出现"不满足 → 满足"的跳变；冷却时间内不会重复执行
- **历史页/学习页没有数据**：新库需要设备先上报；传感器数据保留 7 天，专注数据需设备进入过专注模式
- **修改管理员密码不生效**：账号仅在首次建库时创建，后续修改 `ADMIN_PASSWORD` 不会覆盖已有账号；需要在数据库中更新密码哈希或重建数据卷
- **重置数据库（会删除全部数据）**：`docker compose down -v` 后重新启动；该操作不可恢复，请谨慎

## 部署与安全建议

当前配置面向本地局域网演示，公网部署前建议至少完成：

1. 修改 `JWT_SECRET`、MySQL 密码与管理员密码（管理员密码需在首次建库前设置）
2. 为 EMQX 开启用户名密码认证与 ACL，关闭匿名接入；ESP32 固件同步更新连接凭证
3. 使用 Nginx/Caddy 提供 HTTPS 与 WSS，并限制 CORS 允许来源（当前为 `*`）
4. 不要把 3306、1883、18083 直接暴露到公网
5. 定期备份 MySQL 数据卷；审计与专注会话为永久数据，建议单独保留

## 已知限制与扩展方向

- 当前实时遥测链路以单主设备 `DEVICE_ID` 为中心；`device` 表与 MQTT 主题已按多设备解析，但 `sensor_data` 暂未带 `device_id`，设备页仅主设备展示实时数据
- 阈值为全局单行配置，未按设备隔离
- 审计记录中等待 ack 的队列保存在内存，后端重启后未闭环记录会停留在 `pending`
- 管理员为单账号模型，无角色与用户管理
- 未引入 Alembic 迁移，表结构变更依赖 `create_all` 与手工轻量迁移

## 相关文档

- MQTT 主题与 JSON 契约：[`ESP32固件对接规范.md`](./ESP32固件对接规范.md)
- 固件说明：[`../StudySense/README.md`](../StudySense/README.md)
- 在线接口文档：启动后端后访问 http://localhost:8800/docs
