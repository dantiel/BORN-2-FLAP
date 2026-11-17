"""Read-only probe: diagnose black foam.
1) M_BreakingSurf state (shading model, blend, property inputs, expressions).
2) SHIOMORI map: persisted WaterBodyOcean / Born2FlapSurf actors.
"""
import unreal as u

lib = u.MaterialEditingLibrary
ela = u.EditorAssetLibrary

report = []

def rpt(s):
    report.append(s)
    u.log('PROBE_SURF: ' + s)

# Load the Shiomori map so the actor scan sees persisted level actors.
from unreal import EditorLevelLibrary as ELL
try:
    ELL.load_level('/Game/Shiomori/Maps/SHIOMORI')
    rpt('map loaded OK')
except Exception as ex:
    rpt('map load ERROR %s' % ex)
    try:
        u.EditorLoadingAndSavingUtils.load_map('/Game/Shiomori/Maps/SHIOMORI')
        rpt('map loaded OK (fallback)')
    except Exception as ex2:
        rpt('map load fallback ERROR %s' % ex2)

# --- 1) material ----------------------------------------------------------
p = '/Game/Weather/M_BreakingSurf'
m = u.load_asset(p)
if not m:
    rpt('M_BreakingSurf MISSING')
else:
    rpt('class=%s' % type(m).__name__)
    rpt('shading_model=%s' % m.get_editor_property('shading_model'))
    rpt('blend_mode=%s' % m.get_editor_property('blend_mode'))
    rpt('two_sided=%s' % m.get_editor_property('two_sided'))
    rpt('translucency_pass=%s' % m.get_editor_property('translucency_pass'))
    exprs = lib.get_material_expressions(m)
    rpt('expressions=%d: %s' % (len(exprs), ', '.join(sorted(type(e).__name__ for e in exprs))))
    for prop in ('EMISSIVE_COLOR', 'OPACITY', 'BASE_COLOR'):
        try:
            inp = lib.get_material_property_input_node(m, getattr(u.MaterialProperty, 'MP_' + prop))
            rpt('input %s -> %s' % (prop, type(inp).__name__ if inp else 'NONE'))
        except Exception as ex:
            rpt('input %s ERROR %s' % (prop, ex))
    # texture params of the noise nodes
    # Force a recompile of the CURRENT graph and wait long enough for the async
    # shader compiler to finish, so any SM6/SM5 type errors land in the log
    # AFTER this marker. Grep the log for "Failed to compile" past
    # PROBE_RECOMPILE_SENT to verify the saved graph itself.
    lib.recompile_material(m)
    u.log('PROBE_RECOMPILE_SENT')
    for e in exprs:
        t = type(e).__name__
        if t == 'MaterialExpressionNoise':
            try:
                rpt('noise levels=%s scale=%s' % (e.get_editor_property('levels'), e.get_editor_property('scale')))
            except Exception as ex:
                rpt('noise props ERROR %s' % ex)
        elif t == 'MaterialExpressionComponentMask':
            try:
                rpt('mask %s: r=%s g=%s b=%s a=%s' % (e.get_name(),
                    e.get_editor_property('r'), e.get_editor_property('g'),
                    e.get_editor_property('b'), e.get_editor_property('a')))
            except Exception as ex:
                rpt('mask props ERROR %s' % ex)
        elif t == 'MaterialExpressionMultiply':
            try:
                rpt('multiply const_b=%s' % e.get_editor_property('const_b'))
            except Exception as ex:
                rpt('multiply props ERROR %s' % ex)

# --- 2) map actors --------------------------------------------------------
from unreal import EditorLevelLibrary as ELL
try:
    actors = ELL.get_all_level_actors()
    rpt('map actors=%d' % len(actors))
    for a in actors:
        n = a.get_class().get_name()
        if 'WaterBody' in n or 'Born2FlapSurf' in n or 'Born2FlapWater' in n:
            rpt('actor %s %s' % (a.get_name(), n))
except Exception as ex:
    rpt('map scan ERROR %s' % ex)

import time
time.sleep(45)
with open('V:/BORN 2 FLAP/_probe_surf_report.txt', 'w', encoding='utf-8') as f:
    f.write('\n'.join(report))
u.log('PROBE_SURF_DONE')