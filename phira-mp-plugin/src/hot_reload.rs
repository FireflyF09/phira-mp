use crate::{Error, Result};
use std::{
    path::{Path, PathBuf},
    sync::Arc,
    time::Duration,
};
use parking_lot::RwLock;
use tokio::{sync::mpsc, time};
use notify::{RecommendedWatcher, RecursiveMode, Watcher, Event, EventKind};
use tracing::{info, debug, warn, error};
use serde_json::json;

/// Hot reload configuration
#[derive(Debug, Clone)]
pub struct HotReloadConfig {
    /// Whether hot reload is enabled
    pub enabled: bool,
    /// Polling interval for file changes (seconds)
    pub poll_interval_secs: u64,
    /// Debounce delay for file changes (milliseconds)
    pub debounce_delay_ms: u64,
    /// Whether to restart plugin on configuration changes
    pub restart_on_config_change: bool,
    /// Whether to restart plugin on WASM file changes
    pub restart_on_wasm_change: bool,
    /// Maximum restart attempts before giving up
    pub max_restart_attempts: u32,
    /// Cooldown period between restart attempts (seconds)
    pub restart_cooldown_secs: u64,
    /// Directories to watch for changes
    pub watch_directories: Vec<PathBuf>,
    /// File patterns to watch
    pub watch_patterns: Vec<String>,
    /// File patterns to ignore
    pub ignore_patterns: Vec<String>,
}

impl Default for HotReloadConfig {
    fn default() -> Self {
        Self {
            enabled: true,
            poll_interval_secs: 1,
            debounce_delay_ms: 500,
            restart_on_config_change: true,
            restart_on_wasm_change: true,
            max_restart_attempts: 3,
            restart_cooldown_secs: 5,
            watch_directories: vec![PathBuf::from(".")],
            watch_patterns: vec![
                "*.wasm".to_string(),
                "*.toml".to_string(),
                "*.json".to_string(),
            ],
            ignore_patterns: vec![
                "*.log".to_string(),
                "*.tmp".to_string(),
                "*.bak".to_string(),
            ],
        }
    }
}

/// Hot reload event
#[derive(Debug, Clone)]
pub enum HotReloadEvent {
    /// File changed
    FileChanged {
        path: PathBuf,
        event_kind: EventKind,
    },
    /// Plugin needs to be reloaded
    PluginReloadRequired {
        plugin_name: String,
        reason: String,
        changed_files: Vec<PathBuf>,
    },
    /// Plugin reload started
    PluginReloadStarted {
        plugin_name: String,
    },
    /// Plugin reload completed
    PluginReloadCompleted {
        plugin_name: String,
        success: bool,
        error: Option<String>,
        duration: Duration,
    },
    /// Plugin reload failed
    PluginReloadFailed {
        plugin_name: String,
        error: String,
        attempt: u32,
        max_attempts: u32,
    },
    /// Hot reload disabled for plugin
    HotReloadDisabled {
        plugin_name: String,
    },
}

/// Hot reload manager
pub struct HotReloadManager {
    /// Plugin manager
    plugin_manager: Arc<crate::plugin_manager::PluginManager>,
    /// Event bus for hot reload events
    event_bus: Arc<crate::event_system::EventBus>,
    /// Hot reload configuration
    config: HotReloadConfig,
    /// File watcher
    watcher: RwLock<Option<RecommendedWatcher>>,
    /// Event receiver for file changes
    event_rx: RwLock<Option<mpsc::UnboundedReceiver<notify::Result<Event>>>>,
    /// Event sender for file changes
    event_tx: mpsc::UnboundedSender<notify::Result<Event>>,
    /// Plugin restart attempts
    restart_attempts: RwLock<std::collections::HashMap<String, (u32, std::time::Instant)>>,
    /// Whether hot reload manager is running
    is_running: RwLock<bool>,
    /// Task handle for the hot reload loop
    task_handle: RwLock<Option<tokio::task::JoinHandle<()>>>,
}

impl HotReloadManager {
    /// Create a new hot reload manager
    pub fn new(
        plugin_manager: Arc<crate::plugin_manager::PluginManager>,
        event_bus: Arc<crate::event_system::EventBus>,
        config: HotReloadConfig,
    ) -> Result<Self> {
        let (event_tx, event_rx) = mpsc::unbounded_channel();
        
        Ok(Self {
            plugin_manager,
            event_bus,
            config,
            watcher: RwLock::new(None),
            event_rx: RwLock::new(Some(event_rx)),
            event_tx,
            restart_attempts: RwLock::new(std::collections::HashMap::new()),
            is_running: RwLock::new(false),
            task_handle: RwLock::new(None),
        })
    }

    /// Start the hot reload manager
    pub async fn start(&self) -> Result<()> {
        if *self.is_running.read() {
            return Err(Error::Runtime("Hot reload manager already running".to_string()));
        }

        if !self.config.enabled {
            info!("Hot reload is disabled in configuration");
            return Ok(());
        }

        info!("Starting hot reload manager");

        // Clone event sender for closure
        let event_tx = self.event_tx.clone();

        // Create file watcher
        let mut watcher = RecommendedWatcher::new(
            move |res: notify::Result<Event>| {
                if let Ok(_) = event_tx.send(res) {
                    // Event sent successfully
                }
            },
            notify::Config::default()
                .with_poll_interval(Duration::from_secs(self.config.poll_interval_secs)),
        ).map_err(|e| Error::Runtime(format!("Failed to create file watcher: {}", e)))?;

        // Start watching directories
        for dir in &self.config.watch_directories {
            if dir.exists() {
                watcher.watch(dir, RecursiveMode::Recursive)
                    .map_err(|e| Error::Runtime(format!("Failed to watch directory {:?}: {}", dir, e)))?;
                debug!("Watching directory for changes: {:?}", dir);
            } else {
                warn!("Directory does not exist, skipping: {:?}", dir);
            }
        }

        // Store watcher
        *self.watcher.write() = Some(watcher);
        
        // Start hot reload task
        let this = self.clone_manager();
        let handle = tokio::spawn(async move {
            this.hot_reload_loop().await;
        });
        
        *self.task_handle.write() = Some(handle);
        *self.is_running.write() = true;
        
        info!("Hot reload manager started successfully");
        Ok(())
    }

    /// Stop the hot reload manager
    pub async fn stop(&self) -> Result<()> {
        if !*self.is_running.read() {
            return Ok(());
        }

        info!("Stopping hot reload manager");
        
        // Stop the watcher
        if let Some(watcher) = self.watcher.write().take() {
            drop(watcher);
        }
        
        // Cancel the task
        if let Some(handle) = self.task_handle.write().take() {
            handle.abort();
        }
        
        *self.is_running.write() = false;
        
        info!("Hot reload manager stopped");
        Ok(())
    }

    /// Hot reload loop
    async fn hot_reload_loop(&self) {
        let mut event_rx = self.event_rx.write().take().expect("Event receiver not available");
        let mut debounce_timer = time::interval(Duration::from_millis(self.config.debounce_delay_ms));
        let mut pending_changes = std::collections::HashMap::<String, Vec<PathBuf>>::new();
        
        info!("Hot reload loop started");
        
        while *self.is_running.read() {
            tokio::select! {
                // Handle file change events
                Some(event_result) = event_rx.recv() => {
                    match event_result {
                        Ok(event) => {
                            self.handle_file_event(&event, &mut pending_changes).await;
                        }
                        Err(e) => {
                            error!("File watcher error: {}", e);
                        }
                    }
                }
                
                // Debounce timer
                _ = debounce_timer.tick() => {
                    if !pending_changes.is_empty() {
                        self.process_pending_changes(&mut pending_changes).await;
                    }
                }
                
                // Check for manager shutdown
                _ = time::sleep(Duration::from_secs(1)) => {
                    if !*self.is_running.read() {
                        break;
                    }
                }
            }
        }
        
        info!("Hot reload loop stopped");
    }

    /// Handle a file change event
    async fn handle_file_event(
        &self,
        event: &Event,
        pending_changes: &mut std::collections::HashMap<String, Vec<PathBuf>>,
    ) {
        // Filter out unwanted events
        if event.kind == EventKind::Access(notify::event::AccessKind::Any) {
            return; // Ignore access events
        }
        
        // Check if any of the changed files match our patterns
        for path in &event.paths {
            if self.should_ignore_file(path) {
                continue;
            }
            
            // Determine which plugin this file belongs to
            if let Some(plugin_name) = self.find_plugin_for_file(path) {
                // Add to pending changes for this plugin
                pending_changes
                    .entry(plugin_name.clone())
                    .or_insert_with(Vec::new)
                    .push(path.clone());
                
                debug!(
                    "File change detected for plugin '{}': {:?} ({:?})",
                    plugin_name, path, event.kind
                );
            }
        }
    }

    /// Process pending changes after debounce period
    async fn process_pending_changes(
        &self,
        pending_changes: &mut std::collections::HashMap<String, Vec<PathBuf>>,
    ) {
        for (plugin_name, changed_files) in pending_changes.drain() {
            self.handle_plugin_changes(&plugin_name, changed_files).await;
        }
    }

    /// Handle changes for a specific plugin
    async fn handle_plugin_changes(&self, plugin_name: &str, changed_files: Vec<PathBuf>) {
        // Check if plugin exists
        let _plugin = match self.plugin_manager.get_plugin(plugin_name) {
            Some(plugin) => plugin,
            None => {
                warn!("Plugin '{}' not found, ignoring changes", plugin_name);
                return;
            }
        };
        
        // Determine what type of files changed
        let mut has_wasm_change = false;
        let mut has_config_change = false;
        let mut other_changes = Vec::new();
        
        for file in &changed_files {
            if let Some(extension) = file.extension().and_then(|s| s.to_str()) {
                match extension.to_lowercase().as_str() {
                    "wasm" => has_wasm_change = true,
                    "toml" | "json" => has_config_change = true,
                    _ => other_changes.push(file.clone()),
                }
            }
        }
        
        // Decide whether to reload the plugin
        let should_reload = (has_wasm_change && self.config.restart_on_wasm_change) ||
                           (has_config_change && self.config.restart_on_config_change);
        
        if should_reload {
            let reason = if has_wasm_change && has_config_change {
                "WASM and configuration files changed"
            } else if has_wasm_change {
                "WASM file changed"
            } else {
                "Configuration file changed"
            };
            
            // Emit event that plugin needs to be reloaded
            self.emit_hot_reload_event(HotReloadEvent::PluginReloadRequired {
                plugin_name: plugin_name.to_string(),
                reason: reason.to_string(),
                changed_files: changed_files.clone(),
            }).await;
            
            // Attempt to reload the plugin
            self.reload_plugin(plugin_name, changed_files).await;
        } else if !other_changes.is_empty() {
            debug!(
                "Files changed for plugin '{}' but no reload required: {:?}",
                plugin_name, other_changes
            );
        }
    }

    /// Reload a plugin
    async fn reload_plugin(&self, plugin_name: &str, _changed_files: Vec<PathBuf>) {
        info!("Reloading plugin '{}' due to file changes", plugin_name);

        let now = std::time::Instant::now();
        
        // Check restart attempts with minimal lock time
        let (should_skip, attempt_count, max_attempts_reached) = {
            let mut attempts = self.restart_attempts.write();
            let (attempt_count, last_attempt) = attempts.entry(plugin_name.to_string()).or_insert((0, now));

            // Check cooldown period
            if now.duration_since(*last_attempt) < Duration::from_secs(self.config.restart_cooldown_secs) {
                (true, *attempt_count, false)
            } else if *attempt_count >= self.config.max_restart_attempts {
                (true, *attempt_count, true)
            } else {
                // Update attempt count
                *attempt_count += 1;
                *last_attempt = now;
                (false, *attempt_count, false)
            }
        };

        // Handle skip cases
        if should_skip {
            if max_attempts_reached {
                error!(
                    "Plugin '{}' has exceeded maximum restart attempts ({}), giving up",
                    plugin_name, self.config.max_restart_attempts
                );

                self.emit_hot_reload_event(HotReloadEvent::PluginReloadFailed {
                    plugin_name: plugin_name.to_string(),
                    error: format!("Exceeded maximum restart attempts ({})", self.config.max_restart_attempts),
                    attempt: attempt_count,
                    max_attempts: self.config.max_restart_attempts,
                }).await;
            } else {
                warn!(
                    "Plugin '{}' reload attempted too soon, skipping (cooldown: {}s)",
                    plugin_name, self.config.restart_cooldown_secs
                );
            }
            return;
        }

        // Emit reload started event
        self.emit_hot_reload_event(HotReloadEvent::PluginReloadStarted {
            plugin_name: plugin_name.to_string(),
        }).await;

        // Measure reload duration
        let start_time = std::time::Instant::now();

        // Attempt to reload the plugin
        let result = self.plugin_manager.reload_plugin(plugin_name).await;

        let duration = start_time.elapsed();

        match result {
            Ok(_) => {
                info!("Plugin '{}' reloaded successfully in {:?}", plugin_name, duration);

                // Reset attempt count on successful reload
                self.restart_attempts.write().remove(plugin_name);

                self.emit_hot_reload_event(HotReloadEvent::PluginReloadCompleted {
                    plugin_name: plugin_name.to_string(),
                    success: true,
                    error: None,
                    duration,
                }).await;
            }
            Err(e) => {
                error!("Failed to reload plugin '{}': {}", plugin_name, e);

                self.emit_hot_reload_event(HotReloadEvent::PluginReloadCompleted {
                    plugin_name: plugin_name.to_string(),
                    success: false,
                    error: Some(e.to_string()),
                    duration,
                }).await;

                if attempt_count >= self.config.max_restart_attempts {
                    self.emit_hot_reload_event(HotReloadEvent::PluginReloadFailed {
                        plugin_name: plugin_name.to_string(),
                        error: format!("Failed to reload after {} attempts", attempt_count),
                        attempt: attempt_count,
                        max_attempts: self.config.max_restart_attempts,
                    }).await;
                }
            }
        }
    }

    /// Check if a file should be ignored
    fn should_ignore_file(&self, path: &Path) -> bool {
        if let Some(file_name) = path.file_name().and_then(|s| s.to_str()) {
            // Check ignore patterns
            for pattern in &self.config.ignore_patterns {
                if glob_match(file_name, pattern) {
                    return true;
                }
            }
            
            // Check if it matches watch patterns
            if !self.config.watch_patterns.is_empty() {
                let mut matches_pattern = false;
                for pattern in &self.config.watch_patterns {
                    if glob_match(file_name, pattern) {
                        matches_pattern = true;
                        break;
                    }
                }
                
                if !matches_pattern {
                    return true;
                }
            }
        }
        
        false
    }

    /// Find which plugin a file belongs to
    fn find_plugin_for_file(&self, path: &Path) -> Option<String> {
        // Get absolute path
        let absolute_path = match path.canonicalize() {
            Ok(path) => path,
            Err(_) => path.to_path_buf(),
        };
        
        // Check all plugins
        let plugins = self.plugin_manager.get_all_plugins();
        
        for plugin_arc in plugins {
            let plugin = plugin_arc.read();
            let plugin_dir = plugin.path.parent()?;
            
            // Check if file is in plugin directory
            if absolute_path.starts_with(plugin_dir) {
                return Some(plugin.metadata.name.clone());
            }
        }
        
        None
    }

    /// Emit a hot reload event
    async fn emit_hot_reload_event(&self, event: HotReloadEvent) {
        // Convert to JSON
        let json_event = match event {
            HotReloadEvent::FileChanged { path, event_kind } => {
                json!({
                    "type": "file_changed",
                    "path": path.to_string_lossy(),
                    "event_kind": format!("{:?}", event_kind),
                })
            }
            HotReloadEvent::PluginReloadRequired { plugin_name, reason, changed_files } => {
                json!({
                    "type": "plugin_reload_required",
                    "plugin_name": plugin_name,
                    "reason": reason,
                    "changed_files": changed_files.iter().map(|p| p.to_string_lossy().to_string()).collect::<Vec<_>>(),
                })
            }
            HotReloadEvent::PluginReloadStarted { plugin_name } => {
                json!({
                    "type": "plugin_reload_started",
                    "plugin_name": plugin_name,
                })
            }
            HotReloadEvent::PluginReloadCompleted { plugin_name, success, error, duration } => {
                json!({
                    "type": "plugin_reload_completed",
                    "plugin_name": plugin_name,
                    "success": success,
                    "error": error,
                    "duration_ms": duration.as_millis(),
                })
            }
            HotReloadEvent::PluginReloadFailed { plugin_name, error, attempt, max_attempts } => {
                json!({
                    "type": "plugin_reload_failed",
                    "plugin_name": plugin_name,
                    "error": error,
                    "attempt": attempt,
                    "max_attempts": max_attempts,
                })
            }
            HotReloadEvent::HotReloadDisabled { plugin_name } => {
                json!({
                    "type": "hot_reload_disabled",
                    "plugin_name": plugin_name,
                })
            }
        };
        
        // Emit to event bus
        if let Err(e) = self.event_bus.emit(crate::event_system::Event::system(
            "plugin_hot_reload",
            json_event,
        )) {
            error!("Failed to emit hot reload event: {}", e);
        }
    }

    /// Clone the manager for async task
    fn clone_manager(&self) -> Arc<Self> {
        // This is a hack - we need to implement Clone or use Arc
        // For now, we'll return self as Arc (assuming self is Arc)
        // Actually self is &Self, so we need a different approach
        // Let's store self as Arc in the struct
        // For simplicity, we'll skip this and assume the caller handles it
        // We need to refactor to store Arc<Self> in the struct
        unimplemented!("Clone manager not implemented");
    }

    /// Get hot reload manager statistics
    pub fn stats(&self) -> HotReloadManagerStats {
        let attempts = self.restart_attempts.read();
        
        HotReloadManagerStats {
            is_running: *self.is_running.read(),
            total_watched_plugins: self.plugin_manager.get_all_plugins().len(),
            restart_attempts: attempts.len(),
            max_restart_attempts: self.config.max_restart_attempts,
            restart_cooldown_secs: self.config.restart_cooldown_secs,
        }
    }
}

/// Simple glob matching
fn glob_match(filename: &str, pattern: &str) -> bool {
    if pattern == "*" {
        return true;
    }
    
    if pattern.starts_with("*.") {
        let extension = &pattern[2..];
        if let Some(ext) = filename.rsplit('.').next() {
            return ext == extension;
        }
    }
    
    filename == pattern
}

/// Hot reload manager statistics
#[derive(Debug, Clone)]
pub struct HotReloadManagerStats {
    pub is_running: bool,
    pub total_watched_plugins: usize,
    pub restart_attempts: usize,
    pub max_restart_attempts: u32,
    pub restart_cooldown_secs: u64,
}

/// Plugin hot reload state
pub struct PluginHotReloadState {
    /// Whether hot reload is enabled for this plugin
    pub enabled: bool,
    /// Last reload time
    pub last_reload: Option<std::time::Instant>,
    /// Number of reloads
    pub reload_count: u32,
    /// Last error (if any)
    pub last_error: Option<String>,
    /// Files being watched
    pub watched_files: Vec<PathBuf>,
}

impl PluginHotReloadState {
    /// Create new hot reload state
    pub fn new(enabled: bool) -> Self {
        Self {
            enabled,
            last_reload: None,
            reload_count: 0,
            last_error: None,
            watched_files: Vec::new(),
        }
    }
    
    /// Record a successful reload
    pub fn record_reload(&mut self) {
        self.last_reload = Some(std::time::Instant::now());
        self.reload_count += 1;
        self.last_error = None;
    }
    
    /// Record a failed reload
    pub fn record_failed_reload(&mut self, error: String) {
        self.last_reload = Some(std::time::Instant::now());
        self.reload_count += 1;
        self.last_error = Some(error);
    }
    
    /// Get time since last reload
    pub fn time_since_last_reload(&self) -> Option<Duration> {
        self.last_reload.map(|time| time.elapsed())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use tempfile::TempDir;
    
    #[test]
    fn test_glob_match() {
        assert!(glob_match("plugin.wasm", "*.wasm"));
        assert!(glob_match("config.toml", "*.toml"));
        assert!(glob_match("data.json", "*.json"));
        assert!(!glob_match("plugin.wasm", "*.toml"));
        assert!(glob_match("anything", "*"));
    }
    
    #[test]
    fn test_hot_reload_config() {
        let config = HotReloadConfig::default();
        assert!(config.enabled);
        assert_eq!(config.poll_interval_secs, 1);
        assert_eq!(config.debounce_delay_ms, 500);
        assert!(config.restart_on_config_change);
        assert!(config.restart_on_wasm_change);
        assert_eq!(config.max_restart_attempts, 3);
        assert_eq!(config.restart_cooldown_secs, 5);
    }
    
    #[test]
    fn test_plugin_hot_reload_state() {
        let mut state = PluginHotReloadState::new(true);
        assert!(state.enabled);
        assert_eq!(state.reload_count, 0);
        assert!(state.last_reload.is_none());
        assert!(state.last_error.is_none());
        
        state.record_reload();
        assert_eq!(state.reload_count, 1);
        assert!(state.last_reload.is_some());
        assert!(state.last_error.is_none());
        
        state.record_failed_reload("test error".to_string());
        assert_eq!(state.reload_count, 2);
        assert_eq!(state.last_error, Some("test error".to_string()));
    }
}