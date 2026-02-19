use crate::Error;
use std::{
    collections::HashMap,
    sync::Arc,
};
use parking_lot::RwLock;
use regex::Regex;
use tracing::{info, debug, warn};

/// Command handler function signature
pub type CommandHandler = Box<dyn Fn(&str, &[String]) -> Result<String, Error> + Send + Sync>;

/// Command argument parser
pub type ArgumentParser = Box<dyn Fn(&str) -> Result<Vec<String>, Error> + Send + Sync>;

/// Command structure
pub struct Command {
    /// Command name
    pub name: String,
    /// Command description
    pub description: String,
    /// Command handler
    pub handler: CommandHandler,
    /// Argument parser (optional)
    pub argument_parser: Option<ArgumentParser>,
    /// Command permissions (optional)
    pub permissions: Option<Vec<String>>,
    /// Command aliases (optional)
    pub aliases: Vec<String>,
    /// Plugin that registered this command
    pub plugin: String,
}

impl Command {
    /// Create a new command
    pub fn new(
        name: impl Into<String>,
        description: impl Into<String>,
        handler: CommandHandler,
        plugin: impl Into<String>,
    ) -> Self {
        Self {
            name: name.into(),
            description: description.into(),
            handler,
            argument_parser: None,
            permissions: None,
            aliases: Vec::new(),
            plugin: plugin.into(),
        }
    }

    /// Set argument parser
    pub fn with_argument_parser(mut self, parser: ArgumentParser) -> Self {
        self.argument_parser = Some(parser);
        self
    }

    /// Set permissions
    pub fn with_permissions(mut self, permissions: Vec<String>) -> Self {
        self.permissions = Some(permissions);
        self
    }

    /// Add aliases
    pub fn with_aliases(mut self, aliases: Vec<String>) -> Self {
        self.aliases = aliases;
        self
    }

    /// Parse command arguments
    pub fn parse_arguments(&self, args_str: &str) -> Result<Vec<String>, Error> {
        if let Some(parser) = &self.argument_parser {
            parser(args_str)
        } else {
            // Default argument parser: split by whitespace
            Ok(args_str
                .split_whitespace()
                .map(|s| s.to_string())
                .collect())
        }
    }

    /// Execute the command
    pub fn execute(&self, args_str: &str) -> Result<String, Error> {
        let args = self.parse_arguments(args_str)?;
        (self.handler)(&self.name, &args)
    }

    /// Check if command matches a name or alias
    pub fn matches(&self, name: &str) -> bool {
        self.name == name || self.aliases.iter().any(|alias| alias == name)
    }
}

/// Command registry for managing all commands
pub struct CommandRegistry {
    /// Registered commands by name
    commands: RwLock<HashMap<String, Arc<Command>>>,
    /// Command aliases mapping
    aliases: RwLock<HashMap<String, String>>,
}

impl CommandRegistry {
    /// Create a new command registry
    pub fn new() -> Self {
        Self {
            commands: RwLock::new(HashMap::new()),
            aliases: RwLock::new(HashMap::new()),
        }
    }

    /// Register a command
    pub fn register(&self, command: Command) -> Result<(), Error> {
        let name = command.name.clone();
        let plugin = command.plugin.clone();
        
        debug!("Registering command '{}' from plugin '{}'", name, plugin);
        
        let command_arc = Arc::new(command);
        
        // Check if command already exists
        {
            let commands = self.commands.read();
            if commands.contains_key(&name) {
                return Err(Error::Command(format!(
                    "Command '{}' already registered",
                    name
                )));
            }
        }
        
        // Register command
        {
            let mut commands = self.commands.write();
            commands.insert(name.clone(), command_arc.clone());
        }
        
        // Register aliases
        {
            let mut aliases = self.aliases.write();
            for alias in &command_arc.aliases {
                if aliases.contains_key(alias) {
                    warn!("Command alias '{}' already registered, overwriting", alias);
                }
                aliases.insert(alias.clone(), name.clone());
            }
        }
        
        info!("Command '{}' registered successfully", name);
        Ok(())
    }

    /// Unregister a command
    pub fn unregister(&self, name: &str) -> Result<(), Error> {
        debug!("Unregistering command '{}'", name);
        
        // Find actual command name (handling aliases)
        let actual_name = self.resolve_alias(name).unwrap_or_else(|| name.to_string());
        
        // Remove command
        {
            let mut commands = self.commands.write();
            let command = commands.remove(&actual_name);
            
            if command.is_none() {
                return Err(Error::Command(format!("Command '{}' not found", name)));
            }
            
            let command = command.unwrap();
            
            // Remove aliases
            let mut aliases = self.aliases.write();
            for alias in &command.aliases {
                aliases.remove(alias);
            }
        }
        
        info!("Command '{}' unregistered successfully", actual_name);
        Ok(())
    }

    /// Unregister all commands from a plugin
    pub fn unregister_all_from_plugin(&self, plugin: &str) -> Result<(), Error> {
        debug!("Unregistering all commands from plugin '{}'", plugin);
        
        let mut commands_to_remove = Vec::new();
        
        // Find commands to remove
        {
            let commands = self.commands.read();
            for (name, command) in commands.iter() {
                if command.plugin == plugin {
                    commands_to_remove.push(name.clone());
                }
            }
        }
        
        // Remove commands
        for name in commands_to_remove {
            self.unregister(&name)?;
        }
        
        info!("All commands from plugin '{}' unregistered", plugin);
        Ok(())
    }

    /// Execute a command
    pub fn execute(&self, command_line: &str) -> Result<String, Error> {
        debug!("Executing command line: '{}'", command_line);
        
        let (command_name, args_str) = self.parse_command_line(command_line);
        
        // Resolve alias
        let actual_command_name = self.resolve_alias(&command_name)
            .unwrap_or_else(|| command_name.clone());
        
        // Get command
        let command = {
            let commands = self.commands.read();
            commands.get(&actual_command_name).cloned()
        };
        
        match command {
            Some(command) => {
                // TODO: Check permissions here
                command.execute(args_str)
            }
            None => Err(Error::Command(format!("Command '{}' not found", command_name))),
        }
    }

    /// Get a command by name
    pub fn get_command(&self, name: &str) -> Option<Arc<Command>> {
        let actual_name = self.resolve_alias(name).unwrap_or_else(|| name.to_string());
        let commands = self.commands.read();
        commands.get(&actual_name).cloned()
    }

    /// Get all registered commands
    pub fn get_all_commands(&self) -> Vec<Arc<Command>> {
        let commands = self.commands.read();
        commands.values().cloned().collect()
    }

    /// Get commands from a specific plugin
    pub fn get_commands_from_plugin(&self, plugin: &str) -> Vec<Arc<Command>> {
        let commands = self.commands.read();
        commands
            .values()
            .filter(|cmd| cmd.plugin == plugin)
            .cloned()
            .collect()
    }

    /// Search commands by name or description
    pub fn search_commands(&self, query: &str) -> Vec<Arc<Command>> {
        let commands = self.commands.read();
        commands
            .values()
            .filter(|cmd| {
                cmd.name.contains(query) ||
                cmd.description.contains(query) ||
                cmd.aliases.iter().any(|alias| alias.contains(query))
            })
            .cloned()
            .collect()
    }

    /// Parse a command line into command name and arguments string
    fn parse_command_line<'a>(&self, command_line: &'a str) -> (String, &'a str) {
        let command_line = command_line.trim();

        if let Some(space_pos) = command_line.find(' ') {
            let (cmd, args) = command_line.split_at(space_pos);
            (cmd.to_string(), args.trim())
        } else {
            (command_line.to_string(), "")
        }
    }

    /// Resolve a command alias to the actual command name
    fn resolve_alias(&self, name: &str) -> Option<String> {
        let aliases = self.aliases.read();
        aliases.get(name).cloned()
    }

    /// Get command registry statistics
    pub fn stats(&self) -> CommandRegistryStats {
        let commands = self.commands.read();
        let aliases = self.aliases.read();
        
        CommandRegistryStats {
            total_commands: commands.len(),
            total_aliases: aliases.len(),
            plugins: commands
                .values()
                .map(|cmd| cmd.plugin.clone())
                .collect::<std::collections::HashSet<_>>()
                .len(),
        }
    }

    /// Create a default argument parser for a specific pattern
    pub fn create_regex_parser(pattern: &str) -> Result<ArgumentParser, Error> {
        let regex = Regex::new(pattern)
            .map_err(|e| Error::Command(format!("Invalid regex pattern: {}", e)))?;
        let pattern = pattern.to_string(); // Convert to owned String

        Ok(Box::new(move |args_str| {
            let captures = regex.captures(args_str).ok_or_else(|| {
                Error::Command(format!("Arguments don't match pattern: {}", pattern))
            })?;

            let mut args = Vec::new();
            for i in 1..captures.len() {
                if let Some(matched) = captures.get(i) {
                    args.push(matched.as_str().to_string());
                }
            }

            Ok(args)
        }))
    }

    /// Create a key-value argument parser
    pub fn create_key_value_parser() -> ArgumentParser {
        Box::new(|args_str| {
            let mut args = Vec::new();
            let mut current_arg = String::new();
            let mut in_quotes = false;
            let mut escape_next = false;
            
            for ch in args_str.chars() {
                if escape_next {
                    current_arg.push(ch);
                    escape_next = false;
                } else if ch == '\\' {
                    escape_next = true;
                } else if ch == '"' {
                    in_quotes = !in_quotes;
                } else if ch == ' ' && !in_quotes {
                    if !current_arg.is_empty() {
                        args.push(current_arg);
                        current_arg = String::new();
                    }
                } else {
                    current_arg.push(ch);
                }
            }
            
            if !current_arg.is_empty() {
                args.push(current_arg);
            }
            
            Ok(args)
        })
    }
}

/// Command registry statistics
#[derive(Debug, Clone)]
pub struct CommandRegistryStats {
    pub total_commands: usize,
    pub total_aliases: usize,
    pub plugins: usize,
}

/// Predefined commands from commands.txt
pub mod predefined {
    // User management commands
    pub const HELP: &str = "help";
    pub const KICK_USER: &str = "kick_user";
    pub const BAN_USER_ID: &str = "ban_user_id";
    pub const UNBAN_USER_ID: &str = "unban_user_id";
    pub const BAN_USER_IP: &str = "ban_user_ip";
    pub const UNBAN_USER_IP: &str = "unban_user_ip";
    pub const GET_USER_INFO: &str = "get_user_info";
    pub const GET_USERNAME: &str = "get_username";
    pub const GET_USER_LANGUAGE: &str = "get_user_language";
    pub const GET_USER_PLAYTIME: &str = "get_user_playtime";
    pub const GET_PLAYTIME_LEADERBOARD: &str = "get_playtime_leaderboard";
    pub const GET_BANNED_USERS_ID: &str = "get_banned_users_id";
    pub const GET_BANNED_USERS_IP: &str = "get_banned_users_ip";
    pub const IS_USER_BANNED_ID: &str = "is_user_banned_id";
    pub const IS_USER_BANNED_IP: &str = "is_user_banned_ip";
    pub const BAN_USER_FROM_ROOM_ID: &str = "ban_user_from_room_id";
    pub const UNBAN_USER_FROM_ROOM_ID: &str = "unban_user_from_room_id";
    pub const BAN_USER_FROM_ROOM_IP: &str = "ban_user_from_room_ip";
    pub const UNBAN_USER_FROM_ROOM_IP: &str = "unban_user_from_room_ip";
    pub const IS_USER_BANNED_FROM_ROOM: &str = "is_user_banned_from_room";
    
    // Room management commands
    pub const CREATE_ROOM: &str = "create_room";
    pub const DISBAND_ROOM: &str = "disband_room";
    pub const ADD_USER_TO_ROOM: &str = "add_user_to_room";
    pub const KICK_USER_FROM_ROOM: &str = "kick_user_from_room";
    pub const GET_ROOM_INFO: &str = "get_room_info";
    pub const GET_ROOM_USER_COUNT: &str = "get_room_user_count";
    pub const GET_ROOM_USER_IDS: &str = "get_room_user_ids";
    pub const GET_ROOM_HOST_ID: &str = "get_room_host_id";
    pub const SET_ROOM_MAX_USERS: &str = "set_room_max_users";
    pub const START_ROOM_PREPARATION: &str = "start_room_preparation";
    pub const END_ROOM_PREPARATION: &str = "end_room_preparation";
    pub const FORCE_START_ROOM_GAME: &str = "force_start_room_game";
    pub const SET_ROOM_LOCK: &str = "set_room_lock";
    pub const SWITCH_ROOM_NORMAL_MODE: &str = "switch_room_normal_mode";
    pub const SWITCH_ROOM_CYCLE_MODE: &str = "switch_room_cycle_mode";
    pub const SELECT_ROOM_CHART: &str = "select_room_chart";
    
    // Messaging commands
    pub const SEND_MESSAGE_TO_USER: &str = "send_message_to_user";
    pub const BROADCAST_MESSAGE_TO_ALL: &str = "broadcast_message_to_all";
    pub const BROADCAST_MESSAGE_TO_ROOM: &str = "broadcast_message_to_room";
    pub const BROADCAST_MESSAGE_TO_ALL_ROOMS: &str = "broadcast_message_to_all_rooms";
    
    // Server management commands
    pub const SHUTDOWN_SERVER: &str = "shutdown_server";
    pub const RESTART_SERVER: &str = "restart_server";
    pub const RELOAD_ALL_PLUGINS: &str = "reload_all_plugins";
    pub const RELOAD_PLUGIN: &str = "reload_plugin";
    pub const GET_PLUGIN_LIST: &str = "get_plugin_list";
    pub const GET_PLAYTIME_TOTAL_LEADERBOARD: &str = "get_playtime_total_leaderboard";
    pub const GET_ONLINE_USER_COUNT: &str = "get_online_user_count";
    pub const GET_AVAILABLE_ROOM_COUNT: &str = "get_available_room_count";
    pub const GET_ROOM_LIST: &str = "get_room_list";
    pub const GET_AVAILABLE_ROOM_LIST: &str = "get_available_room_list";
    pub const GET_ONLINE_USER_IDS: &str = "get_online_user_ids";
}

#[cfg(test)]
mod tests {
    use super::*;
    
    #[test]
    fn test_command_registration() {
        let registry = CommandRegistry::new();
        
        let handler: CommandHandler = Box::new(|name, args| {
            Ok(format!("Command '{}' executed with args: {:?}", name, args))
        });
        
        let command = Command::new("test", "Test command", handler, "test_plugin");
        registry.register(command).unwrap();
        
        assert!(registry.get_command("test").is_some());
    }
    
    #[test]
    fn test_command_execution() {
        let registry = CommandRegistry::new();
        
        let handler: CommandHandler = Box::new(|name, args| {
            Ok(format!("Hello from {} with args: {:?}", name, args))
        });
        
        let command = Command::new("hello", "Say hello", handler, "test_plugin");
        registry.register(command).unwrap();
        
        let result = registry.execute("hello world").unwrap();
        assert_eq!(result, "Hello from hello with args: [\"world\"]");
    }
    
    #[test]
    fn test_command_aliases() {
        let registry = CommandRegistry::new();
        
        let handler: CommandHandler = Box::new(|name, _args| {
            Ok(format!("Command {} executed", name))
        });
        
        let command = Command::new("test", "Test command", handler, "test_plugin")
            .with_aliases(vec!["t".to_string(), "testcmd".to_string()]);
        
        registry.register(command).unwrap();
        
        assert!(registry.get_command("t").is_some());
        assert!(registry.get_command("testcmd").is_some());
        assert!(registry.get_command("test").is_some());
    }
}