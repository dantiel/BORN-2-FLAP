"""Diagnostic probe: dump M_BreakingSurf + M_Water expression graphs and
property wiring so we can see what the running game actually renders."""
import unreal as u

lib = u.MaterialEditingLibrary

def dump(matpath):
    m = u.load_asset(matpath)
    if not m:
        print('MISSING', matpath)
        return
    print('==== %s blend=%s shading=%s ====' % (matpath, m.get_editor_property('blend_mode'),
                                                 m.get_editor_property('shading_model')))
    exprs = lib.get_material_expressions(m)
    print('expressions:', len(exprs))
    for e in exprs:
        name = type(e).__name__
        desc = str(e.get_editor_property('desc'))
        extra = ''
        try:
            t = e.get_editor_property('texture')
            extra = ' tex=%s' % (t.get_name() if t else None)
        except Exception:
            pass
        try:
            code = e.get_editor_property('code')
            if code:
                extra += ' code=%r' % code[:160]
        except Exception:
            pass
        try:
            pn = e.get_editor_property('parameter_name')
            if pn:
                extra += ' param=%s' % pn
        except Exception:
            pass
        try:
            extra += ' const=%s' % e.get_editor_property('const')
        except Exception:
            pass
        print(' - %s%s%s' % (name, (' [%s]' % desc) if desc else '', extra))
    for prop in ['EMISSIVE_COLOR', 'OPACITY', 'BASE_COLOR', 'NORMAL', 'WORLD_POSITION_OFFSET']:
        try:
            node = lib.get_material_property_input_node(m, getattr(u.MaterialProperty, 'MP_' + prop))
            out = lib.get_material_property_input_node_output_name(m, getattr(u.MaterialProperty, 'MP_' + prop))
            print('  %s <- %s.%s' % (prop, type(node).__name__ if node else None, out))
        except Exception as ex:
            print('  %s <- ERROR %s' % (prop, ex))

dump('/Game/Weather/M_BreakingSurf')
dump('/Game/Shiomori/Materials/M_Water')
print('PROBE_DONE')
