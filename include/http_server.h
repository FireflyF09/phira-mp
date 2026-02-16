#pragma once
#include "server.h"
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <thread>

class HttpServer {
public:
    using Handler = std::function<void(const std::string& method, const std::string& path, 
                                       const std::string& query, const std::string& body,
                                       std::string& response, std::string& content_type)>;
    
    HttpServer(std::shared_ptr<ServerState> server_state, int port = 12347);
    ~HttpServer();
    
    void start();
    void stop();
    
    // Register a route handler
    void register_route(const std::string& method, const std::string& path, Handler handler);
    
    // Built-in API handlers
    void setup_builtin_handlers();
    
private:
    std::shared_ptr<ServerState> server_state_;
    int port_;
    int listen_fd_ = -1;
    std::thread server_thread_;
    std::atomic<bool> running_{false};
    
    std::unordered_map<std::string, Handler> handlers_; // key: "METHOD_PATH"
    
    void run();
    void handle_client(int client_fd);
    static std::string url_decode(const std::string& str);
    static void parse_request(const std::string& request, std::string& method, 
                              std::string& path, std::string& query, std::string& body);
    static void send_response(int client_fd, int status, const std::string& content, 
                              const std::string& content_type = "application/json");
};