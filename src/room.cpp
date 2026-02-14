#include "room.h"
#include "session.h"
#include "server.h"
#include <algorithm>
#include <iostream>
#include <random>

// ── InternalRoomState ────────────────────────────────────────────────
RoomState InternalRoomState::to_client(std::optional<int32_t> chart_id) const {
    switch (type) {
    case InternalRoomStateType::SelectChart:
        return RoomState::select_chart(chart_id);
    case InternalRoomStateType::WaitForReady:
        return RoomState::waiting_for_ready();
    case InternalRoomStateType::Playing:
        return RoomState::playing();
    }
    return RoomState::select_chart();
}

// ── Room ─────────────────────────────────────────────────────────────

Room::Room(RoomId rid, std::weak_ptr<User> host_user)
    : id(std::move(rid)), host(std::move(host_user))
{
    // The host is the first user
    auto h = host.lock();
    if (h) {
        users_.push_back(std::weak_ptr<User>(host));
    }
}

RoomState Room::client_room_state() const {
    std::shared_lock sl(state_mtx);
    std::shared_lock cl(chart_mtx);
    std::optional<int32_t> cid;
    if (chart) cid = chart->id;
    return state.to_client(cid);
}

ClientRoomState Room::client_state(const User& user) const {
    ClientRoomState cs;
    cs.id = id;
    cs.state = client_room_state();
    cs.live = is_live();
    cs.locked = is_locked();
    cs.cycle_flag = is_cycle();
    cs.is_host = check_host(user);
    {
        std::shared_lock sl(state_mtx);
        cs.is_ready = (state.type == InternalRoomStateType::WaitForReady &&
                       state.started.count(user.id) > 0);
    }
    auto u = users();
    auto m = monitors();
    for (auto& usr : u) cs.users[usr->id] = usr->to_info();
    for (auto& usr : m) cs.users[usr->id] = usr->to_info();
    return cs;
}

void Room::on_state_change() {
    broadcast(ServerCommand::change_state(client_room_state()));
}

bool Room::add_user(std::weak_ptr<User> user, bool is_monitor) {
    if (is_monitor) {
        std::unique_lock lock(monitors_mtx);
        // Clean expired
        monitors_.erase(
            std::remove_if(monitors_.begin(), monitors_.end(),
                           [](auto& w) { return w.expired(); }),
            monitors_.end());
        monitors_.push_back(std::move(user));
        return true;
    } else {
        std::unique_lock lock(users_mtx);
        users_.erase(
            std::remove_if(users_.begin(), users_.end(),
                           [](auto& w) { return w.expired(); }),
            users_.end());
        if ((int)users_.size() >= ROOM_MAX_USERS) return false;
        users_.push_back(std::move(user));
        return true;
    }
}

std::vector<std::shared_ptr<User>> Room::users() const {
    std::shared_lock lock(users_mtx);
    std::vector<std::shared_ptr<User>> result;
    for (auto& w : users_) {
        if (auto s = w.lock()) result.push_back(s);
    }
    return result;
}

std::vector<std::shared_ptr<User>> Room::monitors() const {
    std::shared_lock lock(monitors_mtx);
    std::vector<std::shared_ptr<User>> result;
    for (auto& w : monitors_) {
        if (auto s = w.lock()) result.push_back(s);
    }
    return result;
}

bool Room::check_host(const User& user) const {
    std::shared_lock lock(host_mtx);
    auto h = host.lock();
    return h && h->id == user.id;
}

void Room::send(Message msg) {
    broadcast(ServerCommand::msg(std::move(msg)));
}

void Room::broadcast(ServerCommand cmd) {
    auto u = users();
    auto m = monitors();
    for (auto& usr : u) usr->try_send(cmd);
    for (auto& usr : m) usr->try_send(cmd);
}

void Room::broadcast_monitors(ServerCommand cmd) {
    auto m = monitors();
    for (auto& usr : m) usr->try_send(cmd);
}

void Room::send_as(const User& user, const std::string& content) {
    send(Message::chat(user.id, content));
}

bool Room::on_user_leave(const User& user) {
    send(Message::leave_room(user.id, user.name));

    // Clear user's room reference
    // (caller should handle this)

    bool is_mon = user.monitor.load();
    if (is_mon) {
        std::unique_lock lock(monitors_mtx);
        monitors_.erase(
            std::remove_if(monitors_.begin(), monitors_.end(),
                           [&](auto& w) {
                               auto s = w.lock();
                               return !s || s->id == user.id;
                           }),
            monitors_.end());
    } else {
        std::unique_lock lock(users_mtx);
        users_.erase(
            std::remove_if(users_.begin(), users_.end(),
                           [&](auto& w) {
                               auto s = w.lock();
                               return !s || s->id == user.id;
                           }),
            users_.end());
    }

    if (check_host(user)) {
        std::cerr << "[room] host disconnected!" << std::endl;
        auto usr_list = users();
        if (usr_list.empty()) {
            std::cerr << "[room] room users all disconnected, dropping room" << std::endl;
            return true;
        } else {
            // Pick random new host
            static thread_local std::mt19937 rng(std::random_device{}());
            std::uniform_int_distribution<size_t> dist(0, usr_list.size() - 1);
            auto& new_host = usr_list[dist(rng)];
            std::cerr << "[room] selected " << new_host->id << " as host" << std::endl;
            {
                std::unique_lock lock(host_mtx);
                host = std::weak_ptr<User>(new_host);
            }
            send(Message::new_host(new_host->id));
            new_host->try_send(ServerCommand::change_host(true));
        }
    }

    check_all_ready();
    return false;
}

void Room::reset_game_time() {
    uint32_t neg_inf;
    float val = -std::numeric_limits<float>::infinity();
    memcpy(&neg_inf, &val, 4);
    for (auto& u : users()) {
        u->game_time.store(neg_inf);
    }
}

void Room::check_all_ready() {
    std::unique_lock lock(state_mtx);

    if (state.type == InternalRoomStateType::WaitForReady) {
        auto u = users();
        auto m = monitors();
        bool all_ready = true;
        for (auto& usr : u) {
            if (state.started.count(usr->id) == 0) { all_ready = false; break; }
        }
        if (all_ready) {
            for (auto& usr : m) {
                if (state.started.count(usr->id) == 0) { all_ready = false; break; }
            }
        }
        if (all_ready) {
            lock.unlock();
            std::cerr << "[room] game start: " << id.to_string() << std::endl;
            send(Message::start_playing());
            reset_game_time();
            {
                std::unique_lock lock2(state_mtx);
                state = InternalRoomState::playing();
            }
            on_state_change();
        }
    } else if (state.type == InternalRoomStateType::Playing) {
        auto u = users();
        bool all_done = true;
        for (auto& usr : u) {
            if (state.results.count(usr->id) == 0 && state.aborted.count(usr->id) == 0) {
                all_done = false;
                break;
            }
        }
        if (all_done) {
            lock.unlock();
            send(Message::game_end());
            {
                std::unique_lock lock2(state_mtx);
                state = InternalRoomState::select_chart();
            }
            if (is_cycle()) {
                std::cerr << "[room] cycling: " << id.to_string() << std::endl;
                std::shared_ptr<User> old_host_ptr;
                std::shared_ptr<User> new_host_ptr;
                {
                    std::shared_lock hl(host_mtx);
                    old_host_ptr = host.lock();
                }
                auto usr_list = users();
                if (!usr_list.empty()) {
                    size_t index = 0;
                    if (old_host_ptr) {
                        for (size_t i = 0; i < usr_list.size(); i++) {
                            if (usr_list[i]->id == old_host_ptr->id) {
                                index = (i + 1) % usr_list.size();
                                break;
                            }
                        }
                    }
                    new_host_ptr = usr_list[index];
                    {
                        std::unique_lock hl(host_mtx);
                        host = std::weak_ptr<User>(new_host_ptr);
                    }
                    send(Message::new_host(new_host_ptr->id));
                    if (old_host_ptr) {
                        old_host_ptr->try_send(ServerCommand::change_host(false));
                    }
                    new_host_ptr->try_send(ServerCommand::change_host(true));
                }
            }
            on_state_change();
        }
    }
}
