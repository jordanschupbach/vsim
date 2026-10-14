-- Orbit the arena while looking at its center. The authored camera position
-- determines the starting angle, radius, and height; the lens stays unchanged.
local degrees_per_second = 6 -- One full orbit per minute. Set to 0 for a fixed view.
local target_x, target_y, target_z = 0, 0, 0
local angle, radius, height, pitch

function on_start(self)
    local x, y, z = engine.position(self)
    local dx, dz = x - target_x, z - target_z
    radius = math.sqrt(dx * dx + dz * dz)
    assert(radius > 0, "Orbit camera must be offset from the target on the XZ plane")
    height = y - target_y
    angle = math.atan(dx, dz)
    pitch = -math.atan(height, radius)
end

function on_update(self, dt)
    angle = (angle + math.rad(degrees_per_second) * dt) % (2 * math.pi)
    engine.set_position(self, target_x + radius * math.sin(angle),
                        target_y + height, target_z + radius * math.cos(angle))

    -- Yaw about world +Y, then pitch about local +X. Cameras look along -Z;
    -- the engine's quaternion order is w, x, y, z.
    local cy, sy = math.cos(angle / 2), math.sin(angle / 2)
    local cp, sp = math.cos(pitch / 2), math.sin(pitch / 2)
    engine.set_rotation(self, cy * cp, cy * sp, sy * cp, -sy * sp)
end
