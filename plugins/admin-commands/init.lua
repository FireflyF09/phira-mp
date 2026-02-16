local phira = phira

local admin_users = {} -- In real implementation, this would be a proper auth system
local banned_users = {}

local function on_enable()
    phira.log_info("Admin Commands plugin enabled")
    
    -- Add admin user (for demo)
    admin_users["admin"] = true
    
    local function json_response(data, status)
        if not status then status = 200 end
        local json = phira.json_encode(data)
        return json, "application/json"
    end
    
    -- POST /api/admin/kick - Kick a user
    phira.register_http_route("POST", "/api/admin/kick", function(method, path, query, body, default_content_type)
        phira.log_info("HTTP POST /api/admin/kick requested")
        
        local success, data = pcall(phira.json_decode, body)
        if not success or not data.userId then
            return json_response({error = "Invalid request, expected JSON with 'userId' field"}, 400)
        end
        
        local user_id = tonumber(data.userId)
        local reason = data.reason or "No reason provided"
        
        phira.log_info("Kick user requested: ID=" .. user_id .. ", reason=" .. reason)
        
        -- In real implementation, this would disconnect the user
        -- For now, just log it
        return json_response({status = "ok", message = "User " .. user_id .. " kicked (simulated)"})
    end)
    
    -- POST /api/admin/ban - Ban a user
    phira.register_http_route("POST", "/api/admin/ban", function(method, path, query, body, default_content_type)
        phira.log_info("HTTP POST /api/admin/ban requested")
        
        local success, data = pcall(phira.json_decode, body)
        if not success or not data.userId then
            return json_response({error = "Invalid request, expected JSON with 'userId' field"}, 400)
        end
        
        local user_id = tonumber(data.userId)
        local reason = data.reason or "No reason provided"
        local duration = data.duration or 3600 -- seconds
        
        phira.log_info("Ban user requested: ID=" .. user_id .. ", reason=" .. reason .. ", duration=" .. duration .. "s")
        
        -- Add to banned list
        banned_users[user_id] = {
            reason = reason,
            expires_at = os.time() + duration
        }
        
        return json_response({status = "ok", message = "User " .. user_id .. " banned (simulated)"})
    end)
    
    -- POST /api/admin/unban - Unban a user
    phira.register_http_route("POST", "/api/admin/unban", function(method, path, query, body, default_content_type)
        phira.log_info("HTTP POST /api/admin/unban requested")
        
        local success, data = pcall(phira.json_decode, body)
        if not success or not data.userId then
            return json_response({error = "Invalid request, expected JSON with 'userId' field"}, 400)
        end
        
        local user_id = tonumber(data.userId)
        
        phira.log_info("Unban user requested: ID=" .. user_id)
        
        -- Remove from banned list
        banned_users[user_id] = nil
        
        return json_response({status = "ok", message = "User " .. user_id .. " unbanned (simulated)"})
    end)
    
    -- GET /api/admin/banned - List banned users
    phira.register_http_route("GET", "/api/admin/banned", function(method, path, query, body, default_content_type)
        phira.log_info("HTTP GET /api/admin/banned requested")
        
        local banned_list = {}
        for user_id, ban_info in pairs(banned_users) do
            table.insert(banned_list, {
                userId = user_id,
                reason = ban_info.reason,
                expiresAt = ban_info.expires_at
            })
        end
        
        return json_response({banned = banned_list, count = #banned_list})
    end)
    
    -- POST /api/admin/room/disband - Disband a room
    phira.register_http_route("POST", "/api/admin/room/disband", function(method, path, query, body, default_content_type)
        phira.log_info("HTTP POST /api/admin/room/disband requested")
        
        local success, data = pcall(phira.json_decode, body)
        if not success or not data.roomId then
            return json_response({error = "Invalid request, expected JSON with 'roomId' field"}, 400)
        end
        
        local room_id = data.roomId
        
        phira.log_info("Disband room requested: ID=" .. room_id)
        
        -- In real implementation, this would disband the room
        return json_response({status = "ok", message = "Room " .. room_id .. " disbanded (simulated)"})
    end)
    
    -- POST /api/admin/room/move - Move user to another room
    phira.register_http_route("POST", "/api/admin/room/move", function(method, path, query, body, default_content_type)
        phira.log_info("HTTP POST /api/admin/room/move requested")
        
        local success, data = pcall(phira.json_decode, body)
        if not success or not data.userId or not data.targetRoomId then
            return json_response({error = "Invalid request, expected JSON with 'userId' and 'targetRoomId' fields"}, 400)
        end
        
        local user_id = tonumber(data.userId)
        local target_room_id = data.targetRoomId
        
        phira.log_info("Move user requested: user=" .. user_id .. ", targetRoom=" .. target_room_id)
        
        return json_response({status = "ok", message = "User " .. user_id .. " moved to room " .. target_room_id .. " (simulated)"})
    end)
    
    -- POST /api/admin/contest/start - Start a contest (simulated)
    phira.register_http_route("POST", "/api/admin/contest/start", function(method, path, query, body, default_content_type)
        phira.log_info("HTTP POST /api/admin/contest/start requested")
        
        local success, data = pcall(phira.json_decode, body)
        if not success or not data.contestId then
            return json_response({error = "Invalid request, expected JSON with 'contestId' field"}, 400)
        end
        
        local contest_id = data.contestId
        
        phira.log_info("Start contest requested: contestId=" .. contest_id)
        
        return json_response({status = "ok", message = "Contest " .. contest_id .. " started (simulated)"})
    end)
    
    -- Chat command processor (if on_before_command hook was implemented)
    phira.log_info("Admin commands available via HTTP API")
    
    -- Helper function to check if user is admin
    local function is_admin(user_id)
        -- Simple demo - user with ID 1 is admin
        return user_id == 1
    end
    
    -- Example of how chat commands would be processed
    local function process_chat_command(user_id, message)
        if not is_admin(user_id) then
            return false
        end
        
        -- Check for admin commands
        if message:match("^!kick%s+(%d+)") then
            local target_id = message:match("^!kick%s+(%d+)")
            phira.log_info("Admin " .. user_id .. " issued kick command for user " .. target_id)
            return true
        elseif message:match("^!ban%s+(%d+)") then
            local target_id = message:match("^!ban%s+(%d+)")
            phira.log_info("Admin " .. user_id .. " issued ban command for user " .. target_id)
            return true
        elseif message:match("^!broadcast%s+(.+)$") then
            local broadcast_msg = message:match("^!broadcast%s+(.+)$")
            phira.log_info("Admin " .. user_id .. " issued broadcast: " .. broadcast_msg)
            return true
        end
        
        return false
    end
    
    -- Store for potential future use
    phira.admin = {
        process_chat_command = process_chat_command,
        is_admin = is_admin
    }
end

local function on_disable()
    phira.log_info("Admin Commands plugin disabled")
end

-- Register hooks
on_enable()