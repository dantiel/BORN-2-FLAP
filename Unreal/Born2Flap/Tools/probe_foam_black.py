"""Probe: which materials does SHIOMORI actually use for foam/water, and what is
connected to their EMISSIVE_COLOR? Prints a compact report for diagnosis."""
import unreal as u

lib = u.MaterialEditingLibrary
ela = u.EditorAssetLibrary

MATS = [
    '/Game/Shiomori/Materials/M_Water',
    '/Game/Shiomori/Materials/M_Shallows',
    '/Game/Shiomori/Materials/M_Foam',
    '/Game/Weather/M_BreakingSurf',
]


def custom_head(expr, n=160):
    try:
        code = expr.get_editor_property('code')
        return repr(code[:n])
    except Exception:
        return 'n/a'


for path in MATS:
    m = u.load_asset(path)
    if m is None:
        u.log('MAT %s MISSING' % path)
        continue
    try:
        bm = str(m.get_editor_property('blend_mode'))
    except Exception:
        bm = '?'
    try:
        sm = str(m.get_editor_property('shading_model'))
    except Exception:
        sm = '?'
    try:
        ts = m.get_editor_property('two_sided')
    except Exception:
        ts = '?'
    em = lib.get_material_property_input_node(m, u.MaterialProperty.MP_EMISSIVE_COLOR)
    bc = lib.get_material_property_input_node(m, u.MaterialProperty.MP_BASE_COLOR)
    u.log('MAT %s blend=%s shade=%s twosided=%s nexpr=%d' % (
        path, bm, sm, ts, len(lib.get_material_expressions(m))))
    u.log('  EMISSIVE <- %s %s' % (type(em).__name__ if em else 'NONE',
                                   custom_head(em) if em else ''))
    u.log('  BASE     <- %s %s' % (type(bc).__name__ if bc else 'NONE',
                                   custom_head(bc) if bc else ''))

# Scan the SHIOMORI level for actors using these materials.
try:
    world = u.EditorLevelLibrary.get_editor_world()
    u.log('WORLD %s' % world.get_name())
    import unreal as u2
    actors = u2.EditorLevelLibrary.get_all_level_actors()
    hit = {}
    for a in actors:
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
                        hit.setdefault(key, []).append(a.get_name())
    for (p, i), names in sorted(hit.items()):
        u.log('USE %s slot=%d actors=%d e.g. %s' % (p, i, len(names), names[:4]))
    u.log('USAGE_SCAN_DONE')
except Exception as e:
    u.log('SCAN_FAIL %s' % e)

u.log('FOAM_PROBE_DONE')
