-- Every exposed engine function must be exercised by this scene.
local used = {}
for name, fn in pairs(engine) do
    engine[name] = function(...)
        used[name] = true
        return fn(...)
    end
end

local updates = 0
local spawned

function on_start(self)
    assert(io == nil and os == nil and debug == nil and package == nil)
    assert(loadfile == nil and dofile == nil and print == nil)
    local temporary = engine.create("Temporary")
    assert(engine.alive(temporary))
    assert(engine.name(temporary) == "Temporary")
    assert(engine.input("unused_action") == 0)
    engine.set(temporary, '{"name":"Temporary camera","camera":{"vertical_fov":70},"transform":{"position":[1,2,3]}}')
    assert(string.find(engine.get(temporary), 'Temporary camera', 1, true))
    engine.set_position(temporary, 4, 5, 6)
    local x, y, z = engine.position(temporary)
    assert(x == 4 and y == 5 and z == 6)
    engine.set_rotation(temporary, 0, 0, 1, 0)
    assert(string.find(engine.get(temporary), '"rotation":[0.0,0.0,1.0,0.0]', 1, true))
    engine.set(temporary, '{"name":"Physics probe","mesh":{"primitive":"sphere"},"rigid_body":{"motion":"dynamic","shape":"sphere"},"transform":{"position":[0,3,0]}}')
    engine.set_linear_velocity(temporary, 1, 0, 0)
    local vx, vy, vz = engine.linear_velocity(temporary)
    assert(vx == 1 and vy == 0 and vz == 0)
    engine.apply_impulse(temporary, 0, 1, 0)
    assert(not engine.grounded(temporary))
    local other = engine.create("Other body")
    engine.set(other, '{"rigid_body":{},"mesh":{"primitive":"crystal"},"transform":{"position":[10,0,0]}}')
    assert(not engine.touching(temporary, other))
    engine.set_angular_velocity(temporary, 0, 2, 0)
    local wx, wy, wz = engine.angular_velocity(temporary)
    assert(wx == 0 and wy == 2 and wz == 0)
    -- A planar hinge pendulum: a capsule arm hanging from a weightless base body.
    local base = engine.create("Joint base")
    engine.set(base, '{"rigid_body":{"motion":"dynamic","gravity_scale":0},"transform":{"position":[0,8,0]}}')
    local arm = engine.create("Joint arm")
    engine.set(arm, '{"rigid_body":{"motion":"dynamic","shape":"capsule","gravity_scale":0,"allowed_translation":[true,true,false],"allowed_rotation":[false,false,true]},"transform":{"position":[0,7,0],"scale":[0.2,1,0.2]},"joint":{"type":"hinge","parent":' .. base .. ',"anchor":[0,0.5,0],"axis":[0,0,1],"limited":true,"limit_min":-1,"limit_max":1,"damping":0.1}}')
    local angle, rate = engine.joint_state(arm)
    assert(math.abs(angle) < 0.001 and math.abs(rate) < 0.001)
    engine.set_joint_state(arm, 0.5, 0.25)
    angle, rate = engine.joint_state(arm)
    assert(math.abs(angle - 0.5) < 0.01 and math.abs(rate - 0.25) < 0.01)
    engine.apply_joint_force(arm, 1)
    engine.destroy(arm)
    engine.destroy(base)
    engine.destroy(other)
    engine.destroy(temporary)
    assert(not engine.alive(temporary))
    engine.define_prefab("ScriptPrefab", '{"name":"Spawned from Lua","transform":{"scale":[2,2,2]},"camera":{},"mesh":{},"rigid_body":{}}')
    spawned = engine.spawn("ScriptPrefab")
    local found = false
    for _, id in ipairs(engine.entities()) do
        if id == spawned then found = true end
    end
    assert(found and engine.alive(self))
    for name, _ in pairs(engine) do
        assert(used[name], "conformance is missing API coverage: " .. name)
    end
end

function on_update(self, dt)
    assert(math.abs(dt - 1 / 60) < 0.0000001)
    updates = updates + 1
    engine.set_position(self, updates / 60, 0, 0)
    assert(engine.alive(spawned))
end
