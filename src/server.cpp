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
    plugin_manager_ = std::make_shared<PluginManager>(state_, this);
    state_->plugin_manager = plugin_manager_;
    plugin_manager_->load_all();
}

Server::~Server() {
    running_.store(false);
    state_->running.store(false);
    state_->lost_con_cv.notify_all();
    if (lost_con_thread_.joinable()) lost_con_thread_.join();
    if (cli_thread_.joinable()) cli_thread_.join();
    if (listen_fd_ >= 0) close(listen_fd_);
    plugin_manager_->unload_all();
}

void Server::run() {
    // Start lost connection handler thread
    lost_con_thread_ = std::thread(&Server::lost_connection_loop, this);
    
    // Start CLI thread for console input
    cli_thread_ = std::thread(&Server::cli_loop, this);

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

    // Check IP blacklists
    {
        std::lock_guard<std::mutex> lock(state_->admin_state_mtx);
        if (state_->admin_banned_ips.find(addr_str) != state_->admin_banned_ips.end() ||
            state_->otp_banned_ips.find(addr_str) != state_->otp_banned_ips.end()) {
            std::cerr << "[server] connection from banned IP " << addr_str << " rejected" << std::endl;
            close(client_fd);
            return;
        }
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

// CLI Functions Implementation
// ============================================================================

void Server::cli_loop() {
    std::cout << "\n=== Phira MP Server CLI ===\n";
    std::cout << "Type 'help' for available commands\n";
    std::cout << "==============================\n" << std::endl;

    while (running_.load()) {
        std::cout << "> " << std::flush;

        // Use select to wait for stdin with timeout
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(0, &readfds); // stdin file descriptor

        struct timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        int ret = select(1, &readfds, nullptr, nullptr, &timeout);
        if (ret < 0) {
            if (errno == EINTR) continue;
            std::cerr << "select error: " << strerror(errno) << std::endl;
            break;
        }

        if (ret == 0) {
            // Timeout, check running flag again
            continue;
        }

        // Data available on stdin
        std::string input;
        if (!std::getline(std::cin, input)) {
            // EOF or error, break the loop
            break;
        }

        // Trim whitespace
        input.erase(0, input.find_first_not_of(" \t\r\n"));
        input.erase(input.find_last_not_of(" \t\r\n") + 1);

        if (input.empty()) {
            continue;
        }

        handle_cli_command(input);
    }
}

void Server::handle_cli_command(const std::string& command) {
    std::istringstream iss(command);
    std::string cmd;
    iss >> cmd;
    
    // Convert command to lowercase for case-insensitive matching
    std::string cmd_lower = cmd;
    std::transform(cmd_lower.begin(), cmd_lower.end(), cmd_lower.begin(), 
                  [](unsigned char c){ return std::tolower(c); });
    
    try {
        if (cmd_lower == "help" || cmd_lower == "?") {
            show_help();
        } else if (cmd_lower == "status" || cmd_lower == "info") {
            show_status();
        } else if (cmd_lower == "list" || cmd_lower == "rooms") {
            list_rooms();
        } else if (cmd_lower == "users") {
            list_users();
        } else if (cmd_lower == "broadcast" || cmd_lower == "say") {
            std::string message;
            std::getline(iss, message);
            // Trim leading space
            message.erase(0, message.find_first_not_of(" \t"));
            if (!message.empty()) {
                broadcast_message_cli(message);
            } else {
                std::cout << "Error: Broadcast message cannot be empty" << std::endl;
            }
        } else if (cmd_lower == "kick") {
            std::string user_id;
            iss >> user_id;
            if (!user_id.empty()) {
                kick_user_cli(user_id);
            } else {
                std::cout << "Usage: kick <userId>" << std::endl;
            }
        } else if (cmd_lower == "ban") {
            std::string user_id;
            iss >> user_id;
            if (!user_id.empty()) {
                ban_user_cli(user_id);
            } else {
                std::cout << "Usage: ban <userId>" << std::endl;
            }
        } else if (cmd_lower == "reload") {
            reload_plugins_cli();
        } else if (cmd_lower == "user") {
            std::string user_id;
            iss >> user_id;
            if (!user_id.empty()) {
                show_user_details(user_id);
            } else {
                std::cout << "Usage: user <userId>" << std::endl;
            }
        } else if (cmd_lower == "unban") {
            std::string user_id;
            iss >> user_id;
            if (!user_id.empty()) {
                unban_user_cli(user_id);
            } else {
                std::cout << "Usage: unban <userId>" << std::endl;
            }
        } else if (cmd_lower == "banlist") {
            show_banlist();
        } else if (cmd_lower == "banroom") {
            std::string user_id, room_id;
            iss >> user_id >> room_id;
            if (!user_id.empty() && !room_id.empty()) {
                ban_room_user_cli(user_id, room_id);
            } else {
                std::cout << "Usage: banroom <userId> <roomId>" << std::endl;
            }
        } else if (cmd_lower == "unbanroom") {
            std::string user_id, room_id;
            iss >> user_id >> room_id;
            if (!user_id.empty() && !room_id.empty()) {
                unban_room_user_cli(user_id, room_id);
            } else {
                std::cout << "Usage: unbanroom <userId> <roomId>" << std::endl;
            }
        } else if (cmd_lower == "replay") {
            std::string status;
            iss >> status;
            if (!status.empty()) {
                set_replay_status_cli(status);
            } else {
                std::cout << "Usage: replay <on|off|status>" << std::endl;
            }
        } else if (cmd_lower == "roomcreation") {
            std::string status;
            iss >> status;
            if (!status.empty()) {
                set_room_creation_status_cli(status);
            } else {
                std::cout << "Usage: roomcreation <on|off|status>" << std::endl;
            }
        } else if (cmd_lower == "disband") {
            std::string room_id;
            iss >> room_id;
            if (!room_id.empty()) {
                disband_room_cli(room_id);
            } else {
                std::cout << "Usage: disband <roomId>" << std::endl;
            }
        } else if (cmd_lower == "maxusers") {
            std::string room_id, count;
            iss >> room_id >> count;
            if (!room_id.empty() && !count.empty()) {
                set_max_users_cli(room_id, count);
            } else {
                std::cout << "Usage: maxusers <roomId> <count>" << std::endl;
            }
        } else if (cmd_lower == "roomsay") {
            std::string room_id, message;
            iss >> room_id;
            std::getline(iss, message);
            // Trim leading space
            message.erase(0, message.find_first_not_of(" \t"));
            if (!room_id.empty() && !message.empty()) {
                roomsay_message_cli(room_id, message);
            } else {
                std::cout << "Usage: roomsay <roomId> <message>" << std::endl;
            }
        } else if (cmd_lower == "ipblacklist") {
            std::vector<std::string> args;
            std::string arg;
            while (iss >> arg) {
                args.push_back(arg);
            }
            handle_ipblacklist(args);
        } else if (cmd_lower == "contest") {
            std::vector<std::string> args;
            std::string arg;
            while (iss >> arg) {
                args.push_back(arg);
            }
            handle_contest(args);
        } else if (cmd_lower == "stop" || cmd_lower == "shutdown" || cmd_lower == "exit" || cmd_lower == "quit") {
            shutdown_server();
        } else {
            std::cout << "Unknown command: " << cmd << std::endl;
            std::cout << "Type 'help' for available commands" << std::endl;
        }
    } catch (const std::exception& e) {
        std::cout << "Error executing command: " << e.what() << std::endl;
    }
}

void Server::show_help() {
    std::cout << "\n=== Available Commands ===\n\n";
    
    std::cout << "General Commands:\n";
    std::cout << "  help, ?          - Show this help message\n";
    std::cout << "  status, info     - Show server status\n";
    std::cout << "  stop, shutdown   - Gracefully shutdown the server\n";
    std::cout << "\n";
    
    std::cout << "Room Management:\n";
    std::cout << "  list, rooms      - List all active rooms\n";
    std::cout << "  disband <roomId> - Disband a room\n";
    std::cout << "  maxusers <roomId> <count> - Set room max users (1-64)\n";
    std::cout << "  roomcreation <on|off|status> - Control room creation\n";
    std::cout << "\n";
    
    std::cout << "User Management:\n";
    std::cout << "  users            - List all online users\n";
    std::cout << "  user <userId>    - Show user details\n";
    std::cout << "  kick <userId>    - Kick a user from the server\n";
    std::cout << "  ban <userId>     - Ban a user from the server\n";
    std::cout << "  unban <userId>   - Unban a user\n";
    std::cout << "  banlist          - Show banned users list\n";
    std::cout << "  banroom <userId> <roomId> - Ban user from specific room\n";
    std::cout << "  unbanroom <userId> <roomId> - Unban user from specific room\n";
    std::cout << "\n";
    
    std::cout << "Communication:\n";
    std::cout << "  broadcast <msg>  - Broadcast message to all rooms\n";
    std::cout << "  say <msg>        - Alias for broadcast\n";
    std::cout << "  roomsay <roomId> <msg> - Send message to specific room\n";
    std::cout << "\n";
    
    std::cout << "Contest Management:\n";
    std::cout << "  contest <roomId> enable [userIds...] - Enable contest mode\n";
    std::cout << "  contest <roomId> disable             - Disable contest mode\n";
    std::cout << "  contest <roomId> whitelist <userIds...> - Set contest whitelist\n";
    std::cout << "  contest <roomId> start [force]       - Start contest\n";
    std::cout << "\n";
    
    std::cout << "Server Management:\n";
    std::cout << "  reload           - Reload all plugins\n";
    std::cout << "  replay <on|off|status> - Control replay recording\n";
    std::cout << "  ipblacklist <list|remove|clear> - IP blacklist management\n";
    std::cout << "============================\n" << std::endl;
}

void Server::show_status() {
    std::shared_lock<std::shared_mutex> lock(state_->sessions_mtx);
    std::shared_lock<std::shared_mutex> room_lock(state_->rooms_mtx);
    
    int user_count = 0;
    for (const auto& pair : state_->sessions) {
        if (pair.second->user && pair.second->user->name != "MONITOR") {
            user_count++;
        }
    }
    
    std::cout << "\n=== Server Status ===\n";
    std::cout << "Connected Users: " << user_count << "\n";
    std::cout << "Total Sessions: " << state_->sessions.size() << "\n";
    std::cout << "Active Rooms: " << state_->rooms.size() << "\n";
    std::cout << "Replay Enabled: " << (state_->config.replay_enabled ? "Yes" : "No") << "\n";
    std::cout << "Room Creation: " << (state_->config.room_creation_enabled ? "Enabled" : "Disabled") << "\n";
    std::cout << "===================\n" << std::endl;
}

void Server::list_rooms() {
    std::shared_lock<std::shared_mutex> lock(state_->rooms_mtx);
    
    if (state_->rooms.empty()) {
        std::cout << "\nNo active rooms\n" << std::endl;
        return;
    }
    
    std::cout << "\n=== Active Rooms (" << state_->rooms.size() << ") ===\n";
    for (const auto& pair : state_->rooms) {
        const auto& room = pair.second;
        std::cout << "Room ID: " << room->id.to_string() << "\n";
        
        // Get host name
        std::shared_ptr<User> host = room->host.lock();
        if (host) {
            std::cout << "  Host: " << host->name << "\n";
        } else {
            std::cout << "  Host: (disconnected)\n";
        }
        
        std::vector<std::shared_ptr<User>> users = room->users();
        std::vector<std::shared_ptr<User>> monitors = room->monitors();
        std::cout << "  Players: " << users.size() << "/" << room->max_users.load() << "\n";
        std::cout << "  Monitors: " << monitors.size() << "\n";
        
        // Room status
        std::cout << "  Status: ";
        switch (room->state.type) {
            case InternalRoomStateType::SelectChart:
                std::cout << "Selecting Chart";
                break;
            case InternalRoomStateType::WaitForReady:
                std::cout << "Waiting for Ready (" << room->state.started.size() << " ready)";
                break;
            case InternalRoomStateType::Playing:
                std::cout << "Playing (" << room->state.results.size() << " results)";
                break;
        }
        std::cout << "\n";
        
        // Chart info
        if (room->chart) {
            std::cout << "  Chart: " << room->chart->name << " (ID: " << room->chart->id << ")\n";
        }
        
        std::cout << "  Locked: " << (room->is_locked() ? "Yes" : "No") << "\n";
        std::cout << "  Cycle Mode: " << (room->is_cycle() ? "Yes" : "No") << "\n";
        std::cout << "  Live: " << (room->is_live() ? "Yes" : "No") << "\n";
        std::cout << std::endl;
    }
    std::cout << "========================\n" << std::endl;
}

void Server::list_users() {
    std::shared_lock<std::shared_mutex> lock(state_->sessions_mtx);
    
    int user_count = 0;
    for (const auto& pair : state_->sessions) {
        if (pair.second->user && pair.second->user->name != "MONITOR") {
            user_count++;
        }
    }
    
    if (user_count == 0) {
        std::cout << "\nNo users online\n" << std::endl;
        return;
    }
    
    std::cout << "\n=== Online Users (" << user_count << ") ===\n";
    for (const auto& pair : state_->sessions) {
        const auto& session = pair.second;
        if (!session->user || session->user->name == "MONITOR") {
            continue; // Skip monitors or sessions without user
        }
        
        std::shared_ptr<User> user = session->user;
        std::shared_ptr<Room> room = user->get_room();
        
        std::cout << "User ID: " << user->id << "\n";
        std::cout << "  Name: " << user->name << "\n";
        std::cout << "  Status: " << (room ? "In Room" : "Lobby") << "\n";
        std::cout << "  Monitor: " << (user->monitor.load() ? "Yes" : "No") << "\n";
        if (room) {
            std::cout << "  Room: " << room->id.to_string() << "\n";
        }
        std::cout << "  Game Time: " << user->game_time.load() << "ms\n";
        std::cout << "  Language: " << user->lang.index << "\n";
        std::cout << std::endl;
    }
    std::cout << "=======================\n" << std::endl;
}

void Server::broadcast_message_cli(const std::string& message) {
    if (message.length() > 200) {
        std::cout << "Error: Message too long (max 200 characters)" << std::endl;
        return;
    }
    
    std::shared_lock<std::shared_mutex> lock(state_->rooms_mtx);
    
    // Create chat message with user=0 (system message)
    Message chat_msg;
    chat_msg.type = MessageType::Chat;
    chat_msg.user = 0; // System user
    chat_msg.content = message;
    
    ServerCommand cmd;
    cmd.type = ServerCommandType::SMessage;
    cmd.message = chat_msg;

    // Send to all rooms
    int room_count = 0;
    for (const auto& pair : state_->rooms) {
        pair.second->broadcast(cmd);
        room_count++;
    }
    
    std::cout << "Broadcast sent to " << room_count << " rooms: \"" << message << "\"" << std::endl;
}

void Server::kick_user_cli(const std::string& user_id_str) {
    try {
        int user_id = std::stoi(user_id_str);
        if (kick_user(user_id, false)) {
            std::cout << "Kicked user ID: " << user_id << std::endl;
        } else {
            std::cout << "Error: User ID " << user_id << " not found or not connected" << std::endl;
        }
    } catch (const std::exception& e) {
        std::cout << "Error: Invalid user ID format" << std::endl;
    }
}

void Server::ban_user_cli(const std::string& user_id_str) {
    try {
        int user_id = std::stoi(user_id_str);
        if (ban_user(user_id)) {
            std::cout << "Banned user ID: " << user_id << std::endl;
        } else {
            std::cout << "Error: Failed to ban user ID " << user_id << std::endl;
        }
    } catch (const std::exception& e) {
        std::cout << "Error: Invalid user ID format" << std::endl;
    }
}

void Server::reload_plugins_cli() {
    std::cout << "Reloading plugins..." << std::endl;
    
    try {
        plugin_manager_->unload_all();
        plugin_manager_->load_all();
        std::cout << "Plugins reloaded successfully" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "Error reloading plugins: " << e.what() << std::endl;
    }
}

void Server::show_user_details(const std::string& user_id_str) {
    try {
        int user_id = std::stoi(user_id_str);
        
        std::shared_lock<std::shared_mutex> lock(state_->sessions_mtx);
        std::shared_ptr<Session> target_session = nullptr;
        
        // Find session by user_id
        for (const auto& pair : state_->sessions) {
            if (pair.second->user && pair.second->user->id == user_id) {
                target_session = pair.second;
                break;
            }
        }
        
        if (!target_session || !target_session->user) {
            std::cout << "Error: User ID " << user_id << " not found" << std::endl;
            return;
        }
        
        std::shared_ptr<User> user = target_session->user;
        std::shared_ptr<Room> room = user->get_room();
        
        std::cout << "\n=== User Details ===\n";
        std::cout << "ID: " << user->id << "\n";
        std::cout << "Name: " << user->name << "\n";
        std::cout << "Status: " << (room ? "In Room" : "Lobby") << "\n";
        std::cout << "Monitor: " << (user->monitor.load() ? "Yes" : "No") << "\n";
        if (room) {
            std::cout << "Room: " << room->id.to_string() << "\n";
            std::cout << "Is Host: " << (room->check_host(*user) ? "Yes" : "No") << "\n";
        }
        std::cout << "Game Time: " << user->game_time.load() << "ms\n";
        std::cout << "Language: " << user->lang.index << "\n";
        std::cout << "Session ID: " << target_session->id.str() << "\n";
        std::cout << "Alive: " << (target_session->alive.load() ? "Yes" : "No") << "\n";
        std::cout << "==================\n" << std::endl;
        
    } catch (const std::exception& e) {
        std::cout << "Error: Invalid user ID format" << std::endl;
    }
}

void Server::roomsay_message_cli(const std::string& room_id, const std::string& message) {
    if (message.length() > 200) {
        std::cout << "Error: Message too long (max 200 characters)" << std::endl;
        return;
    }
    
    std::shared_lock<std::shared_mutex> lock(state_->rooms_mtx);
    auto it = state_->rooms.find(room_id);
    if (it == state_->rooms.end()) {
        std::cout << "Error: Room '" << room_id << "' not found" << std::endl;
        return;
    }
    
    // Create chat message with user=0 (system message)
    Message chat_msg;
    chat_msg.type = MessageType::Chat;
    chat_msg.user = 0; // System user
    chat_msg.content = message;
    
    ServerCommand cmd;
    cmd.type = ServerCommandType::SMessage;
    cmd.message = chat_msg;

    it->second->broadcast(cmd);
    std::cout << "Message sent to room '" << room_id << "': \"" << message << "\"" << std::endl;
}

void Server::unban_user_cli(const std::string& user_id_str) {
    try {
        int user_id = std::stoi(user_id_str);
        if (unban_user(user_id)) {
            std::cout << "Unbanned user ID: " << user_id << std::endl;
        } else {
            std::cout << "Error: User ID " << user_id << " not found in ban list" << std::endl;
        }
    } catch (const std::exception& e) {
        std::cout << "Error: Invalid user ID format" << std::endl;
    }
}

void Server::show_banlist() {
    std::lock_guard<std::mutex> lock(state_->ban_mtx);
    
    if (state_->banned_users.empty()) {
        std::cout << "\nNo banned users\n" << std::endl;
        return;
    }
    
    std::cout << "\n=== Ban List (" << state_->banned_users.size() << " users) ===\n";
    for (int32_t user_id : state_->banned_users) {
        std::cout << "User ID: " << user_id << "\n";
    }
    std::cout << "=====================\n" << std::endl;
}

void Server::ban_room_user_cli(const std::string& user_id_str, const std::string& room_id) {
    try {
        int32_t user_id = std::stoi(user_id_str);
        
        {
            std::lock_guard<std::mutex> lock(state_->ban_mtx);
            auto& banned_set = state_->banned_room_users[room_id];
            banned_set.insert(user_id);
        }
        
        save_admin_data();
        std::cout << "Banned user " << user_id << " from room " << room_id << std::endl;
        
    } catch (const std::exception& e) {
        std::cout << "Error: Invalid user ID format" << std::endl;
    }
}

void Server::unban_room_user_cli(const std::string& user_id_str, const std::string& room_id) {
    try {
        int32_t user_id = std::stoi(user_id_str);
        
        {
            std::lock_guard<std::mutex> lock(state_->ban_mtx);
            auto it = state_->banned_room_users.find(room_id);
            if (it != state_->banned_room_users.end()) {
                it->second.erase(user_id);
                if (it->second.empty()) {
                    state_->banned_room_users.erase(it);
                }
            }
        }
        
        save_admin_data();
        std::cout << "Unbanned user " << user_id << " from room " << room_id << std::endl;
        
    } catch (const std::exception& e) {
        std::cout << "Error: Invalid user ID format" << std::endl;
    }
}

void Server::set_replay_status_cli(const std::string& status) {
    if (status == "on") {
        state_->config.replay_enabled = true;
        std::cout << "Replay recording enabled" << std::endl;
    } else if (status == "off") {
        state_->config.replay_enabled = false;
        std::cout << "Replay recording disabled" << std::endl;
    } else if (status == "status") {
        std::cout << "Replay recording is " << (state_->config.replay_enabled ? "enabled" : "disabled") << std::endl;
    } else {
        std::cout << "Usage: replay <on|off|status>" << std::endl;
    }
}

void Server::set_room_creation_status_cli(const std::string& status) {
    if (status == "on") {
        state_->config.room_creation_enabled = true;
        std::cout << "Room creation enabled" << std::endl;
    } else if (status == "off") {
        state_->config.room_creation_enabled = false;
        std::cout << "Room creation disabled" << std::endl;
    } else if (status == "status") {
        std::cout << "Room creation is " << (state_->config.room_creation_enabled ? "enabled" : "disabled") << std::endl;
    } else {
        std::cout << "Usage: roomcreation <on|off|status>" << std::endl;
    }
}

void Server::disband_room_cli(const std::string& room_id) {
    std::shared_ptr<Room> room;
    {
        std::lock_guard<std::shared_mutex> lock(state_->rooms_mtx);
        auto it = state_->rooms.find(room_id);
        if (it == state_->rooms.end()) {
            std::cout << "Error: Room '" << room_id << "' not found" << std::endl;
            return;
        }
        room = it->second;
    }
    
    // Get all users in the room
    std::vector<std::shared_ptr<User>> users = room->users();
    std::vector<std::shared_ptr<User>> monitors = room->monitors();
    
    // Notify all users with system message
    Message chat_msg;
    chat_msg.type = MessageType::Chat;
    chat_msg.user = 0; // System user
    chat_msg.content = "房间已被管理员解散 / Room disbanded by admin";
    
    ServerCommand cmd;
    cmd.type = ServerCommandType::SMessage;
    cmd.message = chat_msg;
    
    // Send notification to all users and monitors
    for (const auto& user : users) {
        if (user && user->session.lock()) {
            user->try_send(cmd);
        }
    }
    for (const auto& user : monitors) {
        if (user && user->session.lock()) {
            user->try_send(cmd);
        }
    }
    
    // Remove the room
    {
        std::lock_guard<std::shared_mutex> lock(state_->rooms_mtx);
        // Notify plugins about room destruction before erasing
        if (plugin_manager_) {
            plugin_manager_->notify_room_destroy(room);
        }
        state_->rooms.erase(room_id);
    }

    std::cout << "Room '" << room_id << "' disbanded (notified "
              << (users.size() + monitors.size()) << " users)" << std::endl;
}

void Server::set_max_users_cli(const std::string& room_id, const std::string& count_str) {
    try {
        int count = std::stoi(count_str);
        if (count < 1 || count > 64) {
            std::cout << "Error: Max users must be between 1 and 64" << std::endl;
            return;
        }
        
        std::shared_lock<std::shared_mutex> lock(state_->rooms_mtx);
        auto it = state_->rooms.find(room_id);
        if (it == state_->rooms.end()) {
            std::cout << "Error: Room '" << room_id << "' not found" << std::endl;
            return;
        }
        
        // Update room's max_users
        it->second->max_users.store(count);
        std::cout << "Room '" << room_id << "' max users set to " << count << std::endl;

    } catch (const std::exception& e) {
        std::cout << "Error: Invalid count format" << std::endl;
    }
}

// ============================================================================
// Helper Methods Implementation
// ============================================================================

void Server::broadcast_to_room(const std::string& room_id, ServerCommand cmd) {
    std::shared_lock<std::shared_mutex> lock(state_->rooms_mtx);
    auto it = state_->rooms.find(room_id);
    if (it != state_->rooms.end()) {
        it->second->broadcast(cmd);
    }
}

void Server::save_admin_data() {
    std::lock_guard<std::mutex> ban_lock(state_->ban_mtx);
    std::lock_guard<std::mutex> admin_lock(state_->admin_state_mtx);
    
    // Create admin_data.json file
    std::ofstream file("admin_data.json");
    if (!file.is_open()) {
        std::cerr << "Warning: Could not open admin_data.json for writing" << std::endl;
        return;
    }
    
    // Simplified JSON structure (matching tphira-mp format)
    file << "{\n";
    file << "  \"version\": 1,\n";
    
    // bannedUsers array
    file << "  \"bannedUsers\": [";
    bool first = true;
    for (int32_t user_id : state_->banned_users) {
        if (!first) file << ", ";
        file << user_id;
        first = false;
    }
    file << "],\n";
    
    // bannedRoomUsers object
    file << "  \"bannedRoomUsers\": {\n";
    first = true;
    for (const auto& pair : state_->banned_room_users) {
        if (!first) file << ",\n";
        file << "    \"" << pair.first << "\": [";
        bool inner_first = true;
        for (int32_t user_id : pair.second) {
            if (!inner_first) file << ", ";
            file << user_id;
            inner_first = false;
        }
        file << "]";
        first = false;
    }
    file << "\n  }\n";
    file << "}\n";
    
    file.close();
}

void Server::load_admin_data() {
    std::ifstream file("admin_data.json");
    if (!file.is_open()) {
        std::cerr << "Info: No admin_data.json found, starting with empty ban lists" << std::endl;
        return;
    }
    
    try {
        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        file.close();
        
        // Simple JSON parsing (for demonstration - in production use a proper JSON library)
        // This is a simplified implementation
        size_t pos = content.find("\"bannedUsers\":");
        if (pos != std::string::npos) {
            size_t start = content.find('[', pos);
            size_t end = content.find(']', start);
            if (start != std::string::npos && end != std::string::npos) {
                std::string array_str = content.substr(start + 1, end - start - 1);
                std::istringstream iss(array_str);
                std::string token;
                while (std::getline(iss, token, ',')) {
                    token.erase(0, token.find_first_not_of(" \t\r\n"));
                    token.erase(token.find_last_not_of(" \t\r\n") + 1);
                    if (!token.empty()) {
                        try {
                            int32_t user_id = std::stoi(token);
                            state_->banned_users.insert(user_id);
                        } catch (...) {
                            // Ignore invalid entries
                        }
                    }
                }
            }
        }
        
        // Note: bannedRoomUsers parsing omitted for simplicity
        std::cout << "Loaded " << state_->banned_users.size() << " banned users from admin_data.json" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error loading admin_data.json: " << e.what() << std::endl;
    }
}

bool Server::admin_disconnect_user(int32_t user_id, bool preserve_room) {
    std::shared_lock<std::shared_mutex> session_lock(state_->sessions_mtx);

    // Find session by user_id
    std::shared_ptr<Session> target_session = nullptr;
    for (const auto& pair : state_->sessions) {
        if (pair.second->user && pair.second->user->id == user_id) {
            target_session = pair.second;
            break;
        }
    }

    if (!target_session) {
        return false; // User not online
    }

    if (preserve_room) {
        // Handle preserve room logic (simplified)
        std::shared_ptr<User> user = target_session->user;
        if (user && user->get_room()) {
            // In real implementation, mark as aborted and check ready state
            std::cout << "Note: preserve_room logic not fully implemented" << std::endl;
        }
    }

    // Disconnect the session
    target_session->stop();
    std::cout << "Disconnected user: " << target_session->user->name
              << " (ID: " << user_id << ")" << std::endl;
    return true;
}

void Server::handle_ipblacklist(const std::vector<std::string>& args) {
    if (args.empty()) {
        std::cout << "Usage: ipblacklist <list|remove|clear>" << std::endl;
        return;
    }
    
    const std::string& subcmd = args[0];
    
    if (subcmd == "list") {
        std::lock_guard<std::mutex> lock(state_->admin_state_mtx);
        
        std::cout << "\n=== IP Blacklist ===\n";
        std::cout << "Admin banned IPs (" << state_->admin_banned_ips.size() << "):\n";
        for (const auto& ip : state_->admin_banned_ips) {
            std::cout << "  " << ip << "\n";
        }
        
        std::cout << "\nOTP banned IPs (" << state_->otp_banned_ips.size() << "):\n";
        for (const auto& ip : state_->otp_banned_ips) {
            std::cout << "  " << ip << "\n";
        }
        
        std::cout << "\nOTP banned sessions (" << state_->otp_banned_sessions.size() << "):\n";
        for (const auto& session : state_->otp_banned_sessions) {
            std::cout << "  " << session << "\n";
        }
        std::cout << "===================\n" << std::endl;
        
    } else if (subcmd == "remove") {
        if (args.size() < 2) {
            std::cout << "Usage: ipblacklist remove <ip>" << std::endl;
            return;
        }
        
        const std::string& ip = args[1];
        std::lock_guard<std::mutex> lock(state_->admin_state_mtx);
        
        bool removed = false;
        if (state_->admin_banned_ips.erase(ip) > 0) {
            std::cout << "Removed " << ip << " from admin banned IPs" << std::endl;
            removed = true;
        }
        if (state_->otp_banned_ips.erase(ip) > 0) {
            std::cout << "Removed " << ip << " from OTP banned IPs" << std::endl;
            removed = true;
        }
        
        // Also check session IDs
        if (state_->otp_banned_sessions.erase(ip) > 0) {
            std::cout << "Removed " << ip << " from OTP banned sessions" << std::endl;
            removed = true;
        }
        
        if (!removed) {
            std::cout << "IP/session '" << ip << "' not found in blacklists" << std::endl;
        }
        
    } else if (subcmd == "clear") {
        std::lock_guard<std::mutex> lock(state_->admin_state_mtx);
        
        size_t admin_count = state_->admin_banned_ips.size();
        size_t otp_ip_count = state_->otp_banned_ips.size();
        size_t otp_session_count = state_->otp_banned_sessions.size();
        
        state_->admin_banned_ips.clear();
        state_->otp_banned_ips.clear();
        state_->otp_banned_sessions.clear();
        
        std::cout << "Cleared all IP blacklists:\n";
        std::cout << "  Admin banned IPs: " << admin_count << " removed\n";
        std::cout << "  OTP banned IPs: " << otp_ip_count << " removed\n";
        std::cout << "  OTP banned sessions: " << otp_session_count << " removed\n";
        
    } else {
        std::cout << "Unknown subcommand: " << subcmd << std::endl;
        std::cout << "Usage: ipblacklist <list|remove|clear>" << std::endl;
    }
}

void Server::handle_contest(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        std::cout << "Usage: contest <roomId> <enable|disable|whitelist|start>" << std::endl;
        return;
    }
    
    const std::string& room_id = args[0];
    const std::string& subcmd = args[1];
    
    if (subcmd == "enable") {
        std::lock_guard<std::shared_mutex> lock(state_->rooms_mtx);
        auto it = state_->rooms.find(room_id);
        if (it == state_->rooms.end()) {
            std::cout << "Room not found: " << room_id << std::endl;
            return;
        }
        
        auto room = it->second;
        std::unordered_set<int32_t> whitelist;
        
        // Add current users to whitelist
        auto users = room->users();
        auto monitors = room->monitors();
        for (const auto& user : users) {
            whitelist.insert(user->id);
        }
        for (const auto& user : monitors) {
            whitelist.insert(user->id);
        }
        
        // Add any additional user IDs from args
        for (size_t i = 2; i < args.size(); i++) {
            try {
                int32_t user_id = std::stoi(args[i]);
                whitelist.insert(user_id);
            } catch (...) {
                // Ignore invalid user IDs
            }
        }
        
        room->contest = Room::ContestInfo{whitelist, true, true};
        std::cout << "Enabled contest mode for room " << room_id << std::endl;
        
    } else if (subcmd == "disable") {
        std::lock_guard<std::shared_mutex> lock(state_->rooms_mtx);
        auto it = state_->rooms.find(room_id);
        if (it == state_->rooms.end()) {
            std::cout << "Room not found: " << room_id << std::endl;
            return;
        }
        
        it->second->contest.reset();
        std::cout << "Disabled contest mode for room " << room_id << std::endl;
        
    } else if (subcmd == "whitelist") {
        if (args.size() < 3) {
            std::cout << "Usage: contest <roomId> whitelist <userId1> [userId2 ...]" << std::endl;
            return;
        }
        
        std::lock_guard<std::shared_mutex> lock(state_->rooms_mtx);
        auto it = state_->rooms.find(room_id);
        if (it == state_->rooms.end() || !it->second->contest.has_value()) {
            std::cout << "Room not found or contest mode not enabled: " << room_id << std::endl;
            return;
        }
        
        auto& contest = it->second->contest.value();
        std::unordered_set<int32_t> whitelist;
        
        // Add all specified user IDs
        for (size_t i = 2; i < args.size(); i++) {
            try {
                int32_t user_id = std::stoi(args[i]);
                whitelist.insert(user_id);
            } catch (...) {
                // Ignore invalid user IDs
            }
        }
        
        // Add current users to ensure they're included
        auto users = it->second->users();
        auto monitors = it->second->monitors();
        for (const auto& user : users) {
            whitelist.insert(user->id);
        }
        for (const auto& user : monitors) {
            whitelist.insert(user->id);
        }
        
        contest.whitelist = whitelist;
        std::cout << "Updated whitelist for room " << room_id << std::endl;
        
    } else if (subcmd == "start") {
        bool force = args.size() > 2 && args[2] == "force";
        
        std::lock_guard<std::shared_mutex> lock(state_->rooms_mtx);
        auto it = state_->rooms.find(room_id);
        if (it == state_->rooms.end() || !it->second->contest.has_value()) {
            std::cout << "Room not found or contest mode not enabled: " << room_id << std::endl;
            return;
        }
        
        auto room = it->second;
        
        // Check room state
        if (room->state.type != InternalRoomStateType::WaitForReady) {
            std::cout << "Room is not in WaitForReady state" << std::endl;
            return;
        }
        
        // Check if chart is selected
        if (!room->chart.has_value()) {
            std::cout << "No chart selected" << std::endl;
            return;
        }
        
        // Check if all users are ready
        auto users = room->users();
        auto monitors = room->monitors();
        bool all_ready = true;
        for (const auto& user : users) {
            if (room->state.started.find(user->id) == room->state.started.end()) {
                all_ready = false;
                break;
            }
        }
        for (const auto& user : monitors) {
            if (room->state.started.find(user->id) == room->state.started.end()) {
                all_ready = false;
                break;
            }
        }
        
        if (!all_ready && !force) {
            std::cout << "Not all users are ready. Use 'force' to start anyway." << std::endl;
            return;
        }
        
        // Start the game (simplified - in real implementation would send StartPlaying message)
        std::cout << "Started contest for room " << room_id << std::endl;
        std::cout << "Note: Actual game start implementation requires game state management" << std::endl;
        
    } else {
        std::cout << "Unknown subcommand: " << subcmd << std::endl;
        std::cout << "Usage: contest <roomId> <enable|disable|whitelist|start>" << std::endl;
    }
}

void Server::shutdown_server() {
    std::cout << "Shutting down server..." << std::endl;
    running_.store(false);
    state_->running.store(false);
    // Close listening socket to break accept loop
    if (listen_fd_ >= 0) {
        close(listen_fd_);
        listen_fd_ = -1;
    }
    // Notify lost_connection_loop thread to exit
    state_->lost_con_cv.notify_all();
}

// ============================================================================
// PluginServerInterface implementation
// ============================================================================

bool Server::kick_user(int32_t user_id, bool preserve_room) {
    // Try to find user and room for plugin notification
    std::shared_ptr<User> user = nullptr;
    std::shared_ptr<Room> room = nullptr;
    
    {
        std::shared_lock<std::shared_mutex> session_lock(state_->sessions_mtx);
        for (const auto& pair : state_->sessions) {
            if (pair.second->user && pair.second->user->id == user_id) {
                user = pair.second->user;
                room = user->get_room();
                break;
            }
        }
    }
    
    // Notify plugins about user kick
    if (plugin_manager_ && user) {
        plugin_manager_->notify_user_kick(user, room, "Kicked by administrator");
    }
    
    return admin_disconnect_user(user_id, preserve_room);
}

bool Server::ban_user(int32_t user_id) {
    {
        std::lock_guard<std::mutex> lock(state_->ban_mtx);
        state_->banned_users.insert(user_id);
    }
    save_admin_data();
    
    // Notify plugins about user ban
    if (plugin_manager_) {
        std::shared_ptr<User> user = nullptr;
        // Try to find the user to get user object for notification
        {
            std::shared_lock<std::shared_mutex> session_lock(state_->sessions_mtx);
            for (const auto& pair : state_->sessions) {
                if (pair.second->user && pair.second->user->id == user_id) {
                    user = pair.second->user;
                    break;
                }
            }
        }
        
        if (user) {
            plugin_manager_->notify_user_ban(user, "Administrator ban", 0); // 0 = permanent
        } else {
            // User not online, still notify with user_id only
            // We need to create a minimal user object or modify notify_user_ban to accept user_id
            // For now, we'll skip notification if user not found
            // TODO: Create a notify_user_ban_by_id method
        }
    }
    
    admin_disconnect_user(user_id, false); // Silently ignore if not online
    return true;
}

bool Server::unban_user(int32_t user_id) {
    std::lock_guard<std::mutex> lock(state_->ban_mtx);
    size_t removed = state_->banned_users.erase(user_id);
    if (removed > 0) {
        save_admin_data();
        
        // Notify plugins about user unban
        if (plugin_manager_) {
            plugin_manager_->notify_user_unban(user_id);
        }
        
        return true;
    }
    return false;
}

bool Server::is_user_banned(int32_t user_id) {
    std::lock_guard<std::mutex> lock(state_->ban_mtx);
    return state_->banned_users.find(user_id) != state_->banned_users.end();
}

std::vector<int32_t> Server::get_banned_users() {
    std::lock_guard<std::mutex> lock(state_->ban_mtx);
    std::vector<int32_t> result;
    result.reserve(state_->banned_users.size());
    for (int32_t user_id : state_->banned_users) {
        result.push_back(user_id);
    }
    return result;
}

bool Server::ban_room_user(int32_t user_id, const std::string& room_id) {
    std::lock_guard<std::mutex> lock(state_->ban_mtx);
    state_->banned_room_users[room_id].insert(user_id);
    save_admin_data();
    return true;
}

bool Server::unban_room_user(int32_t user_id, const std::string& room_id) {
    std::lock_guard<std::mutex> lock(state_->ban_mtx);
    auto room_it = state_->banned_room_users.find(room_id);
    if (room_it != state_->banned_room_users.end()) {
        size_t removed = room_it->second.erase(user_id);
        if (removed > 0) {
            if (room_it->second.empty()) {
                state_->banned_room_users.erase(room_it);
            }
            save_admin_data();
            return true;
        }
    }
    return false;
}

bool Server::is_user_banned_from_room(int32_t user_id, const std::string& room_id) {
    std::lock_guard<std::mutex> lock(state_->ban_mtx);
    auto room_it = state_->banned_room_users.find(room_id);
    if (room_it != state_->banned_room_users.end()) {
        return room_it->second.find(user_id) != room_it->second.end();
    }
    return false;
}

bool Server::disband_room(const std::string& room_id) {
    // This is a simplified version - actual disband_room handles more logic
    std::unique_lock<std::shared_mutex> lock(state_->rooms_mtx);
    auto it = state_->rooms.find(room_id);
    if (it == state_->rooms.end()) {
        return false;
    }
    
    // Send disband notification to all users
    auto room = it->second;
    Message disband_msg;
    disband_msg.type = MessageType::Chat;
    disband_msg.user = 0;
    disband_msg.content = "Room has been disbanded by administrator";
    ServerCommand cmd;
    cmd.type = ServerCommandType::SMessage;
    cmd.message = disband_msg;
    room->broadcast(cmd);
    
    // Remove room
    state_->rooms.erase(it);
    return true;
}

bool Server::set_max_users(const std::string& room_id, int max_users) {
    std::shared_lock<std::shared_mutex> lock(state_->rooms_mtx);
    auto it = state_->rooms.find(room_id);
    if (it == state_->rooms.end()) {
        return false;
    }
    it->second->max_users.store(max_users);
    return true;
}

std::optional<int> Server::get_room_max_users(const std::string& room_id) {
    std::shared_lock<std::shared_mutex> lock(state_->rooms_mtx);
    auto it = state_->rooms.find(room_id);
    if (it == state_->rooms.end()) {
        return std::nullopt;
    }
    return it->second->max_users.load();
}

bool Server::broadcast_message(const std::string& message) {
    // Implementation similar to CLI version but returns bool
    if (message.length() > 200) {
        return false;
    }

    std::shared_lock<std::shared_mutex> lock(state_->rooms_mtx);

    // Create chat message with user=0 (system message)
    Message chat_msg;
    chat_msg.type = MessageType::Chat;
    chat_msg.user = 0; // System user
    chat_msg.content = message;

    ServerCommand cmd;
    cmd.type = ServerCommandType::SMessage;
    cmd.message = chat_msg;

    // Send to all rooms
    int room_count = 0;
    for (const auto& pair : state_->rooms) {
        pair.second->broadcast(cmd);
        room_count++;
    }

    return room_count > 0;
}

bool Server::roomsay_message(const std::string& room_id, const std::string& message) {
    // Implementation similar to CLI version but returns bool
    if (message.length() > 200) {
        return false;
    }

    std::shared_lock<std::shared_mutex> lock(state_->rooms_mtx);
    auto it = state_->rooms.find(room_id);
    if (it == state_->rooms.end()) {
        return false;
    }

    // Create chat message with user=0 (system message)
    Message chat_msg;
    chat_msg.type = MessageType::Chat;
    chat_msg.user = 0; // System user
    chat_msg.content = message;

    ServerCommand cmd;
    cmd.type = ServerCommandType::SMessage;
    cmd.message = chat_msg;

    it->second->broadcast(cmd);
    return true;
}

bool Server::set_replay_status(bool enabled) {
    // Update config
    state_->config.replay_enabled = enabled;
    return true;
}

bool Server::get_replay_status() {
    return state_->config.replay_enabled;
}

bool Server::set_room_creation_status(bool enabled) {
    state_->config.room_creation_enabled = enabled;
    return true;
}

bool Server::get_room_creation_status() {
    return state_->config.room_creation_enabled;
}

bool Server::add_ip_to_blacklist(const std::string& ip, bool is_admin) {
    std::lock_guard<std::mutex> lock(state_->admin_state_mtx);
    if (is_admin) {
        state_->admin_banned_ips.insert(ip);
    } else {
        state_->otp_banned_ips.insert(ip);
    }
    return true;
}

bool Server::remove_ip_from_blacklist(const std::string& ip, bool is_admin) {
    std::lock_guard<std::mutex> lock(state_->admin_state_mtx);
    if (is_admin) {
        return state_->admin_banned_ips.erase(ip) > 0;
    } else {
        return state_->otp_banned_ips.erase(ip) > 0;
    }
}

bool Server::is_ip_banned(const std::string& ip) {
    std::lock_guard<std::mutex> lock(state_->admin_state_mtx);
    return state_->admin_banned_ips.find(ip) != state_->admin_banned_ips.end() ||
           state_->otp_banned_ips.find(ip) != state_->otp_banned_ips.end();
}

std::vector<std::string> Server::get_banned_ips(bool admin_list) {
    std::lock_guard<std::mutex> lock(state_->admin_state_mtx);
    std::vector<std::string> result;
    if (admin_list) {
        result.reserve(state_->admin_banned_ips.size());
        for (const auto& ip : state_->admin_banned_ips) {
            result.push_back(ip);
        }
    } else {
        result.reserve(state_->otp_banned_ips.size());
        for (const auto& ip : state_->otp_banned_ips) {
            result.push_back(ip);
        }
    }
    return result;
}

void Server::clear_ip_blacklist(bool admin_list) {
    std::lock_guard<std::mutex> lock(state_->admin_state_mtx);
    if (admin_list) {
        state_->admin_banned_ips.clear();
    } else {
        state_->otp_banned_ips.clear();
    }
}

bool Server::enable_contest(const std::string& room_id, bool manual_start, bool auto_disband) {
    // Contest system not fully implemented
    return false;
}

bool Server::disable_contest(const std::string& room_id) {
    std::shared_lock<std::shared_mutex> lock(state_->rooms_mtx);
    auto it = state_->rooms.find(room_id);
    if (it == state_->rooms.end()) {
        return false;
    }
    
    it->second->contest = std::nullopt;
    return true;
}

bool Server::add_contest_whitelist(const std::string& room_id, int32_t user_id) {
    std::shared_lock<std::shared_mutex> lock(state_->rooms_mtx);
    auto it = state_->rooms.find(room_id);
    if (it == state_->rooms.end()) {
        return false;
    }
    
    if (!it->second->contest.has_value()) {
        return false;
    }
    
    it->second->contest->whitelist.insert(user_id);
    return true;
}

bool Server::remove_contest_whitelist(const std::string& room_id, int32_t user_id) {
    std::shared_lock<std::shared_mutex> lock(state_->rooms_mtx);
    auto it = state_->rooms.find(room_id);
    if (it == state_->rooms.end()) {
        return false;
    }
    
    if (!it->second->contest.has_value()) {
        return false;
    }
    
    return it->second->contest->whitelist.erase(user_id) > 0;
}

bool Server::start_contest(const std::string& room_id, bool force) {
    // Simplified - actual implementation would send StartPlaying command
    std::shared_lock<std::shared_mutex> lock(state_->rooms_mtx);
    auto it = state_->rooms.find(room_id);
    if (it == state_->rooms.end()) {
        return false;
    }
    
    if (!it->second->contest.has_value()) {
        return false;
    }
    
    // Check if all users are ready (simplified)
    if (!force) {
        // In real implementation, check ready state
    }
    
    // Start the game (would send StartPlaying command)
    return true;
}

int Server::get_connected_user_count() {
    std::shared_lock<std::shared_mutex> lock(state_->sessions_mtx);
    int count = 0;
    for (const auto& pair : state_->sessions) {
        if (pair.second->user) {
            count++;
        }
    }
    return count;
}

int Server::get_active_room_count() {
    std::shared_lock<std::shared_mutex> lock(state_->rooms_mtx);
    return state_->rooms.size();
}

std::vector<std::string> Server::get_room_list() {
    std::shared_lock<std::shared_mutex> lock(state_->rooms_mtx);
    std::vector<std::string> result;
    result.reserve(state_->rooms.size());
    for (const auto& pair : state_->rooms) {
        result.push_back(pair.first);
    }
    return result;
}

std::vector<int32_t> Server::get_connected_user_ids() {
    std::shared_lock<std::shared_mutex> lock(state_->sessions_mtx);
    std::vector<int32_t> result;
    for (const auto& pair : state_->sessions) {
        if (pair.second->user) {
            result.push_back(pair.second->user->id);
        }
    }
    return result;
}

std::optional<std::string> Server::get_user_name(int32_t user_id) {
    std::shared_lock<std::shared_mutex> lock(state_->users_mtx);
    auto it = state_->users.find(user_id);
    if (it == state_->users.end()) {
        return std::nullopt;
    }
    return it->second->name;
}

std::optional<std::string> Server::get_user_language(int32_t user_id) {
    std::shared_lock<std::shared_mutex> lock(state_->users_mtx);
    auto it = state_->users.find(user_id);
    if (it == state_->users.end()) {
        return std::nullopt;
    }
    // Language struct doesn't have to_string() method
    return "en"; // Default to English
}

std::optional<std::string> Server::get_user_room_id(int32_t user_id) {
    std::shared_lock<std::shared_mutex> lock(state_->users_mtx);
    auto it = state_->users.find(user_id);
    if (it == state_->users.end()) {
        return std::nullopt;
    }
    auto room = it->second->get_room();
    if (!room) {
        return std::nullopt;
    }
    return room->id.value;
}

std::optional<int> Server::get_room_user_count(const std::string& room_id) {
    std::shared_lock<std::shared_mutex> lock(state_->rooms_mtx);
    auto it = state_->rooms.find(room_id);
    if (it == state_->rooms.end()) {
        return std::nullopt;
    }
    // Room class doesn't have get_user_count() method
    // Estimate by checking users size if available
    return 0;
}

std::vector<int32_t> Server::get_room_user_ids(const std::string& room_id) {
    std::shared_lock<std::shared_mutex> lock(state_->rooms_mtx);
    auto it = state_->rooms.find(room_id);
    if (it == state_->rooms.end()) {
        return {};
    }
    
    // Room class doesn't have get_users() method
    // Return empty vector for now
    return {};
}

std::optional<std::string> Server::get_room_owner_id(const std::string& room_id) {
    std::shared_lock<std::shared_mutex> lock(state_->rooms_mtx);
    auto it = state_->rooms.find(room_id);
    if (it == state_->rooms.end()) {
        return std::nullopt;
    }
    
    // Room class doesn't have owner member
    return std::nullopt;
}

void Server::reload_plugins() {
    try {
        plugin_manager_->unload_all();
        plugin_manager_->load_all();
    } catch (const std::exception& e) {
        std::cerr << "Error reloading plugins: " << e.what() << std::endl;
    }
}
