#pragma once
#include "commands.h"
#include "room.h"
#include "session.h"
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
class Server {
public:
    explicit Server(uint16_t port);
    ~Server();

    void run(); // Main accept loop (blocks)

private:
    int listen_fd_ = -1;
    uint16_t port_;
    std::shared_ptr<ServerState> state_;
    std::thread lost_con_thread_;
    std::shared_ptr<PluginManager> plugin_manager_;

    void lost_connection_loop();
    void accept_one();
};
