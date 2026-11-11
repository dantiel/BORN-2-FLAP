"""Wind-driven tree/foliage sway (World Position Offset), v2 gust-physics model.

Run after create_nature_assets.py / create_ravenstonefield_assets.py /
finish_weather_assets.py via UnrealEditor-Cmd -ExecutePythonScript (see
.setup/regen-tree-wind.ps1).

Ordering: also run AFTER rebuild_raven_workshop.py — that tool rebuilds the
Ravenstonefield meadow/field materials (M_ManagedGrass, M_FlowerStem,
M_FlowerWhite/Gold/Purple) and calls delete_all_material_expressions, which
wipes any World Position Offset applied here.

What this does
--------------
Trees are placed as plain static meshes; nothing but M_AutumnLeaves had any
wind response (a blind time-sinusoid). v1 of this tool gave every
tree/foliage/bark material one Custom node on World Position Offset with a
height^2 bend + travelling-wave flutter.  v2 replaces that with a
gust-driven sway model:

  * reads MPC_Weather.Wind (a world-space vector in m/s written each tick by
    ABorn2FlapWeather from Born2FlapWind) — so the sway IS the same wind the
    bird flies through and the aero-audio hears;
  * frozen-turbulence gust field (Taylor's hypothesis): two octaves of value
    noise elongated in the sample advected *backward* along the wind at wind
    speed, so eddies sweep through the forest as travelling gust fronts and
    neighbouring trees never sway in lock-step;
  * quadratic drag response s = (len/8 m/s)^2 saturated at 8 m/s — calm days
    barely move a leaf, a monsoon whips the canopy, matching real v^2 drag;
  * size-dependent sway frequency f = SWAY_HZ / sqrt(height in m): a 12 m fir
    rocks at ~0.3 Hz while a 50 cm grass blade whips at ~2 Hz;
  * gust envelope (0.45..~1.05) modulates both the slow sway and the leaf
    flutter, so trees visibly surge during gusts and settle between them;
  * per-tier taper exponent: trees bend with height^2 (stiff trunk), grass
    hinges at the base (linear), wildflowers stay rigid;
  * bow-shortening: the tip drops by ~amp^2 / (2 * height) as it leans, so a
    folded blade shortens like a real bent rod instead of stretching;
  * bark and foliage tiers share BEND/SWAY_HZ where trunk and canopy live on
    one mesh, so the two never separate at the seam.

The amplitude constants below are in centimetres (UE world units) at s = 1
(full 8 m/s+ wind, peak gust envelope); flutter/speed constants are in Hz.
Tweak them here and re-run to re-tune the look.

Idempotence: any existing node carrying a 'B2F TreeWind WPO v*' desc is
deleted first, so re-running always rebuilds the latest model (v1 -> v2
included).
"""
import unreal as u

lib = u.MaterialEditingLibrary
ela = u.EditorAssetLibrary
assets = u.AssetToolsHelpers.get_asset_tools()

MPC_PATH = '/Game/Weather/MPC_Weather'
MARKER = 'B2F TreeWind WPO v2'
STRIP = 'B2F TreeWind WPO v'

# Saturation: full sway at 8 m/s (quadratic below — drag ~ v^2).
SAT = 0.125


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


def world_pos(material):
    wp = node(material, 'WorldPosition')
    # Absolute (non-camera-relative) position keeps the advected gust field
    # anchored to the world: with the camera-relative default the whole noise
    # field would swim against the camera whenever the bird moves fast.
    try:
        wp.set_editor_property('world_position_shader_offset', u.WorldPositionIncludedOffsets.WPT_DEFAULT)
    except Exception:
        pass  # older API name: stays camera-relative (visual only, minor drift)
    return wp


def strip_old(material):
    stale = [e for e in lib.get_material_expressions(material)
             if (e.get_editor_property('desc') or '').startswith(STRIP)]
    for e in stale:
        try:
            lib.delete_material_expression(material, e)
        except Exception:
            pass  # dangling node is harmless; the new connect below replaces it
    return len(stale)


def add_weed_wpo(mpc, material, bend, flutter_amp, flutter_hz):
    """Sway the Ravenstonefield wildflowers (stem + petal materials).

    Unlike trees these are NOT per-instance: every stem and petal lives in one
    big ProceduralMeshComponent spanning the whole meadow (300-650 cm of ground
    relief), so ObjectBounds height-normalisation cannot see a single plant's
    40-85 cm height.  Sway the whole plant as one rigid unit (identical
    displacement for stem and petal => flower heads never detach) and
    decorrelate per-plant via its world position, which is unique per flower.
    Gust envelope rides the same advected noise field as the trees, so a gust
    front lays the meadow over in a wave.
    """
    strip_old(material)
    wind = node(material, 'CollectionParameter', collection=mpc, parameter_name='Wind')
    wp = world_pos(material)
    t = node(material, 'Time')
    code = (
        'float h21(float2 p){ p=frac(p*float2(127.1,311.7)); p+=dot(p,p+269.5); return frac(p.x*p.y); }\n'
        'float vnoise2(float2 p){\n'
        '    float2 i=floor(p), f=frac(p);\n'
        '    float2 u=f*f*(3.0-2.0*f);\n'
        '    float a=h21(i), b=h21(i+float2(1.0,0.0)), c=h21(i+float2(0.0,1.0)), d=h21(i+float2(1.0,1.0));\n'
        '    return lerp(lerp(a,b,u.x), lerp(c,d,u.x), u.y);\n'
        '}\n'
        'float3 w = float3(Wind.x, Wind.y, 0.0);\n'
        'float len = length(w);\n'
        'float s = pow(clamp(len * %f, 0.0, 1.0), 2.0);\n'
        'float3 d = len > 0.0001 ? w / len : float3(1.0, 0.0, 0.0);\n'
        'float2 pp = (float2(WP.x, WP.y) - d.xy * len * T) * 0.0006;\n'
        'float env = 0.45 + 0.6 * vnoise2(pp + T * 0.05);\n'
        'float ph = WP.x * 0.013 + WP.y * 0.017;\n'
        'float amp = s * (%f * (0.55 + 0.45 * env) + sin(T * %f * 6.28318 + ph) * %f * env);\n'
        'return float3(d.x, d.y, 0.0) * amp;'
    ) % (SAT, bend, flutter_hz, flutter_amp)
    wpo = custom(material, code, {'Wind': wind, 'WP': wp, 'T': t}, 3)
    wpo.set_editor_property('desc', MARKER)
    output(wpo, 'WORLD_POSITION_OFFSET')
    return True


def add_wind_wpo(mpc, material, bend, flutter_amp, flutter_hz, wave, sway_hz, taper, gk):
    strip_old(material)
    wind = node(material, 'CollectionParameter', collection=mpc, parameter_name='Wind')
    wp = world_pos(material)
    op = node(material, 'ObjectPositionWS')
    ob = node(material, 'ObjectBounds')
    t = node(material, 'Time')
    code = (
        'float h21(float2 p){ p=frac(p*float2(127.1,311.7)); p+=dot(p,p+269.5); return frac(p.x*p.y); }\n'
        'float vnoise2(float2 p){\n'
        '    float2 i=floor(p), f=frac(p);\n'
        '    float2 u=f*f*(3.0-2.0*f);\n'
        '    float a=h21(i), b=h21(i+float2(1.0,0.0)), c=h21(i+float2(0.0,1.0)), d=h21(i+float2(1.0,1.0));\n'
        '    return lerp(lerp(a,b,u.x), lerp(c,d,u.x), u.y);\n'
        '}\n'
        'float3 w = float3(Wind.x, Wind.y, 0.0);\n'
        'float len = length(w);\n'
        'float s = pow(clamp(len * %f, 0.0, 1.0), 2.0);\n'
        'float3 d = len > 0.0001 ? w / len : float3(1.0, 0.0, 0.0);\n'
        'float H = max(OB.z * 2.0, 1.0);\n'
        'float hn = saturate((WP.z - OP.z) / H);\n'
        'float2 xz = (float2(WP.x, WP.y) - d.xy * len * T) * %f;\n'
        'float gust = vnoise2(xz) * 0.68 + vnoise2(xz * 2.7 - T * 0.05) * 0.32;\n'
        'float env = 0.45 + 0.6 * gust;\n'
        'float ph = OP.x * 0.0013 + OP.y * 0.0017;\n'
        'float hz = %f * rsqrt(max(H * 0.01, 1.0));\n'
        'float shape = pow(hn, %f);\n'
        'float sway = sin(T * hz * 6.28318 + ph + hn * %f) * env * s * %f * shape;\n'
        'float flutter = sin(T * %f * 6.28318 + ph * 3.7 + hn * %f) * hn * s * %f * (0.45 + 0.55 * env);\n'
        'float amp = sway + flutter;\n'
        'float3 off = float3(d.x, d.y, 0.0) * amp;\n'
        'off.z -= 0.5 * amp * amp / H * hn;\n'
        'return off;'
    ) % (SAT, gk, sway_hz, taper, wave, bend, flutter_hz, wave, flutter_amp)
    wpo = custom(material, code, {'Wind': wind, 'WP': wp, 'OP': op, 'OB': ob, 'T': t}, 3)
    wpo.set_editor_property('desc', MARKER)
    output(wpo, 'WORLD_POSITION_OFFSET')
    return True


# (path, bend cm, flutter cm, flutter Hz, wave phase lag, sway Hz @1 m, taper, gust scale)
# Foliage/bark share the same BEND and SWAY_HZ where trunk and canopy live on
# one mesh, so the two never separate at the seam; foliage alone carries the
# stronger leaf flutter.  Sway frequency f = SWAY_HZ / sqrt(height in m).
FOLIAGE = [
    ('/Game/Nature/M_fir_sapling_medium_Foliage', 95, 26, 1.3, 1.7, 1.1, 2.0, 0.00015),
    ('/Game/Nature/M_island_tree_01_Foliage', 125, 32, 1.2, 1.7, 1.0, 2.0, 0.00015),
    ('/Game/Nature/M_jacaranda_tree_Foliage', 120, 34, 1.3, 1.7, 1.0, 2.0, 0.00015),
    ('/Game/Nature/M_calathea_orbifolia_01_Foliage', 65, 22, 1.4, 1.6, 1.1, 2.0, 0.00015),
    ('/Game/Nature/M_anthurium_botany_01_Foliage', 60, 20, 1.5, 1.5, 1.1, 2.0, 0.00015),
    ('/Game/Nature/M_fern_02_Foliage', 75, 24, 1.5, 1.4, 1.2, 2.0, 0.00015),
    ('/Game/Ravenstonefield/Materials/M_AutumnLeaves', 125, 34, 1.3, 1.7, 1.0, 2.0, 0.00015),
    ('/Game/Shiomori/Materials/M_Bush', 80, 24, 1.4, 1.6, 1.2, 2.0, 0.00015),
    ('/Game/Shiomori/Materials/M_East_shrub_02', 75, 22, 1.4, 1.6, 1.2, 2.0, 0.00015),
    ('/Game/Shiomori/Materials/M_CampusBlossom', 90, 26, 1.3, 1.6, 1.1, 2.0, 0.00015),
    ('/Game/Shiomori/Materials/M_CampusBlossomLight', 90, 26, 1.3, 1.6, 1.1, 2.0, 0.00015),
    ('/Game/Shiomori/Materials/M_CampusPineNeedles', 75, 22, 1.5, 1.5, 1.1, 2.0, 0.00015),
    ('/Game/Shiomori/Materials/M_Crop', 95, 28, 1.4, 1.5, 1.2, 2.0, 0.00015),
    ('/Game/Training/M_Leaves', 110, 30, 1.3, 1.6, 1.1, 2.0, 0.00015),
]

# Grass hinges at the base (taper 1.0) and rides a finer gust scale — blades
# answer fast local turbulence, not the 60 m tree-gust eddies.
GRASS = [
    ('/Game/Nature/M_grass_medium_01_Foliage', 48, 16, 1.7, 1.1, 1.4, 1.0, 0.0009),
    ('/Game/Ravenstonefield/Materials/M_DryGrass', 48, 16, 1.7, 1.1, 1.4, 1.0, 0.0009),
    ('/Game/Ravenstonefield/Materials/M_ManagedGrass', 42, 13, 1.7, 1.1, 1.4, 1.0, 0.0009),
]

# Wildflowers: (path, bend cm, flutter cm, flutter Hz). Stem and petals share
# identical constants so the flower head follows the stem top exactly.
WEEDS = [
    ('/Game/Ravenstonefield/Materials/M_FlowerStem', 26, 12, 1.0),
    ('/Game/Ravenstonefield/Materials/M_FlowerWhite', 26, 12, 1.0),
    ('/Game/Ravenstonefield/Materials/M_FlowerGold', 26, 12, 1.0),
    ('/Game/Ravenstonefield/Materials/M_FlowerPurple', 26, 12, 1.0),
]

BARK = [
    ('/Game/Nature/M_FirBark', 95, 5, 0.6, 2.2, 1.1, 2.0, 0.00015),
    ('/Game/Shiomori/Materials/M_CoastalBark', 85, 5, 0.6, 2.2, 1.1, 2.0, 0.00015),
    ('/Game/Ravenstonefield/Materials/M_tree_small_02_branch_diff', 95, 12, 0.8, 1.9, 1.0, 2.0, 0.00015),
    ('/Game/Ravenstonefield/Materials/M_tree_small_02_diff', 95, 5, 0.6, 2.2, 1.0, 2.0, 0.00015),
    ('/Game/Ravenstonefield/Materials/M_dead_tree_trunk_diff', 15, 2, 0.5, 2.4, 0.7, 2.0, 0.00015),
]


def main():
    mpc = ensure_wind_mpc()
    added = skipped = missing = 0
    for tier in (FOLIAGE, GRASS, BARK):
        for path, bend, flut, hz, wave, sway_hz, taper, gk in tier:
            if not ela.does_asset_exist(path):
                u.log_warning('B2F_TREEWIND missing material: ' + path)
                missing += 1
                continue
            m = u.load_asset(path)
            if not isinstance(m, u.Material):
                missing += 1
                continue
            try:
                changed = add_wind_wpo(mpc, m, bend, flut, hz, wave, sway_hz, taper, gk)
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
