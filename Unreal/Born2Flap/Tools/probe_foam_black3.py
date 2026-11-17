"""Probe 3: dump M_SurfDroplet + M_MorningMist + all Weather materials' blend/
shading/emissive, and NS_SurfSpray's renderer sprite material path."""
import unreal as u

lib = u.MaterialEditingLibrary
ela = u.EditorAssetLibrary

MATS = [
    '/Game/Weather/M_SurfDroplet',
    '/Game/Weather/M_MorningMist',
    '/Game/Weather/M_BreakingSurf',
]


def head(expr, n=140):
    if expr is None:
        return 'NONE'
    try:
        return type(expr).__name__ + ' ' + repr(expr.get_editor_property('code')[:n])
    except Exception:
        return type(expr).__name__ + ' n/a'


for path in MATS:
    m = u.load_asset(path)
    if m is None:
        u.log('MISSING %s' % path)
        continue
    bm = str(m.get_editor_property('blend_mode'))
    sm = str(m.get_editor_property('shading_model'))
    em = lib.get_material_property_input_node(m, u.MaterialProperty.MP_EMISSIVE_COLOR)
    bc = lib.get_material_property_input_node(m, u.MaterialProperty.MP_BASE_COLOR)
    op = lib.get_material_property_input_node(m, u.MaterialProperty.MP_OPACITY)
    u.log('MAT %s blend=%s shade=%s' % (path, bm, sm))
    u.log('  EMISSIVE <- %s' % head(em))
    u.log('  BASE     <- %s' % head(bc))
    u.log('  OPACITY  <- %s' % head(op))

# Niagara sprite materials
for syspath in ['/Game/Weather/NS_SurfSpray', '/Game/Weather/NS_SurfMist']:
    sys_ = u.load_asset(syspath)
    if sys_ is None:
        u.log('MISSING %s' % syspath)
        continue
    emitters = sys_.get_editor_property('emitter_handles')
    for eh in emitters:
        try:
            e = eh.get_editor_property('instance')
            name = e.get_editor_property('emitter_name')
            rends = e.get_editor_property('renderer_properties')
            for r in rends:
                u.log('SYS %s emitter=%s renderer=%s' % (syspath, name, type(r).__name__))
                try:
                    mat = r.get_editor_property('sprite_material')
                    u.log('    sprite_material=%s' % (mat.get_path_name() if mat else 'NONE'))
                except Exception as ex:
                    u.log('    no sprite_material: %s' % ex)
        except Exception as ex:
            u.log('SYS %s emitter read failed: %s' % (syspath, ex))

u.log('FOAM_PROBE3_DONE')
