#pragma once
#include "commands.h"
#include "session.h"
#include "room.h"
#include "plugin_api.h"
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
    LuaPlugin(const std::string& path, std::shared_ptr<ServerState> server_state, PluginServerInterface* server_interface = nullptr);
    ~LuaPlugin();

    bool load(); // Load Lua script and call on_enable
    void unload(); // Call on_disable and cleanup

    const PluginMetadata& metadata() const { return metadata_; }
    const std::string& id() const { return metadata_.id; }
    bool is_loaded() const { return loaded_; }

    // Call Lua hook functions
    void on_user_join(std::shared_ptr<User> user, std::shared_ptr<Room> room);
    void on_user_leave(std::shared_ptr<User> user, std::shared_ptr<Room> room);
    void on_user_kick(std::shared_ptr<User> user, std::shared_ptr<Room> room, const std::string& reason);
    void on_user_ban(std::shared_ptr<User> user, const std::string& reason, int32_t duration_seconds);
    void on_user_unban(int32_t user_id);
    bool on_before_command(std::shared_ptr<User> user, const ClientCommand& cmd, ClientCommand* out_cmd);
    void on_room_create(std::shared_ptr<Room> room);
    void on_room_destroy(std::shared_ptr<Room> room);

private:
    std::string path_;
    PluginMetadata metadata_;
    std::shared_ptr<ServerState> server_state_;
    PluginServerInterface* server_interface_;
    lua_State* L_ = nullptr;
    bool loaded_ = false;

    bool load_metadata(); // Read plugin.json
    void register_functions(); // Register C++ functions in Lua environment
    void call_lua_function(const std::string& name);
};

// ── Plugin manager ─────────────────────────────────────────────────────────
class PluginManager {
public:
    explicit PluginManager(std::shared_ptr<ServerState> server_state, PluginServerInterface* server_interface = nullptr);
    ~PluginManager();

    void set_server_interface(PluginServerInterface* server_interface) { server_interface_ = server_interface; }

    void load_all(const std::string& plugins_dir = "plugins");
    void unload_all();

    // Event forwarding
    void notify_user_join(std::shared_ptr<User> user, std::shared_ptr<Room> room);
    void notify_user_leave(std::shared_ptr<User> user, std::shared_ptr<Room> room);
    void notify_user_kick(std::shared_ptr<User> user, std::shared_ptr<Room> room, const std::string& reason = "");
    void notify_user_ban(std::shared_ptr<User> user, const std::string& reason = "", int32_t duration_seconds = 0);
    void notify_user_unban(int32_t user_id);
    bool filter_command(std::shared_ptr<User> user, const ClientCommand& cmd, ClientCommand* out_cmd);
    void notify_room_create(std::shared_ptr<Room> room);
    void notify_room_destroy(std::shared_ptr<Room> room);

    // HTTP server access
    void start_http_server(int port = 12347);
    HttpServer* get_http_server() { return http_server_.get(); }

private:
    std::shared_ptr<ServerState> server_state_;
    PluginServerInterface* server_interface_;
    std::unordered_map<std::string, std::unique_ptr<LuaPlugin>> plugins_;
    std::unique_ptr<HttpServer> http_server_;
};