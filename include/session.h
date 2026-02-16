#pragma once
#include "commands.h"
#include "l10n.h"
#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <shared_mutex>
#include <string>
#include <thread>

struct ServerState; // forward
struct Room;        // forward

// ── Thread-safe send queue ───────────────────────────────────────────
class SendQueue {
public:
    void push(ServerCommand cmd);
    bool pop(ServerCommand& cmd, int timeout_ms = 100);
    void close();
    bool is_closed() const;
private:
    std::queue<ServerCommand> queue_;
    mutable std::mutex mtx_;
    std::condition_variable cv_;
    std::atomic<bool> closed_{false};
};

// ── User ─────────────────────────────────────────────────────────────
struct Session;

struct User : std::enable_shared_from_this<User> {
    int32_t id;
    std::string name;
    Language lang;

    std::shared_ptr<ServerState> server;
    mutable std::shared_mutex session_mtx;
    std::weak_ptr<Session> session;

    mutable std::shared_mutex room_mtx;
    std::shared_ptr<Room> room;

    std::atomic<bool> monitor{false};
    std::atomic<uint32_t> game_time{0}; // f32 bits stored as u32

    std::mutex dangle_mtx;
    std::shared_ptr<void> dangle_mark; // just a ref-counted marker

    User(int32_t id, std::string name, Language lang, std::shared_ptr<ServerState> server);

    UserInfo to_info() const;
    bool can_monitor() const;
    void set_session(std::weak_ptr<Session> s);
    void try_send(ServerCommand cmd) const;
    void dangle();

    // Get/set room (thread-safe)
    std::shared_ptr<Room> get_room() const;
    void set_room(std::shared_ptr<Room> r);
    void clear_room();
};

// ── Session ──────────────────────────────────────────────────────────
struct Session : std::enable_shared_from_this<Session> {
    MpUuid id;
    uint8_t version_ = 0;
    int socket_fd = -1;

    std::shared_ptr<User> user;
    SendQueue send_queue;

    std::thread send_thread;
    std::thread recv_thread;
    std::thread heartbeat_thread;

    std::mutex last_recv_mtx;
    std::chrono::steady_clock::time_point last_recv;

    std::atomic<bool> alive{true};

    // Don't copy/move
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    Session(MpUuid id, int fd, uint8_t version, std::shared_ptr<ServerState> server);
    ~Session();

    uint8_t version() const { return version_; }
    const std::string& name() const { return user->name; }
    void try_send(ServerCommand cmd);
    void stop();

    // Internal: these run in separate threads
    void send_loop();
    void recv_loop(std::shared_ptr<ServerState> server);
    void heartbeat_loop(std::shared_ptr<ServerState> server);

    void update_last_recv();

    // Command processing
    void handle_authenticate(const std::string& token, std::shared_ptr<ServerState> server);
    void process_command(const ClientCommand& cmd);
};
