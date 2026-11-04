"""Wind-driven tree/foliage sway (World Position Offset).

Run after create_nature_assets.py / create_ravenstonefield_assets.py /
finish_weather_assets.py via UnrealEditor-Cmd -ExecutePythonScript (see
.setup/regen-tree-wind.ps1).

Ordering: also run AFTER rebuild_raven_workshop.py — that tool rebuilds the
Ravenstonefield meadow/field materials (M_ManagedGrass, M_FlowerStem,
M_FlowerWhite/Gold/Purple) and calls delete_all_material_expressions, which
wipes any World Position Offset applied here.

What this does
--------------
Trees are placed as plain static meshes; nothing but M_AutumnLeaves has any
wind response, and that one is a blind time-sinusoid with no wind direction or
strength. This tool gives every tree/foliage/bark material a single Custom node
on World Position Offset that bends vertices the way the atmospheric field
actually blows:

  * reads MPC_Weather.Wind (a world-space vector in m/s written each tick by
    ABorn2FlapWeather from Born2FlapWind) — so the sway IS the same wind the
    bird flies through and the aero-audio hears;
  * bends along the horizontal wind direction with a height^2 profile — the
    base is rooted (zero displacement), the crown whips the most;
  * adds a travelling-wave flutter whose phase lags with height and decorrelates
    per-object via the object's world origin, so neighbouring trees never sway
    in lock-step and the top lags the trunk like a real whip;
  * scales everything by wind strength (saturating at 10 m/s), so a calm day
    barely moves a leaf while a gale tears the canopy.

The amplitude/flutter constants below are in centimetres (UE world units) and
are per-material; tweak them here and re-run to re-tune the look.
"""
import unreal as u

lib = u.MaterialEditingLibrary
ela = u.EditorAssetLibrary
assets = u.AssetToolsHelpers.get_asset_tools()

MPC_PATH = '/Game/Weather/MPC_Weather'
MARKER = 'B2F TreeWind WPO v1'


def node(m, kind, **kw):
    n = lib.create_material_expression(m, getattr(u, 'MaterialExpression' + kind))
    for k, v in kw.items():
        n.set_editor_property(k, v)
    return n


def connect(a, b, pin, out=''):
    assert lib.connect_material_expressions(a, out, b, pin)


def output(n, prop, out=''):
    assert lib.connect_material_property(n, out, getattr(u.MaterialProperty, 'MP_' + prop))


def custom(m, code, inputs, size=1):
    n = node(m, 'Custom', code=code,
             output_type=getattr(u.CustomMaterialOutputType, 'CMOT_FLOAT' + (str(size) if size > 1 else '1')))
    entries = []
    for name in inputs:
        e = u.CustomInput()
        e.set_editor_property('input_name', name)
        entries.append(e)
    n.set_editor_property('inputs', entries)
    for name, source in inputs.items():
        connect(source, n, name)
    return n


def ensure_wind_mpc():
    mpc = u.load_asset(MPC_PATH) if ela.does_asset_exist(MPC_PATH) else \
        assets.create_asset('MPC_Weather', '/Game/Weather', u.MaterialParameterCollection,
                            u.MaterialParameterCollectionFactoryNew())
    # Vector param survives finish_weather_assets.py (which only rewrites the
    # scalar list), so a re-run of the weather surface migration won't drop it.
    vectors = list(mpc.get_editor_property('vector_parameters'))
    names = {str(p.get_editor_property('parameter_name')) for p in vectors}
    if 'Wind' not in names:
        p = u.CollectionVectorParameter()
        p.set_editor_property('parameter_name', 'Wind')
        p.set_editor_property('default_value', u.LinearColor(1.0, 0.0, 0.0, 0.0))
        vectors.append(p)
        mpc.set_editor_property('vector_parameters', vectors)
        ela.save_loaded_asset(mpc)
        u.log('B2F_TREEWIND added MPC_Weather.Wind vector parameter')
    return mpc


def add_weed_wpo(mpc, material, bend, flutter_amp, flutter_hz):
    """Sway the Ravenstonefield wildflowers (stem + petal materials).

    Unlike trees these are NOT per-instance: every stem and petal lives in one
    big ProceduralMeshComponent spanning the whole meadow (300-650 cm of ground
    relief), so ObjectBounds height-normalisation cannot see a single plant's
    40-85 cm height.  Sway the whole plant as one rigid unit (identical
    displacement for stem and petal => flower heads never detach) and
    decorrelate per-plant via its world position, which is unique per flower.
    """
    for e in lib.get_material_expressions(material):
        if e.get_editor_property('desc') == MARKER:
            return False  # already swayed
    wind = node(material, 'CollectionParameter', collection=mpc, parameter_name='Wind')
    wp = node(material, 'WorldPosition')
    t = node(material, 'Time')
    code = (
        'float3 w = float3(Wind.x, Wind.y, 0.0);\n'
        'float len = length(w);\n'
        'float s = clamp(len * 0.1, 0.0, 1.0);\n'
        'float3 d = len > 0.0001 ? w / len : float3(1.0, 0.0, 0.0);\n'
        'float ph = WP.x * 0.013 + WP.y * 0.017;\n'
        'float lean = s * %f;\n'
        'float rustle = sin(T * %f + ph) * s * %f;\n'
        'return float3(d.x, d.y, 0.0) * (lean + rustle);'
    ) % (bend, flutter_hz, flutter_amp)
    wpo = custom(material, code, {'Wind': wind, 'WP': wp, 'T': t}, 3)
    wpo.set_editor_property('desc', MARKER)
    output(wpo, 'WORLD_POSITION_OFFSET')
    return True


def add_wind_wpo(mpc, material, bend, flutter_amp, flutter_hz, wave):
    for e in lib.get_material_expressions(material):
        if e.get_editor_property('desc') == MARKER:
            return False  # already swayed
    wind = node(material, 'CollectionParameter', collection=mpc, parameter_name='Wind')
    wp = node(material, 'WorldPosition')
    op = node(material, 'ObjectPositionWS')
    ob = node(material, 'ObjectBounds')
    t = node(material, 'Time')
    code = (
        'float3 w = float3(Wind.x, Wind.y, 0.0);\n'
        'float len = length(w);\n'
        'float s = clamp(len * 0.1, 0.0, 1.0);\n'
        'float3 d = len > 0.0001 ? w / len : float3(1.0, 0.0, 0.0);\n'
        'float hn = saturate((WP.z - OP.z) / max(OB.z * 2.0, 1.0));\n'
        'float bend = hn * hn * s * %f;\n'
        'float ph = OP.x * 0.0013 + OP.y * 0.0017;\n'
        'float flutter = sin(T * %f + ph + hn * %f) * hn * s * %f;\n'
        'return float3(d.x, d.y, 0.0) * (bend + flutter);'
    ) % (bend, flutter_hz, wave, flutter_amp)
    wpo = custom(material, code, {'Wind': wind, 'WP': wp, 'OP': op, 'OB': ob, 'T': t}, 3)
    wpo.set_editor_property('desc', MARKER)
    output(wpo, 'WORLD_POSITION_OFFSET')
    return True


# (path, bend cm, flutter cm, flutter rad/s, wave phase lag)
# Foliage/bark share the same BEND where trunk and canopy live on one mesh so
# the two never separate at the seam; foliage alone carries the leaf flutter.
FOLIAGE = [
    ('/Game/Nature/M_fir_sapling_medium_Foliage', 30, 22, 5.0, 1.7),
    ('/Game/Nature/M_island_tree_01_Foliage', 40, 28, 5.0, 1.7),
    ('/Game/Nature/M_jacaranda_tree_Foliage', 40, 30, 5.0, 1.7),
    ('/Game/Nature/M_calathea_orbifolia_01_Foliage', 24, 18, 6.0, 1.6),
    ('/Game/Nature/M_anthurium_botany_01_Foliage', 22, 16, 6.5, 1.5),
    ('/Game/Nature/M_fern_02_Foliage', 24, 20, 6.5, 1.4),
    ('/Game/Ravenstonefield/Materials/M_AutumnLeaves', 40, 30, 5.0, 1.7),
    ('/Game/Shiomori/Materials/M_Bush', 28, 20, 5.5, 1.6),
    ('/Game/Shiomori/Materials/M_East_shrub_02', 26, 18, 5.5, 1.6),
    ('/Game/Shiomori/Materials/M_CampusBlossom', 30, 22, 5.0, 1.6),
    ('/Game/Shiomori/Materials/M_CampusBlossomLight', 30, 22, 5.0, 1.6),
    ('/Game/Shiomori/Materials/M_CampusPineNeedles', 26, 18, 6.0, 1.5),
    ('/Game/Shiomori/Materials/M_Crop', 28, 22, 5.5, 1.5),
    ('/Game/Training/M_Leaves', 34, 24, 5.0, 1.6),
]

GRASS = [
    ('/Game/Nature/M_grass_medium_01_Foliage', 20, 14, 8.0, 1.1),
    ('/Game/Ravenstonefield/Materials/M_DryGrass', 20, 14, 8.0, 1.1),
    ('/Game/Ravenstonefield/Materials/M_ManagedGrass', 16, 10, 8.0, 1.1),
]

# Wildflowers: (path, bend cm, flutter cm, flutter rad/s). Stem and petals share
# identical constants so the flower head follows the stem top exactly.
WEEDS = [
    ('/Game/Ravenstonefield/Materials/M_FlowerStem', 12, 6, 4.5),
    ('/Game/Ravenstonefield/Materials/M_FlowerWhite', 12, 6, 4.5),
    ('/Game/Ravenstonefield/Materials/M_FlowerGold', 12, 6, 4.5),
    ('/Game/Ravenstonefield/Materials/M_FlowerPurple', 12, 6, 4.5),
]

BARK = [
    ('/Game/Nature/M_FirBark', 30, 4, 2.5, 2.2),
    ('/Game/Shiomori/Materials/M_CoastalBark', 30, 4, 2.5, 2.2),
    ('/Game/Ravenstonefield/Materials/M_tree_small_02_branch_diff', 34, 10, 4.0, 1.9),
    ('/Game/Ravenstonefield/Materials/M_tree_small_02_diff', 30, 4, 2.5, 2.2),
    ('/Game/Ravenstonefield/Materials/M_dead_tree_trunk_diff', 8, 2, 2.0, 2.4),
]


def main():
    mpc = ensure_wind_mpc()
    added = skipped = missing = 0
    for tier in (FOLIAGE, GRASS, BARK):
        for path, bend, flut, hz, wave in tier:
            if not ela.does_asset_exist(path):
                u.log_warning('B2F_TREEWIND missing material: ' + path)
                missing += 1
                continue
            m = u.load_asset(path)
            if not isinstance(m, u.Material):
                missing += 1
                continue
            try:
                changed = add_wind_wpo(mpc, m, bend, flut, hz, wave)
                if changed:
                    lib.recompile_material(m)
                    if not ela.save_loaded_asset(m):
                        raise RuntimeError('could not save ' + path)
                    added += 1
                    u.log('B2F_TREEWIND swayed ' + path)
                else:
                    skipped += 1
            except Exception as exc:
                u.log_warning('B2F_TREEWIND failed %s: %s' % (path, exc))
    for path, bend, flut, hz in WEEDS:
        if not ela.does_asset_exist(path):
            u.log_warning('B2F_TREEWIND missing material: ' + path)
            missing += 1
            continue
        m = u.load_asset(path)
        if not isinstance(m, u.Material):
            missing += 1
            continue
        try:
            changed = add_weed_wpo(mpc, m, bend, flut, hz)
            if changed:
                lib.recompile_material(m)
                if not ela.save_loaded_asset(m):
                    raise RuntimeError('could not save ' + path)
                added += 1
                u.log('B2F_TREEWIND swayed ' + path)
            else:
                skipped += 1
        except Exception as exc:
            u.log_warning('B2F_TREEWIND failed %s: %s' % (path, exc))
    u.log('B2F_TREEWIND_READY added=%d skipped=%d missing=%d' % (added, skipped, missing))


if __name__ == '__main__':
    main()