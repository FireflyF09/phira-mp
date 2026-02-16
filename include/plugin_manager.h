#pragma once
#include "commands.h"
#include "session.h"
#include "room.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

// Forward declarations
struct ServerState;
class HttpServer;

// ── Plugin metadata ────────────────────────────────────────────────────────
struct PluginMetadata {
    std::string id;
    std::string name;
    std::string version;
    std::string description;
    std::string author;
    bool enabled;
    std::vector<std::string> dependencies;
};

// ── Lua plugin instance ────────────────────────────────────────────────────
class LuaPlugin {
public:
    LuaPlugin(const std::string& path, std::shared_ptr<ServerState> server_state);
    ~LuaPlugin();

    bool load(); // Load Lua script and call on_enable
    void unload(); // Call on_disable and cleanup

    const PluginMetadata& metadata() const { return metadata_; }
    const std::string& id() const { return metadata_.id; }
    bool is_loaded() const { return loaded_; }

    // Call Lua hook functions
    void on_user_join(std::shared_ptr<User> user, std::shared_ptr<Room> room);
    void on_user_leave(std::shared_ptr<User> user, std::shared_ptr<Room> room);
    bool on_before_command(std::shared_ptr<User> user, const ClientCommand& cmd, ClientCommand* out_cmd);

private:
    std::string path_;
    PluginMetadata metadata_;
    std::shared_ptr<ServerState> server_state_;
    lua_State* L_ = nullptr;
    bool loaded_ = false;

    bool load_metadata(); // Read plugin.json
    void register_functions(); // Register C++ functions in Lua environment
    void call_lua_function(const std::string& name);
};

// ── Plugin manager ─────────────────────────────────────────────────────────
class PluginManager {
public:
    explicit PluginManager(std::shared_ptr<ServerState> server_state);
    ~PluginManager();

    void load_all(const std::string& plugins_dir = "plugins");
    void unload_all();

    // Event forwarding
    void notify_user_join(std::shared_ptr<User> user, std::shared_ptr<Room> room);
    void notify_user_leave(std::shared_ptr<User> user, std::shared_ptr<Room> room);
    bool filter_command(std::shared_ptr<User> user, const ClientCommand& cmd, ClientCommand* out_cmd);

    // HTTP server access
    void start_http_server(int port = 12347);
    HttpServer* get_http_server() { return http_server_.get(); }

private:
    std::shared_ptr<ServerState> server_state_;
    std::unordered_map<std::string, std::unique_ptr<LuaPlugin>> plugins_;
    std::unique_ptr<HttpServer> http_server_;
};