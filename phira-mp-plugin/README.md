# Phira MP Plugin System

A WebAssembly-based plugin system for Phira MP server, supporting multi-language plugins with sandboxed execution, hot-reload, and comprehensive host APIs.

## Features

- **WebAssembly-based**: Supports plugins written in Rust, C/C++, Go, AssemblyScript, etc.
- **Multi-language support**: Uses WIT (Wasm Interface Types) for language-agnostic interfaces
- **Sandboxed execution**: Plugins run in isolated environments with resource limits
- **Hot reload**: Reload plugins without restarting the server
- **Event system**: Subscribe to and emit server events
- **Command system**: Register and handle custom commands
- **Comprehensive APIs**: Full access to server functionality through host APIs
- **Dependency management**: Resolve plugin dependencies and check for conflicts
- **Monitoring**: Real-time metrics and health monitoring
- **Configuration**: Per-plugin configuration with hot reload
- **Security**: Fine-grained permissions and security policies

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    Phira MP Server                          │
├─────────────────────────────────────────────────────────────┤
│                  Plugin System Host                         │
│  ┌─────────────┐  ┌─────────────┐  ┌───────────────────┐  │
│  │Plugin Manager│  │ Event Bus   │  │ Command Registry  │  │
│  └─────────────┘  └─────────────┘  └───────────────────┘  │
│  ┌─────────────┐  ┌─────────────┐  ┌───────────────────┐  │
│  │WASM Runtime │  │ Host APIs    │  │ Sandbox Manager   │  │
│  └─────────────┘  └─────────────┘  └───────────────────┘  │
└─────────────────────────────────────────────────────────────┘
                            │
┌─────────────────────────────────────────────────────────────┐
│                    Plugin (WASM Module)                     │
│  ┌───────────────────────────────────────────────────────┐  │
│  │  Plugin Code (Rust/C/Go/etc.) → WASM → Component     │  │
│  └───────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
```

## Quick Start

### 1. Add Plugin System to Server

Add the plugin system dependency to your server's `Cargo.toml`:

```toml
[dependencies]
phira-mp-plugin = { path = "../phira-mp-plugin" }
```

### 2. Initialize Plugin System

```rust
use phira_mp_plugin::{PluginManager, EventBus, CommandRegistry, HostApi};
use std::sync::Arc;

async fn initialize_plugin_system() -> Result<(), Box<dyn std::error::Error>> {
    // Create core components
    let event_bus = Arc::new(EventBus::new());
    let command_registry = Arc::new(CommandRegistry::new());
    let host_api = Arc::new(HostApi::new(
        Arc::clone(&event_bus),
        Arc::clone(&command_registry),
        // Plugin manager will be set later
        Arc::new(()), // Placeholder
    ));
    
    // Create plugin manager
    let plugin_manager = Arc::new(PluginManager::new(
        "./plugins",
        Arc::clone(&event_bus),
        Arc::clone(&command_registry),
        Arc::clone(&host_api),
    )?);
    
    // Scan and load plugins
    plugin_manager.scan_and_load().await?;
    
    // Initialize and start plugins
    plugin_manager.initialize_all().await?;
    plugin_manager.start_all().await?;
    
    Ok(())
}
```

### 3. Create a Simple Plugin

Create a new Rust project for your plugin:

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
    // Register event handlers
    host_api.subscribe_event("server_start", Box::new(|event| {
        println!("Server started!");
        Ok(())
    }), "my-plugin").map_err(|e| e.to_string())?;
    
    // Register commands
    host_api.register_command("hello", "Say hello", Box::new(|cmd, args| {
        Ok(format!("Hello from my-plugin! Args: {:?}", args))
    }), "my-plugin").map_err(|e| e.to_string())?;
    
    Ok(())
}

#[no_mangle]
pub extern "C" fn plugin_metadata() -> PluginMetadata {
    PluginMetadata {
        name: "my-plugin".to_string(),
        version: "0.1.0".to_string(),
        author: "Your Name".to_string(),
        description: Some("My first Phira MP plugin".to_string()),
        entry_point: Some("plugin_init".to_string()),
        dependencies: None,
        permissions: Some(vec!["read_users".to_string()]),
        abi_version: "1.0.0".to_string(),
        category: Some("utility".to_string()),
        tags: Some(vec!["example".to_string()]),
        website: None,
        license: Some("MIT".to_string()),
        min_host_version: None,
        config_schema: None,
        custom: None,
    }
}
```

### 4. Build and Deploy Plugin

Build your plugin as a WASM component:

```bash
# Build the plugin
cd my-plugin
cargo build --release --target wasm32-wasip1

# Copy to plugins directory
cp target/wasm32-wasip1/release/my_plugin.wasm ../server/plugins/
```

## Plugin Manifest

Plugins require a manifest file (`plugin.toml`) with metadata:

```toml
name = "my-plugin"
version = "1.0.0"
author = "Your Name"
description = "A useful plugin for Phira MP"
entry_point = "plugin_init"
abi_version = "1.0.0"

[dependencies]
other-plugin = ">=1.0.0"

[permissions]
read_users = true
write_config = false

[config]
default_value = "default"
```

## Host APIs

Plugins have access to comprehensive host APIs:

### User Management
- `kick_user(user_id: u32)`
- `ban_user_by_id(user_id: u32, reason: String)`
- `get_user_info(user_id: u32)`
- `get_online_user_count()`

### Room Management
- `create_room(max_users: u32)`
- `disband_room(room_id: u32)`
- `get_room_info(room_id: u32)`
- `set_room_lock(room_id: u32, locked: bool)`

### Event System
- `subscribe_event(event_type: String, handler: EventHandler)`
- `unsubscribe_event(event_type: String)`
- `emit_event(event_type: String, data: Value)`

### Command System
- `register_command(name: String, description: String, handler: CommandHandler)`
- `unregister_command(name: String)`

### Messaging
- `send_message_to_user(user_id: u32, message: String)`
- `broadcast_message_to_all(message: String)`

### Configuration
- `get_config(key: String)`
- `set_config(key: String, value: Value)`
- `save_config()`

## Event System

Plugins can subscribe to server events:

```rust
// Subscribe to events
host_api.subscribe_event("user_connect", Box::new(|event| {
    let user_id = event.data["user_id"].as_u64().unwrap();
    println!("User {} connected", user_id);
    Ok(())
}), "my-plugin")?;

// Emit custom events
host_api.emit_event("custom_event", json!({"data": "value"}), "my-plugin")?;
```

### Predefined Events
- `server_start`, `server_shutdown`
- `user_connect`, `user_disconnect`
- `room_create`, `room_disband`
- `user_join_room`, `user_leave_room`
- `game_start`, `game_end`
- `command_input`, `message_send`

## Command System

Plugins can register custom commands:

```rust
host_api.register_command("mycommand", "My custom command", Box::new(|cmd, args| {
    match cmd {
        "mycommand" => Ok(format!("Command executed with args: {:?}", args)),
        _ => Err("Unknown command".to_string()),
    }
}), "my-plugin")?;
```

## Security & Sandboxing

Plugins run in isolated sandboxes with configurable security policies:

```rust
let limits = ResourceLimits {
    max_memory: 256 * 1024 * 1024, // 256 MB
    max_cpu_time_ms: 1000, // 1 second
    max_execution_time_ms: 5000, // 5 seconds
    max_open_files: 32,
    max_network_connections: 8,
    max_allocation_size: 16 * 1024 * 1024, // 16 MB
    max_total_allocation: 128 * 1024 * 1024, // 128 MB
    max_stack_size: 8 * 1024 * 1024, // 8 MB
};

let policy = SecurityPolicy {
    allow_filesystem: false,
    allow_network: false,
    allow_subprocesses: false,
    allow_environment: false,
    allow_system_info: false,
    allowed_filesystem_paths: vec![],
    allowed_network_hosts: vec![],
    allowed_environment_vars: vec![],
    max_recursion_depth: 100,
    enable_stack_protection: true,
    enable_memory_sandbox: true,
};
```

## Hot Reload

Plugins can be reloaded without restarting the server:

```toml
# Hot reload configuration
[hot_reload]
enabled = true
poll_interval_secs = 1
debounce_delay_ms = 500
restart_on_config_change = true
restart_on_wasm_change = true
max_restart_attempts = 3
restart_cooldown_secs = 5
```

## Monitoring

Monitor plugin performance and health:

```rust
// Get plugin metrics
let metrics = metrics_collector.get_plugin_metrics("my-plugin");
println!("Memory usage: {} bytes", metrics.memory_usage);
println!("CPU usage: {}%", metrics.cpu_usage);
println!("Active requests: {}", metrics.active_requests);

// Check plugin health
let health = health_monitor.get_plugin_health("my-plugin");
match health {
    HealthStatus::Healthy => println!("Plugin is healthy"),
    HealthStatus::Warning => println!("Plugin has warnings"),
    HealthStatus::Critical => println!("Plugin is critical"),
    HealthStatus::Unknown => println!("Plugin health unknown"),
}
```

## Dependency Management

Plugins can declare dependencies:

```toml
[dependencies]
database-plugin = ">=1.0.0"
auth-plugin = ">=2.1.0"

[optional-dependencies]
logging-plugin = ">=1.0.0"
```

## Testing

Test your plugins with the provided testing utilities:

```rust
#[cfg(test)]
mod tests {
    use super::*;
    
    #[test]
    fn test_plugin_metadata() {
        let metadata = plugin_metadata();
        assert_eq!(metadata.name, "my-plugin");
        assert_eq!(metadata.version, "0.1.0");
        assert_eq!(metadata.author, "Your Name");
    }
    
    #[tokio::test]
    async fn test_plugin_initialization() {
        // Test with mock host API
        // ...
    }
}
```

## Building for Production

### Release Build

```bash
# Optimize for size
cargo build --release --target wasm32-wasip1

# Further optimization
wasm-opt -O3 target/wasm32-wasip1/release/my_plugin.wasm -o my_plugin_optimized.wasm
```

### Security Hardening

1. Enable all sandbox restrictions
2. Set conservative resource limits
3. Review plugin permissions
4. Enable monitoring and alerting
5. Regular security updates

## Troubleshooting

### Common Issues

1. **Plugin fails to load**
   - Check WASM compatibility (must be WASI component)
   - Verify ABI version matches
   - Check dependencies are satisfied

2. **Plugin crashes**
   - Check resource limits
   - Review error logs
   - Test with increased limits

3. **Hot reload not working**
   - Verify file watcher permissions
   - Check debounce settings
   - Ensure plugin supports hot reload

### Logging

Enable debug logging for troubleshooting:

```bash
RUST_LOG=phira_mp_plugin=debug,my_plugin=debug cargo run
```

## Contributing

1. Fork the repository
2. Create a feature branch
3. Implement your changes
4. Add tests
5. Update documentation
6. Submit a pull request

## License

MIT License - see LICENSE file for details.

## Support

- Documentation: [docs.phira-mp.dev](https://docs.phira-mp.dev)
- Issues: [GitHub Issues](https://github.com/TeamFlos/phira-mp/issues)
- Discord: [Phira MP Discord](https://discord.gg/phira-mp)