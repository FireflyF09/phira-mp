# Phira MP Server (C++ Enhanced)

基于 cpp-phira-mp 的增强版本，新增 Web 后台管理、REST API、SSE 实时事件、封禁系统和连接欢迎信息。

## 新增功能

### 1. 后台 Web 管理面板
- 浏览器访问 `http://服务器IP:12345/admin`
- 查看所有房间列表、房间状态、玩家人数及列表
- 实时刷新（每5秒自动更新）
- 一键解散任意房间
- 一键踢出房间内任意玩家
- 封禁/解封玩家 ID（封禁后连接时显示「你已被封禁」提示）
- 封禁列表持久化存储在 `banned.txt`

### 2. REST API（完整实现 api.md）

| 接口 | 说明 |
|------|------|
| `GET /api/rooms/info` | 获取所有房间列表及完整数据 |
| `GET /api/rooms/info/<name>` | 获取指定名称房间信息 |
| `GET /api/rooms/user/<user_id>` | 获取指定用户所在房间信息 |
| `GET /api/rooms/listen` | SSE 实时事件流 |

#### SSE 事件类型
| 事件 | 说明 |
|------|------|
| `create_room` | 新房间创建 |
| `update_room` | 房间数据更新（状态、铺面、锁定等变化） |
| `join_room` | 用户加入房间 |
| `leave_room` | 用户离开房间 |
| `player_score` | 玩家完成游戏（含完整成绩记录） |
| `start_round` | 房间开始新一轮游戏 |

### 3. 连接欢迎信息
- 用户认证成功后自动发送欢迎消息
- 显示 QQ 群号：1049578201
- 展示当前可加入的房间列表（仅显示选图中且未锁定的房间）

---

## Ubuntu 安装依赖

```bash
# 更新包列表
sudo apt update

# 安装编译工具和依赖
sudo apt install -y build-essential g++ uuid-dev curl
```

### 所需依赖清单
| 依赖 | Ubuntu 包名 | 用途 |
|------|------------|------|
| G++ (>=10) | `build-essential` / `g++` | C++20 编译器 |
| uuid-dev | `uuid-dev` | UUID 生成 |
| curl | `curl` | HTTP 请求（获取 Phira API 数据） |
| make | `build-essential` | 构建工具 |

---

## 编译

```bash
cd cpp-phira-mp-main
make clean
make
```

编译成功后生成 `phira-mp-server` 可执行文件。

---

## 运行

```bash
# 默认端口运行（游戏端口 12346，Web 端口 12345）
./phira-mp-server

# 自定义端口
./phira-mp-server -p 12346 -w 8080

# 后台运行
nohup ./phira-mp-server -p 12346 -w 12345 > server.log 2>&1 &
```

### 命令行参数
| 参数 | 说明 | 默认值 |
|------|------|--------|
| `-p, --port` | 游戏服务器端口 | 12346 |
| `-w, --web-port` | Web 管理/API 端口 | 12345 |
| `-h, --help` | 显示帮助 | - |

---

## 文件结构

```
cpp-phira-mp-main/
├── include/
│   ├── ban_manager.h      # [新增] 封禁管理
│   ├── binary_protocol.h  # 二进制协议
│   ├── commands.h          # 命令定义
│   ├── http_client.h       # HTTP 客户端
│   ├── l10n.h              # 本地化
│   ├── room.h              # [修改] 房间 + 轮次历史
│   ├── server.h            # [修改] 服务器 + get_state()
│   ├── session.h           # 会话
│   └── web_server.h        # [新增] Web 服务器
├── src/
│   ├── http_client.cpp
│   ├── l10n.cpp
│   ├── main.cpp            # [修改] 主入口 + Web 启动
│   ├── room.cpp            # [修改] 轮次记录 + SSE
│   ├── server.cpp
│   ├── session.cpp         # [修改] 封禁检查 + 欢迎消息 + SSE
│   └── web_server.cpp      # [新增] Web 服务器实现
├── locales/
│   ├── en-US.ftl
│   ├── zh-CN.ftl
│   └── zh-TW.ftl
├── Makefile
└── README.md
```

### 运行时文件
- `banned.txt` — 封禁玩家 ID 列表（自动创建/管理）
- `server_config.yml` — 服务器配置（可选）

---

## API 使用示例

```bash
# 获取所有房间
curl http://localhost:12345/api/rooms/info

# 获取指定房间
curl http://localhost:12345/api/rooms/info/my-room

# 获取用户所在房间
curl http://localhost:12345/api/rooms/user/12345

# 监听实时事件（SSE）
curl http://localhost:12345/api/rooms/listen

```

---

## QQ 群

**1049578201**
