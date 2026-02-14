#include "server.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>
#include <cerrno>
#include <algorithm>

#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <uuid/uuid.h>

// ══════════════════════════════════════════════════════════════════════
// ServerConfig
// ══════════════════════════════════════════════════════════════════════

ServerConfig ServerConfig::load(const std::string& path) {
    ServerConfig cfg;
    std::ifstream f(path);
    if (!f.is_open()) {
        std::cerr << "[config] could not open " << path << ", using defaults" << std::endl;
        return cfg;
    }

    std::string line;
    while (std::getline(f, line)) {
        // Trim whitespace
        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        line = line.substr(start);
        if (line.empty() || line[0] == '#') continue;

        // Simple YAML-like: "monitors:" followed by "- value" lines
        // or "monitors: [2, 3]"
        if (line.find("monitors:") == 0) {
            std::string val = line.substr(9);
            // Trim
            size_t vs = val.find_first_not_of(" \t");
            if (vs != std::string::npos) val = val.substr(vs);

            if (!val.empty() && val[0] == '[') {
                // Inline array [2, 3, 4]
                cfg.monitors.clear();
                val = val.substr(1);
                auto end = val.find(']');
                if (end != std::string::npos) val = val.substr(0, end);
                std::istringstream ss(val);
                std::string token;
                while (std::getline(ss, token, ',')) {
                    size_t ts = token.find_first_not_of(" \t");
                    if (ts != std::string::npos) {
                        try { cfg.monitors.push_back(std::stoi(token.substr(ts))); } catch (...) {}
                    }
                }
            } else if (val.empty()) {
                // Multi-line array format
                cfg.monitors.clear();
                while (std::getline(f, line)) {
                    size_t s2 = line.find_first_not_of(" \t");
                    if (s2 == std::string::npos) continue;
                    if (line[s2] == '-') {
                        std::string num = line.substr(s2 + 1);
                        size_t ns = num.find_first_not_of(" \t");
                        if (ns != std::string::npos) {
                            try { cfg.monitors.push_back(std::stoi(num.substr(ns))); } catch (...) {}
                        }
                    } else {
                        break; // next key
                    }
                }
            }
        }
    }

    std::cerr << "[config] monitors: ";
    for (auto m : cfg.monitors) std::cerr << m << " ";
    std::cerr << std::endl;

    return cfg;
}

// ══════════════════════════════════════════════════════════════════════
// ServerState
// ══════════════════════════════════════════════════════════════════════

void ServerState::push_lost_connection(MpUuid id) {
    {
        std::lock_guard<std::mutex> lock(lost_con_mtx);
        lost_con_queue.push(id);
    }
    lost_con_cv.notify_one();
}

// ══════════════════════════════════════════════════════════════════════
// Server
// ══════════════════════════════════════════════════════════════════════

Server::Server(uint16_t port) : port_(port) {
    state_ = std::make_shared<ServerState>();
    state_->config = ServerConfig::load("server_config.yml");

    // Create IPv6 socket (dual-stack: also accepts IPv4)
    listen_fd_ = socket(AF_INET6, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        throw std::runtime_error(std::string("socket: ") + strerror(errno));
    }

    int opt = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Allow dual-stack (IPv4+IPv6)
    int v6only = 0;
    setsockopt(listen_fd_, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, sizeof(v6only));

    struct sockaddr_in6 addr{};
    addr.sin6_family = AF_INET6;
    addr.sin6_port = htons(port);
    addr.sin6_addr = in6addr_any;

    if (bind(listen_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(listen_fd_);
        throw std::runtime_error(std::string("bind: ") + strerror(errno));
    }

    if (listen(listen_fd_, 64) < 0) {
        close(listen_fd_);
        throw std::runtime_error(std::string("listen: ") + strerror(errno));
    }

    std::cerr << "[server] listening on [::]:" << port << std::endl;
}

Server::~Server() {
    state_->running.store(false);
    state_->lost_con_cv.notify_all();
    if (lost_con_thread_.joinable()) lost_con_thread_.join();
    if (listen_fd_ >= 0) close(listen_fd_);
}

void Server::run() {
    // Start lost connection handler thread
    lost_con_thread_ = std::thread(&Server::lost_connection_loop, this);

    // Main accept loop
    while (state_->running.load()) {
        accept_one();
    }
}

void Server::lost_connection_loop() {
    while (state_->running.load()) {
        MpUuid id;
        {
            std::unique_lock<std::mutex> lock(state_->lost_con_mtx);
            state_->lost_con_cv.wait(lock, [this] {
                return !state_->lost_con_queue.empty() || !state_->running.load();
            });
            if (!state_->running.load() && state_->lost_con_queue.empty()) break;
            if (state_->lost_con_queue.empty()) continue;
            id = state_->lost_con_queue.front();
            state_->lost_con_queue.pop();
        }

        std::cerr << "[server] lost connection with " << id.str() << std::endl;

        std::shared_ptr<Session> session;
        {
            std::unique_lock<std::shared_mutex> lock(state_->sessions_mtx);
            auto it = state_->sessions.find(id);
            if (it != state_->sessions.end()) {
                session = it->second;
                state_->sessions.erase(it);
            }
        }

        if (session) {
            session->stop();
            if (session->user) {
                // Check if this session is still the user's current session
                bool is_current;
                {
                    std::shared_lock lock(session->user->session_mtx);
                    auto current = session->user->session.lock();
                    is_current = (current.get() == session.get());
                }
                if (is_current) {
                    session->user->dangle();
                }
            }
        }
    }
}

void Server::accept_one() {
    struct sockaddr_in6 client_addr{};
    socklen_t addr_len = sizeof(client_addr);

    int client_fd = accept(listen_fd_, (struct sockaddr*)&client_addr, &addr_len);
    if (client_fd < 0) {
        if (errno == EINTR) return;
        std::cerr << "[server] accept failed: " << strerror(errno) << std::endl;
        return;
    }

    // Set TCP_NODELAY
    int flag = 1;
    setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

    // Get client address string
    char addr_str[INET6_ADDRSTRLEN];
    inet_ntop(AF_INET6, &client_addr.sin6_addr, addr_str, sizeof(addr_str));
    int client_port = ntohs(client_addr.sin6_port);

    // Generate session UUID
    MpUuid session_id = MpUuid::generate();

    // Read version byte from client
    uint8_t version = 0;
    ssize_t r = recv(client_fd, &version, 1, 0);
    if (r != 1) {
        std::cerr << "[server] failed to read version byte from " << addr_str << std::endl;
        close(client_fd);
        return;
    }

    std::cerr << "[server] connection from " << addr_str << ":" << client_port
              << " (" << session_id.str() << "), version: " << (int)version << std::endl;

    // Create session
    auto session = std::make_shared<Session>(session_id, client_fd, version, state_);

    // Store in sessions map
    {
        std::unique_lock<std::shared_mutex> lock(state_->sessions_mtx);
        state_->sessions[session_id] = session;
    }

    // Start session threads
    auto state_copy = state_;
    session->send_thread = std::thread([session]() {
        session->send_loop();
    });
    session->send_thread.detach();

    session->recv_thread = std::thread([session, state_copy]() {
        session->recv_loop(state_copy);
    });
    session->recv_thread.detach();

    session->heartbeat_thread = std::thread([session, state_copy]() {
        session->heartbeat_loop(state_copy);
    });
    session->heartbeat_thread.detach();
}
