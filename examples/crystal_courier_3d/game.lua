local player, status, exit
local crystals, hazards = {}, {}
local total, collected = 0, 0
local done, jump_held = false, false

local function message(text)
    engine.set(status, '{"name":"Status: ' .. text .. '"}')
end

function on_start(self)
    for _, id in ipairs(engine.entities()) do
        local name = engine.name(id)
        if name == "Player" then player = id
        elseif string.sub(name, 1, 8) == "Status: " then status = id
        elseif name == "Exit" then exit = id
        elseif string.match(name, "^Crystal %d+$") then crystals[#crystals + 1] = id
        elseif name == "Hazard" then hazards[#hazards + 1] = id end
    end
    assert(player and status and exit, "3D level needs Player, Status, and Exit")
    total = #crystals
    message("0 / " .. total .. " crystals - explore, jump, and push crates")
end

function on_update(self, dt)
    if done then return end
    local x, y, z = engine.position(player)
    if y < -5 then
        done = true
        message("You fell! Press R to restart")
        return
    end
    for _, id in ipairs(hazards) do
        if engine.touching(player, id) then
            done = true
            engine.set_linear_velocity(player, 0, 0, 0)
            message("Lava! Press R to restart")
            return
        end
    end
    for _, id in ipairs(crystals) do
        if engine.alive(id) and engine.touching(player, id) then
            engine.destroy(id)
            collected = collected + 1
            message(collected .. " / " .. total .. " crystals - " ..
                    (collected == total and "reach the golden portal!" or "keep exploring"))
        end
    end
    if collected == total and engine.touching(player, exit) then
        done = true
        engine.set_linear_velocity(player, 0, 0, 0)
        local trophy = engine.spawn("Trophy")
        local ex, ey, ez = engine.position(exit)
        engine.set_position(trophy, ex, ey + 2, ez)
        message("VICTORY! All crystals delivered - R to play again")
        return
    end
    local dx, dz = engine.input("move_x"), engine.input("move_z")
    local length = math.sqrt(dx * dx + dz * dz)
    if length > 1 then dx, dz = dx / length, dz / length end
    local vx, vy, vz = engine.linear_velocity(player)
    local speed, acceleration = 4.5, 30
    local function approach(value, target)
        return value + math.max(-acceleration * dt, math.min(acceleration * dt, target - value))
    end
    engine.set_linear_velocity(player, approach(vx, dx * speed), vy, approach(vz, dz * speed))
    local jump = engine.input("jump") > 0
    if jump and not jump_held and engine.grounded(player) then
        engine.apply_impulse(player, 0, 12, 0)
    end
    jump_held = jump
end
