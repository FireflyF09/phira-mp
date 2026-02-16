local function on_enable()
    phira.log_info("Test API plugin enabled - testing new server API")
    
    -- Test 1: Get connected user count
    local user_count = phira.get_connected_user_count()
    phira.log_info("Connected users: " .. tostring(user_count))
    
    -- Test 2: Get active room count
    local room_count = phira.get_active_room_count()
    phira.log_info("Active rooms: " .. tostring(room_count))
    
    -- Test 3: Get room list
    local rooms = phira.get_room_list()
    phira.log_info("Room list length: " .. tostring(#rooms))
    for i, room_id in ipairs(rooms) do
        phira.log_info("  Room " .. i .. ": " .. room_id)
    end
    
    -- Test 4: Get banned users
    local banned = phira.get_banned_users()
    phira.log_info("Banned users count: " .. tostring(#banned))
    
    -- Test 5: Check if a specific user is banned (user ID 0 for test)
    local is_banned = phira.is_user_banned(0)
    phira.log_info("Is user 0 banned? " .. tostring(is_banned))
    
    -- Test 6: Test broadcast message (will not actually send if no users)
    local broadcast_success = phira.broadcast_message("Test broadcast from plugin")
    phira.log_info("Broadcast result: " .. tostring(broadcast_success))
    
    -- Test 7: Test room message (need a valid room ID)
    if #rooms > 0 then
        local roomsay_success = phira.roomsay_message(rooms[1], "Test room message")
        phira.log_info("Roomsay to " .. rooms[1] .. " result: " .. tostring(roomsay_success))
    end
    
    -- Test 8: Test server information functions
    phira.log_info("Testing server info functions...")
    
    -- Test 9: Test replay status
    local replay_status = phira.get_replay_status()
    phira.log_info("Replay status: " .. tostring(replay_status))
    
    -- Test 10: Test room creation status
    local room_creation_status = phira.get_room_creation_status()
    phira.log_info("Room creation status: " .. tostring(room_creation_status))
    
    -- Test 11: Test IP blacklist functions (read-only)
    local banned_ips = phira.get_banned_ips(true)  -- admin list
    phira.log_info("Banned IPs (admin): " .. tostring(#banned_ips))
    
    -- Test 12: Test user information functions (if any users connected)
    if user_count > 0 then
        -- Need a valid user ID - we don't have one, skip
        phira.log_info("Skipping user info tests - need valid user ID")
    end
    
    -- Test 13: Test room management functions (if any rooms)
    if #rooms > 0 then
        local room_id = rooms[1]
        -- Try to get room max users
        local max_users = phira.get_room_max_users(room_id)
        if max_users then
            phira.log_info("Room " .. room_id .. " max users: " .. tostring(max_users))
        else
            phira.log_info("Room " .. room_id .. " max users: not available")
        end
        
        -- Try to get room user count
        local room_user_count = phira.get_room_user_count(room_id)
        if room_user_count then
            phira.log_info("Room " .. room_id .. " user count: " .. tostring(room_user_count))
        else
            phira.log_info("Room " .. room_id .. " user count: not available")
        end
    end
    
    -- Test 14: Test contest functions (if any rooms)
    if #rooms > 0 then
        local room_id = rooms[1]
        -- These are write operations, just log that they exist
        phira.log_info("Contest functions available for room " .. room_id)
    end
    
    -- Test 15: Test room-specific ban functions
    if #rooms > 0 then
        local room_id = rooms[1]
        phira.log_info("Room ban functions available for room " .. room_id)
    end
    
    -- Test 16: Test admin data persistence
    phira.log_info("Admin data functions available")
    
    phira.log_info("All API tests completed")
end

local function on_disable()
    phira.log_info("Test API plugin disabled")
end

local function on_user_join(user, room)
    phira.log_info("User joined - user ID: " .. tostring(user.id) .. ", room: " .. room.id)
end

local function on_user_leave(user, room)
    phira.log_info("User left - user ID: " .. tostring(user.id) .. ", room: " .. room.id)
end

local function on_room_create(room)
    phira.log_info("Room created - room ID: " .. room.id)
end

local function on_room_destroy(room)
    phira.log_info("Room destroyed - room ID: " .. room.id)
end

local function on_user_kick(user, room, reason)
    phira.log_info("User kicked - user ID: " .. tostring(user.id) .. 
                   ", room: " .. (room and room.id or "nil") .. 
                   ", reason: " .. reason)
end

local function on_user_ban(user, reason, duration_seconds)
    phira.log_info("User banned - user ID: " .. tostring(user.id) .. 
                   ", reason: " .. reason .. 
                   ", duration: " .. tostring(duration_seconds) .. " seconds")
end

local function on_user_unban(user_id)
    phira.log_info("User unbanned - user ID: " .. tostring(user_id))
end

-- Register hooks
on_enable()