"""Read-only probe: find all oversized/misscaled actors in the SHIOMORI map."""
import json
import unreal as u

sub = u.get_editor_subsystem(u.EditorActorSubsystem)
world = u.EditorLoadingAndSavingUtils.load_map('/Game/Shiomori/Maps/SHIOMORI')
assert world, 'map load failed'

rows = []
for a in sub.get_all_level_actors():
    lab = a.get_actor_label()
    s = a.get_actor_scale3d()
    loc = a.get_actor_location()
    smc = None
    try:
        smc = a.get_component_by_class(u.StaticMeshComponent)
    except Exception:
        pass
    mesh_path = ''
    box = None
    if smc and smc.static_mesh:
        mesh_path = smc.static_mesh.get_path_name()
        b = smc.static_mesh.get_bounds().box_extent
        box = [round(b.x, 1), round(b.y, 1), round(b.z, 1)]
    # world-space half-extents
    wx = (box[0] if box else 0) * s.x
    wy = (box[1] if box else 0) * s.y
    wz = (box[2] if box else 0) * s.z
    biggest = max(wx, wy, wz)
    if biggest >= 400.0:  # anything with a half-extent over 4 m
        rows.append(dict(label=lab, loc=[round(loc.x, 1), round(loc.y, 1), round(loc.z, 1)],
                         scale=[round(s.x, 3), round(s.y, 3), round(s.z, 3)],
                         mesh=mesh_path, mesh_box=box,
                         world_half=[round(wx, 1), round(wy, 1), round(wz, 1)]))
rows.sort(key=lambda r: -max(r['world_half']))
out = 'PROBE_OVERSIZE ' + json.dumps(rows)
u.log(out)
print(out)
u.SystemLibrary.execute_console_command(None, 'quit')
