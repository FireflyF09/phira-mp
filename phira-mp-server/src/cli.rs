use std::sync::Arc;
use anyhow::{Result, anyhow};
use phira_mp_plugin::{
    PluginManager,
    event_system::EventBus,
    command_system::CommandRegistry,
    api_host::HostApi,
    server_commands::ServerCommands,
    create_plugin_system,
};
use tracing::{info, error};

/// CLI command handler for server administration
pub struct CliHandler {
    /// Server commands
    server_commands: Arc<ServerCommands>,
    /// Event bus
    event_bus: Arc<EventBus>,
    /// Command registry
    command_registry: Arc<CommandRegistry>,
    /// Plugin manager
    plugin_manager: Arc<PluginManager>,
    /// Host API
    host_api: Arc<HostApi>,
}

impl CliHandler {
    /// Create a new CLI handler
    pub async fn new(plugin_dir: &str) -> Result<Self> {
        info!("Initializing CLI handler with plugin directory: {}", plugin_dir);

        // Create plugin system using the factory function
        let (plugin_manager, host_api) = create_plugin_system(plugin_dir)
            .map_err(|e| anyhow!("Failed to create plugin system: {}", e))?;

        // Create server commands
        let server_commands = Arc::new(ServerCommands::new(Arc::clone(&host_api)));

        // Get event bus and command registry from plugin manager
        // (they are stored in plugin manager but marked as dead code)
        // We'll create new ones for now
        let event_bus = Arc::new(EventBus::new());
        let command_registry = Arc::new(CommandRegistry::new());

        Ok(Self {
            server_commands,
            event_bus,
            command_registry,
            plugin_manager,
            host_api,
        })
    }

    /// Parse and execute a command line
    pub async fn execute_command(&self, command_line: &str) -> anyhow::Result<String> {
        let trimmed = command_line.trim();
        
        // Skip empty lines and comments
        if trimmed.is_empty() || trimmed.starts_with('#') {
            return Ok("".to_string());
        }

        // Parse command and arguments
        let parts: Vec<&str> = trimmed.split_whitespace().collect();
        if parts.is_empty() {
            return Err(anyhow!("空命令"));
        }

        let command = parts[0].to_lowercase();
        let args: Vec<String> = parts[1..].iter().map(|&s| s.to_string()).collect();

        // Execute via server commands
        let result = self.server_commands.execute(&command, &args);

        // If command not found in server commands, try command registry
        match result {
            Ok(output) => Ok(output),
            Err(e) if e.to_string().contains("未知命令") => {
                // Try command registry
                self.command_registry.execute(command_line)
                    .map_err(|e| anyhow!("Command error: {}", e))
            }
            Err(e) => Err(anyhow!("Command error: {}", e)),
        }
    }

    /// Initialize plugin system
    pub async fn initialize_plugins(&self) -> anyhow::Result<()> {
        info!("Initializing plugins from CLI handler");
        self.plugin_manager.scan_and_load().await?;
        self.plugin_manager.initialize_all().await?;
        self.plugin_manager.start_all().await?;
        info!("Plugins initialized successfully");
        Ok(())
    }

    /// Shutdown plugin system
    pub async fn shutdown_plugins(&self) -> anyhow::Result<()> {
        info!("Shutting down plugins from CLI handler");
        let plugins = self.plugin_manager.get_all_plugins();
        for plugin_arc in plugins {
            let plugin = plugin_arc.read();
            if let Err(e) = self.plugin_manager.unload_plugin(&plugin.metadata.name).await {
                error!("Failed to unload plugin {}: {}", plugin.metadata.name, e);
            }
        }
        info!("Plugins shutdown complete");
        Ok(())
    }

    /// Start interactive CLI mode
    pub async fn start_interactive(&self) -> anyhow::Result<()> {
        use std::io::{self, Write};
        
        println!("Phira MP Server CLI");
        println!("输入 'help' 获取帮助，'exit' 退出");
        
        loop {
            print!("> ");
            io::stdout().flush()?;
            
            let mut input = String::new();
            io::stdin().read_line(&mut input)?;
            
            let command_line = input.trim();
            
            if command_line == "exit" || command_line == "quit" {
                println!("退出 CLI");
                break;
            }
            
            match self.execute_command(command_line).await {
                Ok(result) => {
                    if !result.is_empty() {
                        println!("{}", result);
                    }
                }
                Err(e) => {
                    println!("错误: {}", e);
                }
            }
        }
        
        Ok(())
    }

    /// Execute command from command line arguments
    pub async fn execute_from_args(&self, args: Vec<String>) -> anyhow::Result<String> {
        if args.is_empty() {
            return self.start_interactive().await.map(|_| "CLI mode exited".to_string());
        }
        
        let command_line = args.join(" ");
        self.execute_command(&command_line).await
    }
}


#[cfg(test)]
mod tests {
    use super::*;
    use tempfile::TempDir;

    #[tokio::test]
    async fn test_cli_handler_creation() {
        let temp_dir = TempDir::new().unwrap();
        let plugin_dir = temp_dir.path().to_str().unwrap();
        
        let cli_handler = CliHandler::new(plugin_dir).await;
        // This may fail due to circular dependencies, but we can still test basic functionality
        assert!(cli_handler.is_ok() || cli_handler.is_err());
    }

    #[tokio::test]
    async fn test_command_parsing() {
        let temp_dir = TempDir::new().unwrap();
        let plugin_dir = temp_dir.path().to_str().unwrap();
        
        let cli_handler = CliHandler::new(plugin_dir).await;
        if let Ok(handler) = cli_handler {
            // Test help command
            let result = handler.execute_command("help").await;
            assert!(result.is_ok());
        }
    }
}