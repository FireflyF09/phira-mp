#include "plugin_manager.h"
#include "lua_bindings.h"
#include "commands.h"
#include "http_client.h"
#include "http_server.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace fs = std::filesystem;

// ── LuaPlugin ──────────────────────────────────────────────────────────────

LuaPlugin::LuaPlugin(const std::string& path, std::shared_ptr<ServerState> server_state)
    : path_(path), server_state_(std::move(server_state)) {}

LuaPlugin::~LuaPlugin() {
    unload();
}

bool LuaPlugin::load_metadata() {
    fs::path meta_path = fs::path(path_) / "plugin.json";
    if (!fs::exists(meta_path)) {
        std::cerr << "Plugin metadata not found: " << meta_path << std::endl;
        return false;
    }

    std::ifstream f(meta_path);
    std::stringstream buffer;
    buffer << f.rdbuf();
    std::string json_str = buffer.str();

    metadata_.id = SimpleJson::get_string(json_str, "id");
    metadata_.name = SimpleJson::get_string(json_str, "name");
    metadata_.version = SimpleJson::get_string(json_str, "version");
    metadata_.description = SimpleJson::get_string(json_str, "description");
    metadata_.author = SimpleJson::get_string(json_str, "author");
    // enabled defaults to true if missing
    std::string enabled_str = SimpleJson::get_string(json_str, "enabled");
    if (enabled_str.empty()) {
        metadata_.enabled = true;
    } else {
        metadata_.enabled = SimpleJson::get_bool(json_str, "enabled");
    }

    // Dependencies not supported in SimpleJson (array), skip for now
    // TODO: parse array manually

    if (metadata_.id.empty()) {
        std::cerr << "Plugin missing id" << std::endl;
        return false;
    }
    return true;
}

void LuaPlugin::register_functions() {
    register_lua_bindings(L_, server_state_);
}

bool LuaPlugin::load() {
    if (loaded_) return true;

    if (!load_metadata()) return false;
    if (!metadata_.enabled) return false;

    L_ = luaL_newstate();
    if (!L_) {
        std::cerr << "Failed to create Lua state" << std::endl;
        return false;
    }
    luaL_openlibs(L_);

    register_functions();

    // Load main Lua script
    fs::path main_script = fs::path(path_) / "init.lua";
    if (!fs::exists(main_script)) {
        std::cerr << "init.lua not found in plugin " << metadata_.id << std::endl;
        lua_close(L_);
        L_ = nullptr;
        return false;
    }

    if (luaL_dofile(L_, main_script.c_str()) != LUA_OK) {
        std::cerr << "Failed to load Lua script: " << lua_tostring(L_, -1) << std::endl;
        lua_close(L_);
        L_ = nullptr;
        return false;
    }

    // Call on_enable if defined
    lua_getglobal(L_, "on_enable");
    if (lua_isfunction(L_, -1)) {
        if (lua_pcall(L_, 0, 0, 0) != LUA_OK) {
            std::cerr << "Error in on_enable: " << lua_tostring(L_, -1) << std::endl;
            lua_close(L_);
            L_ = nullptr;
            return false;
        }
    } else {
        lua_pop(L_, 1); // remove non‑function value
    }

    loaded_ = true;
    std::cerr << "Loaded plugin " << metadata_.id << std::endl;
    return true;
}

void LuaPlugin::unload() {
    if (!loaded_) return;

    if (L_) {
        // Call on_disable if defined
        lua_getglobal(L_, "on_disable");
        if (lua_isfunction(L_, -1)) {
            if (lua_pcall(L_, 0, 0, 0) != LUA_OK) {
                std::cerr << "Error in on_disable: " << lua_tostring(L_, -1) << std::endl;
            }
        } else {
            lua_pop(L_, 1);
        }

        lua_close(L_);
        L_ = nullptr;
    }

    loaded_ = false;
    std::cerr << "Unloaded plugin " << metadata_.id << std::endl;
}

void LuaPlugin::on_user_join(std::shared_ptr<User> user, std::shared_ptr<Room> room) {
    if (!loaded_ || !L_) return;
    lua_getglobal(L_, "on_user_join");
    if (lua_isfunction(L_, -1)) {
        push_user(L_, user);
        push_room(L_, room);
        if (lua_pcall(L_, 2, 0, 0) != LUA_OK) {
            std::cerr << "Plugin " << metadata_.id << " on_user_join error: " << lua_tostring(L_, -1) << std::endl;
            lua_pop(L_, 1);
        }
    } else {
        lua_pop(L_, 1); // remove non-function value
    }
}

void LuaPlugin::on_user_leave(std::shared_ptr<User> user, std::shared_ptr<Room> room) {
    if (!loaded_ || !L_) return;
    lua_getglobal(L_, "on_user_leave");
    if (lua_isfunction(L_, -1)) {
        push_user(L_, user);
        push_room(L_, room);
        if (lua_pcall(L_, 2, 0, 0) != LUA_OK) {
            std::cerr << "Plugin " << metadata_.id << " on_user_leave error: " << lua_tostring(L_, -1) << std::endl;
            lua_pop(L_, 1);
        }
    } else {
        lua_pop(L_, 1); // remove non-function value
    }
}

bool LuaPlugin::on_before_command(std::shared_ptr<User> user, const ClientCommand& cmd, ClientCommand* out_cmd) {
    if (!loaded_ || !L_ || !out_cmd) return false;
    
    lua_getglobal(L_, "on_before_command");
    if (lua_isfunction(L_, -1)) {
        push_user(L_, user);
        
        // Push command type as string
        const char* type_str = "unknown";
        switch (cmd.type) {
            case ClientCommandType::Ping: type_str = "ping"; break;
            case ClientCommandType::Authenticate: type_str = "authenticate"; break;
            case ClientCommandType::Chat: type_str = "chat"; break;
            case ClientCommandType::Touches: type_str = "touches"; break;
            case ClientCommandType::Judges: type_str = "judges"; break;
            case ClientCommandType::CreateRoom: type_str = "create_room"; break;
            case ClientCommandType::JoinRoom: type_str = "join_room"; break;
            case ClientCommandType::LeaveRoom: type_str = "leave_room"; break;
            case ClientCommandType::LockRoom: type_str = "lock_room"; break;
            case ClientCommandType::CycleRoom: type_str = "cycle_room"; break;
            case ClientCommandType::SelectChart: type_str = "select_chart"; break;
            case ClientCommandType::RequestStart: type_str = "request_start"; break;
            case ClientCommandType::Ready: type_str = "ready"; break;
            case ClientCommandType::CancelReady: type_str = "cancel_ready"; break;
            case ClientCommandType::Played: type_str = "played"; break;
            case ClientCommandType::Abort: type_str = "abort"; break;
            default: type_str = "unknown";
        }
        lua_pushstring(L_, type_str);
        
        // Push command data as table
        lua_newtable(L_);
        switch (cmd.type) {
            case ClientCommandType::Chat:
                lua_pushstring(L_, cmd.message.c_str());
                lua_setfield(L_, -2, "message");
                break;
            case ClientCommandType::Authenticate:
                lua_pushstring(L_, cmd.token.c_str());
                lua_setfield(L_, -2, "token");
                break;
            case ClientCommandType::CreateRoom:
                lua_pushstring(L_, cmd.room_id.to_string().c_str());
                lua_setfield(L_, -2, "room_id");
                break;
            case ClientCommandType::JoinRoom:
                lua_pushstring(L_, cmd.room_id.to_string().c_str());
                lua_setfield(L_, -2, "room_id");
                lua_pushboolean(L_, cmd.monitor);
                lua_setfield(L_, -2, "monitor");
                break;
            case ClientCommandType::SelectChart:
                lua_pushinteger(L_, cmd.chart_id);
                lua_setfield(L_, -2, "chart_id");
                break;
            case ClientCommandType::Played:
                lua_pushinteger(L_, cmd.chart_id);
                lua_setfield(L_, -2, "chart_id");
                break;
            case ClientCommandType::LockRoom:
            case ClientCommandType::CycleRoom:
                lua_pushboolean(L_, cmd.flag);
                lua_setfield(L_, -2, "flag");
                break;
            default:
                // No extra data for other command types
                break;
        }
        
        // Call Lua function
        if (lua_pcall(L_, 3, 1, 0) == LUA_OK) {
            bool modified = false;
            // Check return value
            if (lua_isboolean(L_, -1)) {
                bool allow = lua_toboolean(L_, -1);
                if (!allow) {
                    // Plugin wants to cancel the command
                    // Mark as cancelled by setting a special Ping command
                    out_cmd->type = ClientCommandType::Ping;
                    out_cmd->monitor = true; // special flag indicating cancelled
                    modified = true;
                }
            } else if (lua_istable(L_, -1)) {
                // Plugin returned a modified command table
                // TODO: parse table and fill out_cmd
                // For now, not implemented
                lua_pop(L_, 1); // pop table
                return false;
            }
            lua_pop(L_, 1); // pop return value
            return modified;
        } else {
            std::cerr << "Plugin " << metadata_.id << " on_before_command error: " << lua_tostring(L_, -1) << std::endl;
            lua_pop(L_, 1); // pop error message
        }
    } else {
        lua_pop(L_, 1); // remove non-function value
    }
    return false;
}

// ── PluginManager ──────────────────────────────────────────────────────────

PluginManager::PluginManager(std::shared_ptr<ServerState> server_state)
    : server_state_(std::move(server_state)), http_server_(nullptr) {}

PluginManager::~PluginManager() {
    unload_all();
}

void PluginManager::load_all(const std::string& plugins_dir) {
    if (!fs::exists(plugins_dir)) {
        std::cerr << "Plugins directory does not exist, skipping" << std::endl;
        return;
    }

    std::cerr << "Scanning plugins directory: " << plugins_dir << std::endl;
    for (const auto& entry : fs::directory_iterator(plugins_dir)) {
        if (!entry.is_directory()) continue;
        std::cerr << "Found plugin directory: " << entry.path().string() << std::endl;
        auto plugin = std::make_unique<LuaPlugin>(entry.path().string(), server_state_);
        if (plugin->load()) {
            plugins_[plugin->id()] = std::move(plugin);
        } else {
            std::cerr << "Failed to load plugin: " << entry.path().string() << std::endl;
        }
    }

    std::cerr << "Loaded " << plugins_.size() << " plugin(s)" << std::endl;
    
    // Start HTTP server for API endpoints
    start_http_server(61234);
}

void PluginManager::unload_all() {
    // Unload in reverse order? just clear map
    plugins_.clear();
}

void PluginManager::notify_user_join(std::shared_ptr<User> user, std::shared_ptr<Room> room) {
    for (auto& [id, plugin] : plugins_) {
        plugin->on_user_join(user, room);
    }
}

void PluginManager::notify_user_leave(std::shared_ptr<User> user, std::shared_ptr<Room> room) {
    for (auto& [id, plugin] : plugins_) {
        plugin->on_user_leave(user, room);
    }
}

bool PluginManager::filter_command(std::shared_ptr<User> user, const ClientCommand& cmd, ClientCommand* out_cmd) {
    bool modified = false;
    ClientCommand current = cmd;
    for (auto& [id, plugin] : plugins_) {
        ClientCommand temp;
        if (plugin->on_before_command(user, current, &temp)) {
            current = temp;
            modified = true;
        }
    }
    if (modified && out_cmd) {
        *out_cmd = current;
    }
    return modified;
}

void PluginManager::start_http_server(int port) {
    if (!http_server_) {
        http_server_ = std::make_unique<HttpServer>(server_state_, port);
        http_server_->start();
        std::cerr << "[plugin] HTTP server started on port " << port << std::endl;
    }
}