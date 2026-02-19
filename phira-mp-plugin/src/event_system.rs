use crate::Error;
use std::{
    collections::{HashMap, HashSet},
    sync::Arc,
};
use parking_lot::RwLock;
use tokio::sync::broadcast;
use tracing::debug;

/// Event data type
pub type EventData = serde_json::Value;

/// Event structure
#[derive(Debug, Clone, serde::Deserialize, serde::Serialize)]
pub struct Event {
    /// Event type identifier
    pub event_type: String,
    /// Event data (JSON serializable)
    pub data: EventData,
    /// Event timestamp (milliseconds since epoch)
    pub timestamp: i64,
    /// Event source (plugin name or "system")
    pub source: String,
}

impl Event {
    /// Create a new event
    pub fn new(event_type: impl Into<String>, data: EventData, source: impl Into<String>) -> Self {
        Self {
            event_type: event_type.into(),
            data,
            timestamp: chrono::Utc::now().timestamp_millis(),
            source: source.into(),
        }
    }

    /// Create a new system event
    pub fn system(event_type: impl Into<String>, data: EventData) -> Self {
        Self::new(event_type, data, "system")
    }

    /// Create a new plugin event
    pub fn plugin(event_type: impl Into<String>, data: EventData, plugin_name: impl Into<String>) -> Self {
        Self::new(event_type, data, plugin_name)
    }

    /// Convert event to JSON string
    pub fn to_json(&self) -> Result<String, Error> {
        serde_json::to_string(self)
            .map_err(|e| Error::Event(format!("Failed to serialize event: {}", e)))
    }

    /// Create event from JSON string
    pub fn from_json(json: &str) -> Result<Self, Error> {
        serde_json::from_str(json)
            .map_err(|e| Error::Event(format!("Failed to deserialize event: {}", e)))
    }
}


/// Event handler function signature
pub type EventHandler = Box<dyn Fn(&Event) -> Result<(), Error> + Send + Sync>;

/// Event subscription
pub struct EventSubscription {
    /// Event type
    pub event_type: String,
    /// Handler function
    pub handler: EventHandler,
    /// Subscriber identifier (plugin name)
    pub subscriber: String,
}

impl EventSubscription {
    /// Create a new event subscription
    pub fn new(
        event_type: impl Into<String>,
        handler: EventHandler,
        subscriber: impl Into<String>,
    ) -> Self {
        Self {
            event_type: event_type.into(),
            handler,
            subscriber: subscriber.into(),
        }
    }
}

/// Event bus for plugin communication
pub struct EventBus {
    /// Event subscriptions by event type
    subscriptions: RwLock<HashMap<String, Vec<Arc<EventSubscription>>>>,
    /// Broadcast channel for real-time event delivery
    broadcast_tx: broadcast::Sender<Arc<Event>>,
    /// List of all registered event types
    event_types: RwLock<HashSet<String>>,
}

impl EventBus {
    /// Create a new event bus
    pub fn new() -> Self {
        let (broadcast_tx, _) = broadcast::channel(100);
        Self {
            subscriptions: RwLock::new(HashMap::new()),
            broadcast_tx,
            event_types: RwLock::new(HashSet::new()),
        }
    }

    /// Subscribe to an event type
    pub fn subscribe(
        &self,
        event_type: impl Into<String>,
        handler: EventHandler,
        subscriber: impl Into<String>,
    ) -> Result<(), Error> {
        let event_type = event_type.into();
        let subscriber = subscriber.into();
        
        debug!("Plugin '{}' subscribing to event '{}'", subscriber, event_type);
        
        let subscription = Arc::new(EventSubscription::new(
            event_type.clone(),
            handler,
            subscriber.clone(),
        ));
        
        let mut subscriptions = self.subscriptions.write();
        let event_subs = subscriptions.entry(event_type.clone()).or_insert_with(Vec::new);
        event_subs.push(subscription);
        
        // Add to event types set
        self.event_types.write().insert(event_type);
        
        Ok(())
    }

    /// Unsubscribe from an event type
    pub fn unsubscribe(
        &self,
        event_type: impl Into<String>,
        subscriber: impl Into<String>,
    ) -> Result<(), Error> {
        let event_type = event_type.into();
        let subscriber = subscriber.into();
        
        debug!("Plugin '{}' unsubscribing from event '{}'", subscriber, event_type);
        
        let mut subscriptions = self.subscriptions.write();
        if let Some(event_subs) = subscriptions.get_mut(&event_type) {
            event_subs.retain(|sub| sub.subscriber != subscriber);
            
            // Remove event type if no subscribers
            if event_subs.is_empty() {
                subscriptions.remove(&event_type);
                self.event_types.write().remove(&event_type);
            }
        }
        
        Ok(())
    }

    /// Unsubscribe all events for a subscriber
    pub fn unsubscribe_all(&self, subscriber: impl Into<String>) -> Result<(), Error> {
        let subscriber = subscriber.into();
        
        debug!("Unsubscribing all events for '{}'", subscriber);
        
        let mut subscriptions = self.subscriptions.write();
        let mut event_types = self.event_types.write();
        
        // Collect event types to remove
        let mut empty_event_types = Vec::new();
        
        for (event_type, event_subs) in subscriptions.iter_mut() {
            event_subs.retain(|sub| sub.subscriber != subscriber);
            
            if event_subs.is_empty() {
                empty_event_types.push(event_type.clone());
            }
        }
        
        // Remove empty event types
        for event_type in empty_event_types {
            subscriptions.remove(&event_type);
            event_types.remove(&event_type);
        }
        
        Ok(())
    }

    /// Emit an event
    pub fn emit(&self, event: Event) -> Result<(), Error> {
        let event = Arc::new(event);
        let event_type = event.event_type.clone();
        
        debug!("Emitting event '{}' from '{}'", event_type, event.source);
        
        // Call synchronous handlers
        {
            let subscriptions = self.subscriptions.read();
            if let Some(event_subs) = subscriptions.get(&event_type) {
                for subscription in event_subs {
                    if let Err(e) = (subscription.handler)(&event) {
                        // Log error but continue with other handlers
                        tracing::error!(
                            "Event handler failed for plugin '{}': {}",
                            subscription.subscriber, e
                        );
                    }
                }
            }
        }
        
        // Broadcast for async listeners
        if self.broadcast_tx.receiver_count() > 0 {
            let _ = self.broadcast_tx.send(event.clone());
        }
        
        Ok(())
    }

    /// Get a receiver for broadcast events
    pub fn subscribe_broadcast(&self) -> broadcast::Receiver<Arc<Event>> {
        self.broadcast_tx.subscribe()
    }

    /// Get list of all registered event types
    pub fn get_event_types(&self) -> Vec<String> {
        self.event_types.read().iter().cloned().collect()
    }

    /// Get subscribers for an event type
    pub fn get_subscribers(&self, event_type: &str) -> Vec<String> {
        let subscriptions = self.subscriptions.read();
        subscriptions
            .get(event_type)
            .map(|subs| subs.iter().map(|sub| sub.subscriber.clone()).collect())
            .unwrap_or_default()
    }

    /// Check if an event type has any subscribers
    pub fn has_subscribers(&self, event_type: &str) -> bool {
        let subscriptions = self.subscriptions.read();
        subscriptions
            .get(event_type)
            .map(|subs| !subs.is_empty())
            .unwrap_or(false)
    }

    /// Get statistics about the event bus
    pub fn stats(&self) -> EventBusStats {
        let subscriptions = self.subscriptions.read();
        let event_types = self.event_types.read();
        
        EventBusStats {
            total_event_types: event_types.len(),
            total_subscriptions: subscriptions.values().map(|subs| subs.len()).sum(),
            broadcast_receivers: self.broadcast_tx.receiver_count(),
        }
    }
}

/// Event bus statistics
#[derive(Debug, Clone)]
pub struct EventBusStats {
    pub total_event_types: usize,
    pub total_subscriptions: usize,
    pub broadcast_receivers: usize,
}

/// Predefined event types from events.txt
pub mod predefined {
    // Server events
    pub const SERVER_START: &str = "server_start";
    pub const SERVER_SHUTDOWN: &str = "server_shutdown";
    
    // User connection events
    pub const USER_CONNECT: &str = "user_connect";
    pub const USER_DISCONNECT: &str = "user_disconnect";
    
    // Room state events
    pub const ROOM_STATE_CHANGE: &str = "room_state_change";
    pub const ROOM_CREATE: &str = "room_create";
    pub const ROOM_DISBAND: &str = "room_disband";
    pub const USER_JOIN_ROOM: &str = "user_join_room";
    pub const USER_LEAVE_ROOM: &str = "user_leave_room";
    pub const ROOM_START_PREPARATION: &str = "room_start_preparation";
    pub const ROOM_END_PREPARATION: &str = "room_end_preparation";
    pub const GAME_END: &str = "game_end";
    pub const GAME_START: &str = "game_start";
    pub const ROOM_LOCK: &str = "room_lock";
    pub const ROOM_UNLOCK: &str = "room_unlock";
    pub const ROOM_SWITCH_NORMAL_MODE: &str = "room_switch_normal_mode";
    pub const ROOM_SWITCH_CYCLE_MODE: &str = "room_switch_cycle_mode";
    pub const USER_GIVE_UP_GAME: &str = "user_give_up_game";
    pub const ROOM_PREPARE_GAME: &str = "room_prepare_game";
    pub const CHART_SELECT: &str = "chart_select";
    
    // Command and message events
    pub const COMMAND_INPUT: &str = "command_input";
    pub const MESSAGE_SEND: &str = "message_send";
    
    // Plugin events
    pub const PLUGIN_LOAD: &str = "plugin_load";
    pub const PLUGIN_UNLOAD: &str = "plugin_unload";
    pub const PLUGIN_ERROR: &str = "plugin_error";
    pub const CONFIG_RELOAD: &str = "config_reload";
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::atomic::{AtomicUsize, Ordering};
    
    #[test]
    fn test_event_creation() {
        let data = serde_json::json!({"key": "value"});
        let event = Event::new("test_event", data.clone(), "test_source");
        
        assert_eq!(event.event_type, "test_event");
        assert_eq!(event.data, data);
        assert_eq!(event.source, "test_source");
        assert!(event.timestamp > 0);
    }
    
    #[test]
    fn test_event_bus_subscription() {
        let event_bus = EventBus::new();
        
        let handler_called = Arc::new(AtomicUsize::new(0));
        let handler_called_clone = Arc::clone(&handler_called);
        
        let handler: EventHandler = Box::new(move |_event| {
            handler_called_clone.fetch_add(1, Ordering::SeqCst);
            Ok(())
        });
        
        event_bus.subscribe("test_event", handler, "test_plugin").unwrap();
        
        let data = serde_json::json!({});
        let event = Event::new("test_event", data, "system");
        event_bus.emit(event).unwrap();
        
        assert_eq!(handler_called.load(Ordering::SeqCst), 1);
    }
    
    #[test]
    fn test_event_bus_unsubscribe() {
        let event_bus = EventBus::new();
        
        let handler_called = Arc::new(AtomicUsize::new(0));
        let handler_called_clone = Arc::clone(&handler_called);
        
        let handler: EventHandler = Box::new(move |_event| {
            handler_called_clone.fetch_add(1, Ordering::SeqCst);
            Ok(())
        });
        
        event_bus.subscribe("test_event", handler, "test_plugin").unwrap();
        event_bus.unsubscribe("test_event", "test_plugin").unwrap();
        
        let data = serde_json::json!({});
        let event = Event::new("test_event", data, "system");
        event_bus.emit(event).unwrap();
        
        assert_eq!(handler_called.load(Ordering::SeqCst), 0);
    }
}