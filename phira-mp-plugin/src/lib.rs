//! Phira MP Plugin System
//! 
//! A WebAssembly-based plugin system for Phira MP server, supporting multi-language plugins
//! with sandboxed execution, hot-reload, and comprehensive host APIs.

pub mod plugin_manager;
pub mod wasm_runtime;
pub mod config;
pub mod event_system;
pub mod command_system;
pub mod api_host;
pub mod metadata;
pub mod dependency;
pub mod sandbox;
pub mod monitoring;
pub mod hot_reload;
pub mod server_commands;
// pub mod wit;
// pub mod bindings;

// Re-exports
pub use plugin_manager::{PluginManager, create_plugin_system};
pub use metadata::PluginMetadata;
pub use config::PluginConfig;
pub use event_system::{Event, EventBus, EventHandler};
pub use command_system::{Command, CommandRegistry};
pub use api_host::HostApi;
pub use server_commands::ServerCommands;

/// Result type for plugin operations
pub type Result<T> = std::result::Result<T, Error>;

/// Error type for plugin system
#[derive(thiserror::Error, Debug)]
pub enum Error {
    #[error("I/O error: {0}")]
    Io(#[from] std::io::Error),
    #[error("Wasmtime error: {0}")]
    Wasmtime(#[from] wasmtime::Error),
    #[error("Plugin metadata error: {0}")]
    Metadata(String),
    #[error("Plugin dependency error: {0}")]
    Dependency(String),
    #[error("Plugin configuration error: {0}")]
    Config(String),
    #[error("Plugin runtime error: {0}")]
    Runtime(String),
    #[error("Plugin already loaded: {0}")]
    AlreadyLoaded(String),
    #[error("Plugin not found: {0}")]
    NotFound(String),
    #[error("Invalid plugin manifest: {0}")]
    InvalidManifest(String),
    #[error("Unsupported plugin ABI version: {0}")]
    UnsupportedAbiVersion(String),
    #[error("Security violation: {0}")]
    SecurityViolation(String),
    #[error("Event system error: {0}")]
    Event(String),
    #[error("Command system error: {0}")]
    Command(String),
    #[error("API error: {0}")]
    Api(String),
    #[error("Serialization error: {0}")]
    Serialization(#[from] serde_json::Error),
    #[error("Other error: {0}")]
    Other(String),
}

impl From<String> for Error {
    fn from(s: String) -> Self {
        Error::Other(s)
    }
}