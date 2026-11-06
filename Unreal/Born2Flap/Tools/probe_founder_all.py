"""Read-only probe: list ALL founder-related actors incl. stale mesh references."""
import json
import unreal as u

sub = u.get_editor_subsystem(u.EditorActorSubsystem)
world = u.EditorLoadingAndSavingUtils.load_map('/Game/Shiomori/Maps/SHIOMORI')
assert world, 'map load failed'

rows = []
for a in sub.get_all_level_actors():
    lab = a.get_actor_label()
    if 'founder' not in lab.lower() and 'kestrel' not in lab.lower():
        continue
    loc = a.get_actor_location()
    s = a.get_actor_scale3d()
    comps = a.get_components_by_class(u.StaticMeshComponent)
    meshes = []
    for c in comps:
        if c.static_mesh:
            meshes.append(c.static_mesh.get_path_name())
        else:
            # component exists but mesh empty -> stale/cleared reference?
            meshes.append('<EMPTY>')
    rows.append(dict(label=lab, loc=[round(loc.x, 1), round(loc.y, 1), round(loc.z, 1)],
                     scale=[round(s.x, 3), round(s.y, 3), round(s.z, 3)],
                     meshes=meshes))
out = 'PROBE_FOUNDER_ALL ' + json.dumps(rows)
u.log(out)
print(out)
u.SystemLibrary.execute_console_command(None, 'quit')
