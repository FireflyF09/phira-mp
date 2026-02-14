#pragma once
#include "commands.h"
#include "room.h"
#include "session.h"
#include <memory>
#include <mutex>
#include <queue>
#include <shared_mutex>
#include <thread>
#include <unordered_map>
#include <vector>

// ── Server config (from YAML-like file) ──────────────────────────────
struct ServerConfig {
    std::vector<int32_t> monitors = {2};

    static ServerConfig load(const std::string& path);
};

// ── Shared server state ──────────────────────────────────────────────
struct ServerState : std::enable_shared_from_this<ServerState> {
    ServerConfig config;

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

    void push_lost_connection(MpUuid id);
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

    void lost_connection_loop();
    void accept_one();
};
