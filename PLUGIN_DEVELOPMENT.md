# Phira MP Server Plugin Development Guide

本文档详细介绍了如何为Phira MP服务器开发Lua插件，包括完整的API参考和事件钩子说明。

## 目录
1. [插件基础](#插件基础)
2. [插件结构](#插件结构)
3. [事件钩子系统](#事件钩子系统)
4. [服务器API参考](#服务器api参考)
5. [用户管理API](#用户管理api)
6. [房间管理API](#房间管理api)
7. [消息系统API](#消息系统api)
8. [服务器控制API](#服务器控制api)
9. [状态查询API](#状态查询api)
10. [IP黑名单管理API](#ip黑名单管理api)
11. [比赛管理API](#比赛管理api)
12. [示例插件](#示例插件)

## 插件基础

### 环境要求
- Phira MP Server (C++20版本)
- Lua 5.4 兼容性
- 基本的Lua编程知识

### 插件目录结构
```
plugins/
└── your-plugin/
    ├── plugin.json      # 插件元数据
    └── main.lua         # 主插件脚本 (或 init.lua 向后兼容)
```

### plugin.json 格式
```json
{
    "id": "your-plugin-id",
    "name": "Your Plugin Name",
    "version": "1.0.0",
    "author": "Your Name",
    "description": "Plugin description",
    "enabled": true
}
```

## 插件结构

### 基本插件模板
```lua
-- 插件全局变量
local phira = phira

-- 事件处理函数
local function on_enable()
    phira.log_info("插件已启用")
    -- 初始化代码
end

local function on_disable()
    phira.log_info("插件已禁用")
    -- 清理代码
end

-- 注册其他事件钩子...

-- 启用插件
on_enable()
```

### 日志记录
```lua
phira.log_info("信息日志")
phira.log_error("错误日志")
```

## 事件钩子系统

### 核心事件钩子

#### `on_enable()`
插件启用时调用。

#### `on_disable()`
插件禁用时调用。

### 用户相关事件

#### `on_user_join(user, room)`
用户加入房间时触发。

**参数:**
- `user`: 用户对象
- `room`: 房间对象

**示例:**
```lua
local function on_user_join(user, room)
    phira.log_info("用户 " .. user.id .. " 加入了房间 " .. room.id)
    -- 可以在这里发送欢迎消息
    phira.roomsay_message(room.id, "欢迎 " .. user.name .. " 加入房间!")
end
```

#### `on_user_leave(user, room)`
用户离开房间时触发。

**参数:**
- `user`: 用户对象
- `room`: 房间对象

#### `on_user_kick(user, room, reason)`
用户被踢出时触发。

**参数:**
- `user`: 用户对象
- `room`: 房间对象 (可能为nil)
- `reason`: 踢出原因字符串

#### `on_user_ban(user, reason, duration_seconds)`
用户被封禁时触发。

**参数:**
- `user`: 用户对象
- `reason`: 封禁原因字符串
- `duration_seconds`: 封禁时长(秒)，0表示永久封禁

#### `on_user_unban(user_id)`
用户被解封时触发。

**参数:**
- `user_id`: 用户ID

### 房间相关事件

#### `on_room_create(room)`
房间创建时触发。

**参数:**
- `room`: 房间对象

**示例:**
```lua
local function on_room_create(room)
    phira.log_info("房间 " .. room.id .. " 已创建")
    -- 可以在这里设置房间初始属性
end
```

#### `on_room_destroy(room)`
房间销毁时触发。

**参数:**
- `room`: 房间对象

### 命令过滤事件

#### `on_before_command(user, command_type, command_data)`
在命令执行前触发，可以修改或拦截命令。

**参数:**
- `user`: 用户对象
- `command_type`: 命令类型字符串
- `command_data`: 命令数据表

**返回值:**
- 修改后的命令数据表，或nil表示拦截命令

**示例:**
```lua
local function on_before_command(user, cmd_type, cmd_data)
    -- 拦截所有聊天命令
    if cmd_type == "chat" then
        phira.log_info("用户 " .. user.id .. " 尝试发送聊天: " .. cmd_data.content)
        -- 可以修改聊天内容
        cmd_data.content = "[已审核] " .. cmd_data.content
        return cmd_data
    end
    -- 返回nil表示拦截命令
    -- return nil
end
```

## 服务器API参考

所有API函数都通过全局的 `phira` 表访问。

## 用户管理API

### `phira.kick_user(user_id, preserve_room)`
踢出用户。

**参数:**
- `user_id`: 用户ID (整数)
- `preserve_room`: 可选，是否保留房间 (布尔值，默认为false)

**返回值:**
- `boolean`: 是否成功

**示例:**
```lua
local success = phira.kick_user(12345, false)
if success then
    phira.log_info("用户踢出成功")
end
```

### `phira.ban_user(user_id)`
封禁用户。

**参数:**
- `user_id`: 用户ID (整数)

**返回值:**
- `boolean`: 是否成功

### `phira.unban_user(user_id)`
解封用户。

**参数:**
- `user_id`: 用户ID (整数)

**返回值:**
- `boolean`: 是否成功

### `phira.is_user_banned(user_id)`
检查用户是否被封禁。

**参数:**
- `user_id`: 用户ID (整数)

**返回值:**
- `boolean`: 是否被封禁

### `phira.get_banned_users()`
获取封禁用户列表。

**返回值:**
- `table`: 封禁用户ID数组

**示例:**
```lua
local banned = phira.get_banned_users()
phira.log_info("封禁用户数量: " .. #banned)
for i, user_id in ipairs(banned) do
    phira.log_info("封禁用户 " .. i .. ": " .. user_id)
end
```

### `phira.ban_room_user(user_id, room_id)`
封禁用户进入特定房间。

**参数:**
- `user_id`: 用户ID (整数)
- `room_id`: 房间ID (字符串)

**返回值:**
- `boolean`: 是否成功

### `phira.unban_room_user(user_id, room_id)`
解封用户进入特定房间。

**参数:**
- `user_id`: 用户ID (整数)
- `room_id`: 房间ID (字符串)

**返回值:**
- `boolean`: 是否成功

### `phira.is_user_banned_from_room(user_id, room_id)`
检查用户是否被特定房间封禁。

**参数:**
- `user_id`: 用户ID (整数)
- `room_id`: 房间ID (字符串)

**返回值:**
- `boolean`: 是否被房间封禁

### `phira.get_user_name(user_id)`
获取用户名。

**参数:**
- `user_id`: 用户ID (整数)

**返回值:**
- `string` 或 `nil`: 用户名，用户不存在时返回nil

### `phira.get_user_language(user_id)`
获取用户语言。

**参数:**
- `user_id`: 用户ID (整数)

**返回值:**
- `string` 或 `nil`: 用户语言代码，用户不存在时返回nil

### `phira.get_user_room_id(user_id)`
获取用户所在房间ID。

**参数:**
- `user_id`: 用户ID (整数)

**返回值:**
- `string` 或 `nil`: 房间ID，用户不在房间时返回nil

## 房间管理API

### `phira.disband_room(room_id)`
解散房间。

**参数:**
- `room_id`: 房间ID (字符串)

**返回值:**
- `boolean`: 是否成功

### `phira.set_max_users(room_id, max_users)`
设置房间最大人数。

**参数:**
- `room_id`: 房间ID (字符串)
- `max_users`: 最大人数 (整数，1-64)

**返回值:**
- `boolean`: 是否成功

### `phira.get_room_max_users(room_id)`
获取房间最大人数。

**参数:**
- `room_id`: 房间ID (字符串)

**返回值:**
- `integer` 或 `nil`: 最大人数，房间不存在时返回nil

### `phira.get_room_user_count(room_id)`
获取房间用户数。

**参数:**
- `room_id`: 房间ID (字符串)

**返回值:**
- `integer` 或 `nil`: 用户数，房间不存在时返回nil

### `phira.get_room_user_ids(room_id)`
获取房间用户ID列表。

**参数:**
- `room_id`: 房间ID (字符串)

**返回值:**
- `table`: 用户ID数组

### `phira.get_room_owner_id(room_id)`
获取房主ID。

**参数:**
- `room_id`: 房间ID (字符串)

**返回值:**
- `string` 或 `nil`: 房主ID，房间不存在时返回nil

## 消息系统API

### `phira.broadcast_message(message)`
全局广播消息。

**参数:**
- `message`: 消息内容 (字符串)

**返回值:**
- `boolean`: 是否成功

**示例:**
```lua
phira.broadcast_message("服务器将在5分钟后重启，请保存进度!")
```

### `phira.roomsay_message(room_id, message)`
向特定房间发送消息。

**参数:**
- `room_id`: 房间ID (字符串)
- `message`: 消息内容 (字符串)

**返回值:**
- `boolean`: 是否成功

### `phira.send_to_user(user_id, command_type, command_data)`
向用户发送命令 (需要构造命令数据)。

**参数:**
- `user_id`: 用户ID (整数)
- `command_type`: 命令类型 (字符串)
- `command_data`: 命令数据 (需要根据命令类型构造)

**注意:** 此API需要构造完整的命令数据，建议高级用户使用。

### `phira.broadcast_to_room(room, command_type, command_data)`
向房间广播命令 (需要构造命令数据)。

**参数:**
- `room`: 房间对象
- `command_type`: 命令类型 (字符串)
- `command_data`: 命令数据

## 服务器控制API

### `phira.shutdown_server()`
关闭服务器。

**警告:** 此操作会立即关闭服务器！

### `phira.reload_plugins()`
重新加载所有插件。

### `phira.save_admin_data()`
保存管理员数据（封禁列表、IP黑名单等）。

### `phira.load_admin_data()`
加载管理员数据。

## 状态查询API

### `phira.get_connected_user_count()`
获取在线用户数。

**返回值:**
- `integer`: 在线用户数

### `phira.get_active_room_count()`
获取活跃房间数。

**返回值:**
- `integer`: 活跃房间数

### `phira.get_room_list()`
获取房间列表。

**返回值:**
- `table`: 房间ID数组

**示例:**
```lua
local rooms = phira.get_room_list()
for i, room_id in ipairs(rooms) do
    local user_count = phira.get_room_user_count(room_id)
    if user_count then
        phira.log_info("房间 " .. room_id .. ": " .. user_count .. " 名用户")
    end
end
```

### `phira.get_connected_user_ids()`
获取在线用户ID列表。

**返回值:**
- `table`: 在线用户ID数组

### `phira.get_user(user_id)`
获取用户对象。

**参数:**
- `user_id`: 用户ID (整数)

**返回值:**
- `user` 对象或 `nil`: 用户不存在时返回nil

### `phira.get_room(room_id)`
获取房间对象。

**参数:**
- `room_id`: 房间ID (字符串)

**返回值:**
- `room` 对象或 `nil`: 房间不存在时返回nil

## IP黑名单管理API

### `phira.add_ip_to_blacklist(ip, is_admin)`
添加IP到黑名单。

**参数:**
- `ip`: IP地址 (字符串)
- `is_admin`: 可选，是否为管理员黑名单 (布尔值，默认为true)

**返回值:**
- `boolean`: 是否成功

### `phira.remove_ip_from_blacklist(ip, is_admin)`
从黑名单移除IP。

**参数:**
- `ip`: IP地址 (字符串)
- `is_admin`: 可选，是否为管理员黑名单 (布尔值，默认为true)

**返回值:**
- `boolean`: 是否成功

### `phira.is_ip_banned(ip)`
检查IP是否被禁。

**参数:**
- `ip`: IP地址 (字符串)

**返回值:**
- `boolean`: 是否被禁

### `phira.get_banned_ips(admin_list)`
获取黑名单IP列表。

**参数:**
- `admin_list`: 可选，是否获取管理员黑名单 (布尔值，默认为true)

**返回值:**
- `table`: IP地址数组

### `phira.clear_ip_blacklist(admin_list)`
清空黑名单。

**参数:**
- `admin_list`: 可选，是否清空管理员黑名单 (布尔值，默认为true)

## 比赛管理API

### `phira.enable_contest(room_id, manual_start, auto_disband)`
启用比赛模式。

**参数:**
- `room_id`: 房间ID (字符串)
- `manual_start`: 可选，手动开始比赛 (布尔值，默认为false)
- `auto_disband`: 可选，自动解散房间 (布尔值，默认为false)

**返回值:**
- `boolean`: 是否成功

### `phira.disable_contest(room_id)`
禁用比赛模式。

**参数:**
- `room_id`: 房间ID (字符串)

**返回值:**
- `boolean`: 是否成功

### `phira.add_contest_whitelist(room_id, user_id)`
添加比赛白名单用户。

**参数:**
- `room_id`: 房间ID (字符串)
- `user_id`: 用户ID (整数)

**返回值:**
- `boolean`: 是否成功

### `phira.remove_contest_whitelist(room_id, user_id)`
移除比赛白名单用户。

**参数:**
- `room_id`: 房间ID (字符串)
- `user_id`: 用户ID (整数)

**返回值:**
- `boolean`: 是否成功

### `phira.start_contest(room_id, force)`
开始比赛。

**参数:**
- `room_id`: 房间ID (字符串)
- `force`: 可选，强制开始 (布尔值，默认为false)

**返回值:**
- `boolean`: 是否成功

## 其他API

### `phira.get_replay_status()`
获取回放状态。

**返回值:**
- `boolean`: 回放是否启用

### `phira.set_replay_status(enabled)`
设置回放状态。

**参数:**
- `enabled`: 是否启用回放 (布尔值)

**返回值:**
- `boolean`: 是否成功

### `phira.get_room_creation_status()`
获取房间创建状态。

**返回值:**
- `boolean`: 房间创建是否启用

### `phira.set_room_creation_status(enabled)`
设置房间创建状态。

**参数:**
- `enabled`: 是否启用房间创建 (布尔值)

**返回值:**
- `boolean`: 是否成功

### `phira.create_virtual_room(room_id)`
创建虚拟房间 (占位功能)。

**参数:**
- `room_id`: 房间ID (字符串)

**返回值:**
- `room` 对象或 `nil`: 创建失败时返回nil

## HTTP路由注册

### `phira.register_http_route(method, path, handler)`
注册HTTP路由。

**参数:**
- `method`: HTTP方法 (字符串，如 "GET", "POST")
- `path`: 路径 (字符串，如 "/api/custom")
- `handler`: 处理函数

**处理函数签名:**
```lua
function handler(method, path, query, body, default_content_type)
    -- method: HTTP方法
    -- path: 请求路径
    -- query: 查询字符串
    -- body: 请求体
    -- default_content_type: 默认响应类型
    
    -- 返回响应体和内容类型
    return response_body, content_type
end
```

**示例:**
```lua
local function json_response(data, status)
    if not status then status = 200 end
    local json = phira.json_encode(data) -- 假设有json_encode函数
    return json, "application/json"
end

phira.register_http_route("GET", "/api/status", function(method, path, query, body, default_content_type)
    local users = phira.get_connected_user_count()
    local rooms = phira.get_active_room_count()
    
    return json_response({
        status = "ok",
        users = users,
        rooms = rooms,
        uptime = os.time() -- 示例
    })
end)
```

## 示例插件

### 简单欢迎插件
```lua
local phira = phira

local function on_enable()
    phira.log_info("欢迎插件已启用")
end

local function on_disable()
    phira.log_info("欢迎插件已禁用")
end

local function on_user_join(user, room)
    phira.log_info("用户 " .. user.id .. " (" .. user.name .. ") 加入了房间 " .. room.id)
    
    -- 发送欢迎消息
    phira.roomsay_message(room.id, "欢迎 " .. user.name .. " 加入房间!")
    
    -- 如果是第一个用户，发送额外提示
    local user_count = phira.get_room_user_count(room.id)
    if user_count and user_count == 1 then
        phira.roomsay_message(room.id, "你是房间的第一个用户，请等待其他人加入!")
    end
end

local function on_user_kick(user, room, reason)
    phira.log_info("用户 " .. user.id .. " 被踢出，原因: " .. reason)
    
    if room then
        phira.roomsay_message(room.id, "用户 " .. user.name .. " 已被管理员踢出")
    end
end

on_enable()
```

### 自动封禁插件
```lua
local phira = phira

-- 记录用户违规次数
local violations = {}

local function on_enable()
    phira.log_info("自动封禁插件已启用")
    
    -- 定时清理过期的违规记录
    -- 实际实现需要使用定时器
end

local function check_violations(user_id)
    local count = violations[user_id] or 0
    if count >= 3 then
        phira.log_info("用户 " .. user_id .. " 违规3次，自动封禁")
        phira.ban_user(user_id)
        violations[user_id] = nil
        return true
    end
    return false
end

local function on_before_command(user, cmd_type, cmd_data)
    -- 检查聊天内容中的违规词汇
    if cmd_type == "chat" then
        local content = cmd_data.content:lower()
        
        -- 简单关键词检查
        local bad_words = {"hack", "cheat", "作弊", "外挂"}
        for _, word in ipairs(bad_words) do
            if content:find(word) then
                violations[user.id] = (violations[user.id] or 0) + 1
                phira.log_info("用户 " .. user.id .. " 发送违规内容，违规次数: " .. violations[user.id])
                
                -- 发送警告
                phira.send_to_user(user.id, "chat", {content = "请勿发送违规内容，多次违规将被封禁!"})
                
                -- 检查是否达到封禁条件
                check_violations(user.id)
                
                -- 拦截消息
                return nil
            end
        end
    end
    
    return cmd_data
end

on_enable()
```

### 房间统计插件
```lua
local phira = phira

local function on_enable()
    phira.log_info("房间统计插件已启用")
    
    -- 注册HTTP路由
    phira.register_http_route("GET", "/api/room/stats", function(method, path, query, body, default_content_type)
        local rooms = phira.get_room_list()
        local room_stats = {}
        
        for _, room_id in ipairs(rooms) do
            local user_count = phira.get_room_user_count(room_id)
            local max_users = phira.get_room_max_users(room_id)
            
            table.insert(room_stats, {
                id = room_id,
                users = user_count or 0,
                max_users = max_users or 8
            })
        end
        
        -- 简单JSON响应（实际需要JSON编码库）
        local response = "{\"rooms\": ["
        for i, stat in ipairs(room_stats) do
            if i > 1 then response = response .. "," end
            response = response .. string.format(
                "{\"id\":\"%s\",\"users\":%d,\"max_users\":%d}",
                stat.id, stat.users, stat.max_users
            )
        end
        response = response .. "], \"total\": " .. #rooms .. "}"
        
        return response, "application/json"
    end)
end

local function on_room_create(room)
    phira.log_info("新房间创建: " .. room.id .. "，当前总房间数: " .. phira.get_active_room_count())
end

local function on_room_destroy(room)
    phira.log_info("房间销毁: " .. room.id .. "，剩余房间数: " .. phira.get_active_room_count())
end

on_enable()
```

## 调试技巧

### 日志记录
- 使用 `phira.log_info()` 记录信息
- 使用 `phira.log_error()` 记录错误
- 日志会输出到服务器控制台

### 错误处理
```lua
local success, result = pcall(function()
    -- 可能出错的代码
    return phira.kick_user(99999)
end)

if not success then
    phira.log_error("插件错误: " .. result)
end
```

### 性能考虑
- 避免在事件处理函数中进行耗时操作
- 使用局部变量缓存频繁访问的数据
- 合理使用 `on_before_command` 钩子，避免影响游戏性能

## 常见问题

### 插件不加载
1. 检查 `plugin.json` 语法
2. 确保 `enabled` 字段为 `true`
3. 检查Lua脚本是否有语法错误
4. 查看服务器日志中的错误信息

### API调用返回false
1. 检查参数类型是否正确
2. 确保目标用户/房间存在
3. 检查权限（某些API需要管理员权限）

### 事件钩子不触发
1. 确保函数名正确
2. 检查函数是否在插件启用前定义
3. 验证事件是否确实发生

## 最佳实践

1. **错误处理**: 所有API调用都应进行错误处理
2. **资源管理**: 插件禁用时清理资源
3. **性能优化**: 避免在频繁触发的事件中进行复杂计算
4. **代码组织**: 将功能模块化，提高代码可读性
5. **文档注释**: 为复杂逻辑添加注释

---

**版本**: 1.0.0  
**最后更新**: 2026年2月17日  
**兼容性**: Phira MP Server C++20 版本