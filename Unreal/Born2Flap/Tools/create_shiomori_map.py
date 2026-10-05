"""Reproducible Shiomori Bay: a quiet, near-future urban waterfront.

Sterile-white pass: bright, almost clinical architecture with cloud-like open
station canopies, a grey-yellowed industrial hinterland, and an animated sea
with long parallel swells rolling shoreward. Sand, concrete and rock are
layered PBR (CC0 + world-space tiling). Layout, tags, collision points and
actor counts that the ShiomoriTest in Born2FlapGameMode relies on are preserved.
"""
import math
import random
from pathlib import Path
import unreal as u

rng = random.Random(290928)
assets = u.AssetToolsHelpers.get_asset_tools()
lib = u.MaterialEditingLibrary
ela = u.EditorAssetLibrary
ROOT = Path(__file__).resolve().parents[1] / 'Saved/ShiomoriSource'

# --------------------------------------------------------------------------- #
# Material graph helpers (mirror the proven create_nature_assets.py patterns). #
# --------------------------------------------------------------------------- #

def node(m, kind, **props):
    n = lib.create_material_expression(m, getattr(u, 'MaterialExpression' + kind))
    for k, v in props.items():
        n.set_editor_property(k, v)
    return n


def wire(a, b, pin, output=''):
    if not output and isinstance(a, u.MaterialExpressionWorldPosition):
        output = 'XYZ'
    assert lib.connect_material_expressions(a, output, b, pin), 'Invalid material connection: %s -> %s.%s' % (type(a).__name__, type(b).__name__, pin)


def output(n, prop, pin=''):
    assert lib.connect_material_property(n, pin, getattr(u.MaterialProperty, 'MP_' + prop)), 'Invalid material output: ' + prop


def custom(m, code, inputs, size=3):
    """Custom HLSL node with named inputs (mirrors create_ravenstonefield_assets.py)."""
    n = node(m, 'Custom', code=code,
             output_type=getattr(u.CustomMaterialOutputType,
                                 'CMOT_FLOAT' + (str(size) if size > 1 else '1')))
    pins = []
    for k in inputs:
        pin = u.CustomInput()
        pin.set_editor_property('input_name', k)
        pins.append(pin)
    n.set_editor_property('inputs', pins)
    for k, v in inputs.items():
        wire(v, n, k)
    return n


def load_texture(subdir, stem, name, normal=False, srgb=True):
    src = ROOT / subdir / (stem + '.jpg')
    if not src.exists():
        raise RuntimeError('Missing source texture: ' + str(src))
    path = '/Game/Shiomori/Textures/' + name
    if ela.does_asset_exist(path):
        return u.load_asset(path)
    ela.make_directory('/Game/Shiomori/Textures')
    task = u.AssetImportTask()
    task.filename = str(src)
    task.destination_path = '/Game/Shiomori/Textures'
    task.destination_name = name
    task.automated = True
    task.replace_existing = True
    task.save = True
    assets.import_asset_tasks([task])
    tex = u.load_asset(path)
    if not tex:
        raise RuntimeError('Failed to import texture ' + name)
    tex.set_editor_property('srgb', srgb)
    if normal:
        tex.set_editor_property('compression_settings', u.TextureCompressionSettings.TC_NORMALMAP)
    assert ela.save_asset(path), 'Failed to save ' + path
    return tex


def radio():
    """Import the two user-supplied beach songs as SoundWave assets."""
    ela.make_directory('/Game/Shiomori/Audio')
    for source_name, asset_name in [('Shiomori Bay I', 'SHIOMORI_BAY_I'),
                                    ('Shiomori Bay II', 'SHIOMORI_BAY_II')]:
        source = Path.home() / ('Downloads/' + source_name + '.mp3')
        if not source.exists():
            u.log_warning('Shiomori radio source missing: ' + str(source))
            continue
        path = '/Game/Shiomori/Audio/' + asset_name
        if not ela.does_asset_exist(path):
            task = u.AssetImportTask()
            task.filename = str(source)
            task.destination_path = '/Game/Shiomori/Audio'
            task.destination_name = asset_name
            task.automated = True
            task.replace_existing = True
            task.save = True
            assets.import_asset_tasks([task])
        wave = u.load_asset(path)
        if not isinstance(wave, u.SoundWave):
            u.log_warning('Shiomori radio track not a SoundWave: ' + asset_name)
            continue
        wave.set_editor_property('looping', False)
        wave.set_editor_property('volume', 0.55)
        assert ela.save_asset(path), 'Failed to save ' + path
        u.log('SHIOMORI_RADIO track=' + asset_name + ' duration=' + str(wave.duration))


def sample_tex(m, tex, tiling=None, normal=False):
    """TextureSample with optional tiling: ('world', cm) or ('uv', repeats)."""
    s = node(m, 'TextureSample', texture=tex)
    if normal:
        s.set_editor_property('sampler_type', u.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    if tiling:
        mode, val = tiling
        sc = node(m, 'Multiply')
        if mode == 'world':
            sc.set_editor_property('const_b', 1.0 / val)
            wp = node(m, 'WorldPosition')
            mask = node(m, 'ComponentMask')
            mask.set_editor_property('r', True)
            mask.set_editor_property('g', True)
            mask.set_editor_property('b', False)
            wire(wp, mask, '')
            wire(mask, sc, 'A')
        else:
            sc.set_editor_property('const_b', val)
            uv = node(m, 'TextureCoordinate')
            wire(uv, sc, 'A')
        wire(sc, s, 'UVs')
    return s


def material_usage_flags(m):
    """Enable the usage flags UE5.8 demands for programmatically-built materials.

    Without 'used_with_nanite', a material applied to a Nanite-enabled mesh (or
    any mesh in a game build) logs 'missing usage flag Nanite' and silently
    falls back to the grey Default Material, which reads as washed-out empty
    space (this hit the basalt island and the Nature foliage)."""
    for prop in ('used_with_nanite', 'b_used_with_nanite'):
        try:
            m.set_editor_property(prop, True)
            return
        except Exception:
            pass


def pbr_material(name, color=None, diffuse=None, normal=None, rough=0.8,
                 rough_tex=None, tiling=None, normal_strength=0.7, tint=None,
                 metallic=0.0, specular=0.5, noise_var=0.0):
    path = '/Game/Shiomori/Materials/M_' + name
    m = u.load_asset(path) if ela.does_asset_exist(path) else assets.create_asset(
        'M_' + name, '/Game/Shiomori/Materials', u.Material, u.MaterialFactoryNew())
    lib.delete_all_material_expressions(m)
    if diffuse is not None:
        bc = sample_tex(m, diffuse, tiling)
        if tint is not None:
            t = node(m, 'Constant3Vector', constant=u.LinearColor(*tint))
            mul = node(m, 'Multiply')
            wire(bc, mul, 'A')
            wire(t, mul, 'B')
            bc = mul
    else:
        bc = node(m, 'Constant3Vector', constant=u.LinearColor(*color))
    if noise_var > 0:
        wp = node(m, 'WorldPosition')
        sc = node(m, 'Multiply')
        sc.set_editor_property('const_b', noise_var)
        wire(wp, sc, 'A')
        nz = node(m, 'Noise', levels=2, output_min=0.85, output_max=1.0)
        wire(sc, nz, '')
        nm = node(m, 'Multiply')
        wire(bc, nm, 'A')
        wire(nz, nm, 'B')
        bc = nm
    output(bc, 'BASE_COLOR')
    if normal is not None:
        ns = sample_tex(m, normal, tiling, normal=True)
        flat = node(m, 'Constant3Vector', constant=u.LinearColor(0, 0, 1))
        blend = node(m, 'LinearInterpolate', const_alpha=normal_strength)
        wire(flat, blend, 'A')
        wire(ns, blend, 'B', 'RGB')
        output(blend, 'NORMAL')
    if rough_tex is not None:
        output(sample_tex(m, rough_tex, tiling), 'ROUGHNESS', 'R')
    else:
        output(node(m, 'Constant', r=rough), 'ROUGHNESS')
    if metallic > 0:
        output(node(m, 'Constant', r=metallic), 'METALLIC')
    output(node(m, 'Constant', r=specular), 'SPECULAR')
    material_usage_flags(m)
    lib.recompile_material(m)
    assert ela.save_asset(path), 'Failed to save ' + path
    return m


def make_water_mpc():
    """Create/load the global ocean Material Parameter Collection.

    Scalar parameter names here MUST match Born2FlapWater.cpp (namespace
    Born2FlapWater) and the CollectionParameter nodes below, otherwise the
    director's runtime values will never reach the water material.
    """
    path = '/Game/Shiomori/Materials/MPC_WaterGlobal'
    ela.make_directory('/Game/Shiomori/Materials')
    if ela.does_asset_exist(path):
        mpc = u.load_asset(path)
    else:
        mpc = assets.create_asset('MPC_WaterGlobal', '/Game/Shiomori/Materials',
                                  u.MaterialParameterCollection, u.MaterialParameterCollectionFactoryNew())
    scalars = [
        ('OceanLevel', -50.0), ('PrimaryWaveDirection', 270.0), ('WindDirection', 270.0),
        ('WindSpeed', 6.0), ('SwellAmplitude', 44.0), ('SwellLength', 4000.0),
        ('SwellSpeed', 530.0), ('SeaState', 3.0), ('FoamAmount', 1.0),
        ('StormAmount', 0.0), ('ShoreBreakIntensity', 1.0), ('WaveTime', 0.0),
        ('QualityLevel', 2.0), ('WaterLOD', 0.0),
    ]
    params = []
    existing = {str(p.get_editor_property('parameter_name')): p for p in mpc.get_editor_property('scalar_parameters')}
    for n, v in scalars:
        sp = existing.get(n, u.CollectionScalarParameter())
        sp.set_editor_property('parameter_name', n)
        sp.set_editor_property('default_value', v)
        params.append(sp)
    mpc.set_editor_property('scalar_parameters', params)
    assert ela.save_asset(path), 'Failed to save ' + path
    return mpc


def water_material(name, color, rough=0.08, specular=0.6, shore_color=(0.03, 0.22, 0.25), waves=None):
    """Translucent ocean with depth-graded colour/opacity, Fresnel sky reflection,
    vertex-displaced parallel swells, and animated
    foam (breaking surf line at the shoreline + white crests on the swells).

    Depth is SceneDepth - PixelDepth (the water-column thickness above the opaque
    seabed), so the shallow turquoise grades smoothly into deep ocean following the
    seabed slope — no hard distance banding. Shallow water is transparent (the sand
    shows through) and deep water is opaque. Single Layer Water cannot take WPO, so
    this is a standard translucent material — robust and clearly visible.
    """
    mpc = make_water_mpc()
    path = '/Game/Shiomori/Materials/M_' + name
    m = u.load_asset(path) if ela.does_asset_exist(path) else assets.create_asset(
        'M_' + name, '/Game/Shiomori/Materials', u.Material, u.MaterialFactoryNew())
    lib.delete_all_material_expressions(m)
    # A Single Layer Water output node survives delete_all_material_expressions
    # (it is bound to the shading model); remove it so this is a plain translucent
    # surface — the SLW pass cannot take WorldPositionOffset, which hid the sea.
    for e in list(lib.get_material_expressions(m)):
        if type(e).__name__ == 'MaterialExpressionSingleLayerWaterMaterialOutput':
            lib.delete_material_expression(m, e)
    try:
        m.set_editor_property('shading_model', u.MaterialShadingModel.MSM_DEFAULT_LIT)
    except Exception:
        pass
    m.set_editor_property('blend_mode', u.BlendMode.BLEND_TRANSLUCENT)
    m.set_editor_property('disable_depth_test', False)
    m.set_editor_property('translucency_pass', u.MaterialTranslucencyPass.MTP_BEFORE_DOF)
    m.set_editor_property('two_sided', True)
    m.set_editor_property('refraction_method', u.RefractionMode.RM_NONE)
    # Screen-space refraction can displace foreground wings through the ocean.
    output(node(m, 'Constant', r=1.33), 'REFRACTION')
    p = node(m, 'WorldPosition')
    t = node(m, 'Time')
    sd = node(m, 'SceneDepth')
    pd = node(m, 'PixelDepth')
    fn = node(m, 'Fresnel')
    # Animated foam: coarse world-space value noise drifting shoreward over time.
    wp = node(m, 'WorldPosition')
    sc = node(m, 'Multiply'); sc.set_editor_property('const_b', 0.0016); wire(wp, sc, 'A')
    tm = node(m, 'Multiply'); tm.set_editor_property('const_b', 8.0); wire(t, tm, 'A')
    z0 = node(m, 'Constant', r=0.0)
    t2 = node(m, 'AppendVector'); wire(tm, t2, 'A'); wire(z0, t2, 'B')
    t3 = node(m, 'AppendVector'); wire(t2, t3, 'A'); wire(z0, t3, 'B')
    add = node(m, 'Add'); wire(sc, add, 'A'); wire(t3, add, 'B')
    nz = node(m, 'Noise', levels=3, output_min=0.0, output_max=1.0); wire(add, nz, '')
    # Base colour: Beer-Lambert depth absorption + Fresnel sky + surf/crest foam.
    # Tuned for the shallow lagoon (floor -300 cm): near shore the turquoise is
    # bright and sand shows through; at 3 m it settles into a rich teal rather
    # than a navy abyss. Fresnel reflection is stronger so the sky dominates at
    # grazing angles like real water.
    bc = custom(m, '''float d=max(SceneDepth-PixelDepth,0.0);
 float3 shallow=float3(%.6g,%.6g,%.6g);
 float3 deep=float3(%.6g,%.6g,%.6g);
 float3 water=deep+(shallow-deep)*exp(-d/250.0);
 float3 sky=float3(0.30,0.52,0.74);
 float3 col=lerp(water, sky, saturate(Fresnel*0.85));
 float h=0.0; // The runtime surf mesh supplies synchronized breaking crests.
 float surf=smoothstep(26.0,6.0,d)*Foam;
 float crest=smoothstep(26.0,80.0,h)*Foam*Foam;
 return lerp(col, float3(0.96,0.97,0.96), saturate(surf+crest));''' % (
        shore_color[0], shore_color[1], shore_color[2], color[0], color[1], color[2]),
        {'SceneDepth': sd, 'PixelDepth': pd, 'Fresnel': fn, 'P': p, 'T': t, 'Foam': nz})
    output(bc, 'BASE_COLOR')
    output(node(m, 'Constant', r=rough), 'ROUGHNESS')
    output(node(m, 'Constant', r=specular), 'SPECULAR')
    op = custom(m, '''float d=max(SceneDepth-PixelDepth,0.0);
return 1.0-exp(-d/90.0);''', {'SceneDepth': sd, 'PixelDepth': pd}, 1)
    output(op, 'OPACITY')
    # Travelling normal: long swell, chop, and fine glitter ripple.
    n = custom(m, '''float2 q=P.xy*0.004;
 float2 n=float2(sin(q.x*0.7+q.y*1.1+T*0.9),cos(q.y*1.3-q.x*0.4-T*0.7))*0.55;
 n+=float2(sin(q.x*2.6+q.y*3.1-T*1.6),cos(q.y*2.9-q.x*2.3+T*1.4))*0.26;
 float2 r=P.xy*0.03;
 n+=float2(sin(r.x*1.7+r.y*1.1+T*3.4),cos(r.y*2.1-r.x*0.8-T*3.0))*0.10;
 float2 s=P.xy*0.12;
 n+=float2(sin(s.x*2.3+s.y*1.7-T*6.0),cos(s.y*2.9-s.x*1.3+T*5.0))*0.05;
 return normalize(float3(n,1.0));''', {'P': p, 'T': t})
    if waves is not None:
        uv = node(m, 'TextureCoordinate')
        rep = node(m, 'Multiply'); rep.set_editor_property('const_b', 300.0); wire(uv, rep, 'A')
        pw = node(m, 'Panner', speed_x=0.0, speed_y=-0.06); wire(rep, pw, 'Coordinate')
        sw = node(m, 'TextureSample', texture=waves)
        sw.set_editor_property('sampler_type', u.MaterialSamplerType.SAMPLERTYPE_NORMAL)
        wire(pw, sw, 'UVs')
        bw = node(m, 'LinearInterpolate', const_alpha=0.45); wire(n, bw, 'A'); wire(sw, bw, 'B', 'RGB')
        n = bw
    output(n, 'NORMAL')
    # Deterministic Gerstner spectrum (8 components: 1 swell + 3 wind + 4 detail)
    # driven entirely by the global ocean MPC, so the rendered surface stays in
    # lock-step with AShiomoriWaterDirector / Born2FlapWater.cpp SampleWaveHeight.
    def _mpc(name):
        return node(m, 'CollectionParameter', collection=mpc, parameter_name=name)
    gerst = custom(m, '''float Wl=max(1.0,WL);
 float Cs=max(0.0,CS);
 float ampScale=lerp(0.35,1.6,Sea/6.0);
 float d2r=0.01745329251;
 float H=0.0;
 float lenF[8]={1.0,0.32,0.19,0.13,0.088,0.059,0.043,0.030};
 float ampF[8]={1.00,0.45,0.28,0.16,0.090,0.055,0.035,0.022};
 float dirOff[8]={0.0,-8.0,7.0,-12.0,18.0,-22.0,28.0,-35.0};
 float phase[8]={0.0,1.3,2.7,4.1,5.2,0.9,3.3,4.7};
 for(int i=0;i<8;i++)
 {
  float L=Wl*lenF[i];
  float A=Amp*ampF[i]*ampScale;
  float ang=(Dir+dirOff[i])*d2r;
  float K=6.28318530718/L;
  float Om=6.28318530718*Cs/(Wl*sqrt(lenF[i]));
  H+=A*cos(K*(P.x*cos(ang)+P.y*sin(ang))-Om*WT+phase[i]);
 }
 float edge=10000+520*sin(P.x*.00030+1.7)+300*sin(P.x*.00105+4.2)+160*sin(P.x*.0024+.6)+85*sin(P.x*.0056+2.3)+45*sin(P.x*.013+5.1);
 float shore=smoothstep(100.0,3000.0,P.y-edge);
 return float3(0.0,0.0,H*shore);''',
        {'P': p, 'Amp': _mpc('SwellAmplitude'), 'WL': _mpc('SwellLength'),
         'CS': _mpc('SwellSpeed'), 'Sea': _mpc('SeaState'),
         'Dir': _mpc('PrimaryWaveDirection'), 'WT': _mpc('WaveTime')}, 3)
    output(gerst, 'WORLD_POSITION_OFFSET')
    material_usage_flags(m)
    lib.recompile_material(m)
    assert ela.save_asset(path), 'Failed to save ' + path
    return m


def sand_material(name, diffuse, normal, rough=0.8, rough_tex=None, tiling_cm=300.0,
                  relief_scale=0.02, relief_strength=0.6, normal_strength=0.6,
                  tint=None, caustic=False):
    """Beach sand: tiled grain normal plus a large-scale wind/sea/footprint
    relief so the surface reads as naturally uneven, not a flat smooth plane."""
    path = '/Game/Shiomori/Materials/M_' + name
    m = u.load_asset(path) if ela.does_asset_exist(path) else assets.create_asset(
        'M_' + name, '/Game/Shiomori/Materials', u.Material, u.MaterialFactoryNew())
    lib.delete_all_material_expressions(m)
    bc = sample_tex(m, diffuse, ('world', tiling_cm))
    if tint is not None:
        t = node(m, 'Constant3Vector', constant=u.LinearColor(*tint))
        mul = node(m, 'Multiply')
        wire(bc, mul, 'A')
        wire(t, mul, 'B')
        bc = mul
    if caustic:
        # Underwater light caustics: crossed sine layers refract into the
        # bright, slowly drifting cell patterns seen on a sunlit sea floor.
        # They only modulate base colour, so the sandy albedo still shades
        # naturally while the light seems to dance across the bay floor.
        cp = node(m, 'WorldPosition')
        ct = node(m, 'Time')
        caus = custom(m, '''float2 q=P.xy*0.015;
 float t=T*0.8;
 float c=sin(q.x*3.7+t)*sin(q.y*2.9-t*1.2)
       +sin(q.x*5.3-t*0.7)*sin(q.y*4.1+t*0.5)
       +sin((q.x+q.y)*2.6+t*0.4)*sin((q.x-q.y)*3.1-t*0.6);
 return smoothstep(0.12,1.0,c/3.0);''', {'P': cp, 'T': ct}, 1)
        sc = node(m, 'Multiply'); sc.set_editor_property('const_b', 0.65); wire(caus, sc, 'A')
        one = node(m, 'Constant', r=1.0)
        add = node(m, 'Add'); wire(one, add, 'A'); wire(sc, add, 'B')
        mul2 = node(m, 'Multiply'); wire(bc, mul2, 'A'); wire(add, mul2, 'B')
        bc = mul2
    output(bc, 'BASE_COLOR')
    if rough_tex is not None:
        output(sample_tex(m, rough_tex, ('world', tiling_cm)), 'ROUGHNESS', 'R')
    else:
        output(node(m, 'Constant', r=rough), 'ROUGHNESS')
    ns = sample_tex(m, normal, ('world', tiling_cm), normal=True)
    p = node(m, 'WorldPosition')
    # Irregular (non-sinusoidal) relief via UE value-noise nodes: fine grain
    # (two decorrelated samples for x/y) plus coarse wind streaks elongated
    # along the shore (X) so the sand reads wind-and-sea-trodden, not sine.
    gx = node(m, 'Multiply'); wire(p, gx, 'A'); gx.set_editor_property('const_b', relief_scale)
    nz_gx = node(m, 'Noise', levels=2, output_min=-1.0, output_max=1.0); wire(gx, nz_gx, '')
    gy = node(m, 'Multiply'); wire(p, gy, 'A'); gy.set_editor_property('const_b', relief_scale * 1.7)
    nz_gy = node(m, 'Noise', levels=2, output_min=-1.0, output_max=1.0); wire(gy, nz_gy, '')
    wv = node(m, 'Constant3Vector', constant=u.LinearColor(0.0009, 0.006, 0.006))
    ws = node(m, 'Multiply'); wire(p, ws, 'A'); wire(wv, ws, 'B')
    nz_w = node(m, 'Noise', levels=2, output_min=-1.0, output_max=1.0); wire(ws, nz_w, '')
    code = '''float rx=NX*%.6g + W*0.2;
float ry=NY*%.6g + W*0.55;
float3 tn=normalize(float3(TN.x*2.0-1.0, TN.y*2.0-1.0, saturate(TN.z*2.0-1.0)+0.25));
float3 r=normalize(float3(tn.x+rx, tn.y+ry, 1.0));
return normalize(float3(lerp(0.0, r.x, %.6g), lerp(0.0, r.y, %.6g), 1.0));
''' % (relief_strength, relief_strength, normal_strength, normal_strength)
    n = node(m, 'Custom', code=code,
             output_type=u.CustomMaterialOutputType.CMOT_FLOAT3)
    pins = []
    for k in ('NX', 'NY', 'W', 'TN'):
        pin = u.CustomInput()
        pin.set_editor_property('input_name', k)
        pins.append(pin)
    n.set_editor_property('inputs', pins)
    wire(nz_gx, n, 'NX')
    wire(nz_gy, n, 'NY')
    wire(nz_w, n, 'W')
    wire(ns, n, 'TN', 'RGB')
    output(n, 'NORMAL')
    material_usage_flags(m)
    lib.recompile_material(m)
    assert ela.save_asset(path), 'Failed to save ' + path
    return m


# --------------------------------------------------------------------------- #
# Parhelion weather staging: a realistic sky does NOT show every halo at once. #
# The apparition is strongly rarity-staged — a normal event is just the sun +  #
# a thin cirrostratus veil + 1-2 sun dogs + a weak 22-degree halo. Tangent    #
# arcs, the sun pillar and the circumzenithal arc are rare add-ons, and the   #
# full simultaneous display is a ~1% sliver, not a debug-menu checkbox.       #
# --------------------------------------------------------------------------- #
PARHELION_WEATHER = [
    # weight, name,                 halo, dogL, dogR, pillar, columns, cza, cloud
    (50, 'CLEAR',              dict(halo=0.0,  dog_l=0.0, dog_r=0.0, pillar=0.0, columns=0.0, cza=0.0, cloud=0.0)),
    (20, 'CIRROSTRATUS',       dict(halo=0.6,  dog_l=1.0, dog_r=1.0, pillar=0.0, columns=0.0, cza=0.0, cloud=0.5)),
    (12, 'CIRROSTRATUS_1DOG',  dict(halo=0.45, dog_l=1.0, dog_r=0.0, pillar=0.0, columns=0.0, cza=0.0, cloud=0.45)),
    (8,  'CIRROSTRATUS_FAINT', dict(halo=0.35, dog_l=0.8, dog_r=0.8, pillar=0.0, columns=0.0, cza=0.0, cloud=0.3)),
    (5,  'BRIGHT_DOGS',        dict(halo=0.8,  dog_l=1.0, dog_r=1.0, pillar=0.6, columns=0.0, cza=0.0, cloud=0.25)),
    (3,  'TANGENT_ARCS',       dict(halo=0.7,  dog_l=1.0, dog_r=1.0, pillar=0.0, columns=1.0, cza=0.0, cloud=0.35)),
    (1,  'FULL_DISPLAY',       dict(halo=1.0,  dog_l=1.0, dog_r=1.0, pillar=1.0, columns=1.0, cza=0.9, cloud=0.6)),
]
PARHELION_BY_NAME = {name: gates for _, name, gates in PARHELION_WEATHER}
# Art direction uses a persistent full display; the weighted presets above
# remain available only when PARHELION_FORCE_WEATHER is explicitly set to None.
PARHELION_DEFAULT = dict(PARHELION_BY_NAME['FULL_DISPLAY'])
# Force a state name for a deterministic sky, or None to roll the weighted table.
PARHELION_FORCE_WEATHER = 'FULL_DISPLAY'
WEATHER_SEED = 42

def pick_parhelion_weather(seed):
    """Deterministic weighted pick so a regeneration reproduces the same sky."""
    rng = random.Random(seed)
    roll = rng.uniform(0.0, sum(w for w, _, _ in PARHELION_WEATHER))
    acc = 0.0
    for weight, name, gates in PARHELION_WEATHER:
        acc += weight
        if roll <= acc:
            return name, dict(gates)
    return PARHELION_WEATHER[-1][1], dict(PARHELION_WEATHER[-1][2])


def sun_parhelion_material(sun_dir, weather=None):
    """Screen-space parhelion post-process: the ~22-degree halo, two parhelia
    (sun dogs) with spectral dispersion, and the faint parhelic circle around
    the existing atmosphere sun. The sun disc itself is NOT re-drawn here (the
    DirectionalLight + SkyAtmosphere already render it); this material only adds
    the halo/dogs additively. The static sun direction is baked in and projected
    against CameraVectorWS each frame, so the apparition tracks the real sun at
    any camera orientation. Preserve the input scene and add bounded light
    after tonemapping, with scene-depth occlusion."""
    w = dict(weather) if weather else dict(PARHELION_DEFAULT)
    path = '/Game/Shiomori/Materials/M_SunParhelion'
    m = u.load_asset(path) if ela.does_asset_exist(path) else assets.create_asset(
        'M_SunParhelion', '/Game/Shiomori/Materials', u.Material, u.MaterialFactoryNew())
    lib.delete_all_material_expressions(m)
    m.set_editor_property('material_domain', u.MaterialDomain.MD_POST_PROCESS)
    # Post-process emissive replaces scene color; surface additive blending
    # does not preserve the input frame. Composite PostProcessInput0 explicitly.
    m.set_editor_property('blend_mode', u.BlendMode.BLEND_OPAQUE)
    m.set_editor_property('blendable_location', u.BlendableLocation.BL_SCENE_COLOR_AFTER_TONEMAPPING)
    m.set_editor_property('shading_model', u.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property('two_sided', True)
    sd = node(m, 'VectorParameter', parameter_name='SunDirection', default_value=u.LinearColor(sun_dir[0], sun_dir[1], sun_dir[2], 0.0))
    view = node(m, 'CameraVectorWS')
    scene = node(m, 'SceneTexture', scene_texture_id=u.SceneTextureId.PPI_POST_PROCESS_INPUT0)
    depth = node(m, 'SceneTexture', scene_texture_id=u.SceneTextureId.PPI_SCENE_DEPTH)
    code = f'''float3 R = normalize(-ViewToCamera);
    float3 S = normalize(SunDir);
  
    // True angular separation between the view ray and the sun (FOV-independent).
    float ang = acos(clamp(dot(R, S), -1.0, 1.0));
    float elS = asin(clamp(S.z, -1.0, 1.0));
    float elR = asin(clamp(R.z, -1.0, 1.0));
    float dEl = elR - elS;
    float dAz = atan2(R.y, R.x) - atan2(S.y, S.x);
    dAz = atan2(sin(dAz), cos(dAz));
  
    // Above-horizon mask so the halo's lower arc is occluded by the sea instead
    // of being painted over it.
    float skyMask = smoothstep(0.0, 0.025, R.z) * step(10000000.0, SceneDepth.r);
  
    float R22 = 0.38397; // 22 degrees: the halo minimum-deviation angle (fixed ring)
    // Physical parhelion geometry — the sun dogs are NOT pinned to the halo. For a
    // horizontal ice plate the in-plane effective index is n/cos(h), so the dog's
    // minimum deviation grows with solar altitude and the pair drifts OUTSIDE the
    // 22 deg halo, turning taller and more diffuse as the sun climbs. At h=0 they
    // coincide at 22 deg and vanish beyond ~49 deg.
    float nIce = 1.31;
    float dev = 2.0 * asin(clamp(nIce / (2.0 * max(cos(elS), 1e-3)), -1.0, 1.0)) - 1.0471976;
    float phi = acos(clamp((cos(dev) - sin(elS) * sin(elS)) / (cos(elS) * cos(elS) + 1e-5), -1.0, 1.0));

    // Sun-altitude driver: parhelia are characteristically compact and intense
    // at a low sun (0-10 deg above the horizon) and diffuse/weak higher up.
    float sunDeg = elS * 57.2958;
    float lowSun = 1.0 - smoothstep(8.0, 20.0, sunDeg);
    float compact = lerp(1.0, 1.5, lowSun);
  
    // The sun disc itself is rendered by the DirectionalLight + SkyAtmosphere
    // (atmosphere_sun_light=True); this material only adds the halo, the two
    // parhelia and the parhelic circle around it. Re-drawing the disc here
    // produced a blown-out double sun.
  
    // (2) 22-degree halo: weak and azimuthally broken into bright/dim arcs, not
    // a perfect bright ring. Sharp reddish inner edge, soft bluish outer falloff.
    float haloEdge = ang - R22;
    float frag = 0.55 + 0.45 * (0.5 + 0.5 * sin(dAz * 2.0 + 1.3))
                          * (0.5 + 0.5 * sin(dAz * 5.0 + 0.4));
    float halo = smoothstep(-0.010, 0.0015, haloEdge) * exp(-haloEdge * 20.0) * frag;
    float3 haloCol = lerp(float3(1.0, 0.56, 0.36), float3(0.80, 0.90, 1.0), saturate(haloEdge * 18.0));
  
    // Sun dogs: altitude-dependent position (see phi above). Vertically-stretched
    // diffuse rectangular-oval (superellipse), red on the sun-facing inner edge.
    // t = 1 -> sun side (red inner edge), t = 0 -> outward tail. As the sun climbs they drift
    // outside the halo, stretch taller and soften.
    float tallness = lerp(1.0, 2.2, 1.0 - lowSun);
    float softness = lerp(1.4, 2.0, 1.0 - lowSun);
    float dogW = 60.0 * compact;
    float tL = saturate((dAz + phi) * dogW + 0.5);
    float tR = saturate(-(dAz - phi) * dogW + 0.5);

    float sxL = abs(dAz + phi) * (60.0 * compact);
    float syL = abs(dEl) * (40.0 * compact / tallness);
    float dogL = exp(-pow(pow(pow(sxL, softness) + pow(syL, softness), 1.0 / softness), 2.0));
    float sxR = abs(dAz - phi) * (60.0 * compact);
    float syR = abs(dEl) * (40.0 * compact / tallness);
    float dogR = exp(-pow(pow(pow(sxR, softness) + pow(syR, softness), 1.0 / softness), 2.0));
  
    // Spectral dispersion across each parhelion: red on the sun-facing inner
    // edge (t=1) running through orange/yellow to a pale blue-white tail on the
    // outer edge (t=0). Mostly saturated so the rainbow reads clearly.
    float3 dogColL = float3(0.92, 0.93, 0.97);
    dogColL = lerp(dogColL, float3(0.45, 0.62, 0.95), smoothstep(0.00, 0.28, tL));
    dogColL = lerp(dogColL, float3(0.95, 0.90, 0.55), smoothstep(0.28, 0.55, tL));
    dogColL = lerp(dogColL, float3(1.00, 0.55, 0.20), smoothstep(0.55, 0.80, tL));
    dogColL = lerp(dogColL, float3(1.00, 0.12, 0.08), smoothstep(0.80, 1.00, tL));
    float lumL = dot(dogColL, float3(0.299, 0.587, 0.114));
    dogColL = lerp(float3(lumL, lumL, lumL), dogColL, 0.65);
  
    float3 dogColR = float3(0.92, 0.93, 0.97);
    dogColR = lerp(dogColR, float3(0.45, 0.62, 0.95), smoothstep(0.00, 0.28, tR));
    dogColR = lerp(dogColR, float3(0.95, 0.90, 0.55), smoothstep(0.28, 0.55, tR));
    dogColR = lerp(dogColR, float3(1.00, 0.55, 0.20), smoothstep(0.55, 0.80, tR));
    dogColR = lerp(dogColR, float3(1.00, 0.12, 0.08), smoothstep(0.80, 1.00, tR));
    float lumR = dot(dogColR, float3(0.299, 0.587, 0.114));
    dogColR = lerp(float3(lumR, lumR, lumR), dogColR, 0.65);
  
    // (7) Ice-crystal quality gate: near-spectral (ghostly) by default, and
    // prominent only under excellent plate conditions. This is the master gain
    // for the whole apparition; the sun itself is the real sun and stays bright
    // regardless. Raise iceQuality toward 1.0 to make the apparition distinct.
    // Independent crystal populations let a field light only one mechanism
    // (plates = sun dogs/CZA/pillar, columns = tangent arcs, random = halo).
    float iceQuality = 1.0;
    float iceGain = lerp(0.08, 1.0, iceQuality);
    // Staggered rarity: each crystal population is gated independently so a
    // normal event is just sun + thin cirrostratus + 1-2 sun dogs + a weak
    // halo. Tangent arcs, the pillar and the CZA are strongly staged rarities.
    float gHalo   = iceGain * {w['halo']};
    float gDogL   = iceGain * {w['dog_l']};
    float gDogR   = iceGain * {w['dog_r']};
    float gPillar = iceGain * {w['pillar']};
    float gColumn = iceGain * {w['columns']};
    float gCza    = iceGain * {w['cza']};
  
    // (9) Long whitish tail running outward along the parhelic circle.
    float tailL = exp(-pow(dEl * 90.0, 2.0))
                * smoothstep(phi - 0.12, phi + 0.08, -dAz)
                * exp(-max(0.0, -dAz - phi) * 0.55);
    float tailR = exp(-pow(dEl * 90.0, 2.0))
                * smoothstep(phi - 0.12, phi + 0.08, dAz)
                * exp(-max(0.0, dAz - phi) * 0.55);
    float3 tailCol = float3(0.93, 0.91, 0.87);
  
    // Parhelic circle: a very weak horizontal whitish light band at sun
    // altitude, running through the sun and both sun dogs. Not a complete
    // circle — it fades with azimuth and is faintly reinforced at the dogs.
    float pcBand = exp(-pow(dEl * 14.0, 2.0));
    float pcFade = exp(-abs(dAz) * 0.22);
    float pcDog  = 1.0 + 0.8 * (exp(-pow((dAz + phi) * 7.0, 2.0))
                              + exp(-pow((dAz - phi) * 7.0, 2.0)));
    float circle = pcBand * pcFade * pcDog * 0.07;
  
    // Sun-visibility gate removed: `ang` (view-vs-sun angle) already suppresses
    // the sun disc when the sun is behind the camera (R never aligns with S),
    // and the clip-space projection of a DIRECTION was fragile under LWC.
  
    // (10) Sun pillar: a vertical golden/white streak above (and reflected
    // below) the sun from reflection off falling plates. No refraction, hence
    // no spectral colour — just a warm column, golder at a low sun.
    float pillar = exp(-pow(dAz * 160.0, 2.0)) * exp(-pow(dEl * 9.0, 2.0));
    float3 pillarCol = lerp(float3(1.0, 0.98, 0.92), float3(1.0, 0.82, 0.55), lowSun);

    // (11) Tangent arcs (horizontal columns): an upper V-bow touching the halo
    // top and a lower inverted V-bow touching the halo bottom at a low sun. As
    // the sun climbs the bows broaden and merge into a circumscribed halo (an
    // oval tangent to the ring at top and bottom). Red on the sun-facing edge.
    float merge = smoothstep(20.0, 55.0, sunDeg);
    float arcAz = abs(dAz);
    float arcExtent = lerp(0.20, 1.5708, merge);
    float arcEnd = 1.0 - smoothstep(arcExtent * 0.55, arcExtent, arcAz);
    float vEl = R22 - R22 * 0.32 * pow(arcAz, 1.8);
    float oEl = R22 * cos(clamp(arcAz, 0.0, 1.5708));
    float arcElUp = lerp(vEl, oEl, merge);
    float arcElLo = -arcElUp;
    float arcUp = exp(-pow((dEl - arcElUp) * 150.0, 2.0)) * arcEnd;
    float arcLo = exp(-pow((dEl - arcElLo) * 150.0, 2.0)) * arcEnd;
    float arcT = saturate((ang - R22) * 20.0 + 0.5);
    float3 arcCol = float3(1.00, 0.45, 0.30);
    arcCol = lerp(arcCol, float3(1.00, 0.75, 0.45), smoothstep(0.0, 0.35, arcT));
    arcCol = lerp(arcCol, float3(0.85, 0.90, 0.92), smoothstep(0.35, 0.75, arcT));
    arcCol = lerp(arcCol, float3(0.92, 0.94, 0.97), smoothstep(0.75, 1.0, arcT));
    float arcLum = dot(arcCol, float3(0.299, 0.587, 0.114));
    arcCol = lerp(float3(arcLum, arcLum, arcLum), arcCol, 0.45);

    // (12) Circumzenithal arc (CZA): the most colourful halo, an inverted
    // rainbow high above the sun on a circle around the ZENITH (not the sun),
    // at zenith distance 46deg - h. Red on the lower (sun-facing) side, blue up.
    // Fully saturated and gated as a rare event.
    float zenAng = acos(clamp(R.z, -1.0, 1.0));
    float czaZen = 0.80285 - elS;
    float czaVisible = step(0.0, czaZen) * step(sunDeg, 32.0);
    float czaArc = exp(-pow((zenAng - czaZen) * 100.0, 2.0));
    float czaAz = exp(-pow(abs(dAz) * 9.0, 2.0));
    float cza = czaArc * czaAz * czaVisible;
    float czaT = saturate(-(zenAng - czaZen) * 60.0 + 0.5);
    float3 czaCol = float3(1.0, 0.15, 0.10);
    czaCol = lerp(czaCol, float3(1.0, 0.55, 0.15), smoothstep(0.0, 0.3, czaT));
    czaCol = lerp(czaCol, float3(1.0, 0.9, 0.2), smoothstep(0.3, 0.5, czaT));
    czaCol = lerp(czaCol, float3(0.3, 0.9, 0.4), smoothstep(0.5, 0.7, czaT));
    czaCol = lerp(czaCol, float3(0.2, 0.5, 1.0), smoothstep(0.7, 1.0, czaT));
    // (cirrus veil removed: its full-sky sine field read as screen artifacts)

    float3 contrib = float3(0.0, 0.0, 0.0);
    contrib += haloCol * halo * (1.20 * gHalo * lerp(0.7, 1.0, lowSun)) * skyMask;
    contrib += dogColL * dogL * (2.00 * gDogL * lerp(1.2, 2.5, lowSun)) * skyMask;
    contrib += dogColR * dogR * (2.00 * gDogR * lerp(1.2, 2.5, lowSun)) * skyMask;
    contrib += tailCol * tailL * (0.20 * gDogL * lerp(0.7, 1.0, lowSun)) * skyMask;
    contrib += tailCol * tailR * (0.20 * gDogR * lerp(0.7, 1.0, lowSun)) * skyMask;
    contrib += float3(0.96, 0.95, 0.92) * circle * gHalo * skyMask;
    contrib += pillarCol * pillar * (0.45 * gPillar * lerp(0.5, 1.2, lowSun)) * skyMask;
    contrib += arcCol * (arcUp + arcLo) * (0.16 * gColumn * lerp(0.8, 1.0, lowSun)) * skyMask;
    contrib += czaCol * cza * (0.6 * gCza) * skyMask;

    // A double rainbow opposite the sun, with reversed secondary spectrum.
    float antiAngle = acos(clamp(dot(R, -S), -1.0, 1.0));
    float bowT = saturate((antiAngle - 0.69813) / 0.03491);
    float3 bowColor = saturate(1.5 - abs(4.0 * bowT - float3(3.0, 2.0, 1.0)));
    float bow = smoothstep(0.695, 0.700, antiAngle) * (1.0 - smoothstep(0.730, 0.736, antiAngle));
    float secondT = 1.0 - saturate((antiAngle - 0.87266) / 0.05236);
    float3 secondColor = saturate(1.5 - abs(4.0 * secondT - float3(3.0, 2.0, 1.0)));
    float secondBow = smoothstep(0.869, 0.875, antiAngle) * (1.0 - smoothstep(0.922, 0.929, antiAngle));
    contrib += (0.45 * bowColor * bow + 0.18 * secondColor * secondBow) * gHalo * skyMask;
    // Bounded display-space light preserves color instead of clipping broad
    // HDR-era lobes to white after tonemapping.
    return SceneColor.rgb + (1.0 - saturate(SceneColor.rgb)) * (1.0 - exp(-0.7 * contrib));'''
    h = node(m, 'Custom', code=code, output_type=u.CustomMaterialOutputType.CMOT_FLOAT3)
    pins = []
    for nm in ('SunDir', 'ViewToCamera', 'SceneColor', 'SceneDepth'):
        pin = u.CustomInput()
        pin.set_editor_property('input_name', nm)
        pins.append(pin)
    h.set_editor_property('inputs', pins)
    wire(sd, h, 'SunDir')
    wire(view, h, 'ViewToCamera')
    wire(scene, h, 'SceneColor', 'Color')
    wire(depth, h, 'SceneDepth', 'Color')
    output(h, 'EMISSIVE_COLOR')
    material_usage_flags(m)
    lib.recompile_material(m)
    assert ela.save_asset(path), 'Failed to save ' + path
    return m


def apply_post_process():
    """Unbound post-process volume: camera-side optics only — gentle bloom for
    specular glitter and screen-space reflections for wet sand/water. The
    parhelion is a separate post-process blendable (M_SunParhelion) attached by
    the pawn to the camera. Property names are UE5.8."""
    vol = u.EditorLevelLibrary.spawn_actor_from_class(u.PostProcessVolume, u.Vector(0, 0, 0))
    vol.set_actor_label('Shiomori PostProcess')
    vol.set_editor_property('unbound', True)
    s = vol.settings
    for k, v in (('bloom_intensity', 1.15),
                 ('bloom_threshold', 0.85),
                 ('screen_space_reflection_intensity', 90.0),
                 ('screen_space_reflection_quality', 80.0),
                 ('screen_space_reflection_max_roughness', 0.6),
                 ('auto_exposure_min_brightness', 0.5),
                 ('auto_exposure_max_brightness', 0.5)):
        try:
            s.set_editor_property(k, v)
        except Exception:
            pass
    try:
        s.set_editor_property('reflection_method', u.ReflectionMethod.RM_LUMEN)
    except Exception:
        pass
    try:
        vol.set_editor_property('settings', s)
    except Exception as e:
        u.log_warning('Shiomori post-process settings failed: ' + str(e))


def fpv_fisheye_material():
    """Fullscreen barrel-distortion (fisheye) post-process material for the FPV
    onboard camera. Samples the scene colour (PostProcessInput0) through a
    radial barrel remap so the FPV lens reads as a wide fisheye rather than a
    flat perspective. Applied by the flight pawn to FpvCamera only."""
    path = '/Game/UI/M_FpvFisheye'
    u.EditorAssetLibrary.make_directory('/Game/UI')
    m = u.load_asset(path) if ela.does_asset_exist(path) else assets.create_asset(
        'M_FpvFisheye', '/Game/UI', u.Material, u.MaterialFactoryNew())
    lib.delete_all_material_expressions(m)
    m.set_editor_property('material_domain', u.MaterialDomain.MD_POST_PROCESS)
    m.set_editor_property('blend_mode', u.BlendMode.BLEND_OPAQUE)
    m.set_editor_property('shading_model', u.MaterialShadingModel.MSM_UNLIT)
    sp = node(m, 'ScreenPosition')
    barrel = node(m, 'Constant', r=0.30)
    aspect = node(m, 'Constant', r=1.7778)
    # Let the SceneTexture expression declare and sample the scene input.
    # A function prototype inside a Custom expression does not implement it.
    code = '''float2 uv = ScreenUV - 0.5;
    uv.x *= Aspect;
    float r2 = dot(uv, uv);
    uv *= (1.0 + Barrel * r2);
    uv.x /= Aspect;
    float2 suv = uv + 0.5;
    return saturate(suv);'''
    h = custom(m, code, {'ScreenUV': sp, 'Barrel': barrel, 'Aspect': aspect}, 2)
    scene = node(m, 'SceneTexture', scene_texture_id=u.SceneTextureId.PPI_POST_PROCESS_INPUT0)
    wire(h, scene, 'UVs')
    output(scene, 'EMISSIVE_COLOR', 'Color')
    lib.recompile_material(m)
    assert ela.save_asset(path), 'Failed to save ' + path
    return m


# --------------------------------------------------------------------------- #
# Import the CC0 surface set (downloaded by fetch_shiomori_assets.py).        #
# --------------------------------------------------------------------------- #
sand_d = load_texture('sand_02', 'Diffuse', 'T_Sand_Diffuse')
sand_n = load_texture('sand_02', 'nor_dx', 'T_Sand_Normal', normal=True, srgb=False)
sand_r = load_texture('sand_02', 'Rough', 'T_Sand_Rough', srgb=False)
concrete_d = load_texture('concrete_floor_02', 'Diffuse', 'T_Concrete_Diffuse')
concrete_n = load_texture('concrete_floor_02', 'nor_dx', 'T_Concrete_Normal', normal=True, srgb=False)
concrete_r = load_texture('concrete_floor_02', 'Rough', 'T_Concrete_Rough', srgb=False)
basalt_d = load_texture('aerial_rocks_02', 'Diffuse', 'T_Basalt_Diffuse')
basalt_n = load_texture('aerial_rocks_02', 'nor_dx', 'T_Basalt_Normal', normal=True, srgb=False)
basalt_r = load_texture('aerial_rocks_02', 'Rough', 'T_Basalt_Rough', srgb=False)


def opt_texture(subdir, stem, name, normal=False, srgb=True):
    """Load an optional, user-supplied texture; returns None when absent."""
    if not (ROOT / subdir / (stem + '.jpg')).exists():
        return None
    return load_texture(subdir, stem, name, normal=normal, srgb=srgb)


# Optional, user-generated textures (drop into Saved/ShiomoriSource). Each is
# a seamless square .jpg; normals are linear, color maps are sRGB.
wave_n = opt_texture('ocean_waves', 'nor', 'T_Waves_Normal', normal=True, srgb=False)
white_d = opt_texture('white_panel', 'Diffuse', 'T_White_Diffuse')
white_n = opt_texture('white_panel', 'nor', 'T_White_Normal', normal=True, srgb=False)
white_r = opt_texture('white_panel', 'Rough', 'T_White_Rough', srgb=False)
sand_d2 = opt_texture('sand_beach', 'Diffuse', 'T_SandBeach_Diffuse')
sand_n2 = opt_texture('sand_beach', 'nor', 'T_SandBeach_Normal', normal=True, srgb=False)
sand_r2 = opt_texture('sand_beach', 'Rough', 'T_SandBeach_Rough', srgb=False)
if sand_d2:
    sand_d, sand_n, sand_r = sand_d2, sand_n2 or sand_n, sand_r2 or sand_r

# --------------------------------------------------------------------------- #
# Build the PBR material palette.                                             #
# --------------------------------------------------------------------------- #
mats = {}
mats['Sand'] = sand_material('Sand', sand_d, sand_n, rough_tex=sand_r, tiling_cm=300.0,
                             normal_strength=0.65)
mats['WetSand'] = sand_material('WetSand', sand_d, sand_n, rough=0.35, tiling_cm=300.0,
                                normal_strength=0.5, tint=(0.52, 0.47, 0.4), caustic=True)
mats['Concrete'] = pbr_material('Concrete', diffuse=concrete_d, normal=concrete_n,
                                rough_tex=concrete_r, tiling=('world', 120.0),
                                normal_strength=0.6, noise_var=0.0015)
mats['Basalt'] = pbr_material('Basalt', diffuse=basalt_d, normal=basalt_n, rough=0.92,
                              tiling=('uv', 2.0), normal_strength=0.9,
                              tint=(0.52, 0.52, 0.55))
mats['Industry'] = pbr_material('Industry', diffuse=concrete_d, normal=concrete_n,
                                rough=0.7, tiling=('uv', 10.0), normal_strength=0.5,
                                tint=(0.68, 0.66, 0.58))
mats['Road'] = pbr_material('Road', diffuse=concrete_d, normal=concrete_n, rough=0.88,
                            tiling=('world', 200.0), normal_strength=0.5,
                            tint=(0.12, 0.13, 0.15))
mats['Wood'] = pbr_material('Wood', color=(0.3, 0.17, 0.085), rough=0.62,
                            specular=0.4, noise_var=0.05)
mats['Ivory'] = pbr_material('Ivory', diffuse=concrete_d, normal=concrete_n, rough=0.5,
                             tiling=('world', 60.0), normal_strength=0.3,
                             tint=(0.86, 0.87, 0.83))
mats['Sterile'] = pbr_material('Sterile', diffuse=white_d or concrete_d, normal=white_n or concrete_n,
                               rough_tex=white_r or concrete_r, tiling=('world', 150.0),
                               normal_strength=0.3, tint=(0.93, 0.94, 0.93))
mats['Teal'] = pbr_material('Teal', color=(0.03, 0.3, 0.32), rough=0.42, specular=0.6,
                            metallic=0.15, noise_var=0.02)
mats['Orange'] = pbr_material('Orange', color=(0.8, 0.25, 0.075), rough=0.45,
                              specular=0.55, noise_var=0.02)
mats['Window'] = pbr_material('Window', color=(0.04, 0.1, 0.13), rough=0.12,
                              metallic=0.85, specular=1.0)
mats['Bush'] = pbr_material('Bush', color=(0.09, 0.24, 0.13), rough=0.85, specular=0.4)
mats['Earth'] = pbr_material('Earth', diffuse=sand_d, normal=sand_n, rough_tex=sand_r,
                             tiling=('world', 260.0), normal_strength=0.6,
                             tint=(0.44, 0.46, 0.37))
mats['Water'] = water_material('Water', (0.008, 0.09, 0.16), rough=0.05, specular=0.8,
                             shore_color=(0.05, 0.36, 0.32), waves=wave_n)
mats['Foam'] = pbr_material('Foam', color=(0.9, 0.94, 0.93), rough=0.35, specular=0.3)

# Translucent foam with soft, noisy edges.
foam = u.load_asset('/Game/Shiomori/Materials/M_Foam') if ela.does_asset_exist('/Game/Shiomori/Materials/M_Foam') else mats['Foam']
foam.set_editor_property('blend_mode', u.BlendMode.BLEND_TRANSLUCENT)
foam.set_editor_property('two_sided', True)
lib.delete_all_material_expressions(foam)
output(node(foam, 'Constant3Vector', constant=u.LinearColor(0.92, 0.95, 0.95)), 'BASE_COLOR')
output(node(foam, 'Constant', r=0.35), 'ROUGHNESS')
wp = node(foam, 'WorldPosition')
sc = node(foam, 'Multiply')
sc.set_editor_property('const_b', 0.02)
wire(wp, sc, 'A')
# Drift the foam edge over time so the surf line subtly breathes and rolls.
tm = node(foam, 'Time')
spd = node(foam, 'Multiply')
spd.set_editor_property('const_b', 14.0)
wire(tm, spd, 'A')
z0 = node(foam, 'Constant', r=0.0)
t2 = node(foam, 'AppendVector')
wire(spd, t2, 'A')
wire(z0, t2, 'B')
t3 = node(foam, 'AppendVector')
wire(t2, t3, 'A')
wire(z0, t3, 'B')
add = node(foam, 'Add')
wire(sc, add, 'A')
wire(t3, add, 'B')
nz = node(foam, 'Noise', levels=3, output_min=0.0, output_max=1.0)
wire(add, nz, '')
op = node(foam, 'Multiply')
op.set_editor_property('const_b', 0.5)
wire(nz, op, 'A')
output(op, 'OPACITY')
lib.recompile_material(foam)
ela.save_asset('/Game/Shiomori/Materials/M_Foam')
mats['Foam'] = foam

# Cloud canopy: translucent white with soft, puffy noise edges — the open,
# art-object roof over each station. Mirrors the proven foam material graph.
cloud = u.load_asset('/Game/Shiomori/Materials/M_Cloud') if ela.does_asset_exist('/Game/Shiomori/Materials/M_Cloud') else assets.create_asset(
    'M_Cloud', '/Game/Shiomori/Materials', u.Material, u.MaterialFactoryNew())
cloud.set_editor_property('blend_mode', u.BlendMode.BLEND_TRANSLUCENT)
cloud.set_editor_property('two_sided', True)
lib.delete_all_material_expressions(cloud)
output(node(cloud, 'Constant3Vector', constant=u.LinearColor(0.96, 0.97, 0.97)), 'BASE_COLOR')
output(node(cloud, 'Constant', r=0.6), 'ROUGHNESS')
wp = node(cloud, 'WorldPosition')
sc = node(cloud, 'Multiply')
sc.set_editor_property('const_b', 0.004)
wire(wp, sc, 'A')
nz = node(cloud, 'Noise', levels=4, output_min=0.2, output_max=1.0)
wire(sc, nz, '')
op = node(cloud, 'Multiply')
op.set_editor_property('const_b', 0.85)
wire(nz, op, 'A')
output(op, 'OPACITY')
lib.recompile_material(cloud)
ela.save_asset('/Game/Shiomori/Materials/M_Cloud')
mats['Cloud'] = cloud

# --------------------------------------------------------------------------- #
# World / meshes.                                                             #
# --------------------------------------------------------------------------- #
world = u.EditorLoadingAndSavingUtils.new_blank_map(False)
world.get_world_settings().set_editor_property('force_no_precomputed_lighting', True)
# A fresh -nullrhi commandlet can start with a cold asset registry: the CC0
# /Game/Nature meshes exist on disk but are not yet indexed, so load_asset
# returns None ("could not be found in the Asset Registry") and foliage()
# crashes. Scan the project asset paths synchronously before the first load.
u.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(['/Game/Nature'])
def _load_mesh(path):
    return ela.load_asset(path) or u.load_asset(path)

def _pick_mesh(pool, rng):
    return rng.choice(pool) if pool else None

meshes = {n: _load_mesh('/Engine/BasicShapes/' + n) for n in ('Cube', 'Sphere', 'Cylinder', 'Cone', 'Plane')}
meshes.update({'Rock' + str(i): _load_mesh('/Game/Nature/SM_Rock' + str(i)) for i in range(4)})
foliage_meshes = {
    'Grass': [m for m in (_load_mesh('/Game/Nature/SM_Grass%d' % i) for i in range(3)) if m],
    'Fir': [m for m in (_load_mesh('/Game/Nature/SM_Fir%d' % i) for i in range(3)) if m],
    'Rock': [m for m in (_load_mesh('/Game/Nature/SM_Rock%d' % i) for i in range(4)) if m],
}
count = 0


# --------------------------------------------------------------------------- #
# Procedural mesh helpers: a subdivided ocean plane for real vertex-displaced  #
# swells, and smooth shoreline ribbons that replace the old rectangular strips. #
# --------------------------------------------------------------------------- #
def build_mesh(name, verts, tris, uvs=None):
    """Create/rebuild /Game/Shiomori/Meshes/SM_<name> from a triangle soup.

    verts are (x, y, z) world-centimetre triples; tris are (i0, i1, i2) index
    triples wound counter-clockwise (+Z normal, faces up); uvs is an optional
    parallel list of (u, v) pairs for UV0. World-space materials ignore UVs, so
    the channel is optional, but it keeps the mesh build well-formed.

    UE5's front-face convention is clockwise (left-handed Z-up); the grid
    generators emit counter-clockwise windings, so we reverse each triangle
    here to make the normals face +Z (up). Without this, one-sided opaque
    materials (sand, seabed) are back-face culled and render as invisible.
    """
    path = '/Game/Shiomori/Meshes/SM_' + name
    if ela.does_asset_exist(path):
        sm = u.load_asset(path)
    else:
        ela.make_directory('/Game/Shiomori/Meshes')
        sm = assets.create_asset('SM_' + name, '/Game/Shiomori/Meshes', u.StaticMesh, None)
    md = sm.create_static_mesh_description()
    md.reserve_new_vertices(len(verts))
    md.reserve_new_vertex_instances(len(verts))
    md.reserve_new_polygons(len(tris))
    pg = md.create_polygon_group()
    md.set_polygon_group_material_slot_name(pg, 'Surface')
    sm.set_editor_property('static_materials', [u.StaticMaterial(material_slot_name='Surface')])
    vids = []
    for (x, y, z) in verts:
        v = md.create_vertex()
        md.set_vertex_position(v, u.Vector(x, y, z))
        vids.append(v)
    insts = []
    for v in vids:
        insts.append(md.create_vertex_instance(v))
    if uvs is not None:
        for k, inst in enumerate(insts):
            md.set_vertex_instance_uv(inst, u.Vector2D(uvs[k][0], uvs[k][1]), 0)
    for (a, b, c) in tris:
        md.create_triangle(pg, [insts[a], insts[c], insts[b]])
    # Keep the CPU copy of the render mesh (bAllowCPUAccess), otherwise the
    # commandlet build drops the triMesh and the mesh has no complex collision.
    try:
        sm.set_editor_property('allow_cpu_access', True)
    except Exception:
        pass
    sm.build_from_static_mesh_descriptions([md], False, False)
    assert ela.save_asset(path), 'Failed to save ' + path
    return sm


def place_mesh(label, p, mesh, mat, rot=(0, 0, 0), collision=False):
    """Spawn a pre-built (world-sized) mesh actor; no scaling applied."""
    global count
    # UE5 Python's Rotator positional args are (roll, pitch, yaw); our rot tuples
    # are (pitch, yaw, roll). Named args keep the mapping unambiguous.
    a = u.EditorLevelLibrary.spawn_actor_from_class(u.StaticMeshActor, u.Vector(*p),
                                                    u.Rotator(pitch=rot[0], yaw=rot[1], roll=rot[2]))
    a.set_actor_label(label)
    a.set_editor_property('tags', [label])
    c = a.static_mesh_component
    c.set_static_mesh(mesh)
    c.set_material(0, mat)
    c.set_collision_profile_name('BlockAll' if collision else 'NoCollision')
    count += 1
    return a


def collision_box(label, cx, cy, top_z, sx, sy, thick=600.0):
    """Invisible BlockAll box whose top face sits at top_z. The commandlet build
    drops complex (triMesh) collision for procedural meshes, so the smooth beach
    gets a stepped ramp of these primitive boxes (proven to collide)."""
    global count
    a = u.EditorLevelLibrary.spawn_actor_from_class(u.StaticMeshActor, u.Vector(cx, cy, top_z - thick / 2.0), u.Rotator())
    a.set_actor_label(label)
    a.set_editor_property('tags', ['BeachCollision'])
    c = a.static_mesh_component
    c.set_static_mesh(meshes['Cube'])
    c.set_collision_profile_name('BlockAll')
    c.set_visibility(False)
    c.set_cast_shadow(False)
    b = meshes['Cube'].get_bounds().box_extent
    a.set_actor_scale3d(u.Vector(sx / (2.0 * b.x), sy / (2.0 * b.y), thick / (2.0 * b.z)))
    count += 1
    return a


def waterline(x):
    # Rugged shoreline: layered sine octaves carve gentle headlands and coves
    # instead of a ruler-straight shore. Kept in sync with GroundHeight.cpp.
    return (10000 + 520 * math.sin(x * 0.00030 + 1.7) + 300 * math.sin(x * 0.00105 + 4.2)
            + 160 * math.sin(x * 0.0024 + 0.6) + 85 * math.sin(x * 0.0056 + 2.3)
            + 45 * math.sin(x * 0.013 + 5.1))


def make_ocean_grid():
    # The sea plane now hugs the rugged waterline on its shoreward edge and only
    # steps seaward from there. The old grid reached far inland (y down to
    # -145000) and read as "water everywhere above the sand".
    xs = [-200000, -100000, -60000] + list(range(-50000, 50001, 250)) + [60000, 100000, 200000]
    near = [waterline(x) - 60.0 for x in xs]
    off = [11500] + list(range(12000, 45001, 200)) + [55000, 75000, 120000, 255000]
    rows = [[(x, near[i], -50.0) for i, x in enumerate(xs)]]
    rows += [[(x, y, -50.0) for x in xs] for y in off]
    verts = [v for row in rows for v in row]
    uvs = [(x / 150.0, y / 150.0) for (x, y, _) in verts]
    tris = []
    stride = len(xs)
    for j in range(len(rows) - 1):
        for i in range(stride - 1):
            a = j * stride + i
            tris.extend([(a, a + 1, a + stride + 1), (a, a + stride + 1, a + stride)])
    return verts, tris, uvs


def beach_height(x, y):
    # Keep in sync with GroundHeight in Born2FlapGameMode.cpp.
    d = y - waterline(x)
    # Natural, X-only dune crests (non-negative berms) that fade to zero at the
    # waterline. X-only keeps the offshore drop monotonic in Y for the coast test.
    dune = max(20.0, 40.0 + 50 * math.sin(x * 0.00029 + 1.2) + 30 * math.sin(x * 0.00091 + 4.1)
               + 15 * math.sin(x * 0.0023 + 0.7) + 8 * math.sin(x * 0.0047 + 2.9))
    if d <= -2500:
        return dune
    if d < 0:
        return dune * (-d / 2500.0) - 50.0 * ((d + 2500.0) / 2500.0) ** 2
    return max(-300.0, -50.0 - 0.04 * d)


def make_beach():
    xs = list(range(-45000, 45001, 250))
    # Sand hugs the mainland shore and stops ~25 m offshore (d=2500). The old
    # grid ran 400 m out to sea (d=40000, Y≈50000) and its floor dropped to
    # Z=-1750, so a bright sand bed read as a slab deep under the island. The
    # volcanic island ellipsoid's nearest point is Y=12500 (at X=-37000); cap the
    # sand there so headlands (where waterline(x)+2500 reaches ~13600) never push
    # bright sand under the island's submerged slope.
    offsets = [-12000, -10000, -6000, -3500] + list(range(-2500, 2501, 100))
    verts, uvs, tris = [], [], []
    for d in offsets:
        for x in xs:
            y = max(-1200, min(waterline(x)+d, 12500.0))
            verts.append((x,y,beach_height(x,y)))
            uvs.append((x/150,y/150))
    stride=len(xs)
    for j in range(len(offsets)-1):
        for i in range(stride-1):
            a=j*stride+i
            tris.extend([(a,a+1,a+stride+1),(a,a+stride+1,a+stride)])
    return verts,tris,uvs


def seabed_z(x, y):
    # Shallow bay floor matching GroundHeight's offshore branch in Born2FlapGameMode.cpp:
    # within the 900 m beach (|x|<=45000) it is the -50 - 0.04*D foreshore slope
    # clamped at -300 (a ~2.5 m lagoon); beyond it the open sea sits at the same floor.
    if abs(x) > 45000:
        return -300.0
    return max(-300.0, -50.0 - 0.04 * (y - waterline(x)))


def make_seabed():
    # The bay floor slopes down from the wet sand's seaward edge (d=2500, Z=-150)
    # to the shallow analytic GroundHeight floor (Z=-300) at d=6250, then runs flat
    # to the far sea. The first row hugs the sand's capped edge so sand and seabed
    # share one seamless boundary (no gap, no z-fight).
    xs = [-200000, -100000, -60000] + list(range(-45000, 45001, 250)) + [60000, 100000, 200000]
    near = [min(waterline(x) + 2500.0, 12500.0) for x in xs]
    off = list(range(13000, 53001, 500)) + [55000, 75000, 120000, 255000]
    rows = [[(x, near[i], seabed_z(x, near[i])) for i, x in enumerate(xs)]]
    rows += [[(x, y, seabed_z(x, y)) for x in xs] for y in off]
    verts = [v for row in rows for v in row]
    uvs = [(x / 150.0, y / 150.0) for (x, y, _) in verts]
    tris = []
    stride = len(xs)
    for j in range(len(rows) - 1):
        for i in range(stride - 1):
            a = j * stride + i
            tris.extend([(a, a + 1, a + stride + 1), (a, a + stride + 1, a + stride)])
    return verts, tris, uvs


ocean_mesh = build_mesh('OceanGrid', *make_ocean_grid())
beach_mesh = build_mesh('ContinuousBeach', *make_beach())
seabed_mesh = build_mesh('Seabed', *make_seabed())
beach_mesh.get_editor_property('body_setup').set_editor_property('collision_trace_flag', u.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
ela.save_loaded_asset(beach_mesh)


def part(label, p, size, mat='Concrete', shape='Cube', rot=(0, 0, 0), collision=True):
    global count
    a = u.EditorLevelLibrary.spawn_actor_from_class(u.StaticMeshActor, u.Vector(*p),
                                                    u.Rotator(pitch=rot[0], yaw=rot[1], roll=rot[2]))
    a.set_actor_label(label)
    a.set_editor_property('tags', [label])
    c = a.static_mesh_component
    c.set_static_mesh(meshes[shape])
    c.set_material(0, mats[mat])
    c.set_collision_profile_name('BlockAll' if collision else 'NoCollision')
    bounds = meshes[shape].get_bounds().box_extent
    a.set_actor_scale3d(u.Vector(size[0] / (2 * bounds.x), size[1] / (2 * bounds.y),
                                 size[2] / (2 * bounds.z) if bounds.z > .001 else 1))
    count += 1
    if shape.startswith('Rock'):
        origin, extent = a.get_actor_bounds(False)
        location = a.get_actor_location()
        a.set_actor_location(u.Vector(location.x + p[0] - origin.x, location.y + p[1] - origin.y,
                                      location.z + p[2] - origin.z), False, False)
    scale = a.get_actor_scale3d()
    assert max(abs(scale.x), abs(scale.y), abs(scale.z)) < 10000, 'Invalid mesh scale: ' + label
    return a


def foliage(label, p, mesh, height, rot=(0, 0, 0), ground_z=None):
    global count
    if mesh is None:
        return None
    a = u.EditorLevelLibrary.spawn_actor_from_class(u.StaticMeshActor, u.Vector(*p),
                                                    u.Rotator(pitch=rot[0], yaw=rot[1], roll=rot[2]))
    a.set_actor_label(label)
    a.set_editor_property('tags', [label])
    c = a.static_mesh_component
    c.set_static_mesh(mesh)
    c.set_collision_profile_name('NoCollision')
    b = mesh.get_bounds()
    s = height / max(2.0 * b.box_extent.z, 0.001)
    a.set_actor_scale3d(u.Vector(s, s, s))
    # Deterministic planting from the LOCAL bounds (pivot-relative) instead of
    # get_actor_bounds (stale/unscaled under -nullrhi). The imported glTF meshes
    # carry a large XY pivot offset (up to ~80 cm on grass tufts), so recentre the
    # foliage onto the planted point; on sloped dunes that offset otherwise leaves
    # grass floating ~90 cm above the terrain it was sampled for.
    z = p[2] if ground_z is None else ground_z
    yaw = math.radians(rot[1])
    ox, oy = s * b.origin.x, s * b.origin.y
    wx = ox * math.cos(yaw) - oy * math.sin(yaw)
    wy = ox * math.sin(yaw) + oy * math.cos(yaw)
    bottom = s * (b.origin.z - b.box_extent.z)
    a.set_actor_location(u.Vector(p[0] - wx, p[1] - wy, z - bottom), False, False)
    count += 1
    return a


# Coordinates: sea to +Y, promenade to -Y, 900 m beach running east-west.
place_mesh('Long beach / clear flight sand', (0, 0, 0), beach_mesh, mats['Sand'], collision=False)
# The commandlet drops complex collision for procedural meshes, so the bird would
# fall straight through the smooth beach. Lay an invisible stepped ramp of BlockAll
# boxes over the same beach_height surface so the bird lands on the dry sand.
# The dunes vary with X (13-216 m crests); a 50 m X step staircases them by up to
# ~50 cm, so the bird hovers above the sand. Denser X cells hug the dune surface.
for cx in range(-42500, 42501, 500):
    for cy in range(-200, 15801, 2000):
        collision_box('Beach collision', cx, cy, beach_height(cx, cy), 500, 2000)
place_mesh('Seabed', (0, 0, 0), seabed_mesh, mats['WetSand'])
place_mesh('Open bay', (0, 0, 0), ocean_mesh, mats['Water'])
part('Raised promenade', (0, -2400, 50), (90000, 1200, 200), 'Sterile')
part('Industrial hinterland', (0, -27000, -100), (150000, 48000, 500), 'Industry')
# Six continuous, shallow stair treads run the entire beach edge.
for i in range(6):
    h = (i + 1) * 25
    part('Tidewalk stair %02d' % i, (0, -1250 - i * 100, h / 2), (90000, 100, h), 'Concrete')
part('Promenade edge', (0, -1880, 153), (90000, 24, 6), 'Concrete', collision=False)
# A gently curving service road: the promenade and buildings stay straight, but
# the road weaves like a real coastal drive (a chain of rotated asphalt slabs).
def road_y(x):
    return -4400 + 260 * math.sin(x * 0.00012 + 2.2) + 120 * math.sin(x * 0.00030 + 5.0)


road_xs = list(range(-44000, 44001, 500))
for i in range(len(road_xs) - 1):
    x0, x1 = road_xs[i], road_xs[i + 1]
    y0, y1 = road_y(x0), road_y(x1)
    dx, dy = x1 - x0, y1 - y0
    seg = math.hypot(dx, dy)
    yaw = math.degrees(math.atan2(dy, dx))
    part('Service road', ((x0 + x1) / 2.0, (y0 + y1) / 2.0, 149), (seg + 60, 1400, 18), 'Road', rot=(0, yaw, 0))
    if i % 3 == 0:
        part('Road dash', ((x0 + x1) / 2.0, (y0 + y1) / 2.0, 160), (520, 16, 2), 'Ivory', rot=(0, yaw, 0), collision=False)
# Open, cloud-like art shelters: slim white piers beneath a cluster of soft,
# translucent white puffs. One puff keeps the counted "Shelter floating roof" tag.
for j, x in enumerate(range(-40000, 41000, 10000)):
    for dx in (-360, 360):
        part('Shelter swept pier', (x + dx, -2920, 335), (24, 40, 370), 'Sterile', rot=(0, 0, -8 if dx < 0 else 8))
    puffs = [(0, 0, 62), (-290, -130, 44), (290, -130, 44), (-170, 130, 36),
             (170, 130, 36), (0, -210, 30), (0, 210, 28)]
    for i, (dx, dy, r) in enumerate(puffs):
        # Every puff is a solid landing panel — the whole cloud roof is a perch
        # with no fly-through gaps. The central puff keeps the counted
        # "Shelter floating roof" tag for the world test intact.
        part('Shelter floating roof' if i == 0 else 'Shelter cloud puff',
             (x + dx, -2750 + dy, 540), (r * 6, r * 4.4, r * 1.2), 'Cloud', 'Sphere',
             collision=True)
    part('Picnic table', (x, -2740, 235), (320, 100, 12), 'Wood')
    for dx in (-115, 115):
        part('Table trestle', (x + dx, -2740, 196), (18, 85, 80), 'Sterile')
    for dy in (-115, 115):
        part('Picnic bench', (x, -2740 + dy, 202), (360, 36, 12), 'Wood')
        for dx in (-135, 135):
            part('Bench leg', (x + dx, -2740 + dy, 176), (14, 28, 45), 'Sterile')
    part('Wayfinding kiosk', (x + 480, -2900, 270), (55, 45, 240), 'Sterile')
    part('Kiosk face', (x + 480, -2875, 294), (44, 4, 135), 'Cloud', collision=False)
# Sterile white buildings behind the promenade; the yard and industrial accents
# (vents, tanks) keep a muted, grey-yellowed concrete tone.
for j, x in enumerate(range(-43000, 44000, 6500)):
    w = rng.uniform(4200, 5600)
    h = rng.uniform(850, 1700)
    y = -8500 - rng.uniform(0, 2500)
    part('Quiet warehouse %02d' % j, (x, y, 145 + h / 2), (w, 4600, h + 10), 'Sterile')
    part('Warehouse pale roof', (x, y, 160 + h), (w + 120, 4780, 60), 'Sterile')
    for dx in (-w * .32, 0, w * .32):
        part('Warehouse loading door', (x + dx, y + 2310, 430), (760, 20, 550), 'Window')
    part('Rooftop ventilation', (x + 600, y, 270 + h), (650, 500, 240), 'Industry')
    if j % 3 == 0:
        for dx in (0, 600):
            part('Utility tank', (x + dx, y - 3100, 745), (460, 460, 1210), 'Industry', 'Cylinder')
# Rolling coastal hills behind the yard break up the otherwise flat hinterland.
hrng = random.Random(77113)
for i in range(22):
    hx = hrng.uniform(-44500, 44500)
    hy = hrng.uniform(-32000, -19000)
    part('Coastal hill', (hx, hy, 150 - hrng.uniform(20, 100)),
         (hrng.uniform(7000, 15000), hrng.uniform(6000, 12000), hrng.uniform(420, 760)),
         'Earth', 'Sphere', rot=(0, hrng.uniform(0, 360), 0))
# Backshore scrub: real coastal vegetation instead of green spheres.
for i in range(180):
    x = rng.uniform(-44500, 44500)
    y = rng.uniform(-6500, -5400)
    kind = rng.choice(('Grass', 'Grass', 'Grass', 'Fir', 'Rock'))
    mesh = _pick_mesh(foliage_meshes[kind], rng)
    h = rng.uniform(60, 260) if kind == 'Grass' else rng.uniform(120, 320) if kind == 'Fir' else rng.uniform(50, 160)
    foliage('Backshore scrub', (x, y, 150), mesh, h, rot=(0, rng.uniform(0, 360), 0), ground_z=150)
# Layered coastal thickets: a soft gradient from marram grass through low bushes
# and saplings into a full treeline, so each far end of the beach closes into a
# lush wood instead of a sparse scatter. Mossy boulders break up the understory.
# Separate seeded RNG keeps other landmark placements stable as density changes.
vrng = random.Random(301026)
for side in (-1, 1):
    for i in range(900):
        x = side*vrng.uniform(40000, 44850)
        y = vrng.uniform(-850, 7600)
        # Irregular edge and openings, rather than a rectangular plantation.
        if abs(x) < 40900+500*math.sin(y*.0014) and vrng.random()<.5:
            continue
        foliage('Coastal grass west' if side<0 else 'Coastal grass east',
                (x,y,0), _pick_mesh(foliage_meshes['Grass'], vrng), vrng.uniform(55,140),
                rot=(0,vrng.uniform(0,360),0), ground_z=beach_height(x,y))
    for i in range(140):
        x = side*vrng.uniform(40900,44750)
        y = vrng.uniform(-700,7300)
        foliage('Coastal bush west' if side<0 else 'Coastal bush east',
                (x,y,0), _pick_mesh(foliage_meshes['Fir'], vrng), vrng.uniform(90,210),
                rot=(0,vrng.uniform(0,360),0), ground_z=beach_height(x,y))
    for i in range(120):
        x = side*vrng.uniform(40400,44900)
        y = vrng.uniform(-700,7200)
        foliage('Coastal boulder west' if side<0 else 'Coastal boulder east',
                (x,y,0), _pick_mesh(foliage_meshes['Rock'], vrng), vrng.uniform(40,150),
                rot=(0,vrng.uniform(0,360),0), ground_z=beach_height(x,y))
    for i in range(90):
        x = side*vrng.uniform(41600,44850)
        y = vrng.uniform(-650,6900)
        foliage('Coastal sapling west' if side<0 else 'Coastal sapling east',
                (x,y,0), _pick_mesh(foliage_meshes['Fir'], vrng), vrng.uniform(240,480),
                rot=(0,vrng.uniform(0,360),0), ground_z=beach_height(x,y))
    for i in range(70):
        x = side*vrng.uniform(42600,44900)
        y = vrng.uniform(-700,6800)
        foliage('Coastal forest west' if side<0 else 'Coastal forest east',
                (x,y,0), _pick_mesh(foliage_meshes['Fir'], vrng), vrng.uniform(500,820),
                rot=(0,vrng.uniform(0,360),0), ground_z=beach_height(x,y))
# Beach dune grasses: marram-like clumps that cluster on the slight elevations
# (dune crests) the onshore wind has built. They keep off the wet foreshore and
# the tidewalk, growing sparse on the flats and denser toward the higher berms
# — the wind-shaped "grass has manifested it" patches along the whole beach.
grng = random.Random(612345)
for i in range(2600):
    x = grng.uniform(-44500, 44500)
    y = grng.uniform(-900, 7800)
    h = beach_height(x, y)
    # Only the raised, dry dune berms carry grass; the wet foreshore stays bare.
    if h < 24.0:
        continue
    # Density rises with elevation — crests are thicker, flanks stay sparse.
    if grng.random() > 0.30 + 0.65 * min(1.0, (h - 24.0) / 95.0):
        continue
    foliage('Beach dune grass', (x, y, 0), _pick_mesh(foliage_meshes['Grass'], grng),
            grng.uniform(45, 115), rot=(0, grng.uniform(0, 360), 0), ground_z=h)
# Volcanic island, to the left, curves around the bay.
part('Basalt island foundation', (-37000, 26000, -600), (17000, 27000, 4400), 'Basalt', 'Sphere')
for i in range(60):
    a = rng.uniform(0, math.tau)
    r = math.sqrt(rng.random())
    x = -37000 + 7600 * r * math.cos(a)
    y = 26000 + 12300 * r * math.sin(a)
    z = -600 + 2200 * math.sqrt(max(0, 1 - r * r))
    part('Volcanic outcrop', (x, y, z + 100), (rng.uniform(500, 1500), rng.uniform(450, 1200), rng.uniform(500, 1600)),
         'Basalt', 'Rock' + str(i % 4), rot=(rng.uniform(-16, 16), rng.uniform(0, 360), rng.uniform(-16, 16)))
for i in range(25):
    a = i * math.tau / 25
    part('Island shore rock', (-37000 + 8000 * math.cos(a), 26000 + 12900 * math.sin(a), -60),
         (600, 850, 450), 'Basalt', 'Rock' + str(i % 4), rot=(0, i * 37, 0))
# Volleyball court: two poles, sparse dark mesh and white top tape.
for x in (11550, 12450):
    part('Volleyball post', (x, 4500, 130), (12, 12, 260), 'Teal', 'Cylinder')
part('Volleyball top tape', (12000, 4500, 243), (900, 5, 7), 'Ivory', collision=False)
for z in range(145, 245, 20):
    part('Volleyball mesh horizontal', (12000, 4500, z), (900, 2, 2), 'Window', collision=False)
for x in range(11550, 12451, 15):
    part('Volleyball mesh vertical', (x, 4500, 193), (2, 2, 100), 'Window', collision=False)
# Few abandoned beach items, no decorative clutter field.
part('Lifebuoy station', (-17000, -1000, 95), (18, 18, 190), 'Orange', 'Cylinder')
part('Driftwood', (23500, 7200, 16), (220, 23, 25), 'Wood', rot=(0, 28, 0))
part('Small beach ball', (10000, 5700, 14), (28, 28, 28), 'Orange', 'Sphere')
# Bright maritime light with a soft blue distance haze. The sun sits low over
# the open bay so its halo and the two parhelia (sun dogs) read clearly as a
# "three suns" apparition straddling the horizon.
def _rot_forward(pitch, yaw):
    cp, sp = math.cos(math.radians(pitch)), math.sin(math.radians(pitch))
    cy, sy = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
    return (cp * cy, cp * sy, sp)

# Golden sun over the open bay (the sea is +Y). UE5 Python's Rotator takes
# positional args as (roll, pitch, yaw) — a known API quirk — so use named args
# to stay unambiguous: pitch=-10 puts the disc ~10 degrees above the horizon —
# a LOW sun where the 22-degree halo and the two sun dogs stay bright and
# compact near the ring and read as a clear "three suns" apparition. yaw=-90
# aims the light shoreward so the sun hangs over the water.
sun_pitch, sun_yaw, sun_roll = -10.0, -90.0, 0.0
sun = u.EditorLevelLibrary.spawn_actor_from_class(
    u.DirectionalLight, u.Vector(0, 0, 10000),
    u.Rotator(pitch=sun_pitch, yaw=sun_yaw, roll=sun_roll))
sun.light_component.set_editor_property('intensity', 90000.)
sun.light_component.set_editor_property('atmosphere_sun_light', True)
try:
    sun.light_component.set_editor_property('light_color', u.Color(255, 238, 216))
except Exception:
    pass
sky = u.EditorLevelLibrary.spawn_actor_from_class(u.SkyLight, u.Vector(0, 0, 0))
sky.light_component.set_editor_property('real_time_capture', True)
sky.light_component.set_editor_property('intensity', 1.2)
u.EditorLevelLibrary.spawn_actor_from_class(u.SkyAtmosphere, u.Vector(0, 0, 0))
fog = u.EditorLevelLibrary.spawn_actor_from_class(u.ExponentialHeightFog, u.Vector(0, 0, 0))
fog.component.set_editor_property('fog_density', .0008)
try:
    fog.component.set_editor_property('fog_inscattering_color', u.LinearColor(.55, .68, .82))
except Exception:
    pass
# Photorealistic light pass: the parhelion is an ATMOSPHERIC sky-dome material
# (world-space, depth-tested, spawned by the flight pawn so it follows the
# camera); bloom/SSR below are camera-side optics only.
fw = _rot_forward(sun_pitch, sun_yaw)
sun_dir = (-fw[0], -fw[1], -fw[2])
if PARHELION_FORCE_WEATHER:
    weather_name = PARHELION_FORCE_WEATHER
    weather = dict(PARHELION_BY_NAME[weather_name])
else:
    weather_name, weather = pick_parhelion_weather(WEATHER_SEED)
u.log('PARHELION_WEATHER=' + weather_name)
sun_parhelion_material(sun_dir, weather)
apply_post_process()
fpv_fisheye_material()
# Saved viewpoints also make the generated map easy to inspect in the editor.
for name, p, target in [('Sun horizon', (0, -6000, 400), (0, 13226, 5912)),
                        ('Bay overlook', (12000, -14000, 15000), (-12000, 10000, 0)),
                        ('Tidewalk', (-1500, -500, 650), (1000, -2700, 250)),
                        ('Basalt cove', (-22000, 12000, 5000), (-37000, 26000, 500)),
                        ('Shoreline', (0, 7900, 220), (3500, 12500, -50)),
                        ('East vegetation', (38700, 3000, 320), (43700, 4800, 240)),
                        ('West vegetation', (-38500, 2800, 380), (-43800, 4800, 220))]:
    a = u.EditorLevelLibrary.spawn_actor_from_class(u.CameraActor, u.Vector(*p),
                                                    u.MathLibrary.find_look_at_rotation(u.Vector(*p), u.Vector(*target)))
    a.set_actor_label(name)
    a.set_editor_property('tags', [name])
    a.camera_component.set_editor_property('field_of_view', 70.)
u.EditorLevelLibrary.set_level_viewport_camera_info(u.Vector(12000, -14000, 15000),
                                                    u.MathLibrary.find_look_at_rotation(u.Vector(12000, -14000, 15000), u.Vector(-12000, 10000, 0)))
radio()
u.EditorAssetLibrary.make_directory('/Game/Shiomori/Maps')
assert u.EditorLoadingAndSavingUtils.save_map(world, '/Game/Shiomori/Maps/SHIOMORI')
u.log('SHIOMORI_MAP_READY actors=' + str(count))
