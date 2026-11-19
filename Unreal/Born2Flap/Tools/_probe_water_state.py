"""Probe the current expression graph of M_Water / M_Shallows / M_BreakingSurf."""
import unreal as u

lib = u.MaterialEditingLibrary
ela = u.EditorAssetLibrary

for path in ['/Game/Shiomori/Materials/M_Water',
             '/Game/Shiomori/Materials/M_Shallows',
             '/Game/Weather/M_BreakingSurf']:
    m = u.load_asset(path)
    if not m:
        u.log('PROBE %s: MISSING' % path)
        continue
    exps = lib.get_material_expressions(m)
    try:
        tsn = str(m.get_editor_property('tangent_space_normal'))
    except Exception as ex:
        tsn = 'ERR:%s' % ex
    u.log('PROBE %s: %d expressions, blend=%s shading=%s tangent_space_normal=%s' % (
        path, len(exps), str(m.get_editor_property('blend_mode')),
        str(m.get_editor_property('shading_model')), tsn))
    customs = []
    for e in exps:
        tn = type(e).__name__
        if tn == 'MaterialExpressionCustom':
            try:
                code = e.get_editor_property('code') or ''
            except Exception:
                code = '?'
            first = code.strip().split('\n')[0] if code.strip() else '(empty)'
            customs.append(first)
        elif tn in ('MaterialExpressionCollectionParameter',
                    'MaterialExpressionTextureSample',
                    'MaterialExpressionTextureSampleParameter2D'):
            try:
                pn = e.get_editor_property('parameter_name')
            except Exception:
                pn = '?'
            u.log('  %s param=%s' % (tn, pn))
    u.log('  custom nodes=%d' % len(customs))
    for i, first in enumerate(customs):
        u.log('    custom[%d] first-line: %s' % (i, first))
    # Full code of the emissive/body custom (the one with the HDR body glow).
    for e in exps:
        if type(e).__name__ == 'MaterialExpressionCustom':
            try:
                code = e.get_editor_property('code') or ''
            except Exception:
                code = ''
            if 'water*2.4' in code or 'body=' in code or 'water*0.95' in code:
                for ln in code.split('\n'):
                    u.log('    EMISSIVE-LINE: %s' % ln)
                break
    for prop in ['BASE_COLOR', 'EMISSIVE_COLOR', 'OPACITY', 'NORMAL',
                 'WORLD_POSITION_OFFSET', 'REFRACTION']:
        mp = getattr(u.MaterialProperty, 'MP_' + prop)
        conn = lib.get_material_property_input_node(m, mp)
        u.log('  output %s -> %s' % (prop, type(conn).__name__ if conn else 'NONE'))
u.log('PROBE_DONE')