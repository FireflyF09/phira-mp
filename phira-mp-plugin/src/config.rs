use crate::Error;
use serde::{Deserialize, Serialize};
use std::{
    path::Path,
    collections::HashMap,
};
use config::{Config, File, FileFormat};
use notify::Watcher;

/// Plugin configuration
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct PluginConfig {
    /// Configuration values
    #[serde(flatten)]
    pub values: HashMap<String, toml::Value>,
    /// Configuration file path
    #[serde(skip)]
    pub path: Option<String>,
}

impl PluginConfig {
    /// Create an empty configuration
    pub fn new() -> Self {
        Self {
            values: HashMap::new(),
            path: None,
        }
    }

    /// Load configuration from a file
    pub fn from_file(path: impl AsRef<Path>) -> Result<Self, Error> {
        let path = path.as_ref();
        
        // Check if file exists
        if !path.exists() {
            return Ok(Self::new());
        }
        
        let config = Config::builder()
            .add_source(File::new(path.to_str().unwrap(), FileFormat::Toml))
            .build()
            .map_err(|e| Error::Config(format!("Failed to load config: {}", e)))?;
        
        let values: HashMap<String, toml::Value> = config
            .try_deserialize()
            .map_err(|e| Error::Config(format!("Failed to deserialize config: {}", e)))?;
        
        Ok(Self {
            values,
            path: Some(path.to_string_lossy().to_string()),
        })
    }

    /// Save configuration to file
    pub fn save(&self) -> Result<(), Error> {
        if let Some(path) = &self.path {
            self.save_to_file(path)
        } else {
            Err(Error::Config("No configuration file path specified".to_string()))
        }
    }

    /// Save configuration to specific file
    pub fn save_to_file(&self, path: impl AsRef<Path>) -> Result<(), Error> {
        let path = path.as_ref();
        let toml = toml::to_string(&self.values)
            .map_err(|e| Error::Config(format!("Failed to serialize config: {}", e)))?;
        
        // Ensure directory exists
        if let Some(parent) = path.parent() {
            std::fs::create_dir_all(parent)?;
        }
        
        std::fs::write(path, toml)?;
        Ok(())
    }

    /// Get a configuration value
    pub fn get<T>(&self, key: &str) -> Option<T>
    where
        T: serde::de::DeserializeOwned,
    {
        self.values.get(key).and_then(|value| {
            value.clone().try_into().ok()
        })
    }

    /// Get a configuration value with default
    pub fn get_or<T>(&self, key: &str, default: T) -> T
    where
        T: serde::de::DeserializeOwned + Clone,
    {
        self.get(key).unwrap_or(default)
    }

    /// Set a configuration value
    pub fn set<T>(&mut self, key: &str, value: T) -> Result<(), Error>
    where
        T: Serialize,
    {
        // Serialize to JSON first, then convert to TOML
        let json_value = serde_json::to_value(value)
            .map_err(|e| Error::Config(format!("Failed to serialize value: {}", e)))?;
        let json_str = serde_json::to_string(&json_value)
            .map_err(|e| Error::Config(format!("Failed to convert to JSON string: {}", e)))?;
        let toml_value = toml::from_str(&json_str)
            .map_err(|e| Error::Config(format!("Failed to convert JSON to TOML: {}", e)))?;
        self.values.insert(key.to_string(), toml_value);
        Ok(())
    }

    /// Remove a configuration value
    pub fn remove(&mut self, key: &str) -> Option<toml::Value> {
        self.values.remove(key)
    }

    /// Check if configuration has a key
    pub fn has_key(&self, key: &str) -> bool {
        self.values.contains_key(key)
    }

    /// Get all configuration keys
    pub fn keys(&self) -> impl Iterator<Item = &String> {
        self.values.keys()
    }

    /// Get all configuration values
    pub fn values(&self) -> impl Iterator<Item = &toml::Value> {
        self.values.values()
    }

    /// Get configuration as JSON string
    pub fn to_json(&self) -> Result<String, Error> {
        serde_json::to_string(&self.values)
            .map_err(|e| Error::Config(format!("Failed to convert to JSON: {}", e)))
    }

    /// Get configuration as TOML string
    pub fn to_toml(&self) -> Result<String, Error> {
        toml::to_string(&self.values)
            .map_err(|e| Error::Config(format!("Failed to convert to TOML: {}", e)))
    }

    /// Merge another configuration into this one
    pub fn merge(&mut self, other: &PluginConfig) {
        for (key, value) in &other.values {
            self.values.insert(key.clone(), value.clone());
        }
    }

    /// Clear all configuration values
    pub fn clear(&mut self) {
        self.values.clear();
    }

    /// Reload configuration from file
    pub fn reload(&mut self) -> Result<(), Error> {
        if let Some(path) = &self.path {
            let new_config = Self::from_file(path)?;
            self.values = new_config.values;
            Ok(())
        } else {
            Err(Error::Config("No configuration file path specified".to_string()))
        }
    }
}

impl Default for PluginConfig {
    fn default() -> Self {
        Self::new()
    }
}

/// Configuration watcher for hot reload
pub struct ConfigWatcher {
    watcher: notify::RecommendedWatcher,
    rx: std::sync::mpsc::Receiver<notify::Result<notify::Event>>,
}

impl ConfigWatcher {
    /// Create a new config watcher
    pub fn new() -> Result<Self, Error> {
        let (tx, rx) = std::sync::mpsc::channel();
        let watcher = notify::recommended_watcher(tx)
            .map_err(|e| Error::Config(format!("Failed to create watcher: {}", e)))?;
        
        Ok(Self { watcher, rx })
    }

    /// Watch a configuration file for changes
    pub fn watch(&mut self, path: impl AsRef<Path>) -> Result<(), Error> {
        let path = path.as_ref();
        self.watcher
            .watch(path, notify::RecursiveMode::NonRecursive)
            .map_err(|e| Error::Config(format!("Failed to watch file: {}", e)))?;
        Ok(())
    }

    /// Check for configuration changes
    pub fn check_changes(&self) -> Result<Vec<notify::Event>, Error> {
        let mut events = Vec::new();
        while let Ok(result) = self.rx.try_recv() {
            match result {
                Ok(event) => events.push(event),
                Err(e) => {
                    // Log the error but continue
                    tracing::warn!("File watch error: {}", e);
                }
            }
        }
        Ok(events)
    }

    /// Stop watching a file
    pub fn unwatch(&mut self, path: impl AsRef<Path>) -> Result<(), Error> {
        let path = path.as_ref();
        self.watcher
            .unwatch(path)
            .map_err(|e| Error::Config(format!("Failed to unwatch file: {}", e)))?;
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use tempfile::NamedTempFile;
    
    #[test]
    fn test_config_creation() {
        let config = PluginConfig::new();
        assert!(config.values.is_empty());
        assert!(config.path.is_none());
    }
    
    #[test]
    fn test_config_get_set() {
        let mut config = PluginConfig::new();
        
        config.set("test_key", "test_value").unwrap();
        assert_eq!(config.get::<String>("test_key"), Some("test_value".to_string()));
        
        config.set("int_key", 42).unwrap();
        assert_eq!(config.get::<i32>("int_key"), Some(42));
        
        config.set("bool_key", true).unwrap();
        assert_eq!(config.get::<bool>("bool_key"), Some(true));
    }
    
    #[test]
    fn test_config_file() {
        let temp_file = NamedTempFile::new().unwrap();
        let file_path = temp_file.path();
        
        let mut config = PluginConfig::new();
        config.set("key1", "value1").unwrap();
        config.set("key2", 123).unwrap();
        
        config.save_to_file(file_path).unwrap();
        
        let loaded_config = PluginConfig::from_file(file_path).unwrap();
        assert_eq!(loaded_config.get::<String>("key1"), Some("value1".to_string()));
        assert_eq!(loaded_config.get::<i32>("key2"), Some(123));
    }
}