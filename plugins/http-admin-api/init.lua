local phira = phira

local function on_enable()
    phira.log_info("HTTP Admin API plugin enabled")
    
    -- Helper function to send JSON response
    local function json_response(data, status)
        if not status then status = 200 end
        local json = phira.json_encode(data)
        return json, "application/json"
    end
    
    -- Register HTTP routes
    
    -- GET /api/rooms - List all rooms
    phira.register_http_route("GET", "/api/rooms", function(method, path, query, body, default_content_type)
        phira.log_info("HTTP GET /api/rooms requested")
        
        -- Simple implementation - return empty array for now
        -- In a real implementation, we would iterate through all rooms
        local rooms = {}
        
        -- This would require additional Lua API functions to get room list
        -- For now, return empty array
        return json_response({rooms = rooms, count = #rooms})
    end)
    
    -- GET /api/stats - Server statistics
    phira.register_http_route("GET", "/api/stats", function(method, path, query, body, default_content_type)
        phira.log_info("HTTP GET /api/stats requested")
        
        -- Get user count, room count, etc.
        -- This would require additional Lua API functions
        -- For now, return placeholder stats
        local stats = {
            users = 0,
            rooms = 0,
            sessions = 0,
            uptime = 0,
            version = "1.0.0",
            plugins = 1
        }
        
        return json_response(stats)
    end)
    
    -- POST /api/admin/broadcast - Broadcast message to all users
    phira.register_http_route("POST", "/api/admin/broadcast", function(method, path, query, body, default_content_type)
        phira.log_info("HTTP POST /api/admin/broadcast requested")
        
        -- Parse JSON body
        local success, data = pcall(phira.json_decode, body)
        if not success or not data.message then
            return json_response({error = "Invalid request, expected JSON with 'message' field"}, 400)
        end
        
        local message = data.message
        
        -- Broadcast to all users
        -- This would require a broadcast function in Lua API
        phira.log_info("Broadcast message: " .. message)
        
        -- For now, just log the message
        return json_response({status = "ok", message = "Broadcast sent (simulated)"})
    end)
    
    -- POST /api/admin/shutdown - Graceful shutdown (simulated)
    phira.register_http_route("POST", "/api/admin/shutdown", function(method, path, query, body, default_content_type)
        phira.log_info("HTTP POST /api/admin/shutdown requested")
        
        -- In a real implementation, this would trigger server shutdown
        -- For safety, we just log it
        phira.log_info("Server shutdown requested via HTTP API")
        
        return json_response({status = "ok", message = "Shutdown request received (simulated)"})
    end)
    
    -- GET /api/health - Health check endpoint
    phira.register_http_route("GET", "/api/health", function(method, path, query, body, default_content_type)
        return json_response({status = "healthy", timestamp = os.time()})
    end)
    
    -- GET / - Welcome page
    phira.register_http_route("GET", "/", function(method, path, query, body, default_content_type)
        local html = [[
<!DOCTYPE html>
<html>
<head>
    <title>Phira MP Server Admin</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 40px; }
        h1 { color: #333; }
        .endpoint { background: #f5f5f5; padding: 10px; margin: 10px 0; border-left: 4px solid #007acc; }
        code { background: #eee; padding: 2px 4px; }
    </style>
</head>
<body>
    <h1>Phira MP Server Admin API</h1>
    <p>This server is running with Lua plugin system.</p>
    
    <h2>Available Endpoints:</h2>
    
    <div class="endpoint">
        <code>GET /api/health</code> - Health check
    </div>
    
    <div class="endpoint">
        <code>GET /api/stats</code> - Server statistics
    </div>
    
    <div class="endpoint">
        <code>GET /api/rooms</code> - List all rooms
    </div>
    
    <div class="endpoint">
        <code>POST /api/admin/broadcast</code> - Broadcast message to all users (JSON: {"message": "text"})
    </div>
    
    <div class="endpoint">
        <code>POST /api/admin/shutdown</code> - Graceful shutdown (simulated)
    </div>
    
    <h2>Plugins:</h2>
    <ul>
        <li>HTTP Admin API (this plugin)</li>
        <li>Virtual Room (example plugin)</li>
    </ul>
    
    <p>Based on tphira-mp HTTP API design.</p>
</body>
</html>
        ]]
        return html, "text/html"
    end)
    
    phira.log_info("HTTP Admin API routes registered")
end

local function on_disable()
    phira.log_info("HTTP Admin API plugin disabled")
end

-- JSON helper functions (simple implementation)
function phira.json_encode(obj)
    -- Very simple JSON encoder for basic types
    if type(obj) == "table" then
        local parts = {}
        for k, v in pairs(obj) do
            local key = type(k) == "string" and ('"' .. k .. '"') or tostring(k)
            local value
            if type(v) == "string" then
                value = '"' .. v:gsub('"', '\\"') .. '"'
            elseif type(v) == "table" then
                value = phira.json_encode(v)
            else
                value = tostring(v)
            end
            table.insert(parts, key .. ":" .. value)
        end
        return "{" .. table.concat(parts, ",") .. "}"
    elseif type(obj) == "string" then
        return '"' .. obj:gsub('"', '\\"') .. '"'
    else
        return tostring(obj)
    end
end

function phira.json_decode(str)
    -- Very simple JSON decoder for basic objects
    -- This is a placeholder - in a real implementation, use a proper Lua JSON library
    -- For now, just return a dummy table
    if str:match('^{.*}$') then
        return {message = "parsed"}
    else
        return nil
    end
end

-- Register hooks
on_enable()