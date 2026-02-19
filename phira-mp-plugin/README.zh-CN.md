# Phira MP 插件系统

基于 WebAssembly 的 Phira MP 服务器插件系统，支持多语言插件开发，提供沙箱执行、热重载和全面的宿主 API。

## 功能特性

- **基于 WebAssembly**: 支持 Rust、C/C++、Go、AssemblyScript 等多种语言编写的插件
- **多语言支持**: 使用 WIT（Wasm Interface Types）实现语言无关的接口
- **沙箱执行**: 插件在隔离环境中运行，有资源限制
- **热重载**: 无需重启服务器即可重新加载插件
- **事件系统**: 订阅和发射服务器事件
- **命令系统**: 注册和处理自定义命令
- **全面 API**: 通过宿主 API 完整访问服务器功能
- **依赖管理**: 解析插件依赖关系，检查冲突
- **监控**: 实时指标和健康监控
- **配置**: 每个插件的独立配置，支持热重载
- **安全**: 细粒度的权限控制和安全策略

## 架构设计

```
┌─────────────────────────────────────────────────────────────┐
│                    Phira MP 服务器                          │
├─────────────────────────────────────────────────────────────┤
│                  插件系统宿主层                            │
│  ┌─────────────┐  ┌─────────────┐  ┌───────────────────┐  │
│  │插件管理器   │  │ 事件总线    │  │ 命令注册器        │  │
│  └─────────────┘  └─────────────┘  └───────────────────┘  │
│  ┌─────────────┐  ┌─────────────┐  ┌───────────────────┐  │
│  │WASM 运行时  │  │ 宿主 API    │  │ 沙箱管理器        │  │
│  └─────────────┘  └─────────────┘  └───────────────────┘  │
└─────────────────────────────────────────────────────────────┘
                            │
┌─────────────────────────────────────────────────────────────┐
│                    插件（WASM 模块）                        │
│  ┌───────────────────────────────────────────────────────┐  │
│  │ 插件代码（Rust/C/Go等） → WASM → 组件模型            │  │
│  └───────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
```

## 快速开始

### 1. 在服务器中添加插件系统

在服务器的 `Cargo.toml` 中添加插件系统依赖：

```toml
[dependencies]
phira-mp-plugin = { path = "../phira-mp-plugin" }
```

### 2. 初始化插件系统

```rust
use phira_mp_plugin::{PluginManager, EventBus, CommandRegistry, HostApi};
use std::sync::Arc;

async fn initialize_plugin_system() -> Result<(), Box<dyn std::error::Error>> {
    // 创建核心组件
    let event_bus = Arc::new(EventBus::new());
    let command_registry = Arc::new(CommandRegistry::new());
    let host_api = Arc::new(HostApi::new(
        Arc::clone(&event_bus),
        Arc::clone(&command_registry),
        // 插件管理器稍后设置
        Arc::new(()), // 占位符
    ));
    
    // 创建插件管理器
    let plugin_manager = Arc::new(PluginManager::new(
        "./plugins",
        Arc::clone(&event_bus),
        Arc::clone(&command_registry),
        Arc::clone(&host_api),
    )?);
    
    // 扫描并加载插件
    plugin_manager.scan_and_load().await?;
    
    // 初始化和启动插件
    plugin_manager.initialize_all().await?;
    plugin_manager.start_all().await?;
    
    Ok(())
}
```

### 3. 创建简单插件

为你的插件创建一个新的 Rust 项目：

```toml
# Cargo.toml
[package]
name = "my-plugin"
version = "0.1.0"
edition = "2024"

[lib]
crate-type = ["cdylib"]

[dependencies]
phira-mp-plugin = { path = "../../phira-mp-plugin" }
serde_json = "1.0"
```

```rust
// src/lib.rs
use phira_mp_plugin::{PluginMetadata, PluginConfig};
use std::sync::Arc;

#[no_mangle]
pub extern "C" fn plugin_init(host_api: Arc<phira_mp_plugin::api_host::HostApi>) -> Result<(), String> {
    // 注册事件处理器
    host_api.subscribe_event("server_start", Box::new(|event| {
        println!("服务器已启动！");
        Ok(())
    }), "my-plugin").map_err(|e| e.to_string())?;
    
    // 注册命令
    host_api.register_command("hello", "打招呼", Box::new(|cmd, args| {
        Ok(format!("你好，来自 my-plugin！参数：{:?}", args))
    }), "my-plugin").map_err(|e| e.to_string())?;
    
    Ok(())
}

#[no_mangle]
pub extern "C" fn plugin_metadata() -> PluginMetadata {
    PluginMetadata {
        name: "my-plugin".to_string(),
        version: "0.1.0".to_string(),
        author: "你的名字".to_string(),
        description: Some("我的第一个 Phira MP 插件".to_string()),
        entry_point: Some("plugin_init".to_string()),
        dependencies: None,
        permissions: Some(vec!["read_users".to_string()]),
        abi_version: "1.0.0".to_string(),
        category: Some("实用工具".to_string()),
        tags: Some(vec!["示例".to_string()]),
        website: None,
        license: Some("MIT".to_string()),
        min_host_version: None,
        config_schema: None,
        custom: None,
    }
}
```

### 4. 构建和部署插件

将插件构建为 WASM 组件：

```bash
# 构建插件
cd my-plugin
cargo build --release --target wasm32-wasip1

# 复制到插件目录
cp target/wasm32-wasip1/release/my_plugin.wasm ../server/plugins/
```

## 插件清单

插件需要清单文件（`plugin.toml`）包含元数据：

```toml
name = "my-plugin"
version = "1.0.0"
author = "你的名字"
description = "一个有用的 Phira MP 插件"
entry_point = "plugin_init"
abi_version = "1.0.0"

[dependencies]
other-plugin = ">=1.0.0"

[permissions]
read_users = true
write_config = false

[config]
default_value = "默认值"
```

## 宿主 API

插件可以访问全面的宿主 API：

### 用户管理
- `kick_user(user_id: u32)` - 踢出用户
- `ban_user_by_id(user_id: u32, reason: String)` - 封禁用户（ID）
- `get_user_info(user_id: u32)` - 获取用户信息
- `get_online_user_count()` - 获取在线用户数

### 房间管理
- `create_room(max_users: u32)` - 创建房间
- `disband_room(room_id: u32)` - 解散房间
- `get_room_info(room_id: u32)` - 获取房间信息
- `set_room_lock(room_id: u32, locked: bool)` - 设置房间锁定状态

### 事件系统
- `subscribe_event(event_type: String, handler: EventHandler)` - 订阅事件
- `unsubscribe_event(event_type: String)` - 取消订阅事件
- `emit_event(event_type: String, data: Value)` - 发射事件

### 命令系统
- `register_command(name: String, description: String, handler: CommandHandler)` - 注册命令
- `unregister_command(name: String)` - 取消注册命令

### 消息系统
- `send_message_to_user(user_id: u32, message: String)` - 发送消息给用户
- `broadcast_message_to_all(message: String)` - 广播消息给所有用户

### 配置管理
- `get_config(key: String)` - 获取配置
- `set_config(key: String, value: Value)` - 设置配置
- `save_config()` - 保存配置

## 事件系统

插件可以订阅服务器事件：

```rust
// 订阅事件
host_api.subscribe_event("user_connect", Box::new(|event| {
    let user_id = event.data["user_id"].as_u64().unwrap();
    println!("用户 {} 已连接", user_id);
    Ok(())
}), "my-plugin")?;

// 发射自定义事件
host_api.emit_event("custom_event", json!({"data": "值"}), "my-plugin")?;
```

### 预定义事件
- `server_start`, `server_shutdown` - 服务器启动/关闭
- `user_connect`, `user_disconnect` - 用户连接/断开
- `room_create`, `room_disband` - 房间创建/解散
- `user_join_room`, `user_leave_room` - 用户加入/离开房间
- `game_start`, `game_end` - 游戏开始/结束
- `command_input`, `message_send` - 命令输入/消息发送

## 命令系统

插件可以注册自定义命令：

```rust
host_api.register_command("mycommand", "我的自定义命令", Box::new(|cmd, args| {
    match cmd {
        "mycommand" => Ok(format!("命令执行，参数：{:?}", args)),
        _ => Err("未知命令".to_string()),
    }
}), "my-plugin")?;
```

## 安全与沙箱

插件在隔离的沙箱中运行，有可配置的安全策略：

```rust
let limits = ResourceLimits {
    max_memory: 256 * 1024 * 1024, // 256 MB
    max_cpu_time_ms: 1000, // 1 秒
    max_execution_time_ms: 5000, // 5 秒
    max_open_files: 32, // 最大打开文件数
    max_network_connections: 8, // 最大网络连接数
    max_allocation_size: 16 * 1024 * 1024, // 16 MB
    max_total_allocation: 128 * 1024 * 1024, // 128 MB
    max_stack_size: 8 * 1024 * 1024, // 8 MB
};

let policy = SecurityPolicy {
    allow_filesystem: false, // 禁止文件系统访问
    allow_network: false, // 禁止网络访问
    allow_subprocesses: false, // 禁止子进程
    allow_environment: false, // 禁止环境变量访问
    allow_system_info: false, // 禁止系统信息访问
    allowed_filesystem_paths: vec![], // 允许的文件系统路径
    allowed_network_hosts: vec![], // 允许的网络主机
    allowed_environment_vars: vec![], // 允许的环境变量
    max_recursion_depth: 100, // 最大递归深度
    enable_stack_protection: true, // 启用栈保护
    enable_memory_sandbox: true, // 启用内存沙箱
};
```

## 热重载

插件可以在不重启服务器的情况下重新加载：

```toml
# 热重载配置
[hot_reload]
enabled = true
poll_interval_secs = 1
debounce_delay_ms = 500
restart_on_config_change = true
restart_on_wasm_change = true
max_restart_attempts = 3
restart_cooldown_secs = 5
```

## 监控

监控插件性能和健康状态：

```rust
// 获取插件指标
let metrics = metrics_collector.get_plugin_metrics("my-plugin");
println!("内存使用：{} 字节", metrics.memory_usage);
println!("CPU 使用：{}%", metrics.cpu_usage);
println!("活动请求数：{}", metrics.active_requests);

// 检查插件健康状态
let health = health_monitor.get_plugin_health("my-plugin");
match health {
    HealthStatus::Healthy => println!("插件健康"),
    HealthStatus::Warning => println!("插件有警告"),
    HealthStatus::Critical => println!("插件状态危急"),
    HealthStatus::Unknown => println!("插件健康状态未知"),
}
```

## 依赖管理

插件可以声明依赖关系：

```toml
[dependencies]
database-plugin = ">=1.0.0"
auth-plugin = ">=2.1.0"

[optional-dependencies]
logging-plugin = ">=1.0.0"
```

## 测试

使用提供的测试工具测试插件：

```rust
#[cfg(test)]
mod tests {
    use super::*;
    
    #[test]
    fn test_plugin_metadata() {
        let metadata = plugin_metadata();
        assert_eq!(metadata.name, "my-plugin");
        assert_eq!(metadata.version, "0.1.0");
        assert_eq!(metadata.author, "你的名字");
    }
    
    #[tokio::test]
    async fn test_plugin_initialization() {
        // 使用模拟宿主 API 测试
        // ...
    }
}
```

## 生产环境构建

### 发布构建

```bash
# 优化大小
cargo build --release --target wasm32-wasip1

# 进一步优化
wasm-opt -O3 target/wasm32-wasip1/release/my_plugin.wasm -o my_plugin_optimized.wasm
```

### 安全加固

1. 启用所有沙箱限制
2. 设置保守的资源限制
3. 审查插件权限
4. 启用监控和告警
5. 定期安全更新

## 故障排除

### 常见问题

1. **插件加载失败**
   - 检查 WASM 兼容性（必须是 WASI 组件）
   - 验证 ABI 版本是否匹配
   - 检查依赖是否满足

2. **插件崩溃**
   - 检查资源限制
   - 查看错误日志
   - 使用更高的限制测试

3. **热重载不工作**
   - 验证文件监视器权限
   - 检查防抖设置
   - 确保插件支持热重载

### 日志记录

启用调试日志进行故障排除：

```bash
RUST_LOG=phira_mp_plugin=debug,my_plugin=debug cargo run
```

## 贡献指南

1. Fork 代码仓库
2. 创建特性分支
3. 实现你的更改
4. 添加测试
5. 更新文档
6. 提交 Pull Request

## 许可证

MIT 许可证 - 详见 LICENSE 文件。

## 支持

- 文档：[docs.phira-mp.dev](https://docs.phira-mp.dev)
- 问题：[GitHub Issues](https://github.com/TeamFlos/phira-mp/issues)
- Discord：[Phira MP Discord](https://discord.gg/phira-mp)