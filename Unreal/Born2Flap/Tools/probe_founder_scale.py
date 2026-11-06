"""Read-only probe: print founder-related mesh bounds and actor scales."""
import json
import unreal as u

lib = u.EditorAssetLibrary
sub = u.get_editor_subsystem(u.EditorActorSubsystem)
report = {}

for p in ['/Game/Shiomori/Imported/Founder/Founder_posed/Founder_posed',
          '/Game/Shiomori/Meshes/SM_KestrelRaised',
          '/Game/Shiomori/Meshes/FounderRC_Bronze',
          '/Game/Shiomori/Meshes/FounderRC_Dark']:
    m = u.load_asset(p)
    if not m:
        report[p] = 'MISSING'
        continue
    b = m.get_bounds()
    report[p] = dict(
        origin=[round(v, 3) for v in (b.origin.x, b.origin.y, b.origin.z)],
        box=[round(v, 3) for v in (b.box_extent.x, b.box_extent.y, b.box_extent.z)],
        radius=round(b.sphere_radius, 3))

world = u.EditorLoadingAndSavingUtils.load_map('/Game/Shiomori/Maps/SHIOMORI')
assert world, 'map load failed'
acts = {}
for a in sub.get_all_level_actors():
    lab = a.get_actor_label()
    tags = [str(t) for t in a.tags]
    if 'founder' not in lab.lower() and 'CampusFounder' not in tags and 'FounderRC' not in tags:
        continue
    info = dict(loc=[round(v, 2) for v in (a.get_actor_location().x, a.get_actor_location().y, a.get_actor_location().z)],
                scale=[round(v, 4) for v in (a.get_actor_scale3d().x, a.get_actor_scale3d().y, a.get_actor_scale3d().z)])
    smc = getattr(a, 'static_mesh_component', None)
    if not smc:
        smc = a.get_component_by_class(u.StaticMeshComponent) if hasattr(a, 'get_component_by_class') else None
    if smc and smc.static_mesh:
        info['mesh'] = smc.static_mesh.get_path_name()
        bb = smc.static_mesh.get_bounds().box_extent
        info['mesh_box'] = [round(v, 2) for v in (bb.x, bb.y, bb.z)]
        info['world_height'] = round(2 * bb.z * info['scale'][2], 2)
    acts[lab] = info

out = 'PROBE_FOUNDER_SCALE ' + json.dumps(dict(meshes=report, actors=acts))
u.log(out)
print(out)
u.SystemLibrary.execute_console_command(None, 'quit')
