local phira = phira

local virtual_rooms = {}
local room_settings = {}

local function on_enable()
    phira.log_info("Advanced Room Management plugin enabled")
    
    local function json_response(data, status)
        if not status then status = 200 end
        local json = phira.json_encode(data)
        return json, "application/json"
    end
    
    -- POST /api/rooms/virtual/create - Create a virtual room
    phira.register_http_route("POST", "/api/rooms/virtual/create", function(method, path, query, body, default_content_type)
        phira.log_info("HTTP POST /api/rooms/virtual/create requested")
        
        local success, data = pcall(phira.json_decode, body)
        if not success or not data.name then
            return json_response({error = "Invalid request, expected JSON with 'name' field"}, 400)
        end
        
        local room_name = data.name
        local room_id = "_virtual_" .. room_name .. "_" .. os.time()
        
        phira.log_info("Creating virtual room: " .. room_name .. " (ID: " .. room_id .. ")")
        
        -- Create virtual room using existing API
        local success, room = pcall(phira.create_virtual_room, room_id, room_name)
        
        if success and room then
            virtual_rooms[room_id] = {
                name = room_name,
                created_at = os.time(),
                settings = data.settings or {}
            }
            
            return json_response({
                status = "ok", 
                roomId = room_id,
                message = "Virtual room created successfully"
            })
        else
            return json_response({error = "Failed to create virtual room"}, 500)
        end
    end)
    
    -- POST /api/rooms/virtual/delete - Delete a virtual room
    phira.register_http_route("POST", "/api/rooms/virtual/delete", function(method, path, query, body, default_content_type)
        phira.log_info("HTTP POST /api/rooms/virtual/delete requested")
        
        local success, data = pcall(phira.json_decode, body)
        if not success or not data.roomId then
            return json_response({error = "Invalid request, expected JSON with 'roomId' field"}, 400)
        end
        
        local room_id = data.roomId
        
        phira.log_info("Deleting virtual room: " .. room_id)
        
        -- Remove from tracking
        virtual_rooms[room_id] = nil
        
        -- In real implementation, this would actually delete the room
        return json_response({
            status = "ok", 
            message = "Virtual room " .. room_id .. " deleted (simulated)"
        })
    end)
    
    -- GET /api/rooms/virtual/list - List virtual rooms
    phira.register_http_route("GET", "/api/rooms/virtual/list", function(method, path, query, body, default_content_type)
        phira.log_info("HTTP GET /api/rooms/virtual/list requested")
        
        local rooms_list = {}
        for room_id, room_info in pairs(virtual_rooms) do
            table.insert(rooms_list, {
                id = room_id,
                name = room_info.name,
                created_at = room_info.created_at,
                user_count = 0, -- Would need to get actual count from room
                settings = room_info.settings
            })
        end
        
        return json_response({rooms = rooms_list, count = #rooms_list})
    end)
    
    -- POST /api/rooms/cycle/enable - Enable room cycling
    phira.register_http_route("POST", "/api/rooms/cycle/enable", function(method, path, query, body, default_content_type)
        phira.log_info("HTTP POST /api/rooms/cycle/enable requested")
        
        local success, data = pcall(phira.json_decode, body)
        if not success or not data.roomId then
            return json_response({error = "Invalid request, expected JSON with 'roomId' field"}, 400)
        end
        
        local room_id = data.roomId
        
        phira.log_info("Enabling room cycling for: " .. room_id)
        
        -- In real implementation, this would enable room cycling
        return json_response({
            status = "ok", 
            message = "Room cycling enabled for " .. room_id .. " (simulated)"
        })
    end)
    
    -- POST /api/rooms/lock - Lock/unlock a room
    phira.register_http_route("POST", "/api/rooms/lock", function(method, path, query, body, default_content_type)
        phira.log_info("HTTP POST /api/rooms/lock requested")
        
        local success, data = pcall(phira.json_decode, body)
        if not success or not data.roomId or data.locked == nil then
            return json_response({error = "Invalid request, expected JSON with 'roomId' and 'locked' fields"}, 400)
        end
        
        local room_id = data.roomId
        local locked = data.locked
        
        phira.log_info("Setting room lock for " .. room_id .. " to " .. tostring(locked))
        
        -- In real implementation, this would lock/unlock the room
        return json_response({
            status = "ok", 
            message = "Room " .. room_id .. " " .. (locked and "locked" or "unlocked") .. " (simulated)"
        })
    end)
    
    -- POST /api/rooms/settings - Update room settings
    phira.register_http_route("POST", "/api/rooms/settings", function(method, path, query, body, default_content_type)
        phira.log_info("HTTP POST /api/rooms/settings requested")
        
        local success, data = pcall(phira.json_decode, body)
        if not success or not data.roomId or not data.settings then
            return json_response({error = "Invalid request, expected JSON with 'roomId' and 'settings' fields"}, 400)
        end
        
        local room_id = data.roomId
        local settings = data.settings
        
        phira.log_info("Updating settings for room: " .. room_id)
        
        -- Store settings
        room_settings[room_id] = settings
        
        return json_response({
            status = "ok", 
            message = "Settings updated for room " .. room_id
        })
    end)
    
    -- Room event handlers
    local function on_room_created(room_id)
        phira.log_info("Room created: " .. room_id)
        
        -- Initialize default settings for new room
        room_settings[room_id] = {
            allow_spectators = true,
            max_players = 4,
            auto_kick_afk = false,
            afk_timeout = 300
        }
    end
    
    local function on_room_destroyed(room_id)
        phira.log_info("Room destroyed: " .. room_id)
        
        -- Clean up settings
        room_settings[room_id] = nil
        virtual_rooms[room_id] = nil
    end
    
    -- Demo: Create a default virtual room on startup
    phira.log_info("Creating demo virtual room...")
    
    local demo_room_id = "_virtual_demo_" .. os.time()
    local success, demo_room = pcall(phira.create_virtual_room, demo_room_id, "Demo Virtual Room")
    
    if success and demo_room then
        virtual_rooms[demo_room_id] = {
            name = "Demo Virtual Room",
            created_at = os.time(),
            settings = {demo = true}
        }
        phira.log_info("Demo virtual room created: " .. demo_room_id)
    end
    
    phira.log_info("Advanced Room Management plugin ready")
    
    -- Export functions for potential future use
    phira.room_manager = {
        on_room_created = on_room_created,
        on_room_destroyed = on_room_destroyed,
        get_room_settings = function(room_id) return room_settings[room_id] end,
        get_virtual_rooms = function() return virtual_rooms end
    }
end

local function on_disable()
    phira.log_info("Advanced Room Management plugin disabled")
    
    -- Clean up all virtual rooms (in real implementation)
    for room_id, _ in pairs(virtual_rooms) do
        phira.log_info("Cleaning up virtual room: " .. room_id)
    end
end

-- Register hooks
on_enable()