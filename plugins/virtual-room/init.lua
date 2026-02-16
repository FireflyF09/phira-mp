function on_enable()
    phira.log_info("Virtual room plugin enabled!")
end

function on_disable()
    phira.log_info("Virtual room plugin disabled.")
end

function on_user_join(user, room)
    phira.log_info("User " .. user.id .. " joined room " .. room.id.value)
    -- TODO: implement virtual room logic
end

function on_user_leave(user, room)
    phira.log_info("User " .. user.id .. " left room " .. room.id.value)
end