//! A simple example plugin for Phira MP
//! 
//! This plugin demonstrates basic plugin functionality including:
//! - Event handling
//! - Command registration
//! - Configuration management

use std::sync::Arc;
use phira_mp_plugin::{
    PluginMetadata, PluginConfig,
    event_system::{Event, EventHandler},
    command_system::{Command, CommandHandler},
    Error, Result,
};
use serde_json::{json, Value};

/// Simple plugin structure
pub struct SimplePlugin {
    metadata: PluginMetadata,
    config: PluginConfig,
    event_handler: Option<EventHandler>,
    command_handler: Option<CommandHandler>,
}

impl SimplePlugin {
    /// Create a new simple plugin
    pub fn new() -> Result<Self> {
        let metadata = PluginMetadata {
            name: "simple-plugin".to_string(),
            version: "1.0.0".to_string(),
            author: "Example Author".to_string(),
            description: Some("A simple example plugin for Phira MP".to_string()),
            entry_point: None,
            dependencies: None,
            permissions: Some(vec![
                "read_users".to_string(),
                "write_config".to_string(),
            ]),
            abi_version: "1.0.0".to_string(),
            category: Some("example".to_string()),
            tags: Some(vec!["example".to_string(), "simple".to_string()]),
            website: None,
            license: Some("MIT".to_string()),
            min_host_version: None,
            config_schema: None,
            custom: None,
        };
        
        let config = PluginConfig::new();
        
        Ok(Self {
            metadata,
            config,
            event_handler: None,
            command_handler: None,
        })
    }
    
    /// Initialize the plugin
    pub async fn initialize(&mut self, host_api: Arc<phira_mp_plugin::api_host::HostApi>) -> Result<()> {
        // Register event handler
        let event_handler: EventHandler = Box::new(|event| {
            println!("[SimplePlugin] Event received: {} from {}", event.event_type, event.source);
            Ok(())
        });
        
        // Subscribe to server start event
        host_api.subscribe_event("server_start", event_handler.clone(), "simple-plugin")?;
        
        self.event_handler = Some(event_handler);
        
        // Register command handler
        let command_handler: CommandHandler = Box::new(|command, args| {
            match command {
                "hello" => Ok(format!("Hello from SimplePlugin! Args: {:?}", args)),
                "echo" => Ok(args.join(" ")),
                "ping" => Ok("pong".to_string()),
                _ => Err(Error::Command(format!("Unknown command: {}", command))),
            }
        });
        
        // Register commands
        host_api.register_command("hello", "Say hello from the plugin", command_handler.clone(), "simple-plugin")?;
        host_api.register_command("echo", "Echo back the arguments", command_handler.clone(), "simple-plugin")?;
        host_api.register_command("ping", "Respond with pong", command_handler.clone(), "simple-plugin")?;
        
        self.command_handler = Some(command_handler);
        
        // Log initialization
        host_api.log_info("SimplePlugin initialized successfully");
        
        Ok(())
    }
    
    /// Start the plugin
    pub async fn start(&self, host_api: Arc<phira_mp_plugin::api_host::HostApi>) -> Result<()> {
        host_api.log_info("SimplePlugin starting");
        
        // Emit a custom event
        host_api.emit_event("plugin_started", json!({"plugin": "simple-plugin"}), "simple-plugin")?;
        
        host_api.log_info("SimplePlugin started");
        Ok(())
    }
    
    /// Stop the plugin
    pub async fn stop(&self, host_api: Arc<phira_mp_plugin::api_host::HostApi>) -> Result<()> {
        host_api.log_info("SimplePlugin stopping");
        
        // Unregister event handler
        if let Some(_) = &self.event_handler {
            host_api.unsubscribe_event("server_start", "simple-plugin")?;
        }
        
        // Unregister commands
        host_api.unregister_command("hello")?;
        host_api.unregister_command("echo")?;
        host_api.unregister_command("ping")?;
        
        host_api.log_info("SimplePlugin stopped");
        Ok(())
    }
    
    /// Get plugin metadata
    pub fn metadata(&self) -> &PluginMetadata {
        &self.metadata
    }
    
    /// Get plugin configuration
    pub fn config(&self) -> &PluginConfig {
        &self.config
    }
    
    /// Set plugin configuration
    pub fn set_config(&mut self, config: PluginConfig) {
        self.config = config;
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    
    #[test]
    fn test_plugin_creation() {
        let plugin = SimplePlugin::new();
        assert!(plugin.is_ok());
        
        let plugin = plugin.unwrap();
        assert_eq!(plugin.metadata().name(), "simple-plugin");
        assert_eq!(plugin.metadata().version(), "1.0.0");
        assert_eq!(plugin.metadata().author(), "Example Author");
    }
    
    #[tokio::test]
    async fn test_plugin_initialization() {
        // Note: This test would require a mock HostApi
        // For now, just test that the plugin can be created
        let plugin = SimplePlugin::new().unwrap();
        assert_eq!(plugin.metadata().name(), "simple-plugin");
    }
}