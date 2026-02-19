use crate::Error;
use serde::{Deserialize, Serialize};
use std::{
    path::Path,
    collections::HashMap,
};
use toml;

/// Plugin metadata from manifest file
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct PluginMetadata {
    /// Plugin name (must be unique)
    pub name: String,
    /// Plugin version (semver)
    pub version: String,
    /// Plugin author
    pub author: String,
    /// Plugin description (optional)
    pub description: Option<String>,
    /// Plugin entry point (WASM function name)
    pub entry_point: Option<String>,
    /// Plugin dependencies (optional)
    pub dependencies: Option<Vec<String>>,
    /// Required permissions (optional)
    pub permissions: Option<Vec<String>>,
    /// Supported ABI version
    pub abi_version: String,
    /// Plugin category (optional)
    pub category: Option<String>,
    /// Plugin tags (optional)
    pub tags: Option<Vec<String>>,
    /// Plugin website (optional)
    pub website: Option<String>,
    /// Plugin license (optional)
    pub license: Option<String>,
    /// Minimum required host version (optional)
    pub min_host_version: Option<String>,
    /// Plugin configuration schema (optional)
    pub config_schema: Option<toml::Value>,
    /// Custom metadata fields (optional)
    #[serde(flatten)]
    pub custom: Option<HashMap<String, toml::Value>>,
}

impl PluginMetadata {
    /// Load plugin metadata from a TOML file
    pub fn from_file(path: impl AsRef<Path>) -> Result<Self, Error> {
        let path = path.as_ref();
        let content = std::fs::read_to_string(path)?;
        Self::from_str(&content)
    }

    /// Load plugin metadata from a string
    pub fn from_str(content: &str) -> Result<Self, Error> {
        let metadata: Self = toml::from_str(content)
            .map_err(|e| Error::InvalidManifest(format!("Failed to parse metadata: {}", e)))?;
        
        // Validate required fields
        if metadata.name.is_empty() {
            return Err(Error::InvalidManifest("Plugin name cannot be empty".to_string()));
        }
        
        if metadata.version.is_empty() {
            return Err(Error::InvalidManifest("Plugin version cannot be empty".to_string()));
        }
        
        if metadata.author.is_empty() {
            return Err(Error::InvalidManifest("Plugin author cannot be empty".to_string()));
        }
        
        if metadata.abi_version.is_empty() {
            return Err(Error::InvalidManifest("ABI version cannot be empty".to_string()));
        }
        
        // Validate ABI version format (semver)
        // Simple check for now
        if !metadata.abi_version.contains('.') {
            return Err(Error::InvalidManifest(
                "ABI version must be in semver format (e.g., 1.0.0)".to_string()
            ));
        }
        
        Ok(metadata)
    }

    /// Get plugin name
    pub fn name(&self) -> &str {
        &self.name
    }

    /// Get plugin version
    pub fn version(&self) -> &str {
        &self.version
    }

    /// Get plugin author
    pub fn author(&self) -> &str {
        &self.author
    }

    /// Get plugin description
    pub fn description(&self) -> Option<&str> {
        self.description.as_deref()
    }

    /// Get plugin dependencies
    pub fn dependencies(&self) -> Option<&Vec<String>> {
        self.dependencies.as_ref()
    }

    /// Get required permissions
    pub fn permissions(&self) -> Option<&Vec<String>> {
        self.permissions.as_ref()
    }

    /// Get ABI version
    pub fn abi_version(&self) -> &str {
        &self.abi_version
    }

    /// Get plugin category
    pub fn category(&self) -> Option<&str> {
        self.category.as_deref()
    }

    /// Get plugin tags
    pub fn tags(&self) -> Option<&Vec<String>> {
        self.tags.as_ref()
    }

    /// Check if plugin has a specific tag
    pub fn has_tag(&self, tag: &str) -> bool {
        self.tags.as_ref().map(|tags| tags.contains(&tag.to_string())).unwrap_or(false)
    }

    /// Check if plugin requires a specific permission
    pub fn requires_permission(&self, permission: &str) -> bool {
        self.permissions.as_ref().map(|perms| perms.contains(&permission.to_string())).unwrap_or(false)
    }

    /// Check if plugin depends on another plugin
    pub fn depends_on(&self, plugin_name: &str) -> bool {
        self.dependencies.as_ref().map(|deps| deps.contains(&plugin_name.to_string())).unwrap_or(false)
    }

    /// Convert metadata to TOML string
    pub fn to_toml(&self) -> Result<String, Error> {
        toml::to_string(self)
            .map_err(|e| Error::InvalidManifest(format!("Failed to serialize metadata: {}", e)))
    }

    /// Save metadata to file
    pub fn save_to_file(&self, path: impl AsRef<Path>) -> Result<(), Error> {
        let toml = self.to_toml()?;
        std::fs::write(path, toml)?;
        Ok(())
    }
}

impl Default for PluginMetadata {
    fn default() -> Self {
        Self {
            name: String::new(),
            version: String::new(),
            author: String::new(),
            description: None,
            entry_point: None,
            dependencies: None,
            permissions: None,
            abi_version: "1.0.0".to_string(),
            category: None,
            tags: None,
            website: None,
            license: None,
            min_host_version: None,
            config_schema: None,
            custom: None,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    
    #[test]
    fn test_metadata_parsing() {
        let toml_content = r#"
            name = "test-plugin"
            version = "1.0.0"
            author = "Test Author"
            description = "A test plugin"
            abi_version = "1.0.0"
            dependencies = ["dependency1", "dependency2"]
            permissions = ["read_users", "write_config"]
            category = "utility"
            tags = ["test", "example"]
        "#;
        
        let metadata = PluginMetadata::from_str(toml_content).unwrap();
        
        assert_eq!(metadata.name(), "test-plugin");
        assert_eq!(metadata.version(), "1.0.0");
        assert_eq!(metadata.author(), "Test Author");
        assert_eq!(metadata.description(), Some("A test plugin"));
        assert_eq!(metadata.abi_version(), "1.0.0");
        assert!(metadata.depends_on("dependency1"));
        assert!(metadata.requires_permission("read_users"));
        assert_eq!(metadata.category(), Some("utility"));
        assert!(metadata.has_tag("test"));
    }
    
    #[test]
    fn test_invalid_metadata() {
        let toml_content = r#"
            name = ""
            version = "1.0.0"
            author = "Test Author"
            abi_version = "1.0.0"
        "#;
        
        let result = PluginMetadata::from_str(toml_content);
        assert!(result.is_err());
    }
}