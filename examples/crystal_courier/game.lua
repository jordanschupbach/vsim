-- Turn-based rules run entirely in VSim. The terminal client only supplies input and draws entities.
local player, status
local walls, crystals, traps = {}, {}, {}
local exit_x, exit_z
local moves, collected, total = 0, 0, 0
local finished = false

local function message(text)
    -- Messages contain fixed ASCII text and formatted integers, never external strings.
    engine.set(status, '{"name":"Status: ' .. text .. '"}')
end

local function at(id, x, z)
    local ex, _, ez = engine.position(id)
    return ex == x and ez == z
end

function on_start(self)
    for _, id in ipairs(engine.entities()) do
        local name = engine.name(id)
        if name == "Player" then
            assert(player == nil, "Expected one Player")
            player = id
        elseif string.sub(name, 1, 8) == "Status: " then
            assert(status == nil, "Expected one Status entity")
            status = id
        elseif name == "Wall" then
            table.insert(walls, id)
        elseif name == "Crystal" then
            table.insert(crystals, id)
        elseif name == "Trap" then
            table.insert(traps, id)
        elseif name == "Exit" then
            assert(exit_x == nil, "Expected one Exit")
            local x, _, z = engine.position(id)
            exit_x, exit_z = x, z
        end
    end
    assert(player and status and exit_x, "Game needs Player, Status, and Exit entities")
    total = #crystals
    message("Collect all " .. total .. " crystals, then reach E. Avoid X.")
end

function on_update(self, dt)
    if finished then return end
    local dx, dz = engine.input("move_x"), engine.input("move_z")
    if dx == 0 and dz == 0 then return end
    if (dx ~= 0 and dz ~= 0) or dx % 1 ~= 0 or dz % 1 ~= 0 then
        message("Move one tile along one axis.")
        return
    end
    local x, y, z = engine.position(player)
    x, z = x + dx, z + dz
    for _, wall in ipairs(walls) do
        if at(wall, x, z) then
            message("A wall blocks the way. Crystals: " .. collected .. "/" .. total)
            return
        end
    end
    moves = moves + 1
    engine.set_position(player, x, y, z)
    for _, trap in ipairs(traps) do
        if at(trap, x, z) then
            engine.destroy(player)
            message("Game over! You stepped on a trap after " .. moves .. " moves.")
            finished = true
            return
        end
    end
    for index = #crystals, 1, -1 do
        if at(crystals[index], x, z) then
            engine.destroy(crystals[index])
            table.remove(crystals, index)
            collected = collected + 1
        end
    end
    if x == exit_x and z == exit_z then
        if #crystals == 0 then
            local trophy = engine.spawn("Trophy")
            engine.set_position(trophy, x, y, z)
            message("Victory! Delivered all " .. total .. " crystals in " .. moves .. " moves.")
            finished = true
        else
            message("Exit locked: " .. #crystals .. " crystals remain.")
        end
    else
        message("Crystals: " .. collected .. "/" .. total .. " | Moves: " .. moves)
    end
end
