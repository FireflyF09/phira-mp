#include "lua_bindings.h"
#include "plugin_manager.h"
#include "http_server.h"
#include <iostream>

// ── User userdata ─────────────────────────────────────────────────────────

struct UserWrapper {
    std::shared_ptr<User> user;
};

static int user_gc(lua_State* L) {
    UserWrapper* w = static_cast<UserWrapper*>(lua_touserdata(L, 1));
    w->~UserWrapper();
    return 0;
}

static const luaL_Reg user_metatable[] = {
    {"__gc", user_gc},
    {nullptr, nullptr}
};

void push_user(lua_State* L, std::shared_ptr<User> user) {
    UserWrapper* w = static_cast<UserWrapper*>(lua_newuserdata(L, sizeof(UserWrapper)));
    new (w) UserWrapper{std::move(user)};
    luaL_getmetatable(L, "phira.User");
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        luaL_newmetatable(L, "phira.User");
        luaL_setfuncs(L, user_metatable, 0);
    }
    lua_setmetatable(L, -2);
}

std::shared_ptr<User> get_user(lua_State* L, int index) {
    UserWrapper* w = static_cast<UserWrapper*>(luaL_checkudata(L, index, "phira.User"));
    return w->user;
}

// ── Room userdata ─────────────────────────────────────────────────────────

struct RoomWrapper {
    std::shared_ptr<Room> room;
};

static int room_gc(lua_State* L) {
    RoomWrapper* w = static_cast<RoomWrapper*>(lua_touserdata(L, 1));
    w->~RoomWrapper();
    return 0;
}

static const luaL_Reg room_metatable[] = {
    {"__gc", room_gc},
    {nullptr, nullptr}
};

void push_room(lua_State* L, std::shared_ptr<Room> room) {
    RoomWrapper* w = static_cast<RoomWrapper*>(lua_newuserdata(L, sizeof(RoomWrapper)));
    new (w) RoomWrapper{std::move(room)};
    luaL_getmetatable(L, "phira.Room");
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        luaL_newmetatable(L, "phira.Room");
        luaL_setfuncs(L, room_metatable, 0);
    }
    lua_setmetatable(L, -2);
}

std::shared_ptr<Room> get_room(lua_State* L, int index) {
    RoomWrapper* w = static_cast<RoomWrapper*>(luaL_checkudata(L, index, "phira.Room"));
    return w->room;
}

// ── Lua functions ─────────────────────────────────────────────────────────

static std::shared_ptr<ServerState> get_server_state(lua_State* L) {
    lua_getglobal(L, "__server_state");
    if (lua_islightuserdata(L, -1)) {
        void* ptr = lua_touserdata(L, -1);
        lua_pop(L, 1);
        return *static_cast<std::shared_ptr<ServerState>*>(ptr);
    }
    lua_pop(L, 1);
    return nullptr;
}

static PluginServerInterface* get_server_interface(lua_State* L) {
    lua_getglobal(L, "__server_interface");
    if (lua_islightuserdata(L, -1)) {
        void* ptr = lua_touserdata(L, -1);
        lua_pop(L, 1);
        return static_cast<PluginServerInterface*>(ptr);
    }
    lua_pop(L, 1);
    return nullptr;
}

int lua_get_user(lua_State* L) {
    auto state = get_server_state(L);
    if (!state) return luaL_error(L, "server state not available");
    int32_t user_id = luaL_checkinteger(L, 1);
    {
        std::shared_lock lock(state->users_mtx);
        auto it = state->users.find(user_id);
        if (it == state->users.end()) {
            lua_pushnil(L);
            return 1;
        }
        push_user(L, it->second);
    }
    return 1;
}

int lua_get_room(lua_State* L) {
    auto state = get_server_state(L);
    if (!state) return luaL_error(L, "server state not available");
    const char* room_id_str = luaL_checkstring(L, 1);
    std::string room_id(room_id_str);
    {
        std::shared_lock lock(state->rooms_mtx);
        auto it = state->rooms.find(room_id);
        if (it == state->rooms.end()) {
            lua_pushnil(L);
            return 1;
        }
        push_room(L, it->second);
    }
    return 1;
}

int lua_send_to_user(lua_State* L) {
    auto state = get_server_state(L);
    if (!state) return luaL_error(L, "server state not available");
    int32_t user_id = luaL_checkinteger(L, 1);
    const char* cmd_type = luaL_checkstring(L, 2);
    // TODO: construct ServerCommand from Lua table
    // For now just log
    std::cerr << "[plugin] send_to_user " << user_id << " " << cmd_type << std::endl;
    return 0;
}

int lua_broadcast_to_room(lua_State* L) {
    auto room = get_room(L, 1);
    const char* cmd_type = luaL_checkstring(L, 2);
    // TODO
    std::cerr << "[plugin] broadcast_to_room " << room->id.value << " " << cmd_type << std::endl;
    return 0;
}

int lua_create_virtual_room(lua_State* L) {
    auto state = get_server_state(L);
    if (!state) return luaL_error(L, "server state not available");
    const char* room_id_str = luaL_checkstring(L, 1);
    std::string room_id(room_id_str);
    // TODO: create virtual room
    lua_pushnil(L);
    return 1;
}

int lua_log_info(lua_State* L) {
    const char* msg = luaL_checkstring(L, 1);
    std::cerr << "[plugin] INFO: " << msg << std::endl;
    return 0;
}

int lua_log_error(lua_State* L) {
    const char* msg = luaL_checkstring(L, 1);
    std::cerr << "[plugin] ERROR: " << msg << std::endl;
    return 0;
}

// ── Server API functions ────────────────────────────────────────────────

int lua_kick_user(lua_State* L) {
    PluginServerInterface* server = get_server_interface(L);
    if (!server) return luaL_error(L, "server interface not available");
    int32_t user_id = luaL_checkinteger(L, 1);
    bool preserve_room = lua_isboolean(L, 2) ? lua_toboolean(L, 2) : false;
    bool success = server->kick_user(user_id, preserve_room);
    lua_pushboolean(L, success);
    return 1;
}

int lua_ban_user(lua_State* L) {
    PluginServerInterface* server = get_server_interface(L);
    if (!server) return luaL_error(L, "server interface not available");
    int32_t user_id = luaL_checkinteger(L, 1);
    bool success = server->ban_user(user_id);
    lua_pushboolean(L, success);
    return 1;
}

int lua_unban_user(lua_State* L) {
    PluginServerInterface* server = get_server_interface(L);
    if (!server) return luaL_error(L, "server interface not available");
    int32_t user_id = luaL_checkinteger(L, 1);
    bool success = server->unban_user(user_id);
    lua_pushboolean(L, success);
    return 1;
}

int lua_broadcast_message(lua_State* L) {
    PluginServerInterface* server = get_server_interface(L);
    if (!server) return luaL_error(L, "server interface not available");
    const char* message = luaL_checkstring(L, 1);
    bool success = server->broadcast_message(message);
    lua_pushboolean(L, success);
    return 1;
}

int lua_roomsay_message(lua_State* L) {
    PluginServerInterface* server = get_server_interface(L);
    if (!server) return luaL_error(L, "server interface not available");
    const char* room_id = luaL_checkstring(L, 1);
    const char* message = luaL_checkstring(L, 2);
    bool success = server->roomsay_message(room_id, message);
    lua_pushboolean(L, success);
    return 1;
}

int lua_shutdown_server(lua_State* L) {
    PluginServerInterface* server = get_server_interface(L);
    if (!server) return luaL_error(L, "server interface not available");
    server->shutdown_server();
    return 0;
}

int lua_reload_plugins(lua_State* L) {
    PluginServerInterface* server = get_server_interface(L);
    if (!server) return luaL_error(L, "server interface not available");
    server->reload_plugins();
    return 0;
}

int lua_get_connected_user_count(lua_State* L) {
    PluginServerInterface* server = get_server_interface(L);
    if (!server) return luaL_error(L, "server interface not available");
    int count = server->get_connected_user_count();
    lua_pushinteger(L, count);
    return 1;
}

int lua_get_active_room_count(lua_State* L) {
    PluginServerInterface* server = get_server_interface(L);
    if (!server) return luaL_error(L, "server interface not available");
    int count = server->get_active_room_count();
    lua_pushinteger(L, count);
    return 1;
}

int lua_get_room_list(lua_State* L) {
    PluginServerInterface* server = get_server_interface(L);
    if (!server) return luaL_error(L, "server interface not available");
    auto rooms = server->get_room_list();
    lua_newtable(L);
    for (size_t i = 0; i < rooms.size(); ++i) {
        lua_pushstring(L, rooms[i].c_str());
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

int lua_get_banned_users(lua_State* L) {
    PluginServerInterface* server = get_server_interface(L);
    if (!server) return luaL_error(L, "server interface not available");
    auto banned = server->get_banned_users();
    lua_newtable(L);
    for (size_t i = 0; i < banned.size(); ++i) {
        lua_pushinteger(L, banned[i]);
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

int lua_is_user_banned(lua_State* L) {
    PluginServerInterface* server = get_server_interface(L);
    if (!server) return luaL_error(L, "server interface not available");
    int32_t user_id = luaL_checkinteger(L, 1);
    bool banned = server->is_user_banned(user_id);
    lua_pushboolean(L, banned);
    return 1;
}

int lua_disband_room(lua_State* L) {
    PluginServerInterface* server = get_server_interface(L);
    if (!server) return luaL_error(L, "server interface not available");
    const char* room_id = luaL_checkstring(L, 1);
    bool success = server->disband_room(room_id);
    lua_pushboolean(L, success);
    return 1;
}

int lua_set_max_users(lua_State* L) {
    PluginServerInterface* server = get_server_interface(L);
    if (!server) return luaL_error(L, "server interface not available");
    const char* room_id = luaL_checkstring(L, 1);
    int max_users = luaL_checkinteger(L, 2);
    bool success = server->set_max_users(room_id, max_users);
    lua_pushboolean(L, success);
    return 1;
}

int lua_get_room_max_users(lua_State* L) {
    PluginServerInterface* server = get_server_interface(L);
    if (!server) return luaL_error(L, "server interface not available");
    const char* room_id = luaL_checkstring(L, 1);
    auto max_users = server->get_room_max_users(room_id);
    if (max_users.has_value()) {
        lua_pushinteger(L, *max_users);
    } else {
        lua_pushnil(L);
    }
    return 1;
}

int lua_set_replay_status(lua_State* L) {
    PluginServerInterface* server = get_server_interface(L);
    if (!server) return luaL_error(L, "server interface not available");
    bool enabled = lua_toboolean(L, 1);
    bool success = server->set_replay_status(enabled);
    lua_pushboolean(L, success);
    return 1;
}

int lua_get_replay_status(lua_State* L) {
    PluginServerInterface* server = get_server_interface(L);
    if (!server) return luaL_error(L, "server interface not available");
    bool status = server->get_replay_status();
    lua_pushboolean(L, status);
    return 1;
}

int lua_set_room_creation_status(lua_State* L) {
    PluginServerInterface* server = get_server_interface(L);
    if (!server) return luaL_error(L, "server interface not available");
    bool enabled = lua_toboolean(L, 1);
    bool success = server->set_room_creation_status(enabled);
    lua_pushboolean(L, success);
    return 1;
}

int lua_get_room_creation_status(lua_State* L) {
    PluginServerInterface* server = get_server_interface(L);
    if (!server) return luaL_error(L, "server interface not available");
    bool status = server->get_room_creation_status();
    lua_pushboolean(L, status);
    return 1;
}

int lua_add_ip_to_blacklist(lua_State* L) {
    PluginServerInterface* server = get_server_interface(L);
    if (!server) return luaL_error(L, "server interface not available");
    const char* ip = luaL_checkstring(L, 1);
    bool is_admin = lua_isboolean(L, 2) ? lua_toboolean(L, 2) : true;
    bool success = server->add_ip_to_blacklist(ip, is_admin);
    lua_pushboolean(L, success);
    return 1;
}

int lua_remove_ip_from_blacklist(lua_State* L) {
    PluginServerInterface* server = get_server_interface(L);
    if (!server) return luaL_error(L, "server interface not available");
    const char* ip = luaL_checkstring(L, 1);
    bool is_admin = lua_isboolean(L, 2) ? lua_toboolean(L, 2) : true;
    bool success = server->remove_ip_from_blacklist(ip, is_admin);
    lua_pushboolean(L, success);
    return 1;
}

int lua_is_ip_banned(lua_State* L) {
    PluginServerInterface* server = get_server_interface(L);
    if (!server) return luaL_error(L, "server interface not available");
    const char* ip = luaL_checkstring(L, 1);
    bool banned = server->is_ip_banned(ip);
    lua_pushboolean(L, banned);
    return 1;
}

int lua_get_banned_ips(lua_State* L) {
    PluginServerInterface* server = get_server_interface(L);
    if (!server) return luaL_error(L, "server interface not available");
    bool admin_list = lua_isboolean(L, 1) ? lua_toboolean(L, 1) : true;
    auto ips = server->get_banned_ips(admin_list);
    lua_newtable(L);
    for (size_t i = 0; i < ips.size(); ++i) {
        lua_pushstring(L, ips[i].c_str());
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

int lua_clear_ip_blacklist(lua_State* L) {
    PluginServerInterface* server = get_server_interface(L);
    if (!server) return luaL_error(L, "server interface not available");
    bool admin_list = lua_isboolean(L, 1) ? lua_toboolean(L, 1) : true;
    server->clear_ip_blacklist(admin_list);
    return 0;
}

int lua_ban_room_user(lua_State* L) {
    PluginServerInterface* server = get_server_interface(L);
    if (!server) return luaL_error(L, "server interface not available");
    int32_t user_id = luaL_checkinteger(L, 1);
    const char* room_id = luaL_checkstring(L, 2);
    bool success = server->ban_room_user(user_id, room_id);
    lua_pushboolean(L, success);
    return 1;
}

int lua_unban_room_user(lua_State* L) {
    PluginServerInterface* server = get_server_interface(L);
    if (!server) return luaL_error(L, "server interface not available");
    int32_t user_id = luaL_checkinteger(L, 1);
    const char* room_id = luaL_checkstring(L, 2);
    bool success = server->unban_room_user(user_id, room_id);
    lua_pushboolean(L, success);
    return 1;
}

int lua_is_user_banned_from_room(lua_State* L) {
    PluginServerInterface* server = get_server_interface(L);
    if (!server) return luaL_error(L, "server interface not available");
    int32_t user_id = luaL_checkinteger(L, 1);
    const char* room_id = luaL_checkstring(L, 2);
    bool banned = server->is_user_banned_from_room(user_id, room_id);
    lua_pushboolean(L, banned);
    return 1;
}

int lua_enable_contest(lua_State* L) {
    PluginServerInterface* server = get_server_interface(L);
    if (!server) return luaL_error(L, "server interface not available");
    const char* room_id = luaL_checkstring(L, 1);
    bool manual_start = lua_isboolean(L, 2) ? lua_toboolean(L, 2) : false;
    bool auto_disband = lua_isboolean(L, 3) ? lua_toboolean(L, 3) : false;
    bool success = server->enable_contest(room_id, manual_start, auto_disband);
    lua_pushboolean(L, success);
    return 1;
}

int lua_disable_contest(lua_State* L) {
    PluginServerInterface* server = get_server_interface(L);
    if (!server) return luaL_error(L, "server interface not available");
    const char* room_id = luaL_checkstring(L, 1);
    bool success = server->disable_contest(room_id);
    lua_pushboolean(L, success);
    return 1;
}

int lua_add_contest_whitelist(lua_State* L) {
    PluginServerInterface* server = get_server_interface(L);
    if (!server) return luaL_error(L, "server interface not available");
    const char* room_id = luaL_checkstring(L, 1);
    int32_t user_id = luaL_checkinteger(L, 2);
    bool success = server->add_contest_whitelist(room_id, user_id);
    lua_pushboolean(L, success);
    return 1;
}

int lua_remove_contest_whitelist(lua_State* L) {
    PluginServerInterface* server = get_server_interface(L);
    if (!server) return luaL_error(L, "server interface not available");
    const char* room_id = luaL_checkstring(L, 1);
    int32_t user_id = luaL_checkinteger(L, 2);
    bool success = server->remove_contest_whitelist(room_id, user_id);
    lua_pushboolean(L, success);
    return 1;
}

int lua_start_contest(lua_State* L) {
    PluginServerInterface* server = get_server_interface(L);
    if (!server) return luaL_error(L, "server interface not available");
    const char* room_id = luaL_checkstring(L, 1);
    bool force = lua_isboolean(L, 2) ? lua_toboolean(L, 2) : false;
    bool success = server->start_contest(room_id, force);
    lua_pushboolean(L, success);
    return 1;
}

int lua_get_user_name(lua_State* L) {
    PluginServerInterface* server = get_server_interface(L);
    if (!server) return luaL_error(L, "server interface not available");
    int32_t user_id = luaL_checkinteger(L, 1);
    auto name = server->get_user_name(user_id);
    if (name.has_value()) {
        lua_pushstring(L, name->c_str());
    } else {
        lua_pushnil(L);
    }
    return 1;
}

int lua_get_user_language(lua_State* L) {
    PluginServerInterface* server = get_server_interface(L);
    if (!server) return luaL_error(L, "server interface not available");
    int32_t user_id = luaL_checkinteger(L, 1);
    auto language = server->get_user_language(user_id);
    if (language.has_value()) {
        lua_pushstring(L, language->c_str());
    } else {
        lua_pushnil(L);
    }
    return 1;
}

int lua_get_user_room_id(lua_State* L) {
    PluginServerInterface* server = get_server_interface(L);
    if (!server) return luaL_error(L, "server interface not available");
    int32_t user_id = luaL_checkinteger(L, 1);
    auto room_id = server->get_user_room_id(user_id);
    if (room_id.has_value()) {
        lua_pushstring(L, room_id->c_str());
    } else {
        lua_pushnil(L);
    }
    return 1;
}

int lua_get_room_user_count(lua_State* L) {
    PluginServerInterface* server = get_server_interface(L);
    if (!server) return luaL_error(L, "server interface not available");
    const char* room_id = luaL_checkstring(L, 1);
    auto count = server->get_room_user_count(room_id);
    if (count.has_value()) {
        lua_pushinteger(L, *count);
    } else {
        lua_pushnil(L);
    }
    return 1;
}

int lua_get_room_user_ids(lua_State* L) {
    PluginServerInterface* server = get_server_interface(L);
    if (!server) return luaL_error(L, "server interface not available");
    const char* room_id = luaL_checkstring(L, 1);
    auto user_ids = server->get_room_user_ids(room_id);
    lua_newtable(L);
    for (size_t i = 0; i < user_ids.size(); ++i) {
        lua_pushinteger(L, user_ids[i]);
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

int lua_get_room_owner_id(lua_State* L) {
    PluginServerInterface* server = get_server_interface(L);
    if (!server) return luaL_error(L, "server interface not available");
    const char* room_id = luaL_checkstring(L, 1);
    auto owner_id = server->get_room_owner_id(room_id);
    if (owner_id.has_value()) {
        lua_pushstring(L, owner_id->c_str());
    } else {
        lua_pushnil(L);
    }
    return 1;
}

int lua_save_admin_data(lua_State* L) {
    PluginServerInterface* server = get_server_interface(L);
    if (!server) return luaL_error(L, "server interface not available");
    server->save_admin_data();
    return 0;
}

int lua_load_admin_data(lua_State* L) {
    PluginServerInterface* server = get_server_interface(L);
    if (!server) return luaL_error(L, "server interface not available");
    server->load_admin_data();
    return 0;
}

// ── HTTP route registration ───────────────────────────────────────────────

static int lua_register_http_route(lua_State* L) {
    // Get server state from global
    lua_getglobal(L, "__server_state");
    if (lua_islightuserdata(L, -1)) {
        std::shared_ptr<ServerState>* server_state_ptr = static_cast<std::shared_ptr<ServerState>*>(lua_touserdata(L, -1));
        if (server_state_ptr && *server_state_ptr) {
            std::shared_ptr<ServerState> server_state = *server_state_ptr;
            auto plugin_manager = server_state->plugin_manager.lock();
            if (plugin_manager) {
                HttpServer* http_server = plugin_manager->get_http_server();
                if (http_server) {
                    // Get arguments: method, path, handler function
                    const char* method = luaL_checkstring(L, 1);
                    const char* path = luaL_checkstring(L, 2);
                    luaL_checktype(L, 3, LUA_TFUNCTION);
                    
                    // Store the Lua function reference
                    lua_pushvalue(L, 3); // copy function to top
                    int ref = luaL_ref(L, LUA_REGISTRYINDEX); // store in registry
                    
                    // Create C++ handler that calls Lua function
                    http_server->register_route(method, path, 
                        [L, ref](const std::string& method, const std::string& path,
                                 const std::string& query, const std::string& body,
                                 std::string& response, std::string& content_type) {
                            // Call Lua function
                            lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
                            lua_pushstring(L, method.c_str());
                            lua_pushstring(L, path.c_str());
                            lua_pushstring(L, query.c_str());
                            lua_pushstring(L, body.c_str());
                            lua_pushstring(L, "application/json"); // default response type
                            
                            if (lua_pcall(L, 5, 2, 0) != LUA_OK) {
                                response = "{\"error\":\"Lua handler error\"}";
                                content_type = "application/json";
                                std::cerr << "[plugin] HTTP handler error: " << lua_tostring(L, -1) << std::endl;
                                lua_pop(L, 1); // pop error message
                                return;
                            }
                            
                            // Get return values: response, content_type
                            if (lua_isstring(L, -2)) {
                                response = lua_tostring(L, -2);
                            }
                            if (lua_isstring(L, -1)) {
                                content_type = lua_tostring(L, -1);
                            }
                            lua_pop(L, 2); // pop return values
                        });
                    
                    lua_pushboolean(L, true);
                    return 1;
                }
            }
        }
        lua_pop(L, 1); // pop __server_state
    }
    
    lua_pushboolean(L, false);
    return 1;
}

// ── Registration ──────────────────────────────────────────────────────────

static const luaL_Reg phira_lib[] = {
    {"get_user", lua_get_user},
    {"get_room", lua_get_room},
    {"send_to_user", lua_send_to_user},
    {"broadcast_to_room", lua_broadcast_to_room},
    {"create_virtual_room", lua_create_virtual_room},
    {"log_info", lua_log_info},
    {"log_error", lua_log_error},
    {"register_http_route", lua_register_http_route},
    {"kick_user", lua_kick_user},
    {"ban_user", lua_ban_user},
    {"unban_user", lua_unban_user},
    {"broadcast_message", lua_broadcast_message},
    {"roomsay_message", lua_roomsay_message},
    {"shutdown_server", lua_shutdown_server},
    {"reload_plugins", lua_reload_plugins},
    {"get_connected_user_count", lua_get_connected_user_count},
    {"get_active_room_count", lua_get_active_room_count},
    {"get_room_list", lua_get_room_list},
    {"get_banned_users", lua_get_banned_users},
    {"disband_room", lua_disband_room},
    {"set_max_users", lua_set_max_users},
    {"get_room_max_users", lua_get_room_max_users},
    {"set_replay_status", lua_set_replay_status},
    {"get_replay_status", lua_get_replay_status},
    {"set_room_creation_status", lua_set_room_creation_status},
    {"get_room_creation_status", lua_get_room_creation_status},
    {"add_ip_to_blacklist", lua_add_ip_to_blacklist},
    {"remove_ip_from_blacklist", lua_remove_ip_from_blacklist},
    {"is_ip_banned", lua_is_ip_banned},
    {"get_banned_ips", lua_get_banned_ips},
    {"clear_ip_blacklist", lua_clear_ip_blacklist},
    {"is_user_banned", lua_is_user_banned},
    {"ban_room_user", lua_ban_room_user},
    {"unban_room_user", lua_unban_room_user},
    {"is_user_banned_from_room", lua_is_user_banned_from_room},
    {"enable_contest", lua_enable_contest},
    {"disable_contest", lua_disable_contest},
    {"add_contest_whitelist", lua_add_contest_whitelist},
    {"remove_contest_whitelist", lua_remove_contest_whitelist},
    {"start_contest", lua_start_contest},
    {"get_user_name", lua_get_user_name},
    {"get_user_language", lua_get_user_language},
    {"get_user_room_id", lua_get_user_room_id},
    {"get_room_user_count", lua_get_room_user_count},
    {"get_room_user_ids", lua_get_room_user_ids},
    {"get_room_owner_id", lua_get_room_owner_id},
    {"save_admin_data", lua_save_admin_data},
    {"load_admin_data", lua_load_admin_data},
    {nullptr, nullptr}
};

void register_lua_bindings(lua_State* L, std::shared_ptr<ServerState> server_state, PluginServerInterface* server_interface) {
    // Store server state as light userdata in registry (or global)
    // We'll store a pointer to shared_ptr in a static location? Simpler: store as light userdata with a pointer to shared_ptr.
    // We'll allocate a shared_ptr on heap and store pointer.
    std::shared_ptr<ServerState>* ptr = new std::shared_ptr<ServerState>(server_state);
    lua_pushlightuserdata(L, ptr);
    lua_setglobal(L, "__server_state");

    // Store server interface
    if (server_interface) {
        lua_pushlightuserdata(L, server_interface);
        lua_setglobal(L, "__server_interface");
    }

    // Create phira table
    luaL_newlib(L, phira_lib);
    lua_setglobal(L, "phira");

    // Create metatables
    luaL_newmetatable(L, "phira.User");
    luaL_setfuncs(L, user_metatable, 0);
    lua_pop(L, 1);

    luaL_newmetatable(L, "phira.Room");
    luaL_setfuncs(L, room_metatable, 0);
    lua_pop(L, 1);
}