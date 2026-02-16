#include "server.h"
#include "plugin_manager.h"
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
    plugin_manager_ = std::make_shared<PluginManager>(state_);
    state_->plugin_manager = plugin_manager_;
    plugin_manager_->load_all();
}

Server::~Server() {
    state_->running.store(false);
    state_->lost_con_cv.notify_all();
    if (lost_con_thread_.joinable()) lost_con_thread_.join();
    if (listen_fd_ >= 0) close(listen_fd_);
    plugin_manager_->unload_all();
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

// ============================================================================
// Admin authentication helper functions
// ============================================================================

bool ServerState::check_admin_auth(const std::string& token, const std::string& client_ip) {
    // Simplified admin auth - waiting for HSN integration
    // For now, just check if token matches config.admin_token
    std::lock_guard<std::mutex> lock(admin_state_mtx);
    
    if (config.admin_token.empty()) {
        return false;
    }
    
    return token == config.admin_token;
}

std::string ServerState::request_otp(const std::string& client_ip) {
    // OTP system disabled - waiting for HSN integration
    // Return a dummy session ID for compatibility
    std::lock_guard<std::mutex> lock(admin_state_mtx);
    
    static int counter = 0;
    counter++;
    
    std::string session_id = "otp_dummy_" + std::to_string(counter) + "_" + 
                           std::to_string(std::time(nullptr));
    
    // Generate dummy OTP "123456"
    std::string otp = "123456";
    
    uint64_t expires_at = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count() + 5 * 60 * 1000; // 5 minutes
    
    otp_sessions[session_id] = OtpSession{otp, expires_at, client_ip};
    
    return session_id;
}

std::string ServerState::verify_otp(const std::string& session_id, const std::string& otp, const std::string& client_ip) {
    // OTP system disabled - waiting for HSN integration
    // Simplified version that always accepts "123456" and returns dummy token
    std::lock_guard<std::mutex> lock(admin_state_mtx);
    
    // Clean up expired sessions
    cleanup_expired_auth();
    
    // Check if session exists
    auto it = otp_sessions.find(session_id);
    if (it == otp_sessions.end()) {
        return ""; // Empty string indicates failure
    }
    
    const auto& session = it->second;
    
    // Check expiration
    uint64_t now = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    if (now > session.expires_at) {
        otp_sessions.erase(it);
        return "";
    }
    
    // Check IP match (optional, but good for security)
    if (session.ip != client_ip) {
        otp_sessions.erase(it);
        return "";
    }
    
    // Check OTP - accept only "123456" for testing
    if (session.otp != otp || otp != "123456") {
        return "";
    }
    
    // OTP verified successfully
    // Create temporary admin token
    static int token_counter = 0;
    token_counter++;
    std::string temp_token = "temp_dummy_token_" + std::to_string(token_counter) + "_" + 
                           std::to_string(std::time(nullptr));
    
    uint64_t token_expires_at = now + 4 * 60 * 60 * 1000; // 4 hours
    
    temp_admin_tokens[temp_token] = TempAdminToken{client_ip, token_expires_at, false};
    
    // Clean up OTP session
    otp_sessions.erase(it);
    
    return temp_token;
}

void ServerState::cleanup_expired_auth() {
    uint64_t now = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    // Clean expired temporary tokens
    for (auto it = temp_admin_tokens.begin(); it != temp_admin_tokens.end(); ) {
        if (now > it->second.expires_at) {
            it = temp_admin_tokens.erase(it);
        } else {
            ++it;
        }
    }
    
    // Clean expired OTP sessions
    for (auto it = otp_sessions.begin(); it != otp_sessions.end(); ) {
        if (now > it->second.expires_at) {
            it = otp_sessions.erase(it);
        } else {
            ++it;
        }
    }
}

// Replay management functions
// ============================================================================

std::string ServerState::save_replay(const std::string& replay_data, const std::string& player_name, const std::string& song_id) {
    std::lock_guard<std::mutex> lock(replay_mtx);
    
    // Generate unique replay ID
    std::string replay_id = "replay_" + std::to_string(std::rand()) + "_" + 
                          std::to_string(std::time(nullptr));
    
    // Create filename
    std::string filename = replay_id + ".bin";
    std::string filepath = "replays/" + filename;
    
    // Save to file
    std::ofstream file(filepath, std::ios::binary);
    if (!file) {
        std::cerr << "Failed to save replay to " << filepath << std::endl;
        return "";
    }
    
    file.write(replay_data.data(), replay_data.size());
    file.close();
    
    if (!file.good()) {
        std::cerr << "Failed to write replay data to " << filepath << std::endl;
        return "";
    }
    
    // Store replay info
    ReplayInfo info;
    info.id = replay_id;
    info.filename = filename;
    info.player_name = player_name;
    info.song_id = song_id;
    info.created_at = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    info.size = replay_data.size();
    
    replays[replay_id] = info;
    
    std::cout << "Saved replay " << replay_id << " (" << replay_data.size() 
              << " bytes) for player " << player_name << ", song " << song_id << std::endl;
    
    return replay_id;
}

bool ServerState::delete_replay(const std::string& replay_id) {
    std::lock_guard<std::mutex> lock(replay_mtx);
    
    auto it = replays.find(replay_id);
    if (it == replays.end()) {
        return false;
    }
    
    const ReplayInfo& info = it->second;
    std::string filepath = "replays/" + info.filename;
    
    // Delete file
    if (std::remove(filepath.c_str()) != 0) {
        std::cerr << "Failed to delete replay file " << filepath << ": " 
                  << std::strerror(errno) << std::endl;
        // Continue to remove from memory even if file deletion fails
    }
    
    // Remove from memory
    replays.erase(it);
    
    std::cout << "Deleted replay " << replay_id << std::endl;
    return true;
}

std::string ServerState::get_replay_filepath(const std::string& replay_id) {
    std::lock_guard<std::mutex> lock(replay_mtx);
    
    auto it = replays.find(replay_id);
    if (it == replays.end()) {
        return "";
    }
    
    return "replays/" + it->second.filename;
}

std::vector<ServerState::ReplayInfo> ServerState::list_replays() {
    std::lock_guard<std::mutex> lock(replay_mtx);
    
    std::vector<ReplayInfo> result;
    result.reserve(replays.size());
    
    for (const auto& pair : replays) {
        result.push_back(pair.second);
    }
    
    return result;
}
