#pragma once
#include "commands.h"
#include "http_client.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <set>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

struct User; // forward

static constexpr int ROOM_MAX_USERS = 8;

// ── Chart info (from API) ────────────────────────────────────────────
struct Chart {
    int32_t id = 0;
    std::string name;
};

// ── Record info (from API) ───────────────────────────────────────────
struct Record {
    int32_t id = 0, player = 0, score = 0;
    int32_t perfect = 0, good = 0, bad = 0, miss = 0, max_combo = 0;
    float accuracy = 0;
    bool full_combo = false;
    float std_dev = 0, std_score = 0;
};

// ── Internal room state ──────────────────────────────────────────────
enum class InternalRoomStateType { SelectChart, WaitForReady, Playing };

struct InternalRoomState {
    InternalRoomStateType type = InternalRoomStateType::SelectChart;
    std::set<int32_t> started;     // for WaitForReady
    std::unordered_map<int32_t, Record> results; // for Playing
    std::set<int32_t> aborted;     // for Playing

    RoomState to_client(std::optional<int32_t> chart_id) const;

    static InternalRoomState select_chart() { return {InternalRoomStateType::SelectChart, {}, {}, {}}; }
    static InternalRoomState wait_for_ready(std::set<int32_t> s) {
        return {InternalRoomStateType::WaitForReady, std::move(s), {}, {}};
    }
    static InternalRoomState playing() {
        return {InternalRoomStateType::Playing, {}, {}, {}};
    }
};

// ── Room ─────────────────────────────────────────────────────────────
struct Room : std::enable_shared_from_this<Room> {
    RoomId id;

    mutable std::shared_mutex host_mtx;
    std::weak_ptr<User> host;

    mutable std::shared_mutex state_mtx;
    InternalRoomState state;

    std::atomic<bool> live{false};
    std::atomic<bool> locked{false};
    std::atomic<bool> cycle{false};

    mutable std::shared_mutex users_mtx;
    std::vector<std::weak_ptr<User>> users_;

    mutable std::shared_mutex monitors_mtx;
    std::vector<std::weak_ptr<User>> monitors_;

    mutable std::shared_mutex chart_mtx;
    std::optional<Chart> chart;

    Room(RoomId id, std::weak_ptr<User> host_user);

    bool is_live() const { return live.load(); }
    bool is_locked() const { return locked.load(); }
    bool is_cycle() const { return cycle.load(); }

    RoomState client_room_state() const;
    ClientRoomState client_state(const User& user) const;
    void on_state_change();

    bool add_user(std::weak_ptr<User> user, bool monitor);

    std::vector<std::shared_ptr<User>> users() const;
    std::vector<std::shared_ptr<User>> monitors() const;

    bool check_host(const User& user) const;

    void send(Message msg);
    void broadcast(ServerCommand cmd);
    void broadcast_monitors(ServerCommand cmd);
    void send_as(const User& user, const std::string& content);

    // Returns true if room should be dropped
    bool on_user_leave(const User& user);

    void reset_game_time();
    void check_all_ready();
};
