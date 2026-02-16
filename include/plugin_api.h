#pragma once
#include <string>
#include <vector>
#include <memory>
#include <optional>

// Forward declarations
class User;
class Room;

// Interface for plugins to interact with server functionality
class PluginServerInterface {
public:
    virtual ~PluginServerInterface() = default;

    // Server management
    virtual void shutdown_server() = 0;
    virtual void reload_plugins() = 0;
    
    // User management
    virtual bool kick_user(int32_t user_id, bool preserve_room = false) = 0;
    virtual bool ban_user(int32_t user_id) = 0;
    virtual bool unban_user(int32_t user_id) = 0;
    virtual bool is_user_banned(int32_t user_id) = 0;
    virtual std::vector<int32_t> get_banned_users() = 0;
    
    // Room-specific bans
    virtual bool ban_room_user(int32_t user_id, const std::string& room_id) = 0;
    virtual bool unban_room_user(int32_t user_id, const std::string& room_id) = 0;
    virtual bool is_user_banned_from_room(int32_t user_id, const std::string& room_id) = 0;
    
    // Room management
    virtual bool disband_room(const std::string& room_id) = 0;
    virtual bool set_max_users(const std::string& room_id, int max_users) = 0;
    virtual std::optional<int> get_room_max_users(const std::string& room_id) = 0;
    
    // Messaging
    virtual bool broadcast_message(const std::string& message) = 0;
    virtual bool roomsay_message(const std::string& room_id, const std::string& message) = 0;
    
    // Replay management
    virtual bool set_replay_status(bool enabled) = 0;
    virtual bool get_replay_status() = 0;
    
    // Room creation management
    virtual bool set_room_creation_status(bool enabled) = 0;
    virtual bool get_room_creation_status() = 0;
    
    // IP blacklist management
    virtual bool add_ip_to_blacklist(const std::string& ip, bool is_admin = true) = 0;
    virtual bool remove_ip_from_blacklist(const std::string& ip, bool is_admin = true) = 0;
    virtual bool is_ip_banned(const std::string& ip) = 0;
    virtual std::vector<std::string> get_banned_ips(bool admin_list = true) = 0;
    virtual void clear_ip_blacklist(bool admin_list = true) = 0;
    
    // Contest management
    virtual bool enable_contest(const std::string& room_id, bool manual_start = false, bool auto_disband = false) = 0;
    virtual bool disable_contest(const std::string& room_id) = 0;
    virtual bool add_contest_whitelist(const std::string& room_id, int32_t user_id) = 0;
    virtual bool remove_contest_whitelist(const std::string& room_id, int32_t user_id) = 0;
    virtual bool start_contest(const std::string& room_id, bool force = false) = 0;
    
    // Server information
    virtual int get_connected_user_count() = 0;
    virtual int get_active_room_count() = 0;
    virtual std::vector<std::string> get_room_list() = 0;
    virtual std::vector<int32_t> get_connected_user_ids() = 0;
    
    // User information
    virtual std::optional<std::string> get_user_name(int32_t user_id) = 0;
    virtual std::optional<std::string> get_user_language(int32_t user_id) = 0;
    virtual std::optional<std::string> get_user_room_id(int32_t user_id) = 0;
    
    // Room information
    virtual std::optional<int> get_room_user_count(const std::string& room_id) = 0;
    virtual std::vector<int32_t> get_room_user_ids(const std::string& room_id) = 0;
    virtual std::optional<std::string> get_room_owner_id(const std::string& room_id) = 0;
    
    // Admin data persistence
    virtual void save_admin_data() = 0;
    virtual void load_admin_data() = 0;
};