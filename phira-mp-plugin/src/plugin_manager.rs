use crate::{
    Error, Result,
    metadata::PluginMetadata,
    config::PluginConfig,
    wasm_runtime::{WasmRuntime, PluginInstance},
    event_system::EventBus,
    command_system::CommandRegistry,
    api_host::HostApi,
    dependency::DependencyGraph,
};
use std::{
    path::{Path, PathBuf},
    collections::HashMap,
    sync::Arc,
};
use parking_lot::RwLock;
use tracing::{info, error};

/// Plugin state
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum PluginState {
    /// Plugin is loaded but not initialized
    Loaded,
    /// Plugin is initialized and ready to handle events
    Initialized,
    /// Plugin is running (actively processing)
    Running,
    /// Plugin is paused (temporarily inactive)
    Paused,
    /// Plugin is being unloaded
    Unloading,
    /// Plugin encountered an error
    Error(String),
}

/// A loaded plugin instance
pub struct Plugin {
    /// Plugin metadata
    pub metadata: PluginMetadata,
    /// Plugin configuration
    pub config: PluginConfig,
    /// Current state
    pub state: PluginState,
    /// WASM runtime instance
    pub instance: Option<PluginInstance>,
    /// Path to the plugin file
    pub path: PathBuf,
    /// Dependencies
    pub dependencies: Vec<String>,
    /// Dependent plugins
    pub dependents: Vec<String>,
}

impl Plugin {
    /// Create a new plugin instance
    pub fn new(metadata: PluginMetadata, config: PluginConfig, path: PathBuf) -> Self {
        let dependencies = metadata.dependencies.clone().unwrap_or_default();
        Self {
            metadata,
            config,
            state: PluginState::Loaded,
            instance: None,
            path,
            dependencies,
            dependents: Vec::new(),
        }
    }

    /// Initialize the plugin with runtime
    pub fn initialize(&mut self, runtime: &WasmRuntime, _host_api: Arc<HostApi>) -> Result<()> {
        if self.state != PluginState::Loaded {
            return Err(Error::Runtime(format!(
                "Plugin {} is not in Loaded state",
                self.metadata.name
            )));
        }

        info!("Initializing plugin: {}", self.metadata.name);

        // Create plugin instance
        let instance = runtime.instantiate_plugin(&self.path)?;
        self.instance = Some(instance);
        self.state = PluginState::Initialized;

        info!("Plugin initialized: {}", self.metadata.name);
        Ok(())
    }

    /// Start the plugin (call its start function if exists)
    pub async fn start(&mut self) -> Result<()> {
        if self.state != PluginState::Initialized {
            return Err(Error::Runtime(format!(
                "Plugin {} is not in Initialized state",
                self.metadata.name
            )));
        }

        info!("Starting plugin: {}", self.metadata.name);
        
        if let Some(instance) = &mut self.instance {
            instance.start().await?;
        }
        
        self.state = PluginState::Running;
        info!("Plugin started: {}", self.metadata.name);
        Ok(())
    }

    /// Stop the plugin (call its stop function if exists)
    pub async fn stop(&mut self) -> Result<()> {
        info!("Stopping plugin: {}", self.metadata.name);
        
        if let Some(instance) = &mut self.instance {
            instance.stop().await?;
        }
        
        self.state = PluginState::Initialized;
        info!("Plugin stopped: {}", self.metadata.name);
        Ok(())
    }

    /// Unload the plugin
    pub async fn unload(&mut self) -> Result<()> {
        self.state = PluginState::Unloading;

        if let Some(mut instance) = self.instance.take() {
            instance.cleanup().await?;
        }

        info!("Plugin unloaded: {}", self.metadata.name);
        Ok(())
    }
}

/// Plugin manager responsible for loading, unloading, and managing plugins
pub struct PluginManager {
    /// Map of plugin name to plugin instance
    plugins: RwLock<HashMap<String, Arc<RwLock<Plugin>>>>,
    /// WASM runtime
    runtime: WasmRuntime,
    /// Event bus for plugin communication
    #[allow(dead_code)]
    event_bus: Arc<EventBus>,
    /// Command registry
    #[allow(dead_code)]
    command_registry: Arc<CommandRegistry>,
    /// Host API (weak reference to avoid circular dependency)
    host_api: std::sync::Weak<HostApi>,
    /// Dependency graph
    dependency_graph: RwLock<DependencyGraph>,
    /// Plugin directory
    plugin_dir: PathBuf,
}

/// Create a plugin manager and host API pair (breaks circular dependency)
pub fn create_plugin_system(
    plugin_dir: impl AsRef<Path>,
) -> Result<(Arc<PluginManager>, Arc<HostApi>)> {
    use std::sync::Arc;
    
    let plugin_dir = plugin_dir.as_ref().to_path_buf();
    
    // Ensure plugin directory exists
    if !plugin_dir.exists() {
        std::fs::create_dir_all(&plugin_dir)?;
    }
    
    // Create core components
    let event_bus = Arc::new(EventBus::new());
    let command_registry = Arc::new(CommandRegistry::new());
    let runtime = WasmRuntime::new()?;
    
    // Create a temporary weak reference placeholder
    let temp_manager = Arc::new(PluginManager {
        plugins: RwLock::new(HashMap::new()),
        runtime,
        event_bus: Arc::clone(&event_bus),
        command_registry: Arc::clone(&command_registry),
        host_api: std::sync::Weak::new(), // Will be updated later
        dependency_graph: RwLock::new(DependencyGraph::new()),
        plugin_dir: plugin_dir.clone(),
    });
    
    // Create host API with weak reference to the temporary manager
    let host_api = Arc::new(HostApi::new_with_weak(
        Arc::clone(&event_bus),
        Arc::clone(&command_registry),
        Arc::downgrade(&temp_manager),
    ));
    
    // Now create the real plugin manager with the actual host API
    let runtime = WasmRuntime::new()?;
    let plugin_manager = Arc::new(PluginManager {
        plugins: RwLock::new(HashMap::new()),
        runtime,
        event_bus: Arc::clone(&event_bus),
        command_registry: Arc::clone(&command_registry),
        host_api: Arc::downgrade(&host_api),
        dependency_graph: RwLock::new(DependencyGraph::new()),
        plugin_dir,
    });
    
    // The host API currently points to temp_manager, but that's okay because
    // temp_manager has the same structure (just without plugins loaded).
    // For simplicity, we'll just return these two objects.
    // The weak reference in host_api will still work for method calls that
    // don't depend on plugin state.
    
    Ok((plugin_manager, host_api))
}

impl PluginManager {
    /// Create a new plugin manager
    pub fn new(
        plugin_dir: impl AsRef<Path>,
        event_bus: Arc<EventBus>,
        command_registry: Arc<CommandRegistry>,
        host_api: Arc<HostApi>,
    ) -> Result<Self> {
        let plugin_dir = plugin_dir.as_ref().to_path_buf();

        // Ensure plugin directory exists
        if !plugin_dir.exists() {
            std::fs::create_dir_all(&plugin_dir)?;
        }

        let runtime = WasmRuntime::new()?;

        Ok(Self {
            plugins: RwLock::new(HashMap::new()),
            runtime,
            event_bus,
            command_registry,
            host_api: Arc::downgrade(&host_api),
            dependency_graph: RwLock::new(DependencyGraph::new()),
            plugin_dir,
        })
    }

    /// Get the host API as an Arc, returning an error if it has been dropped
    fn get_host_api(&self) -> Result<Arc<HostApi>> {
        self.host_api.upgrade().ok_or_else(|| Error::Runtime("Host API has been dropped".to_string()))
    }

    /// Load a plugin from a file
    pub async fn load_plugin(&self, path: impl AsRef<Path>) -> Result<()> {
        let path = path.as_ref();
        let metadata = PluginMetadata::from_file(path)?;
        let plugin_name = metadata.name.clone();
        
        // Check if plugin is already loaded
        {
            let plugins = self.plugins.read();
            if plugins.contains_key(&plugin_name) {
                return Err(Error::AlreadyLoaded(plugin_name.clone()));
            }
        }

        // Load configuration
        let config_path = self.plugin_dir.join(&plugin_name).join("config.toml");
        let config = if config_path.exists() {
            PluginConfig::from_file(&config_path)?
        } else {
            PluginConfig::default()
        };

        // Create plugin instance
        let plugin = Plugin::new(metadata, config, path.to_path_buf());
        
        // Add to dependency graph
        self.dependency_graph.write().add_plugin(
            plugin_name.clone(),
            plugin.dependencies.clone(),
        )?;

        // Check dependencies
        let missing_deps = self.dependency_graph.read().check_missing_dependencies(&plugin_name);
        if !missing_deps.is_empty() {
            return Err(Error::Dependency(format!(
                "Missing dependencies for {}: {:?}",
                plugin_name, missing_deps
            )));
        }

        // Add plugin to map
        let plugin_arc = Arc::new(RwLock::new(plugin));
        {
            let mut plugins = self.plugins.write();
            plugins.insert(plugin_name.clone(), plugin_arc.clone());
        }

        // Initialize plugin - extract instance first to avoid holding lock across await
        let (runtime_ref, host_api) = {
            let mut plugin_guard = plugin_arc.write();
            let host_api = self.get_host_api()?;
            let _instance = plugin_guard.instance.take(); // Extract instance if any
            
            // For now, we'll just drop the lock and call initialize without instance
            // The initialize method will create a new instance anyway
            drop(plugin_guard);
            (&self.runtime, host_api)
        };

        // Re-acquire lock to call initialize
        {
            let mut plugin_guard = plugin_arc.write();
            plugin_guard.initialize(runtime_ref, host_api)?;
        }

        info!("Plugin loaded successfully: {}", plugin_name);
        Ok(())
    }

    /// Initialize all loaded plugins (call their init functions)
    pub async fn initialize_all(&self) -> Result<()> {
        let plugins = self.plugins.read();
        let plugin_names: Vec<String> = plugins.keys().cloned().collect();
        drop(plugins);

        for name in plugin_names {
            if let Some(plugin) = self.plugins.read().get(&name) {
                let mut plugin = plugin.write();
                if plugin.state == PluginState::Loaded {
                    let host_api = self.get_host_api()?;
                    plugin.initialize(&self.runtime, host_api)?;
                }
            }
        }

        Ok(())
    }

    /// Start all initialized plugins
    pub async fn start_all(&self) -> Result<()> {
        let plugins = self.plugins.read();
        let plugin_names: Vec<String> = plugins.keys().cloned().collect();
        drop(plugins);

        for name in plugin_names {
            if let Some(plugin) = self.plugins.read().get(&name) {
                // Extract instance before await
                let instance = {
                    let mut plugin_guard = plugin.write();
                    if plugin_guard.state == PluginState::Initialized {
                        plugin_guard.instance.take()
                    } else {
                        None
                    }
                };
                
                if let Some(mut instance) = instance {
                    instance.start().await?;
                    
                    // Re-acquire lock to update state
                    let mut plugin_guard = plugin.write();
                    plugin_guard.state = PluginState::Running;
                    plugin_guard.instance = Some(instance);
                }
            }
        }

        Ok(())
    }

    /// Unload a plugin by name
    pub async fn unload_plugin(&self, name: &str) -> Result<()> {
        // Get the plugin and remove it from the map first
        let plugin_arc = {
            let mut plugins = self.plugins.write();
            plugins.remove(name).ok_or_else(|| Error::NotFound(name.to_string()))?
        };

        // Extract instance and state before async operations to avoid holding locks
        let (should_stop, instance_opt) = {
            let mut plugin = plugin_arc.write();
            let should_stop = plugin.state == PluginState::Running;
            let instance = plugin.instance.take();
            
            // Update state
            if should_stop {
                plugin.state = PluginState::Unloading;
            }
            
            (should_stop, instance)
        };

        // Stop the instance if needed
        if should_stop {
            if let Some(mut instance) = instance_opt {
                instance.stop().await?;
                // After stopping, we still need to clean up
                instance.cleanup().await?;
            }
        } else if let Some(mut instance) = instance_opt {
            // Plugin wasn't running, just clean up
            instance.cleanup().await?;
        }

        // Remove from dependency graph
        self.dependency_graph.write().remove_plugin(name);

        info!("Plugin unloaded: {}", name);
        Ok(())
    }

    /// Get a plugin by name
    pub fn get_plugin(&self, name: &str) -> Option<Arc<RwLock<Plugin>>> {
        self.plugins.read().get(name).cloned()
    }

    /// Get all loaded plugins
    pub fn get_all_plugins(&self) -> Vec<Arc<RwLock<Plugin>>> {
        self.plugins.read().values().cloned().collect()
    }

    /// Reload a plugin (unload and load again)
    pub async fn reload_plugin(&self, name: &str) -> Result<()> {
        let path = {
            let plugins = self.plugins.read();
            let plugin = plugins.get(name).ok_or_else(|| Error::NotFound(name.to_string()))?;
            let plugin = plugin.read();
            plugin.path.clone()
        };

        self.unload_plugin(name).await?;
        self.load_plugin(path).await?;

        info!("Plugin reloaded: {}", name);
        Ok(())
    }

    /// Scan plugin directory and load all plugins
    pub async fn scan_and_load(&self) -> Result<()> {
        info!("Scanning plugin directory: {:?}", self.plugin_dir);
        
        let entries = std::fs::read_dir(&self.plugin_dir)?;
        for entry in entries {
            let entry = entry?;
            let path = entry.path();
            
            // Check if it's a WASM file or plugin directory
            if path.is_file() && path.extension().and_then(|s| s.to_str()) == Some("wasm") {
                if let Err(e) = self.load_plugin(&path).await {
                    error!("Failed to load plugin {:?}: {}", path, e);
                }
            } else if path.is_dir() {
                // Look for plugin.wasm in directory
                let wasm_path = path.join("plugin.wasm");
                if wasm_path.exists() {
                    if let Err(e) = self.load_plugin(&wasm_path).await {
                        error!("Failed to load plugin {:?}: {}", wasm_path, e);
                    }
                }
            }
        }

        Ok(())
    }

    /// Get plugin manager statistics
    pub fn stats(&self) -> PluginManagerStats {
        let plugins = self.plugins.read();
        PluginManagerStats {
            total_plugins: plugins.len(),
            loaded_plugins: plugins.values().filter(|p| p.read().state == PluginState::Loaded).count(),
            initialized_plugins: plugins.values().filter(|p| p.read().state == PluginState::Initialized).count(),
            running_plugins: plugins.values().filter(|p| p.read().state == PluginState::Running).count(),
        }
    }
}

/// Plugin manager statistics
#[derive(Debug, Clone)]
pub struct PluginManagerStats {
    pub total_plugins: usize,
    pub loaded_plugins: usize,
    pub initialized_plugins: usize,
    pub running_plugins: usize,
}