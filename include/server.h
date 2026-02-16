#pragma once
#include "commands.h"
#include "room.h"
#include "session.h"
#include "plugin_api.h"
// PluginManager forward declaration
class PluginManager;
#include <memory>
#include <mutex>
#include <queue>
#include <shared_mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// ── Server config (from YAML-like file) ──────────────────────────────
struct ServerConfig {
    std::vector<int32_t> monitors = {2};
    std::string admin_token; // Permanent admin token
    bool replay_enabled = true;
    bool room_creation_enabled = true;

    static ServerConfig load(const std::string& path);
};

// ── Shared server state ──────────────────────────────────────────────
struct ServerState : std::enable_shared_from_this<ServerState> {
    ServerConfig config;
    std::weak_ptr<PluginManager> plugin_manager;

    mutable std::shared_mutex sessions_mtx;
    std::unordered_map<MpUuid, std::shared_ptr<Session>, MpUuidHash> sessions;

    mutable std::shared_mutex users_mtx;
    std::unordered_map<int32_t, std::shared_ptr<User>> users;

    mutable std::shared_mutex rooms_mtx;
    std::unordered_map<std::string, std::shared_ptr<Room>> rooms; // keyed by RoomId string

    // Lost connection channel
    std::mutex lost_con_mtx;
    std::condition_variable lost_con_cv;
    std::queue<MpUuid> lost_con_queue;
    std::atomic<bool> running{true};

    // Admin authentication state
    mutable std::mutex admin_state_mtx;
    struct TempAdminToken {
        std::string ip;
        uint64_t expires_at;
        bool banned = false;
    };
    std::unordered_map<std::string, TempAdminToken> temp_admin_tokens;
    
    // Ban management
    mutable std::mutex ban_mtx;
    std::unordered_set<int32_t> banned_users; // banned user IDs
    std::unordered_map<std::string, std::unordered_set<int32_t>> banned_room_users; // room ID -> banned user IDs
    
    struct OtpSession {
        std::string otp;
        uint64_t expires_at;
        std::string ip;
    };
    std::unordered_map<std::string, OtpSession> otp_sessions; // key: session ID
    
    std::unordered_map<std::string, int> admin_failed_attempts; // key: IP
    std::unordered_set<std::string> admin_banned_ips;
    
    std::unordered_map<std::string, int> otp_failed_attempts_ip; // key: IP
    std::unordered_map<std::string, int> otp_failed_attempts_session; // key: session ID
    std::unordered_set<std::string> otp_banned_ips;
    std::unordered_set<std::string> otp_banned_sessions;

    // Replay storage
    mutable std::mutex replay_mtx;
    struct ReplayInfo {
        std::string id;
        std::string filename;
        std::string player_name;
        std::string song_id;
        uint64_t created_at;
        uint64_t size;
    };
    std::unordered_map<std::string, ReplayInfo> replays; // key: replay ID

    void push_lost_connection(MpUuid id);

    // Admin auth helper functions
    bool check_admin_auth(const std::string& token, const std::string& client_ip);
    std::string request_otp(const std::string& client_ip);
    std::string verify_otp(const std::string& session_id, const std::string& otp, const std::string& client_ip);
    void cleanup_expired_auth();

    // Replay management functions
    std::string save_replay(const std::string& replay_data, const std::string& player_name, const std::string& song_id);
    bool delete_replay(const std::string& replay_id);
    std::string get_replay_filepath(const std::string& replay_id);
    std::vector<ReplayInfo> list_replays();
};

// ── Server ───────────────────────────────────────────────────────────
class Server : public PluginServerInterface {
public:
    explicit Server(uint16_t port);
    ~Server();

    void run(); // Main accept loop (blocks)

    // PluginServerInterface implementation
    void shutdown_server() override;
    void reload_plugins() override;
    
    bool kick_user(int32_t user_id, bool preserve_room = false) override;
    bool ban_user(int32_t user_id) override;
    bool unban_user(int32_t user_id) override;
    bool is_user_banned(int32_t user_id) override;
    std::vector<int32_t> get_banned_users() override;
    
    bool ban_room_user(int32_t user_id, const std::string& room_id) override;
    bool unban_room_user(int32_t user_id, const std::string& room_id) override;
    bool is_user_banned_from_room(int32_t user_id, const std::string& room_id) override;
    
    bool disband_room(const std::string& room_id) override;
    bool set_max_users(const std::string& room_id, int max_users) override;
    std::optional<int> get_room_max_users(const std::string& room_id) override;
    
    bool broadcast_message(const std::string& message) override;
    bool roomsay_message(const std::string& room_id, const std::string& message) override;
    
    bool set_replay_status(bool enabled) override;
    bool get_replay_status() override;
    
    bool set_room_creation_status(bool enabled) override;
    bool get_room_creation_status() override;
    
    bool add_ip_to_blacklist(const std::string& ip, bool is_admin = true) override;
    bool remove_ip_from_blacklist(const std::string& ip, bool is_admin = true) override;
    bool is_ip_banned(const std::string& ip) override;
    std::vector<std::string> get_banned_ips(bool admin_list = true) override;
    void clear_ip_blacklist(bool admin_list = true) override;
    
    bool enable_contest(const std::string& room_id, bool manual_start = false, bool auto_disband = false) override;
    bool disable_contest(const std::string& room_id) override;
    bool add_contest_whitelist(const std::string& room_id, int32_t user_id) override;
    bool remove_contest_whitelist(const std::string& room_id, int32_t user_id) override;
    bool start_contest(const std::string& room_id, bool force = false) override;
    
    int get_connected_user_count() override;
    int get_active_room_count() override;
    std::vector<std::string> get_room_list() override;
    std::vector<int32_t> get_connected_user_ids() override;
    
    std::optional<std::string> get_user_name(int32_t user_id) override;
    std::optional<std::string> get_user_language(int32_t user_id) override;
    std::optional<std::string> get_user_room_id(int32_t user_id) override;
    
    std::optional<int> get_room_user_count(const std::string& room_id) override;
    std::vector<int32_t> get_room_user_ids(const std::string& room_id) override;
    std::optional<std::string> get_room_owner_id(const std::string& room_id) override;
    
    void save_admin_data() override;
    void load_admin_data() override;

private:
    int listen_fd_ = -1;
    uint16_t port_;
    std::shared_ptr<ServerState> state_;
    std::thread lost_con_thread_;
    std::thread cli_thread_;
    std::shared_ptr<PluginManager> plugin_manager_;
    std::atomic<bool> running_{true};

    void lost_connection_loop();
    void cli_loop();
    void accept_one();

    // CLI command handlers
    void handle_cli_command(const std::string& command);
    void show_help();
    void show_status();
    void list_rooms();
    void list_users();
    void show_user_details(const std::string& user_id_str);
    void broadcast_message_cli(const std::string& message);
    void roomsay_message_cli(const std::string& room_id, const std::string& message);
    void kick_user_cli(const std::string& user_id);
    void ban_user_cli(const std::string& user_id);
    void unban_user_cli(const std::string& user_id_str);
    void show_banlist();
    void ban_room_user_cli(const std::string& user_id_str, const std::string& room_id);
    void unban_room_user_cli(const std::string& user_id_str, const std::string& room_id);
    void set_replay_status_cli(const std::string& status);
    void set_room_creation_status_cli(const std::string& status);
    void disband_room_cli(const std::string& room_id);
    void set_max_users_cli(const std::string& room_id, const std::string& count_str);
    void handle_ipblacklist(const std::vector<std::string>& args);
    void handle_contest(const std::vector<std::string>& args);
    void reload_plugins_cli();
    void shutdown_server_cli();

    // Helper methods for CLI commands
    void broadcast_to_room(const std::string& room_id, ServerCommand cmd);
    void save_admin_data_cli();
    void load_admin_data_cli();
    
    // Admin helper methods
    bool admin_disconnect_user(int32_t user_id, bool preserve_room = false);
};
