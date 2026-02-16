#pragma once
#include "server.h"
#include "session.h"
#include "room.h"
#include <memory>
#include <string>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

// Register all C++ classes into Lua state
void register_lua_bindings(lua_State* L, std::shared_ptr<ServerState> server_state, PluginServerInterface* server_interface = nullptr);

// Lua‑visible functions
int lua_get_user(lua_State* L);
int lua_get_room(lua_State* L);
int lua_send_to_user(lua_State* L);
int lua_broadcast_to_room(lua_State* L);
int lua_create_virtual_room(lua_State* L);
int lua_log_info(lua_State* L);
int lua_log_error(lua_State* L);

// Helper: push a User object as Lua userdata
void push_user(lua_State* L, std::shared_ptr<User> user);

// Helper: push a Room object as Lua userdata
void push_room(lua_State* L, std::shared_ptr<Room> room);

// Get User from Lua stack (expects userdata at index)
std::shared_ptr<User> get_user(lua_State* L, int index);

// Get Room from Lua stack
std::shared_ptr<Room> get_room(lua_State* L, int index);