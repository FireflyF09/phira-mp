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
    {nullptr, nullptr}
};

void register_lua_bindings(lua_State* L, std::shared_ptr<ServerState> server_state) {
    // Store server state as light userdata in registry (or global)
    // We'll store a pointer to shared_ptr in a static location? Simpler: store as light userdata with a pointer to shared_ptr.
    // We'll allocate a shared_ptr on heap and store pointer.
    std::shared_ptr<ServerState>* ptr = new std::shared_ptr<ServerState>(server_state);
    lua_pushlightuserdata(L, ptr);
    lua_setglobal(L, "__server_state");

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