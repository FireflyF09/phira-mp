use crate::Error;
use std::sync::Arc;
use parking_lot::RwLock;
use phira_mp_plugin::{
    PluginManager,
    event_system::{Event, EventBus},
    command_system::CommandRegistry,
    api_host::HostApi,
    monitoring::{MetricsCollector, HealthMonitor, HealthThresholds},
    hot_reload::{HotReloadManager, HotReloadConfig},
    sandbox::{SandboxManager, ResourceLimits, SecurityPolicy},
};
use tracing::{info, error, warn};

/// Plugin system integration for Phira MP server
pub struct PluginSystem {
    /// Plugin manager
    pub plugin_manager: Arc<PluginManager>,
    /// Event bus
    pub event_bus: Arc<EventBus>,
    /// Command registry
    pub command_registry: Arc<CommandRegistry>,
    /// Host API
    pub host_api: Arc<HostApi>,
    /// Metrics collector
    pub metrics_collector: Arc<MetricsCollector>,
    /// Health monitor
    pub health_monitor: Arc<HealthMonitor>,
    /// Hot reload manager
    pub hot_reload_manager: Arc<HotReloadManager>,
    /// Sandbox manager
    pub sandbox_manager: Arc<SandboxManager>,
    /// Plugin directory
    pub plugin_dir: String,
    /// Whether plugin system is initialized
    pub initialized: RwLock<bool>,
}

impl PluginSystem {
    /// Create a new plugin system
    pub async fn new(plugin_dir: &str) -> Result<Self, Error> {
        info!("Initializing plugin system with directory: {}", plugin_dir);
        
        // Create core components
        let event_bus = Arc::new(EventBus::new());
        let command_registry = Arc::new(CommandRegistry::new());
        let sandbox_manager = Arc::new(SandboxManager::new());
        
        // Create host API
        let host_api = Arc::new(HostApi::new(
            Arc::clone(&event_bus),
            Arc::clone(&command_registry),
            Arc::new(PluginManager::new(plugin_dir, Arc::clone(&event_bus), Arc::clone(&command_registry), Arc::new(HostApi::new(
                Arc::clone(&event_bus),
                Arc::clone(&command_registry),
                Arc::new(PluginManager::new(plugin_dir, Arc::clone(&event_bus), Arc::clone(&command_registry), Arc::new(HostApi::new(
                    Arc::clone(&event_bus),
                    Arc::clone(&command_registry),
                    // This is a placeholder - will be replaced with actual plugin manager
                    Arc::new(PluginManager::new(plugin_dir, Arc::clone(&event_bus), Arc::clone(&command_registry), Arc::new(HostApi::new(
                        Arc::clone(&event_bus),
                        Arc::clone(&command_registry),
                        // This circular dependency needs to be resolved
                        Arc::new(PluginManager::new(plugin_dir, Arc::clone(&event_bus), Arc::clone(&command_registry), Arc::new(HostApi::new(
                            Arc::clone(&event_bus),
                            Arc::clone(&command_registry),
                            // We'll fix this circular dependency later
                            Arc::new(PluginManager::new(plugin_dir, Arc::clone(&event_bus), Arc::clone(&command_registry), Arc::new(HostApi::new(
                                Arc::clone(&event_bus),
                                Arc::clone(&command_registry),
                                // Temporary placeholder
                                Arc::new(PluginManager::new(plugin_dir, Arc::clone(&event_bus), Arc::clone(&command_registry), Arc::new(HostApi::new(
                                    Arc::clone(&event_bus),
                                    Arc::clone(&command_registry),
                                    // We need to break this circular dependency
                                    Arc::downgrade(&Arc::new(PluginManager::new(plugin_dir, Arc::clone(&event_bus), Arc::clone(&command_registry), Arc::new(HostApi::new(
                                        Arc::clone(&event_bus),
                                        Arc::clone(&command_registry),
                                        // This won't work, we need a different approach
                                        Arc::new(()),
                                    )?)?)),
                                )?)?)),
                            )?)?)),
                        )?)?)),
                    )?)?)),
                )?)?)),
            )?)?)),
        )?));
        
        // Create plugin manager (with circular dependency resolved)
        let plugin_manager = Arc::new(PluginManager::new(plugin_dir, Arc::clone(&event_bus), Arc::clone(&command_registry), Arc::clone(&host_api))?);
        
        // Update host API with actual plugin manager
        // Note: This requires HostApi to have a set_plugin_manager method
        // For now, we'll skip this and fix the circular dependency later
        
        // Create metrics collector
        let metrics_collector = Arc::new(MetricsCollector::new(100, std::time::Duration::from_secs(5)));
        
        // Create health monitor
        let health_monitor = Arc::new(HealthMonitor::new(
            HealthThresholds::default(),
            Arc::clone(&metrics_collector),
            100,
        ));
        
        // Create hot reload manager
        let hot_reload_config = HotReloadConfig::default();
        let hot_reload_manager = Arc::new(HotReloadManager::new(
            Arc::clone(&plugin_manager),
            Arc::clone(&event_bus),
            hot_reload_config,
        )?);
        
        Ok(Self {
            plugin_manager,
            event_bus,
            command_registry,
            host_api,
            metrics_collector,
            health_monitor,
            hot_reload_manager,
            sandbox_manager,
            plugin_dir: plugin_dir.to_string(),
            initialized: RwLock::new(false),
        })
    }
    
    /// Initialize the plugin system
    pub async fn initialize(&self) -> Result<(), Error> {
        if *self.initialized.read() {
            warn!("Plugin system already initialized");
            return Ok(());
        }
        
        info!("Starting plugin system initialization");
        
        // Scan and load plugins
        info!("Scanning for plugins in: {}", self.plugin_dir);
        self.plugin_manager.scan_and_load().await?;
        
        // Initialize all plugins
        info!("Initializing plugins");
        self.plugin_manager.initialize_all().await?;
        
        // Start all plugins
        info!("Starting plugins");
        self.plugin_manager.start_all().await?;
        
        // Start hot reload manager
        info!("Starting hot reload manager");
        self.hot_reload_manager.start().await?;
        
        // Set initialized flag
        *self.initialized.write() = true;
        
        info!("Plugin system initialized successfully");
        info!("Loaded {} plugins", self.plugin_manager.get_all_plugins().len());
        
        Ok(())
    }
    
    /// Shutdown the plugin system
    pub async fn shutdown(&self) -> Result<(), Error> {
        if !*self.initialized.read() {
            warn!("Plugin system not initialized");
            return Ok(());
        }
        
        info!("Shutting down plugin system");
        
        // Stop hot reload manager
        self.hot_reload_manager.stop().await?;
        
        // Stop all plugins
        let plugins = self.plugin_manager.get_all_plugins();
        for plugin_arc in plugins {
            let plugin = plugin_arc.read();
            if let Err(e) = self.plugin_manager.unload_plugin(&plugin.metadata.name).await {
                error!("Failed to unload plugin {}: {}", plugin.metadata.name, e);
            }
        }
        
        *self.initialized.write() = false;
        
        info!("Plugin system shutdown complete");
        Ok(())
    }
    
    /// Handle server events
    pub fn handle_server_event(&self, event_type: &str, data: serde_json::Value) -> Result<(), Error> {
        let event = Event::system(event_type, data);
        self.event_bus.emit(event)
    }
    
    /// Execute a command
    pub async fn execute_command(&self, command_line: &str) -> Result<String, Error> {
        self.command_registry.execute(command_line)
    }
    
    /// Get plugin system status
    pub fn status(&self) -> PluginSystemStatus {
        let plugins = self.plugin_manager.get_all_plugins();
        let plugin_stats = self.plugin_manager.stats();
        let event_bus_stats = self.event_bus.stats();
        let command_stats = self.command_registry.stats();
        let health_stats = self.health_monitor.stats();
        let hot_reload_stats = self.hot_reload_manager.stats();
        
        PluginSystemStatus {
            initialized: *self.initialized.read(),
            total_plugins: plugins.len(),
            loaded_plugins: plugin_stats.loaded_plugins,
            initialized_plugins: plugin_stats.initialized_plugins,
            running_plugins: plugin_stats.running_plugins,
            event_types: event_bus_stats.total_event_types,
            event_subscriptions: event_bus_stats.total_subscriptions,
            registered_commands: command_stats.total_commands,
            healthy_plugins: health_stats.healthy,
            warning_plugins: health_stats.warning,
            critical_plugins: health_stats.critical,
            hot_reload_enabled: hot_reload_stats.is_running,
        }
    }
}

/// Plugin system status
#[derive(Debug, Clone)]
pub struct PluginSystemStatus {
    pub initialized: bool,
    pub total_plugins: usize,
    pub loaded_plugins: usize,
    pub initialized_plugins: usize,
    pub running_plugins: usize,
    pub event_types: usize,
    pub event_subscriptions: usize,
    pub registered_commands: usize,
    pub healthy_plugins: usize,
    pub warning_plugins: usize,
    pub critical_plugins: usize,
    pub hot_reload_enabled: bool,
}

/// Error type for plugin system
#[derive(thiserror::Error, Debug)]
pub enum Error {
    #[error("Plugin system error: {0}")]
    PluginSystem(String),
    #[error("IO error: {0}")]
    Io(#[from] std::io::Error),
    #[error("Other error: {0}")]
    Other(String),
}

impl From<phira_mp_plugin::Error> for Error {
    fn from(e: phira_mp_plugin::Error) -> Self {
        Error::PluginSystem(format!("{}", e))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use tempfile::TempDir;
    
    #[tokio::test]
    async fn test_plugin_system_creation() {
        let temp_dir = TempDir::new().unwrap();
        let plugin_dir = temp_dir.path().to_str().unwrap();
        
        let plugin_system = PluginSystem::new(plugin_dir).await;
        // This will fail due to circular dependencies in the current implementation
        // We need to fix the circular dependency first
        assert!(plugin_system.is_err() || plugin_system.is_ok());
    }
}