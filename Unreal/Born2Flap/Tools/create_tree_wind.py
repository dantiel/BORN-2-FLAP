"""Wind-driven tree/foliage sway (World Position Offset), v4 lean-physics model.

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
wind response (a blind time-sinusoid). v4 gives every tree/foliage/bark/
grass/weed material one Custom node on World Position Offset with a
lean-physics model — a plant in a wind field is HELD bent downwind by the
mean wind, and only oscillates around that lean when gusts pass:

  * reads MPC_Weather.Wind (a world-space vector in m/s written each tick by
    ABorn2FlapWeather from Born2FlapWind) — the bend IS the same wind the
    bird flies through and the aero-audio hears;
  * CONSTANT MEAN LEAN: lean = s * LEAN * shape — the steady-wind component
    bends the plant downwind permanently, proportional to drag s (quadratic
    in wind speed, saturated at 8 m/s). Weak plants (grass, weeds) carry the
    largest LEAN relative to their height and lay over hardest; trunks are
    stiff (small lean * height^2 taper keeps their upper vertices low);
  * GUST SURGE: (gust - 0.5) * env * s * SURGE * shape — driven DIRECTLY by
    the frozen-turbulence gust field (Taylor's hypothesis: two octaves of
    value noise advected *backward* along the wind at wind speed), so a gust
    front sweeps through the forest, bends it further and releases it —
    neighbouring trees never surge in lock-step and lulls visibly settle;
  * RESIDUAL SWAY: a small size-dependent sinusoid (f = SWAY_HZ /
    sqrt(height in m)) whose amplitude rides the gust envelope, so plants
    keep a little aerodynamic flutter between gusts and swing more during
    them;
  * leaf flutter: fast, gust-modulated, only on the upper part of the plant;
  * bow-shortening, ground-clamped: the tip drops by ~amp^2 / (2 * height)
    as it bends, so a folded blade shortens like a real bent rod instead of
    stretching — but never sinks below its base (a laid-flat blade lies ON
    the ground, not under it);
  * bark and foliage tiers share LEAN/SURGE/SWAY/SWAY_HZ where trunk and
    canopy live on one mesh, so the two never separate at the seam.

The amplitude constants below are in centimetres (UE world units) at s = 1
(full 8 m/s+ wind, peak gust envelope); flutter/speed constants are in Hz.
Tweak them here and re-run to re-tune the look.

Idempotence: any existing node carrying a 'B2F TreeWind WPO v*' desc is
deleted first, so re-running always rebuilds the latest model.
"""
import unreal as u

lib = u.MaterialEditingLibrary
ela = u.EditorAssetLibrary
assets = u.AssetToolsHelpers.get_asset_tools()

MPC_PATH = '/Game/Weather/MPC_Weather'
MARKER = 'B2F TreeWind WPO v4'
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


def _flat_noise(pfx, p_expr):
    """Flat (no function definitions) 2D value noise, since UE injects Custom
    node code INSIDE the material function where 'float f(...){...}' is a
    compile error (M_CoastalBark cooked to Default Material). Inlines h21 +
    one octave of value noise under the given variable prefix; the result
    lands in `pfx + 'v'`."""
    C = 'float2(127.1,311.7)'
    return (
        'float2 %si = floor(%s);\n' % (pfx, p_expr) +
        'float2 %sf = frac(%s);\n' % (pfx, p_expr) +
        'float2 %su = %sf*%sf*(3.0-2.0*%sf);\n' % (pfx, pfx, pfx, pfx) +
        'float2 %sa = frac(%si*%s); %sa += dot(%sa, %sa+269.5);\n' % (pfx, pfx, C, pfx, pfx, pfx) +
        'float2 %sb = frac((%si+float2(1.0,0.0))*%s); %sb += dot(%sb, %sb+269.5);\n' % (pfx, pfx, C, pfx, pfx, pfx) +
        'float2 %sc = frac((%si+float2(0.0,1.0))*%s); %sc += dot(%sc, %sc+269.5);\n' % (pfx, pfx, C, pfx, pfx, pfx) +
        'float2 %sd = frac((%si+float2(1.0,1.0))*%s); %sd += dot(%sd, %sd+269.5);\n' % (pfx, pfx, C, pfx, pfx, pfx) +
        'float %sv = lerp(lerp(frac(%sa.x*%sa.y), frac(%sb.x*%sb.y), %su.x),\n' % (pfx, pfx, pfx, pfx, pfx, pfx) +
        '               lerp(frac(%sc.x*%sc.y), frac(%sd.x*%sd.y), %su.x), %su.y);\n' % (pfx, pfx, pfx, pfx, pfx, pfx)
    )


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


def add_weed_wpo(mpc, material, lean, surge, flutter_amp, flutter_hz):
    """Sway the Ravenstonefield wildflowers (stem + petal materials).

    Unlike trees these are NOT per-instance: every stem and petal lives in one
    big ProceduralMeshComponent spanning the whole meadow (300-650 cm of ground
    relief), so ObjectBounds height-normalisation cannot see a single plant's
    40-85 cm height.  Sway the whole plant as one rigid unit (identical
    displacement for stem and petal => flower heads never detach) and
    decorrelate per-plant via its world position, which is unique per flower.

    Weeds are WEAK: the constant lean dominates and scales purely with drag
    s = (len/8)^2, so the flowers are held over downwind for as long as the
    field blows; the advected gust noise surges them further and a small
    gust-modulated flutter keeps the heads alive. A gust front lays the
    meadow over in a wave, same frozen-turbulence field as the trees.
    """
    strip_old(material)
    wind = node(material, 'CollectionParameter', collection=mpc, parameter_name='Wind')
    wp = world_pos(material)
    t = node(material, 'Time')
    code = (
        'float3 w = float3(Wind.x, Wind.y, 0.0);\n'
        'float len = length(w);\n'
        'float s = pow(clamp(len * %f, 0.0, 1.0), 2.0);\n'
        'float3 d = len > 0.0001 ? w / len : float3(1.0, 0.0, 0.0);\n'
        'float2 pp = (float2(WP.x, WP.y) - d.xy * len * T * 0.5) * 0.0006;\n'
    ) % (SAT,)
    code += _flat_noise('n0', 'pp + T * 0.05')
    code += (
        'float env = 0.45 + 0.6 * n0v;\n'
        'float ph = WP.x * 0.013 + WP.y * 0.017;\n'
        'float leanb = s * %f;\n'
        'float surgeb = (env - 0.7) * 2.0 * s * %f;\n'
        'float flutter = sin(T * %f * 6.28318 + ph) * env * s * %f;\n'
        'float amp = leanb + surgeb + flutter;\n'
        'return float3(d.x, d.y, 0.0) * amp;'
    ) % (lean, surge, flutter_hz, flutter_amp)
    wpo = custom(material, code, {'Wind': wind, 'WP': wp, 'T': t}, 3)
    wpo.set_editor_property('desc', MARKER)
    output(wpo, 'WORLD_POSITION_OFFSET')
    return True


def add_wind_wpo(mpc, material, lean, surge, sway, flutter_amp, flutter_hz, wave, sway_hz, taper, gk):
    strip_old(material)
    wind = node(material, 'CollectionParameter', collection=mpc, parameter_name='Wind')
    wp = world_pos(material)
    op = node(material, 'ObjectPositionWS')
    ob = node(material, 'ObjectBounds')
    t = node(material, 'Time')
    code = (
        'float3 w = float3(Wind.x, Wind.y, 0.0);\n'
        'float len = length(w);\n'
        'float s = pow(clamp(len * %f, 0.0, 1.0), 2.0);\n'
        'float3 d = len > 0.0001 ? w / len : float3(1.0, 0.0, 0.0);\n'
        'float H = max(OB.z * 2.0, 1.0);\n'
        'float hn = saturate((WP.z - OP.z) / H);\n'
        'float2 xz = (float2(WP.x, WP.y) - d.xy * len * T) * %f;\n'
    ) % (SAT, gk)
    code += _flat_noise('n1', 'xz')
    code += _flat_noise('n2', 'xz * 2.7 - T * 0.05')
    code += (
        'float gust = n1v * 0.6 + n2v * 0.4;\n'
        'float env = 0.45 + 0.6 * gust;\n'
        'float ph = OP.x * 0.0013 + OP.y * 0.0017;\n'
        'float hz = %f * rsqrt(max(H * 0.01, 1.0));\n'
        'float shape = pow(hn, %f);\n'
        'float leanb = s * %f * shape;\n'
        'float surgeb = (gust - 0.5) * 2.0 * env * s * %f * shape;\n'
        'float swayb = sin(T * hz * 6.28318 + ph + hn * %f) * s * %f * shape * (0.35 + 0.65 * env);\n'
        'float flutter = sin(T * %f * 6.28318 + ph * 3.7 + hn * %f) * hn * s * %f * (0.35 + 0.65 * env);\n'
        'float amp = leanb + surgeb + swayb + flutter;\n'
        'float3 off = float3(d.x, d.y, 0.0) * amp;\n'
        'off.z -= min(0.5 * amp * amp / H * hn, H * hn);\n'
        'return off;'
    ) % (sway_hz, taper, lean, surge, wave, sway, flutter_hz, wave, flutter_amp)
    wpo = custom(material, code, {'Wind': wind, 'WP': wp, 'OP': op, 'OB': ob, 'T': t}, 3)
    wpo.set_editor_property('desc', MARKER)
    output(wpo, 'WORLD_POSITION_OFFSET')
    return True


# (path, lean cm, surge cm, sway cm, flutter cm, flutter Hz, wave phase lag,
#  sway Hz @1 m, taper, gust scale)
# Foliage/bark share the same LEAN/SURGE/SWAY and SWAY_HZ where trunk and
# canopy live on one mesh, so the two never separate at the seam; foliage
# alone carries the stronger leaf flutter.  Sway frequency f = SWAY_HZ /
# sqrt(height in m).  The lean is the CONSTANT downwind bend at full wind —
# trees carry a large lean because their height^2 taper confines it to the
# crown tips; the trunk's own vertices (low hn) barely move.
FOLIAGE = [
    ('/Game/Nature/M_fir_sapling_medium_Foliage', 150, 65, 38, 18, 1.3, 1.7, 1.1, 2.0, 0.00015),
    ('/Game/Nature/M_island_tree_01_Foliage', 170, 75, 42, 22, 1.2, 1.7, 1.0, 2.0, 0.00015),
    ('/Game/Nature/M_jacaranda_tree_Foliage', 165, 72, 40, 24, 1.3, 1.7, 1.0, 2.0, 0.00015),
    ('/Game/Nature/M_calathea_orbifolia_01_Foliage', 85, 38, 22, 15, 1.4, 1.6, 1.1, 2.0, 0.00015),
    ('/Game/Nature/M_anthurium_botany_01_Foliage', 80, 35, 20, 14, 1.5, 1.5, 1.1, 2.0, 0.00015),
    ('/Game/Nature/M_fern_02_Foliage', 95, 42, 24, 17, 1.5, 1.4, 1.2, 2.0, 0.00015),
    ('/Game/Ravenstonefield/Materials/M_AutumnLeaves', 165, 72, 40, 24, 1.3, 1.7, 1.0, 2.0, 0.00015),
    ('/Game/Shiomori/Materials/M_Bush', 105, 46, 26, 17, 1.4, 1.6, 1.2, 2.0, 0.00015),
    ('/Game/Shiomori/Materials/M_East_shrub_02', 95, 42, 24, 15, 1.4, 1.6, 1.2, 2.0, 0.00015),
    ('/Game/Shiomori/Materials/M_CampusBlossom', 115, 50, 28, 18, 1.3, 1.6, 1.1, 2.0, 0.00015),
    ('/Game/Shiomori/Materials/M_CampusBlossomLight', 115, 50, 28, 18, 1.3, 1.6, 1.1, 2.0, 0.00015),
    ('/Game/Shiomori/Materials/M_CampusPineNeedles', 95, 42, 24, 15, 1.5, 1.5, 1.1, 2.0, 0.00015),
    ('/Game/Shiomori/Materials/M_Crop', 120, 52, 30, 20, 1.4, 1.5, 1.2, 2.0, 0.00015),
    ('/Game/Training/M_Leaves', 140, 60, 34, 21, 1.3, 1.6, 1.1, 2.0, 0.00015),
]

# Grass hinges at the base (taper 1.0) and rides a finer gust scale — blades
# answer fast local turbulence, not the 60 m tree-gust eddies. Lean ~= blade
# height, so a monsoon lays the sward flat (the bow-shortening clamp stops
# the tips sinking below the base) while moderate wind holds a visible tilt.
GRASS = [
    ('/Game/Nature/M_grass_medium_01_Foliage', 55, 26, 12, 10, 1.7, 1.1, 1.4, 1.0, 0.0009),
    # Campus periwinkle: a weak creeping flower — the lean lays it over hard at
    # full wind, but its base (hn ~= 0) stays anchored.
    ('/Game/Nature/M_CampusPeriwinkle', 38, 18, 10, 8, 1.6, 1.3, 1.3, 1.0, 0.0009),
    ('/Game/Ravenstonefield/Materials/M_DryGrass', 52, 24, 11, 9, 1.7, 1.1, 1.4, 1.0, 0.0009),
    ('/Game/Ravenstonefield/Materials/M_ManagedGrass', 48, 22, 10, 8, 1.7, 1.1, 1.4, 1.0, 0.0009),
]

# Wildflowers: (path, lean cm, surge cm, flutter cm, flutter Hz). Stem and
# petals share identical constants so the flower head follows the stem top
# exactly. Weeds are weak — the lean lays them over hard at full wind.
WEEDS = [
    ('/Game/Ravenstonefield/Materials/M_FlowerStem', 45, 18, 8, 1.0),
    ('/Game/Ravenstonefield/Materials/M_FlowerWhite', 45, 18, 8, 1.0),
    ('/Game/Ravenstonefield/Materials/M_FlowerGold', 45, 18, 8, 1.0),
    ('/Game/Ravenstonefield/Materials/M_FlowerPurple', 45, 18, 8, 1.0),
]

BARK = [
    ('/Game/Nature/M_FirBark', 150, 65, 38, 5, 0.6, 2.2, 1.1, 2.0, 0.00015),
    ('/Game/Shiomori/Materials/M_CoastalBark', 140, 60, 34, 5, 0.6, 2.2, 1.1, 2.0, 0.00015),
    ('/Game/Ravenstonefield/Materials/M_tree_small_02_branch_diff', 140, 60, 32, 9, 0.8, 1.9, 1.0, 2.0, 0.00015),
    ('/Game/Ravenstonefield/Materials/M_tree_small_02_diff', 140, 60, 32, 4, 0.6, 2.2, 1.0, 2.0, 0.00015),
    ('/Game/Ravenstonefield/Materials/M_dead_tree_trunk_diff', 8, 5, 10, 2, 0.5, 2.4, 0.7, 2.0, 0.00015),
]


def main():
    mpc = ensure_wind_mpc()
    added = skipped = missing = 0
    for tier in (FOLIAGE, GRASS, BARK):
        for path, lean, surge, sway, flut, hz, wave, sway_hz, taper, gk in tier:
            if not ela.does_asset_exist(path):
                u.log_warning('B2F_TREEWIND missing material: ' + path)
                missing += 1
                continue
            m = u.load_asset(path)
            if not isinstance(m, u.Material):
                missing += 1
                continue
            try:
                changed = add_wind_wpo(mpc, m, lean, surge, sway, flut, hz, wave, sway_hz, taper, gk)
                if changed:
                    rc = lib.recompile_material(m)
                    if len(rc) != 0:
                        u.log_warning('B2F_TREEWIND compile errors in %s: %s' % (path, ' | '.join(rc)))
                    if not ela.save_loaded_asset(m):
                        raise RuntimeError('could not save ' + path)
                    added += 1
                    u.log('B2F_TREEWIND swayed ' + path)
                else:
                    skipped += 1
            except Exception as exc:
                u.log_warning('B2F_TREEWIND failed %s: %s' % (path, exc))
    for path, lean, surge, flut, hz in WEEDS:
        if not ela.does_asset_exist(path):
            u.log_warning('B2F_TREEWIND missing material: ' + path)
            missing += 1
            continue
        m = u.load_asset(path)
        if not isinstance(m, u.Material):
            missing += 1
            continue
        try:
            changed = add_weed_wpo(mpc, m, lean, surge, flut, hz)
            if changed:
                rc = lib.recompile_material(m)
                if len(rc) != 0:
                    u.log_warning('B2F_TREEWIND compile errors in %s: %s' % (path, ' | '.join(rc)))
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