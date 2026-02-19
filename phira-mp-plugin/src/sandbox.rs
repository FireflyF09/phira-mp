use crate::Error;
use std::{
    sync::Arc,
    time::{Duration, Instant},
};
use parking_lot::RwLock;
use tracing::error;

/// Resource limits for a plugin
#[derive(Debug, Clone)]
pub struct ResourceLimits {
    /// Maximum memory usage in bytes
    pub max_memory: usize,
    /// Maximum CPU time per operation in milliseconds
    pub max_cpu_time_ms: u64,
    /// Maximum execution time per call in milliseconds
    pub max_execution_time_ms: u64,
    /// Maximum number of files that can be opened
    pub max_open_files: usize,
    /// Maximum number of network connections
    pub max_network_connections: usize,
    /// Maximum size of a single allocation in bytes
    pub max_allocation_size: usize,
    /// Maximum total allocation in bytes
    pub max_total_allocation: usize,
    /// Maximum stack size in bytes
    pub max_stack_size: usize,
}

impl Default for ResourceLimits {
    fn default() -> Self {
        Self {
            max_memory: 256 * 1024 * 1024, // 256 MB
            max_cpu_time_ms: 1000, // 1 second
            max_execution_time_ms: 5000, // 5 seconds
            max_open_files: 32,
            max_network_connections: 8,
            max_allocation_size: 16 * 1024 * 1024, // 16 MB
            max_total_allocation: 128 * 1024 * 1024, // 128 MB
            max_stack_size: 8 * 1024 * 1024, // 8 MB
        }
    }
}

/// Resource usage statistics
#[derive(Debug, Clone, Default)]
pub struct ResourceUsage {
    /// Memory usage in bytes
    pub memory_used: usize,
    /// CPU time used in milliseconds
    pub cpu_time_used_ms: u64,
    /// Total execution time in milliseconds
    pub execution_time_used_ms: u64,
    /// Number of files currently open
    pub open_files: usize,
    /// Number of active network connections
    pub network_connections: usize,
    /// Number of allocations
    pub allocation_count: usize,
    /// Total allocated memory
    pub total_allocated: usize,
    /// Peak memory usage
    pub peak_memory: usize,
    /// Number of security violations
    pub security_violations: u32,
    /// Last violation timestamp
    pub last_violation_time: Option<Instant>,
}

impl ResourceUsage {
    /// Create new resource usage tracker
    pub fn new() -> Self {
        Self::default()
    }

    /// Record memory allocation
    pub fn record_allocation(&mut self, size: usize) {
        self.memory_used += size;
        self.total_allocated += size;
        self.allocation_count += 1;
        self.peak_memory = self.peak_memory.max(self.memory_used);
    }

    /// Record memory deallocation
    pub fn record_deallocation(&mut self, size: usize) {
        if size <= self.memory_used {
            self.memory_used -= size;
        } else {
            self.memory_used = 0;
        }
    }

    /// Record CPU time usage
    pub fn record_cpu_time(&mut self, duration: Duration) {
        self.cpu_time_used_ms += duration.as_millis() as u64;
    }

    /// Record execution time
    pub fn record_execution_time(&mut self, duration: Duration) {
        self.execution_time_used_ms += duration.as_millis() as u64;
    }

    /// Record security violation
    pub fn record_security_violation(&mut self) {
        self.security_violations += 1;
        self.last_violation_time = Some(Instant::now());
    }

    /// Check if usage exceeds limits
    pub fn check_limits(&self, limits: &ResourceLimits) -> Result<(), Error> {
        if self.memory_used > limits.max_memory {
            return Err(Error::SecurityViolation(format!(
                "Memory limit exceeded: {} > {} bytes",
                self.memory_used, limits.max_memory
            )));
        }

        if self.cpu_time_used_ms > limits.max_cpu_time_ms {
            return Err(Error::SecurityViolation(format!(
                "CPU time limit exceeded: {} > {} ms",
                self.cpu_time_used_ms, limits.max_cpu_time_ms
            )));
        }

        if self.execution_time_used_ms > limits.max_execution_time_ms {
            return Err(Error::SecurityViolation(format!(
                "Execution time limit exceeded: {} > {} ms",
                self.execution_time_used_ms, limits.max_execution_time_ms
            )));
        }

        if self.open_files > limits.max_open_files {
            return Err(Error::SecurityViolation(format!(
                "Open files limit exceeded: {} > {}",
                self.open_files, limits.max_open_files
            )));
        }

        if self.network_connections > limits.max_network_connections {
            return Err(Error::SecurityViolation(format!(
                "Network connections limit exceeded: {} > {}",
                self.network_connections, limits.max_network_connections
            )));
        }

        if self.total_allocated > limits.max_total_allocation {
            return Err(Error::SecurityViolation(format!(
                "Total allocation limit exceeded: {} > {} bytes",
                self.total_allocated, limits.max_total_allocation
            )));
        }

        Ok(())
    }

    /// Reset usage statistics
    pub fn reset(&mut self) {
        self.memory_used = 0;
        self.cpu_time_used_ms = 0;
        self.execution_time_used_ms = 0;
        self.open_files = 0;
        self.network_connections = 0;
        self.allocation_count = 0;
        self.total_allocated = 0;
        self.peak_memory = 0;
        // Don't reset security violations
    }
}

/// Security policy for a plugin
#[derive(Debug, Clone)]
pub struct SecurityPolicy {
    /// Whether the plugin can access the filesystem
    pub allow_filesystem: bool,
    /// Whether the plugin can make network requests
    pub allow_network: bool,
    /// Whether the plugin can spawn subprocesses
    pub allow_subprocesses: bool,
    /// Whether the plugin can access environment variables
    pub allow_environment: bool,
    /// Whether the plugin can access system information
    pub allow_system_info: bool,
    /// List of allowed filesystem paths (if filesystem access is allowed)
    pub allowed_filesystem_paths: Vec<String>,
    /// List of allowed network hosts (if network access is allowed)
    pub allowed_network_hosts: Vec<String>,
    /// List of allowed environment variables (if environment access is allowed)
    pub allowed_environment_vars: Vec<String>,
    /// Maximum recursion depth for function calls
    pub max_recursion_depth: usize,
    /// Whether to enable stack canaries for overflow protection
    pub enable_stack_protection: bool,
    /// Whether to enable memory sandboxing
    pub enable_memory_sandbox: bool,
}

impl Default for SecurityPolicy {
    fn default() -> Self {
        Self {
            allow_filesystem: false,
            allow_network: false,
            allow_subprocesses: false,
            allow_environment: false,
            allow_system_info: false,
            allowed_filesystem_paths: Vec::new(),
            allowed_network_hosts: Vec::new(),
            allowed_environment_vars: Vec::new(),
            max_recursion_depth: 100,
            enable_stack_protection: true,
            enable_memory_sandbox: true,
        }
    }
}

impl SecurityPolicy {
    /// Create a restrictive policy (default)
    pub fn restrictive() -> Self {
        Self::default()
    }

    /// Create a permissive policy (for trusted plugins)
    pub fn permissive() -> Self {
        Self {
            allow_filesystem: true,
            allow_network: true,
            allow_subprocesses: false,
            allow_environment: true,
            allow_system_info: true,
            allowed_filesystem_paths: vec!["/tmp".to_string()],
            allowed_network_hosts: vec!["localhost".to_string(), "127.0.0.1".to_string()],
            allowed_environment_vars: vec!["PATH".to_string(), "HOME".to_string()],
            max_recursion_depth: 1000,
            enable_stack_protection: true,
            enable_memory_sandbox: true,
        }
    }

    /// Check if a filesystem path is allowed
    pub fn is_filesystem_path_allowed(&self, path: &str) -> bool {
        if !self.allow_filesystem {
            return false;
        }
        
        if self.allowed_filesystem_paths.is_empty() {
            return true;
        }
        
        self.allowed_filesystem_paths.iter().any(|allowed_path| {
            path.starts_with(allowed_path)
        })
    }

    /// Check if a network host is allowed
    pub fn is_network_host_allowed(&self, host: &str) -> bool {
        if !self.allow_network {
            return false;
        }
        
        if self.allowed_network_hosts.is_empty() {
            return true;
        }
        
        self.allowed_network_hosts.iter().any(|allowed_host| {
            host == allowed_host
        })
    }

    /// Check if an environment variable is allowed
    pub fn is_environment_var_allowed(&self, var: &str) -> bool {
        if !self.allow_environment {
            return false;
        }
        
        if self.allowed_environment_vars.is_empty() {
            return true;
        }
        
        self.allowed_environment_vars.contains(&var.to_string())
    }
}

/// Sandbox for plugin execution
pub struct Sandbox {
    /// Plugin name
    plugin_name: String,
    /// Resource limits
    limits: ResourceLimits,
    /// Security policy
    policy: SecurityPolicy,
    /// Resource usage tracker
    usage: RwLock<ResourceUsage>,
    /// Start time of current operation
    operation_start_time: RwLock<Option<Instant>>,
    /// Whether the sandbox is active
    is_active: RwLock<bool>,
}

impl Sandbox {
    /// Create a new sandbox for a plugin
    pub fn new(plugin_name: String, limits: ResourceLimits, policy: SecurityPolicy) -> Self {
        Self {
            plugin_name,
            limits,
            policy,
            usage: RwLock::new(ResourceUsage::new()),
            operation_start_time: RwLock::new(None),
            is_active: RwLock::new(false),
        }
    }

    /// Start a new operation
    pub fn start_operation(&self) -> Result<(), Error> {
        let mut is_active = self.is_active.write();
        if *is_active {
            return Err(Error::SecurityViolation(
                "Another operation is already in progress".to_string(),
            ));
        }
        
        *is_active = true;
        *self.operation_start_time.write() = Some(Instant::now());
        
        Ok(())
    }

    /// End the current operation
    pub fn end_operation(&self) -> Result<(), Error> {
        let mut is_active = self.is_active.write();
        if !*is_active {
            return Err(Error::SecurityViolation(
                "No operation is in progress".to_string(),
            ));
        }
        
        // Record execution time
        if let Some(start_time) = *self.operation_start_time.read() {
            let duration = start_time.elapsed();
            self.usage.write().record_execution_time(duration);
        }
        
        *is_active = false;
        *self.operation_start_time.write() = None;
        
        // Check limits
        self.check_limits()?;
        
        Ok(())
    }

    /// Check resource limits
    pub fn check_limits(&self) -> Result<(), Error> {
        let usage = self.usage.read();
        usage.check_limits(&self.limits)
    }

    /// Record memory allocation
    pub fn record_allocation(&self, size: usize) -> Result<(), Error> {
        if size > self.limits.max_allocation_size {
            return Err(Error::SecurityViolation(format!(
                "Allocation size limit exceeded: {} > {} bytes",
                size, self.limits.max_allocation_size
            )));
        }
        
        let mut usage = self.usage.write();
        usage.record_allocation(size);
        
        // Check limits after allocation
        usage.check_limits(&self.limits)
    }

    /// Record memory deallocation
    pub fn record_deallocation(&self, size: usize) {
        let mut usage = self.usage.write();
        usage.record_deallocation(size);
    }

    /// Record CPU time usage
    pub fn record_cpu_time(&self, duration: Duration) -> Result<(), Error> {
        let mut usage = self.usage.write();
        usage.record_cpu_time(duration);
        
        // Check limits
        usage.check_limits(&self.limits)
    }

    /// Check filesystem access permission
    pub fn check_filesystem_access(&self, path: &str) -> Result<(), Error> {
        if !self.policy.is_filesystem_path_allowed(path) {
            self.record_security_violation();
            return Err(Error::SecurityViolation(format!(
                "Filesystem access denied to path: {}",
                path
            )));
        }
        
        Ok(())
    }

    /// Check network access permission
    pub fn check_network_access(&self, host: &str) -> Result<(), Error> {
        if !self.policy.is_network_host_allowed(host) {
            self.record_security_violation();
            return Err(Error::SecurityViolation(format!(
                "Network access denied to host: {}",
                host
            )));
        }
        
        Ok(())
    }

    /// Check environment variable access permission
    pub fn check_environment_access(&self, var: &str) -> Result<(), Error> {
        if !self.policy.is_environment_var_allowed(var) {
            self.record_security_violation();
            return Err(Error::SecurityViolation(format!(
                "Environment variable access denied: {}",
                var
            )));
        }
        
        Ok(())
    }

    /// Check subprocess execution permission
    pub fn check_subprocess_execution(&self) -> Result<(), Error> {
        if !self.policy.allow_subprocesses {
            self.record_security_violation();
            return Err(Error::SecurityViolation(
                "Subprocess execution not allowed".to_string(),
            ));
        }
        
        Ok(())
    }

    /// Check system information access permission
    pub fn check_system_info_access(&self) -> Result<(), Error> {
        if !self.policy.allow_system_info {
            self.record_security_violation();
            return Err(Error::SecurityViolation(
                "System information access not allowed".to_string(),
            ));
        }
        
        Ok(())
    }

    /// Check recursion depth
    pub fn check_recursion_depth(&self, depth: usize) -> Result<(), Error> {
        if depth > self.policy.max_recursion_depth {
            self.record_security_violation();
            return Err(Error::SecurityViolation(format!(
                "Recursion depth limit exceeded: {} > {}",
                depth, self.policy.max_recursion_depth
            )));
        }
        
        Ok(())
    }

    /// Record a security violation
    pub fn record_security_violation(&self) {
        let mut usage = self.usage.write();
        usage.record_security_violation();
        
        error!(
            "Security violation recorded for plugin '{}' (total: {})",
            self.plugin_name, usage.security_violations
        );
    }

    /// Get resource usage statistics
    pub fn get_resource_usage(&self) -> ResourceUsage {
        self.usage.read().clone()
    }

    /// Get security policy
    pub fn get_security_policy(&self) -> &SecurityPolicy {
        &self.policy
    }

    /// Get resource limits
    pub fn get_resource_limits(&self) -> &ResourceLimits {
        &self.limits
    }

    /// Reset resource usage statistics
    pub fn reset_usage(&self) {
        self.usage.write().reset();
    }

    /// Check if sandbox is active
    pub fn is_active(&self) -> bool {
        *self.is_active.read()
    }

    /// Get plugin name
    pub fn plugin_name(&self) -> &str {
        &self.plugin_name
    }

    /// Get number of security violations
    pub fn security_violations(&self) -> u32 {
        self.usage.read().security_violations
    }

    /// Check if plugin should be terminated due to excessive violations
    pub fn should_terminate(&self) -> bool {
        let violations = self.security_violations();
        violations >= 10 // Terminate after 10 violations
    }
}

/// Sandbox manager for multiple plugins
pub struct SandboxManager {
    sandboxes: RwLock<std::collections::HashMap<String, Arc<Sandbox>>>,
}

impl SandboxManager {
    /// Create a new sandbox manager
    pub fn new() -> Self {
        Self {
            sandboxes: RwLock::new(std::collections::HashMap::new()),
        }
    }

    /// Create a sandbox for a plugin
    pub fn create_sandbox(
        &self,
        plugin_name: String,
        limits: ResourceLimits,
        policy: SecurityPolicy,
    ) -> Arc<Sandbox> {
        let sandbox = Arc::new(Sandbox::new(plugin_name.clone(), limits, policy));
        self.sandboxes.write().insert(plugin_name, sandbox.clone());
        sandbox
    }

    /// Get a sandbox by plugin name
    pub fn get_sandbox(&self, plugin_name: &str) -> Option<Arc<Sandbox>> {
        self.sandboxes.read().get(plugin_name).cloned()
    }

    /// Remove a sandbox
    pub fn remove_sandbox(&self, plugin_name: &str) {
        self.sandboxes.write().remove(plugin_name);
    }

    /// Get all sandboxes
    pub fn get_all_sandboxes(&self) -> Vec<Arc<Sandbox>> {
        self.sandboxes.read().values().cloned().collect()
    }

    /// Check for plugins that should be terminated
    pub fn check_for_termination(&self) -> Vec<String> {
        let mut to_terminate = Vec::new();
        
        for (plugin_name, sandbox) in self.sandboxes.read().iter() {
            if sandbox.should_terminate() {
                to_terminate.push(plugin_name.clone());
            }
        }
        
        to_terminate
    }

    /// Get sandbox manager statistics
    pub fn stats(&self) -> SandboxManagerStats {
        let sandboxes = self.sandboxes.read();
        
        let mut total_violations = 0;
        let mut active_sandboxes = 0;
        let mut total_memory_used = 0;
        
        for sandbox in sandboxes.values() {
            let usage = sandbox.get_resource_usage();
            total_violations += usage.security_violations;
            total_memory_used += usage.memory_used;
            
            if sandbox.is_active() {
                active_sandboxes += 1;
            }
        }
        
        SandboxManagerStats {
            total_sandboxes: sandboxes.len(),
            active_sandboxes,
            total_violations,
            total_memory_used,
        }
    }
}

/// Sandbox manager statistics
#[derive(Debug, Clone)]
pub struct SandboxManagerStats {
    pub total_sandboxes: usize,
    pub active_sandboxes: usize,
    pub total_violations: u32,
    pub total_memory_used: usize,
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::thread;
    use std::time::Duration;
    
    #[test]
    fn test_resource_limits() {
        let limits = ResourceLimits {
            max_memory: 1000,
            max_cpu_time_ms: 100,
            max_execution_time_ms: 1000,
            max_open_files: 10,
            max_network_connections: 5,
            max_allocation_size: 100,
            max_total_allocation: 500,
            max_stack_size: 1000,
        };
        
        let mut usage = ResourceUsage::new();
        usage.record_allocation(600); // Should exceed total allocation limit
        
        assert!(usage.check_limits(&limits).is_err());
    }
    
    #[test]
    fn test_sandbox_operation() {
        let sandbox = Sandbox::new(
            "test_plugin".to_string(),
            ResourceLimits::default(),
            SecurityPolicy::default(),
        );
        
        assert!(sandbox.start_operation().is_ok());
        assert!(sandbox.start_operation().is_err()); // Already active
        
        thread::sleep(Duration::from_millis(10));
        
        assert!(sandbox.end_operation().is_ok());
        assert!(sandbox.end_operation().is_err()); // Not active
    }
    
    #[test]
    fn test_security_policy() {
        let policy = SecurityPolicy {
            allow_filesystem: true,
            allowed_filesystem_paths: vec!["/tmp".to_string(), "/home".to_string()],
            ..SecurityPolicy::default()
        };
        
        assert!(policy.is_filesystem_path_allowed("/tmp/file.txt"));
        assert!(policy.is_filesystem_path_allowed("/home/user/doc.txt"));
        assert!(!policy.is_filesystem_path_allowed("/etc/passwd"));
        
        let policy = SecurityPolicy::restrictive();
        assert!(!policy.is_filesystem_path_allowed("/tmp/file.txt"));
    }
}