"""Probe 2: load SHIOMORI and scan which actors use M_Foam / M_BreakingSurf /
M_Water / M_Shallows, plus any other translucent materials near the waterline."""
import unreal as u

MATS = [
    '/Game/Shiomori/Materials/M_Water',
    '/Game/Shiomori/Materials/M_Shallows',
    '/Game/Shiomori/Materials/M_Foam',
    '/Game/Weather/M_BreakingSurf',
]

u.EditorLevelLibrary.load_level('/Game/Shiomori/Maps/SHIOMORI')
world = u.EditorLevelLibrary.get_editor_world()
u.log('WORLD %s' % world.get_name())
actors = u.EditorLevelLibrary.get_all_level_actors()
u.log('ACTOR_COUNT %d' % len(actors))
hit = {}
other_trans = {}
for a in actors:
    try:
        loc = a.get_actor_location()
    except Exception:
        loc = None
    comps = a.get_components_by_class(u.StaticMeshComponent)
    if not comps:
        continue
    for c in comps:
        for i in range(c.get_num_materials()):
            mat = c.get_material(i)
            if mat is None:
                continue
            p = mat.get_path_name()
            for want in MATS:
                if p == want:
                    key = (p, i)
                    hit.setdefault(key, []).append((a.get_name(), loc))
            try:
                if str(mat.get_editor_property('blend_mode')).startswith('<BlendMode.BLEND_TRANSLUCENT'):
                    other_trans.setdefault(p, []).append((a.get_name(), loc))
            except Exception:
                pass
for (p, i), lst in sorted(hit.items()):
    u.log('USE %s slot=%d n=%d' % (p, i, len(lst)))
    for name, loc in lst[:6]:
        u.log('    %s @ %s' % (name, loc))
u.log('OTHER_TRANSLUCENT:')
for p, lst in sorted(other_trans.items()):
    if p not in MATS:
        u.log('  %s n=%d e.g. %s' % (p, len(lst), lst[:3]))
u.log('FOAM_PROBE2_DONE')
