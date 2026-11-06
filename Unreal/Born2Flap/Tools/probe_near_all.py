"""Read-only probe: ALL actors (any size) within 30 m of the founder statue."""
import json
import unreal as u

sub = u.get_editor_subsystem(u.EditorActorSubsystem)
world = u.EditorLoadingAndSavingUtils.load_map('/Game/Shiomori/Maps/SHIOMORI')
assert world, 'map load failed'

cx, cy = -70618.0, 14955.0
rows = []
for a in sub.get_all_level_actors():
    lab = a.get_actor_label()
    loc = a.get_actor_location()
    dx, dy = loc.x - cx, loc.y - cy
    if abs(dx) > 3000 or abs(dy) > 3000:
        continue
    s = a.get_actor_scale3d()
    comps = a.get_components_by_class(u.StaticMeshComponent)
    meshes = []
    for c in comps:
        if c.static_mesh:
            b = c.static_mesh.get_bounds().box_extent
            meshes.append(dict(m=c.static_mesh.get_name(),
                               box=[round(b.x, 1), round(b.y, 1), round(b.z, 1)]))
    rows.append(dict(label=lab, loc=[round(loc.x, 1), round(loc.y, 1), round(loc.z, 1)],
                     scale=[round(s.x, 2), round(s.y, 2), round(s.z, 2)], meshes=meshes))
out = 'PROBE_NEAR_ALL ' + json.dumps(rows)
u.log(out)
print(out)
u.SystemLibrary.execute_console_command(None, 'quit')
