use std::{
    sync::Arc,
    time::{Duration, Instant},
    collections::{HashMap, VecDeque},
};
use parking_lot::RwLock;
use tokio::sync::mpsc;
use serde_json::Value;
use tracing::debug;

/// Plugin performance metrics
#[derive(Debug, Clone)]
pub struct PluginMetrics {
    /// Plugin name
    pub plugin_name: String,
    /// Memory usage in bytes
    pub memory_usage: u64,
    /// CPU usage percentage (0-100)
    pub cpu_usage: f32,
    /// Number of active requests
    pub active_requests: u32,
    /// Total requests processed
    pub total_requests: u64,
    /// Average request latency in milliseconds
    pub avg_latency_ms: f64,
    /// Error rate (errors per request)
    pub error_rate: f64,
    /// Timestamp when metrics were collected
    pub timestamp: Instant,
    /// Custom metrics
    pub custom_metrics: HashMap<String, Value>,
}

impl PluginMetrics {
    /// Create new metrics
    pub fn new(plugin_name: String) -> Self {
        Self {
            plugin_name,
            memory_usage: 0,
            cpu_usage: 0.0,
            active_requests: 0,
            total_requests: 0,
            avg_latency_ms: 0.0,
            error_rate: 0.0,
            timestamp: Instant::now(),
            custom_metrics: HashMap::new(),
        }
    }

    /// Update memory usage
    pub fn update_memory_usage(&mut self, usage: u64) {
        self.memory_usage = usage;
        self.timestamp = Instant::now();
    }

    /// Update CPU usage
    pub fn update_cpu_usage(&mut self, usage: f32) {
        self.cpu_usage = usage;
        self.timestamp = Instant::now();
    }

    /// Start a request
    pub fn start_request(&mut self) -> RequestTracker {
        self.active_requests += 1;
        RequestTracker::new(self.plugin_name.clone())
    }

    /// End a request
    pub fn end_request(&mut self, success: bool, latency: Duration) {
        if self.active_requests > 0 {
            self.active_requests -= 1;
        }
        
        self.total_requests += 1;
        
        // Update average latency (exponential moving average)
        let latency_ms = latency.as_millis() as f64;
        if self.total_requests == 1 {
            self.avg_latency_ms = latency_ms;
        } else {
            self.avg_latency_ms = (self.avg_latency_ms * 0.9) + (latency_ms * 0.1);
        }
        
        // Update error rate
        if !success {
            let errors = self.total_requests as f64 * self.error_rate + 1.0;
            self.error_rate = errors / self.total_requests as f64;
        } else {
            let errors = self.total_requests as f64 * self.error_rate;
            self.error_rate = errors / self.total_requests as f64;
        }
        
        self.timestamp = Instant::now();
    }

    /// Add custom metric
    pub fn add_custom_metric(&mut self, name: String, value: Value) {
        self.custom_metrics.insert(name, value);
        self.timestamp = Instant::now();
    }

    /// Get metrics as JSON
    pub fn to_json(&self) -> Value {
        serde_json::json!({
            "plugin_name": self.plugin_name,
            "memory_usage": self.memory_usage,
            "cpu_usage": self.cpu_usage,
            "active_requests": self.active_requests,
            "total_requests": self.total_requests,
            "avg_latency_ms": self.avg_latency_ms,
            "error_rate": self.error_rate,
            "timestamp": self.timestamp.elapsed().as_millis(),
            "custom_metrics": self.custom_metrics,
        })
    }

    /// Check if metrics are stale (older than threshold)
    pub fn is_stale(&self, threshold: Duration) -> bool {
        self.timestamp.elapsed() > threshold
    }
}

/// Request tracker for measuring latency
pub struct RequestTracker {
    plugin_name: String,
    start_time: Instant,
}

impl RequestTracker {
    /// Create new request tracker
    pub fn new(plugin_name: String) -> Self {
        Self {
            plugin_name,
            start_time: Instant::now(),
        }
    }

    /// Get elapsed time
    pub fn elapsed(&self) -> Duration {
        self.start_time.elapsed()
    }

    /// Get plugin name
    pub fn plugin_name(&self) -> &str {
        &self.plugin_name
    }
}

impl Drop for RequestTracker {
    fn drop(&mut self) {
        // Request ended without explicit success/failure
        // This is okay for tracking purposes
    }
}

/// Metrics collector for plugins
pub struct MetricsCollector {
    /// Plugin metrics by plugin name
    metrics: RwLock<HashMap<String, Arc<RwLock<PluginMetrics>>>>,
    /// Metrics history (ring buffer)
    history: RwLock<VecDeque<HashMap<String, PluginMetrics>>>,
    /// Maximum history size
    max_history_size: usize,
    /// Metrics aggregation interval
    aggregation_interval: Duration,
    /// Last aggregation time
    last_aggregation: RwLock<Instant>,
    /// Metrics subscribers
    subscribers: RwLock<Vec<mpsc::Sender<PluginMetrics>>>,
}

impl MetricsCollector {
    /// Create a new metrics collector
    pub fn new(max_history_size: usize, aggregation_interval: Duration) -> Self {
        Self {
            metrics: RwLock::new(HashMap::new()),
            history: RwLock::new(VecDeque::with_capacity(max_history_size)),
            max_history_size,
            aggregation_interval,
            last_aggregation: RwLock::new(Instant::now()),
            subscribers: RwLock::new(Vec::new()),
        }
    }

    /// Register a plugin for metrics collection
    pub fn register_plugin(&self, plugin_name: String) -> Arc<RwLock<PluginMetrics>> {
        let metrics = PluginMetrics::new(plugin_name.clone());
        let metrics_arc = Arc::new(RwLock::new(metrics));
        
        self.metrics.write().insert(plugin_name.clone(), metrics_arc.clone());
        
        debug!("Registered plugin for metrics collection: {}", plugin_name);
        metrics_arc
    }

    /// Unregister a plugin from metrics collection
    pub fn unregister_plugin(&self, plugin_name: &str) {
        self.metrics.write().remove(plugin_name);
        debug!("Unregistered plugin from metrics collection: {}", plugin_name);
    }

    /// Get metrics for a plugin
    pub fn get_plugin_metrics(&self, plugin_name: &str) -> Option<PluginMetrics> {
        self.metrics
            .read()
            .get(plugin_name)
            .map(|metrics| metrics.read().clone())
    }

    /// Get all plugin metrics
    pub fn get_all_metrics(&self) -> HashMap<String, PluginMetrics> {
        self.metrics
            .read()
            .iter()
            .map(|(name, metrics)| (name.clone(), metrics.read().clone()))
            .collect()
    }

    /// Start a request for a plugin
    pub fn start_request(&self, plugin_name: &str) -> Option<RequestTracker> {
        if let Some(metrics) = self.metrics.read().get(plugin_name) {
            let tracker = metrics.write().start_request();
            Some(tracker)
        } else {
            None
        }
    }

    /// End a request for a plugin
    pub fn end_request(&self, plugin_name: &str, success: bool, latency: Duration) {
        if let Some(metrics) = self.metrics.read().get(plugin_name) {
            metrics.write().end_request(success, latency);
        }
    }

    /// Update plugin memory usage
    pub fn update_memory_usage(&self, plugin_name: &str, usage: u64) {
        if let Some(metrics) = self.metrics.read().get(plugin_name) {
            metrics.write().update_memory_usage(usage);
        }
    }

    /// Update plugin CPU usage
    pub fn update_cpu_usage(&self, plugin_name: &str, usage: f32) {
        if let Some(metrics) = self.metrics.read().get(plugin_name) {
            metrics.write().update_cpu_usage(usage);
        }
    }

    /// Add custom metric for a plugin
    pub fn add_custom_metric(&self, plugin_name: &str, name: String, value: Value) {
        if let Some(metrics) = self.metrics.read().get(plugin_name) {
            metrics.write().add_custom_metric(name, value);
        }
    }

    /// Collect and aggregate metrics
    pub fn collect_metrics(&self) {
        let now = Instant::now();
        let last_aggregation = *self.last_aggregation.read();
        
        if now.duration_since(last_aggregation) < self.aggregation_interval {
            return;
        }
        
        // Update last aggregation time
        *self.last_aggregation.write() = now;
        
        // Get current metrics snapshot
        let snapshot = self.get_all_metrics();
        
        // Add to history
        let mut history = self.history.write();
        history.push_back(snapshot);
        
        // Trim history if it exceeds max size
        while history.len() > self.max_history_size {
            history.pop_front();
        }
        
        // Notify subscribers
        self.notify_subscribers();
        
        debug!("Collected metrics snapshot (history size: {})", history.len());
    }

    /// Get metrics history
    pub fn get_history(&self) -> Vec<HashMap<String, PluginMetrics>> {
        self.history.read().iter().cloned().collect()
    }

    /// Get aggregated metrics over time window
    pub fn get_aggregated_metrics(&self, _window: Duration) -> HashMap<String, AggregatedMetrics> {
        let history = self.history.read();
        let _now = Instant::now();

        let mut aggregated = HashMap::new();

        for snapshot in history.iter().rev() {
            // Check if snapshot is within time window
            // Note: This is a simplification - real implementation would track timestamps
            for (plugin_name, metrics) in snapshot {
                let entry = aggregated.entry(plugin_name.clone()).or_insert_with(|| {
                    AggregatedMetrics::new(plugin_name.clone())
                });

                entry.add_sample(metrics);
            }
        }
        
        aggregated
    }

    /// Subscribe to metrics updates
    pub fn subscribe(&self) -> mpsc::Receiver<PluginMetrics> {
        let (tx, rx) = mpsc::channel(100);
        self.subscribers.write().push(tx);
        rx
    }

    /// Notify subscribers of metrics updates
    fn notify_subscribers(&self) {
        let metrics = self.get_all_metrics();
        let mut subscribers = self.subscribers.write();
        
        // Remove dead subscribers
        subscribers.retain(|subscriber| !subscriber.is_closed());
        
        // Notify each subscriber
        for (_plugin_name, metric) in metrics {
            for subscriber in subscribers.iter_mut() {
                let _ = subscriber.try_send(metric.clone());
            }
        }
    }

    /// Get metrics collector statistics
    pub fn stats(&self) -> MetricsCollectorStats {
        let metrics = self.metrics.read();
        let history = self.history.read();
        
        MetricsCollectorStats {
            tracked_plugins: metrics.len(),
            history_size: history.len(),
            max_history_size: self.max_history_size,
            subscribers: self.subscribers.read().len(),
        }
    }
}

/// Aggregated metrics over time window
#[derive(Debug, Clone)]
pub struct AggregatedMetrics {
    /// Plugin name
    pub plugin_name: String,
    /// Minimum memory usage
    pub min_memory: u64,
    /// Maximum memory usage
    pub max_memory: u64,
    /// Average memory usage
    pub avg_memory: f64,
    /// Minimum CPU usage
    pub min_cpu: f32,
    /// Maximum CPU usage
    pub max_cpu: f32,
    /// Average CPU usage
    pub avg_cpu: f32,
    /// Total requests
    pub total_requests: u64,
    /// Error rate
    pub error_rate: f64,
    /// Average latency
    pub avg_latency: f64,
    /// Number of samples
    pub samples: usize,
}

impl AggregatedMetrics {
    /// Create new aggregated metrics
    pub fn new(plugin_name: String) -> Self {
        Self {
            plugin_name,
            min_memory: u64::MAX,
            max_memory: 0,
            avg_memory: 0.0,
            min_cpu: f32::MAX,
            max_cpu: 0.0,
            avg_cpu: 0.0,
            total_requests: 0,
            error_rate: 0.0,
            avg_latency: 0.0,
            samples: 0,
        }
    }

    /// Add a sample to aggregation
    pub fn add_sample(&mut self, metrics: &PluginMetrics) {
        self.min_memory = self.min_memory.min(metrics.memory_usage);
        self.max_memory = self.max_memory.max(metrics.memory_usage);
        
        self.min_cpu = self.min_cpu.min(metrics.cpu_usage);
        self.max_cpu = self.max_cpu.max(metrics.cpu_usage);
        
        // Update averages
        let total_memory = self.avg_memory * self.samples as f64 + metrics.memory_usage as f64;
        let total_cpu = self.avg_cpu * self.samples as f32 + metrics.cpu_usage;
        
        self.samples += 1;
        
        self.avg_memory = total_memory / self.samples as f64;
        self.avg_cpu = total_cpu / self.samples as f32;
        
        self.total_requests += metrics.total_requests;
        self.error_rate = metrics.error_rate;
        self.avg_latency = metrics.avg_latency_ms;
    }

    /// Get aggregated metrics as JSON
    pub fn to_json(&self) -> Value {
        serde_json::json!({
            "plugin_name": self.plugin_name,
            "memory_usage": {
                "min": self.min_memory,
                "max": self.max_memory,
                "avg": self.avg_memory,
            },
            "cpu_usage": {
                "min": self.min_cpu,
                "max": self.max_cpu,
                "avg": self.avg_cpu,
            },
            "total_requests": self.total_requests,
            "error_rate": self.error_rate,
            "avg_latency": self.avg_latency,
            "samples": self.samples,
        })
    }
}

/// Metrics collector statistics
#[derive(Debug, Clone)]
pub struct MetricsCollectorStats {
    pub tracked_plugins: usize,
    pub history_size: usize,
    pub max_history_size: usize,
    pub subscribers: usize,
}

/// Plugin health status
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum HealthStatus {
    Healthy,
    Warning,
    Critical,
    Unknown,
}

impl HealthStatus {
    /// Get status from metrics
    pub fn from_metrics(metrics: &PluginMetrics, thresholds: &HealthThresholds) -> Self {
        let mut status = HealthStatus::Healthy;
        
        // Check memory usage
        if metrics.memory_usage > thresholds.critical_memory {
            return HealthStatus::Critical;
        } else if metrics.memory_usage > thresholds.warning_memory {
            status = HealthStatus::Warning;
        }
        
        // Check CPU usage
        if metrics.cpu_usage > thresholds.critical_cpu {
            return HealthStatus::Critical;
        } else if metrics.cpu_usage > thresholds.warning_cpu {
            status = HealthStatus::Warning;
        }
        
        // Check error rate
        if metrics.error_rate > thresholds.critical_error_rate {
            return HealthStatus::Critical;
        } else if metrics.error_rate > thresholds.warning_error_rate {
            status = HealthStatus::Warning;
        }
        
        // Check latency
        if metrics.avg_latency_ms > thresholds.critical_latency_ms {
            return HealthStatus::Critical;
        } else if metrics.avg_latency_ms > thresholds.warning_latency_ms {
            status = HealthStatus::Warning;
        }
        
        status
    }
    
    /// Get status as string
    pub fn as_str(&self) -> &'static str {
        match self {
            HealthStatus::Healthy => "healthy",
            HealthStatus::Warning => "warning",
            HealthStatus::Critical => "critical",
            HealthStatus::Unknown => "unknown",
        }
    }
}

/// Health thresholds for plugin monitoring
#[derive(Debug, Clone)]
pub struct HealthThresholds {
    /// Warning threshold for memory usage (bytes)
    pub warning_memory: u64,
    /// Critical threshold for memory usage (bytes)
    pub critical_memory: u64,
    /// Warning threshold for CPU usage (percentage)
    pub warning_cpu: f32,
    /// Critical threshold for CPU usage (percentage)
    pub critical_cpu: f32,
    /// Warning threshold for error rate (0-1)
    pub warning_error_rate: f64,
    /// Critical threshold for error rate (0-1)
    pub critical_error_rate: f64,
    /// Warning threshold for latency (milliseconds)
    pub warning_latency_ms: f64,
    /// Critical threshold for latency (milliseconds)
    pub critical_latency_ms: f64,
}

impl Default for HealthThresholds {
    fn default() -> Self {
        Self {
            warning_memory: 128 * 1024 * 1024, // 128 MB
            critical_memory: 256 * 1024 * 1024, // 256 MB
            warning_cpu: 80.0, // 80%
            critical_cpu: 95.0, // 95%
            warning_error_rate: 0.05, // 5%
            critical_error_rate: 0.2, // 20%
            warning_latency_ms: 1000.0, // 1 second
            critical_latency_ms: 5000.0, // 5 seconds
        }
    }
}

/// Health monitor for plugins
pub struct HealthMonitor {
    thresholds: HealthThresholds,
    metrics_collector: Arc<MetricsCollector>,
    status_history: RwLock<VecDeque<HashMap<String, HealthStatus>>>,
    max_status_history: usize,
}

impl HealthMonitor {
    /// Create a new health monitor
    pub fn new(
        thresholds: HealthThresholds,
        metrics_collector: Arc<MetricsCollector>,
        max_status_history: usize,
    ) -> Self {
        Self {
            thresholds,
            metrics_collector,
            status_history: RwLock::new(VecDeque::with_capacity(max_status_history)),
            max_status_history,
        }
    }

    /// Check health of all plugins
    pub fn check_health(&self) -> HashMap<String, HealthStatus> {
        let metrics = self.metrics_collector.get_all_metrics();
        let mut statuses = HashMap::new();
        
        for (plugin_name, plugin_metrics) in metrics {
            let status = HealthStatus::from_metrics(&plugin_metrics, &self.thresholds);
            statuses.insert(plugin_name, status);
        }
        
        // Add to history
        let mut history = self.status_history.write();
        history.push_back(statuses.clone());
        
        // Trim history
        while history.len() > self.max_status_history {
            history.pop_front();
        }
        
        statuses
    }

    /// Get health status for a specific plugin
    pub fn get_plugin_health(&self, plugin_name: &str) -> HealthStatus {
        if let Some(metrics) = self.metrics_collector.get_plugin_metrics(plugin_name) {
            HealthStatus::from_metrics(&metrics, &self.thresholds)
        } else {
            HealthStatus::Unknown
        }
    }

    /// Get health status history
    pub fn get_health_history(&self) -> Vec<HashMap<String, HealthStatus>> {
        self.status_history.read().iter().cloned().collect()
    }

    /// Get plugins with critical health status
    pub fn get_critical_plugins(&self) -> Vec<String> {
        let statuses = self.check_health();
        
        statuses
            .into_iter()
            .filter(|(_, status)| *status == HealthStatus::Critical)
            .map(|(name, _)| name)
            .collect()
    }

    /// Get health monitor statistics
    pub fn stats(&self) -> HealthMonitorStats {
        let statuses = self.check_health();
        let history = self.status_history.read();
        
        let mut healthy = 0;
        let mut warning = 0;
        let mut critical = 0;
        let mut unknown = 0;
        
        for status in statuses.values() {
            match status {
                HealthStatus::Healthy => healthy += 1,
                HealthStatus::Warning => warning += 1,
                HealthStatus::Critical => critical += 1,
                HealthStatus::Unknown => unknown += 1,
            }
        }
        
        HealthMonitorStats {
            total_plugins: statuses.len(),
            healthy,
            warning,
            critical,
            unknown,
            history_size: history.len(),
        }
    }
}

/// Health monitor statistics
#[derive(Debug, Clone)]
pub struct HealthMonitorStats {
    pub total_plugins: usize,
    pub healthy: usize,
    pub warning: usize,
    pub critical: usize,
    pub unknown: usize,
    pub history_size: usize,
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::thread;
    use std::time::Duration;
    
    #[test]
    fn test_plugin_metrics() {
        let mut metrics = PluginMetrics::new("test_plugin".to_string());
        
        metrics.update_memory_usage(1024 * 1024); // 1 MB
        metrics.update_cpu_usage(50.0); // 50%
        
        let tracker = metrics.start_request();
        thread::sleep(Duration::from_millis(10));
        metrics.end_request(true, tracker.elapsed());
        
        assert_eq!(metrics.memory_usage, 1024 * 1024);
        assert_eq!(metrics.cpu_usage, 50.0);
        assert_eq!(metrics.active_requests, 0);
        assert_eq!(metrics.total_requests, 1);
        assert!(metrics.avg_latency_ms > 0.0);
    }
    
    #[test]
    fn test_metrics_collector() {
        let collector = MetricsCollector::new(10, Duration::from_secs(1));
        
        let plugin_metrics = collector.register_plugin("test_plugin".to_string());
        
        plugin_metrics.write().update_memory_usage(1024 * 1024);
        
        let metrics = collector.get_plugin_metrics("test_plugin");
        assert!(metrics.is_some());
        assert_eq!(metrics.unwrap().memory_usage, 1024 * 1024);
        
        collector.unregister_plugin("test_plugin");
        assert!(collector.get_plugin_metrics("test_plugin").is_none());
    }
    
    #[test]
    fn test_health_status() {
        let thresholds = HealthThresholds::default();
        let mut metrics = PluginMetrics::new("test_plugin".to_string());
        
        metrics.update_memory_usage(300 * 1024 * 1024); // 300 MB (> critical)
        let status = HealthStatus::from_metrics(&metrics, &thresholds);
        assert_eq!(status, HealthStatus::Critical);
        
        metrics.update_memory_usage(150 * 1024 * 1024); // 150 MB (> warning, < critical)
        let status = HealthStatus::from_metrics(&metrics, &thresholds);
        assert_eq!(status, HealthStatus::Warning);
        
        metrics.update_memory_usage(50 * 1024 * 1024); // 50 MB (< warning)
        let status = HealthStatus::from_metrics(&metrics, &thresholds);
        assert_eq!(status, HealthStatus::Healthy);
    }
}