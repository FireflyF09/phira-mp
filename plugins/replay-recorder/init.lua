local phira = phira

local replay_storage = {}
local recordings = {}

local function on_enable()
    phira.log_info("Replay Recorder plugin enabled")
    
    -- Create replays directory if it doesn't exist
    os.execute("mkdir -p replays")
    
    -- Register HTTP endpoints for replay management
    local function json_response(data, status)
        if not status then status = 200 end
        local json = phira.json_encode(data)
        return json, "application/json"
    end
    
    -- GET /api/replays/list - List available replays
    phira.register_http_route("GET", "/api/replays/list", function(method, path, query, body, default_content_type)
        phira.log_info("HTTP GET /api/replays/list requested")
        
        -- Simulated replay list
        local replays = {
            {id = "1", userId = 1001, chartId = 101, timestamp = os.time(), score = 950000},
            {id = "2", userId = 1002, chartId = 102, timestamp = os.time() - 3600, score = 980000},
            {id = "3", userId = 1003, chartId = 103, timestamp = os.time() - 7200, score = 920000}
        }
        
        return json_response({replays = replays, count = #replays})
    end)
    
    -- GET /api/replays/download/:id - Download replay file (simulated)
    phira.register_http_route("GET", "/api/replays/download", function(method, path, query, body, default_content_type)
        phira.log_info("HTTP GET /api/replays/download requested")
        
        -- In a real implementation, this would serve the actual replay file
        -- For now, return a placeholder
        local placeholder = "Replay data placeholder - in real implementation this would be binary replay data"
        
        return placeholder, "application/octet-stream"
    end)
    
    -- POST /api/replays/delete - Delete a replay
    phira.register_http_route("POST", "/api/replays/delete", function(method, path, query, body, default_content_type)
        phira.log_info("HTTP POST /api/replays/delete requested")
        
        local success, data = pcall(phira.json_decode, body)
        if not success or not data.id then
            return json_response({error = "Invalid request, expected JSON with 'id' field"}, 400)
        end
        
        local replay_id = data.id
        phira.log_info("Delete replay requested for ID: " .. replay_id)
        
        -- Simulate deletion
        return json_response({status = "ok", message = "Replay " .. replay_id .. " deleted (simulated)"})
    end)
    
    -- Hook into game events
    phira.log_info("Replay recorder ready to capture game events")
    
    -- Simulate starting a recording when game starts
    local function simulate_recording_start(room_id, chart_id, user_ids)
        phira.log_info("Starting replay recording for room " .. room_id .. ", chart " .. chart_id)
        
        -- Create recording entry
        local recording_id = "rec_" .. os.time()
        recordings[recording_id] = {
            room_id = room_id,
            chart_id = chart_id,
            user_ids = user_ids,
            start_time = os.time(),
            events = {}
        }
        
        -- Log first event
        table.insert(recordings[recording_id].events, {
            time = 0,
            type = "recording_start",
            data = {room_id = room_id, chart_id = chart_id}
        })
        
        phira.log_info("Recording started: " .. recording_id)
        return recording_id
    end
    
    -- Simulate adding game event
    local function simulate_game_event(recording_id, event_type, event_data)
        if recordings[recording_id] then
            local time_offset = os.time() - recordings[recording_id].start_time
            table.insert(recordings[recording_id].events, {
                time = time_offset,
                type = event_type,
                data = event_data
            })
            phira.log_info("Event recorded: " .. event_type .. " at " .. time_offset .. "s")
        end
    end
    
    -- Simulate stopping recording
    local function simulate_recording_stop(recording_id)
        if recordings[recording_id] then
            phira.log_info("Stopping recording: " .. recording_id)
            
            -- Save to file (simulated)
            local filename = "replays/" .. recording_id .. ".json"
            local file = io.open(filename, "w")
            if file then
                file:write(phira.json_encode(recordings[recording_id]))
                file:close()
                phira.log_info("Replay saved to " .. filename)
            end
            
            recordings[recording_id] = nil
        end
    end
    
    -- Store simulation functions for demo purposes
    replay_storage.simulate_recording_start = simulate_recording_start
    replay_storage.simulate_game_event = simulate_game_event
    replay_storage.simulate_recording_stop = simulate_recording_stop
    
    phira.log_info("Replay Recorder plugin fully initialized")
end

local function on_disable()
    phira.log_info("Replay Recorder plugin disabled")
    
    -- Stop all active recordings
    for recording_id, _ in pairs(recordings) do
        replay_storage.simulate_recording_stop(recording_id)
    end
end

-- Simple event hooks (would be called from C++ in real implementation)
local function on_room_playing_start(room_id, chart_id, user_ids)
    phira.log_info("Room " .. room_id .. " started playing chart " .. chart_id)
    
    -- Start recording
    if replay_storage.simulate_recording_start then
        replay_storage.simulate_recording_start(room_id, chart_id, user_ids)
    end
end

local function on_room_playing_end(room_id)
    phira.log_info("Room " .. room_id .. " finished playing")
    
    -- Stop recording (simplified - just stop first found recording for this room)
    for recording_id, rec in pairs(recordings) do
        if rec.room_id == room_id then
            if replay_storage.simulate_recording_stop then
                replay_storage.simulate_recording_stop(recording_id)
            end
            break
        end
    end
end

-- Register hooks
on_enable()

-- Export functions for testing
phira.replay = {
    on_room_playing_start = on_room_playing_start,
    on_room_playing_end = on_room_playing_end
}