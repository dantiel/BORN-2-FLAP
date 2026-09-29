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
    lib.connect_material_expressions(a, output, b, pin)


def output(n, prop, pin=''):
    lib.connect_material_property(n, pin, getattr(u.MaterialProperty, 'MP_' + prop))


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
    ela.save_asset(path)
    return tex


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
    lib.recompile_material(m)
    ela.save_asset(path)
    return m


def water_material(name, color, rough=0.08, specular=0.6, opacity=0.85,
                   swell_scale=0.003, swell_amp=0.6, ripple_amp=0.06,
                   shore_color=(0.05, 0.30, 0.32), waves=None):
    """Translucent ocean surface with real vertex-displaced parallel swells.

    A three-octave travelling normal (long swell + chop + fine glitter ripple)
    drives the specular shimmer and a shore fade grades turquoise shallows into
    open-water colour. The surface itself is displaced by WorldPositionOffset so
    the sea visibly rolls shoreward; Single Layer Water cannot take WPO, so this
    is a standard translucent material — robust and clearly visible.
    """
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
    m.set_editor_property('two_sided', True)
    p = node(m, 'WorldPosition')
    t = node(m, 'Time')
    # Shore fade: turquoise shallows grade into open-water colour with distance.
    bc = custom(m, '''float s=saturate((P.y-10000.0)/8000.0);
return lerp(float3(%.6g,%.6g,%.6g), float3(%.6g,%.6g,%.6g), s);''' % (
        shore_color[0], shore_color[1], shore_color[2],
        color[0], color[1], color[2]), {'P': p})
    output(bc, 'BASE_COLOR')
    output(node(m, 'Constant', r=rough), 'ROUGHNESS')
    output(node(m, 'Constant', r=specular), 'SPECULAR')
    output(node(m, 'Constant', r=opacity), 'OPACITY')
    # Three-octave travelling normal: long swell, chop, and fine glitter ripple.
    n = custom(m, '''float2 q=P.xy*%.6g;
float2 n=float2(cos(q.x*0.8+q.y*0.42+T*0.9),sin(q.y*1.1-q.x*0.3-T*0.7))*%.6g;
n+=float2(sin(q.x*2.7+q.y*1.3-T*1.6),cos(q.y*2.4-q.x*0.9+T*1.4))*%.6g;
float2 r=P.xy*0.022;
n+=float2(sin(r.x*1.3+r.y*0.9+T*3.2),cos(r.y*1.7-r.x*0.5-T*2.8))*%.6g;
return normalize(float3(n,1));''' % (swell_scale, swell_amp, swell_amp * 0.36, ripple_amp),
               {'P': p, 'T': t})
    if waves is not None:
        uv = node(m, 'TextureCoordinate')
        rep = node(m, 'Multiply')
        rep.set_editor_property('const_b', 300.0)
        wire(uv, rep, 'A')
        pw = node(m, 'Panner', speed_x=0.0, speed_y=-0.06)
        wire(rep, pw, 'Coordinate')
        sw = node(m, 'TextureSample', texture=waves)
        sw.set_editor_property('sampler_type', u.MaterialSamplerType.SAMPLERTYPE_NORMAL)
        wire(pw, sw, 'UVs')
        bw = node(m, 'LinearInterpolate', const_alpha=0.45)
        wire(n, bw, 'A')
        wire(sw, bw, 'B', 'RGB')
        n = bw
    output(n, 'NORMAL')
    # Real vertex-displaced parallel swells: the surface rolls shoreward (-Y),
    # amplitude ramping from flat at the waterline to full offshore. This is what
    # makes the sea actually undulate instead of merely shimmering per-pixel.
    wpo = custom(m, '''float shore=smoothstep(10000.0,16000.0,P.y);
float h=sin(P.y*0.0016+T*0.85)*80.0;
h+=sin(P.y*0.0028+T*1.35+sin(P.x*0.00035)*1.8)*40.0;
h+=sin(P.y*0.0041-T*1.9)*18.0;
h+=sin(P.x*0.0009+T*0.6)*22.0;
return float3(0.0,0.0,h*shore);''', {'P': p, 'T': t}, 3)
    output(wpo, 'WORLD_POSITION_OFFSET')
    lib.recompile_material(m)
    ela.save_asset(path)
    return m


def sand_material(name, diffuse, normal, rough=0.8, rough_tex=None, tiling_cm=300.0,
                  relief_scale=0.02, relief_strength=0.6, normal_strength=0.6,
                  tint=None):
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
    output(bc, 'BASE_COLOR')
    if rough_tex is not None:
        output(sample_tex(m, rough_tex, ('world', tiling_cm)), 'ROUGHNESS', 'R')
    else:
        output(node(m, 'Constant', r=rough), 'ROUGHNESS')
    ns = sample_tex(m, normal, ('world', tiling_cm), normal=True)
    p = node(m, 'WorldPosition')
    code = '''float2 q=P.xy*%.6g;
float rx=sin(q.x*1.0+q.y*0.6)*0.5+sin(q.x*2.3-q.y*1.7)*0.25+sin(q.x*4.7+q.y*3.1)*0.12;
float ry=sin(q.y*1.1-q.x*0.4)*0.5+sin(q.y*2.7+q.x*1.3)*0.25+sin(q.y*4.3-q.x*2.1)*0.12;
float3 tn=normalize(float3(TN.x*2.0-1.0, TN.y*2.0-1.0, saturate(TN.z*2.0-1.0)+0.25));
float3 r=normalize(float3(tn.x+rx*%.6g, tn.y+ry*%.6g, 1.0));
return normalize(float3(lerp(0.0, r.x, %.6g), lerp(0.0, r.y, %.6g), 1.0));
''' % (relief_scale, relief_strength, relief_strength, normal_strength, normal_strength)
    n = node(m, 'Custom', code=code,
             output_type=u.CustomMaterialOutputType.CMOT_FLOAT3)
    pins = []
    for k in ('P', 'TN'):
        pin = u.CustomInput()
        pin.set_editor_property('input_name', k)
        pins.append(pin)
    n.set_editor_property('inputs', pins)
    wire(p, n, 'P')
    wire(ns, n, 'TN', 'RGB')
    output(n, 'NORMAL')
    lib.recompile_material(m)
    ela.save_asset(path)
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
                                normal_strength=0.5, tint=(0.52, 0.47, 0.4))
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
mats['Water'] = water_material('Water', (0.02, 0.10, 0.22), rough=0.08, specular=0.6, waves=wave_n)
mats['Shallows'] = water_material('Shallows', (0.02, 0.18, 0.20), rough=0.12, specular=0.6,
                                  swell_scale=0.01, swell_amp=0.6, waves=wave_n)
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
meshes = {n: u.load_asset('/Engine/BasicShapes/' + n) for n in ('Cube', 'Sphere', 'Cylinder', 'Cone', 'Plane')}
meshes.update({'Rock' + str(i): u.load_asset('/Game/Nature/SM_Rock' + str(i)) for i in range(4)})
foliage_meshes = {
    'Grass': [u.load_asset('/Game/Nature/SM_Grass%d' % i) for i in range(3)],
    'Fir': [u.load_asset('/Game/Nature/SM_Fir%d' % i) for i in range(3)],
    'Rock': [u.load_asset('/Game/Nature/SM_Rock%d' % i) for i in range(4)],
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
        md.create_triangle(pg, [insts[a], insts[b], insts[c]])
    sm.build_from_static_mesh_descriptions([md])
    ela.save_asset(path)
    return sm


def place_mesh(label, p, mesh, mat, rot=(0, 0, 0), collision=False):
    """Spawn a pre-built (world-sized) mesh actor; no scaling applied."""
    global count
    a = u.EditorLevelLibrary.spawn_actor_from_class(u.StaticMeshActor, u.Vector(*p), u.Rotator(*rot))
    a.set_actor_label(label)
    a.set_editor_property('tags', [label])
    c = a.static_mesh_component
    c.set_static_mesh(mesh)
    c.set_material(0, mat)
    c.set_collision_profile_name('BlockAll' if collision else 'NoCollision')
    count += 1
    return a


def waterline(x):
    return 10000 + 300 * math.sin(x * 0.00042 + 1.7) + 190 * math.sin(x * 0.0011 + 4.2) + 80 * math.sin(x * 0.0024 + 0.6)


def shore_detail(x):
    """High-frequency organic jitter so the waterline reads as a natural coast,
    not a clean sine curve."""
    return 90 * math.sin(x * 0.0037 + 1.1) + 55 * math.sin(x * 0.0079 + 3.4) + 30 * math.sin(x * 0.0143 + 5.7)


def make_ocean_grid(ncols=384, nrows=384, half=200000.0, cy=55000.0, z=-50.0):
    """Subdivided 4 km x 4 km plane for vertex-displaced swells (~10 m spacing)."""
    verts, uvs, tris = [], [], []
    for j in range(nrows):
        y = cy - half + (2.0 * half) * j / (nrows - 1)
        for i in range(ncols):
            x = -half + (2.0 * half) * i / (ncols - 1)
            verts.append((x, y, z))
            uvs.append((i / (ncols - 1), j / (nrows - 1)))
    stride = ncols
    for j in range(nrows - 1):
        for i in range(ncols - 1):
            a = j * stride + i
            tris.append((a, a + 1, a + stride + 1))
            tris.append((a, a + stride + 1, a + stride))
    return verts, tris, uvs


def make_ribbon(x0, x1, ncols, nrows, y_inshore, y_offshore, z_inshore, z_offshore, jitter=0.0):
    """A smooth strip following the waterline. y/z are lerped inshore->offshore."""
    verts, uvs, tris = [], [], []
    for j in range(nrows + 1):
        t = j / nrows
        for i in range(ncols + 1):
            x = x0 + (x1 - x0) * i / ncols
            wl = waterline(x) + shore_detail(x)
            y = y_inshore + (y_offshore - y_inshore) * t
            if jitter:
                y += jitter * math.sin(x * 0.0053 + t * 2.1)
            z = z_inshore + (z_offshore - z_inshore) * t
            verts.append((x, wl + y, z))
            uvs.append((i / ncols, t))
    stride = ncols + 1
    for j in range(nrows):
        for i in range(ncols):
            a = j * stride + i
            tris.append((a, a + 1, a + stride + 1))
            tris.append((a, a + stride + 1, a + stride))
    return verts, tris, uvs


ocean_mesh = build_mesh('OceanGrid', *make_ocean_grid())
wet_sand_mesh = build_mesh('ShoreWetSand', *make_ribbon(-45000.0, 45000.0, 720, 5, -550.0, 300.0, -5.0, -90.0))
foam_mesh = build_mesh('ShoreFoam', *make_ribbon(-45000.0, 45000.0, 900, 3, 120.0, 440.0, -47.0, -47.0, jitter=40.0))


def part(label, p, size, mat='Concrete', shape='Cube', rot=(0, 0, 0), collision=True):
    global count
    a = u.EditorLevelLibrary.spawn_actor_from_class(u.StaticMeshActor, u.Vector(*p), u.Rotator(*rot))
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
    a = u.EditorLevelLibrary.spawn_actor_from_class(u.StaticMeshActor, u.Vector(*p), u.Rotator(*rot))
    a.set_actor_label(label)
    a.set_editor_property('tags', [label])
    c = a.static_mesh_component
    c.set_static_mesh(mesh)
    c.set_collision_profile_name('NoCollision')
    b = mesh.get_bounds().box_extent
    s = height / max(2 * b.z, 0.001)
    a.set_actor_scale3d(u.Vector(s, s, s))
    if ground_z is not None:
        origin, extent = a.get_actor_bounds(False)
        loc = a.get_actor_location()
        a.set_actor_location(u.Vector(loc.x, loc.y, ground_z + (loc.z - origin.z)), False, False)
    count += 1
    return a


# Coordinates: sea to +Y, promenade to -Y, 900 m beach running east-west.
part('Long beach / clear flight sand', (0, 4400, -70), (90000, 11200, 140), 'Sand')
part('Seabed', (0, 45000, -650), (250000, 200000, 400), 'WetSand')
# Real vertex-displaced ocean surface (subdivided grid) instead of a flat quad.
place_mesh('Open bay', (0, 0, 0), ocean_mesh, mats['Water'])
# Natural shoreline: a single smooth wet-sand ribbon sloping into the surf and a
# continuous rolling foam line, both following the meandering waterline.
place_mesh('Wet-sand ribbon', (0, 0, 0), wet_sand_mesh, mats['WetSand'])
place_mesh('Surf foam ribbon', (0, 0, 0), foam_mesh, mats['Foam'])
# Shallow sand shelf: the seabed rises toward the beach so the water is shallow
# (turquoise) inshore and deep (blue) offshore.
part('Shallow sand shelf', (0, 17500, -225), (90000, 15000, 310), 'WetSand', rot=(0, 0, -1.18), collision=False)
part('Raised promenade', (0, -2650, 50), (90000, 1700, 200), 'Sterile')
part('Industrial hinterland', (0, -27000, -100), (150000, 48000, 500), 'Industry')
# Six continuous, shallow stair treads run the entire beach edge.
for i in range(6):
    h = (i + 1) * 25
    part('Tidewalk stair %02d' % i, (0, -1250 - i * 100, h / 2), (90000, 100, h), 'Sterile')
part('Promenade edge', (0, -1880, 153), (90000, 24, 6), 'Sterile', collision=False)
part('Service road', (0, -4400, 154), (100000, 1400, 8), 'Road')
for x in range(-44000, 45000, 1300):
    part('Road dash', (x, -4400, 160), (520, 16, 2), 'Ivory', collision=False)
# Open, cloud-like art shelters: slim white piers beneath a cluster of soft,
# translucent white puffs. One puff keeps the counted "Shelter floating roof" tag.
for j, x in enumerate(range(-40000, 41000, 10000)):
    for dx in (-360, 360):
        part('Shelter swept pier', (x + dx, -2920, 335), (24, 40, 370), 'Sterile', rot=(0, 0, -8 if dx < 0 else 8))
    puffs = [(0, 0, 62), (-290, -130, 44), (290, -130, 44), (-170, 130, 36),
             (170, 130, 36), (0, -210, 30), (0, 210, 28)]
    for i, (dx, dy, r) in enumerate(puffs):
        part('Shelter floating roof' if i == 0 else 'Shelter cloud puff',
             (x + dx, -2750 + dy, 540), (r * 6, r * 4.4, r * 1.2), 'Cloud', 'Sphere', collision=False)
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
    part('Quiet warehouse %02d' % j, (x, y, 150 + h / 2), (w, 4600, h), 'Sterile')
    part('Warehouse pale roof', (x, y, 160 + h), (w + 120, 4780, 60), 'Sterile')
    for dx in (-w * .32, 0, w * .32):
        part('Warehouse loading door', (x + dx, y + 2310, 430), (760, 20, 550), 'Window')
    part('Rooftop ventilation', (x + 600, y, 270 + h), (650, 500, 240), 'Industry')
    if j % 3 == 0:
        for dx in (0, 600):
            part('Utility tank', (x + dx, y - 3100, 750), (460, 460, 1200), 'Industry', 'Cylinder')
# Backshore scrub: real coastal vegetation instead of green spheres.
for i in range(180):
    x = rng.uniform(-44500, 44500)
    y = rng.uniform(-6500, -5400)
    kind = rng.choice(('Grass', 'Grass', 'Grass', 'Fir', 'Rock'))
    mesh = rng.choice(foliage_meshes[kind])
    h = rng.uniform(60, 260) if kind == 'Grass' else rng.uniform(120, 320) if kind == 'Fir' else rng.uniform(50, 160)
    foliage('Backshore scrub', (x, y, 150), mesh, h, rot=(0, rng.uniform(0, 360), 0), ground_z=150)
# Lush dune vegetation at the east end of the beach: a layered tangle of tall
# firs, dense dune grass, low scrub and a few rocks spilling toward the surf.
for i in range(150):
    x = rng.uniform(39700, 44900)
    y = rng.uniform(-900, 8600)
    kind = rng.choice(('Grass', 'Grass', 'Grass', 'Fir', 'Fir', 'Rock'))
    mesh = rng.choice(foliage_meshes[kind])
    if kind == 'Fir':
        h = rng.uniform(200, 540)
    elif kind == 'Rock':
        h = rng.uniform(40, 130)
    else:
        h = rng.uniform(50, 230)
    foliage('Dune-end lush', (x, y, 0), mesh, h, rot=(0, rng.uniform(0, 360), 0), ground_z=0)
# A dense fir copse anchors the far tip, where the beach meets the water.
for i in range(45):
    x = rng.uniform(42800, 44900)
    y = rng.uniform(1500, 7200)
    mesh = rng.choice(foliage_meshes['Fir'])
    foliage('Dune-end fir copse', (x, y, 0), mesh, rng.uniform(260, 640), rot=(0, rng.uniform(0, 360), 0), ground_z=0)
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
# Bright maritime light with a soft blue distance haze.
sun = u.EditorLevelLibrary.spawn_actor_from_class(u.DirectionalLight, u.Vector(0, 0, 10000), u.Rotator(-38, -48, 0))
sun.light_component.set_editor_property('intensity', 70000.)
sun.light_component.set_editor_property('atmosphere_sun_light', True)
try:
    sun.light_component.set_editor_property('light_color', u.Color(255, 240, 222))
except Exception:
    pass
sky = u.EditorLevelLibrary.spawn_actor_from_class(u.SkyLight, u.Vector(0, 0, 0))
sky.light_component.set_editor_property('real_time_capture', True)
sky.light_component.set_editor_property('intensity', 1.2)
u.EditorLevelLibrary.spawn_actor_from_class(u.SkyAtmosphere, u.Vector(0, 0, 0))
fog = u.EditorLevelLibrary.spawn_actor_from_class(u.ExponentialHeightFog, u.Vector(0, 0, 0))
fog.component.set_editor_property('fog_density', .0015)
try:
    fog.component.set_editor_property('fog_inscattering_color', u.LinearColor(.55, .68, .82))
except Exception:
    pass
# Planar reflection on the water surface: real mirrored sky/building/vegetation
# reflections for the Single Layer Water, instead of normal-shimmer alone.
refl = u.EditorLevelLibrary.spawn_actor_from_class(u.PlanarReflection, u.Vector(0, 18000, 0), u.Rotator(0, 0, 0))
refl.set_actor_label('Water planar reflection')
pc = refl.get_component_by_class(u.PlanarReflectionComponent)
if pc:
    try:
        pc.set_editor_property('normal_distortion_strength', 80.0)
    except Exception:
        pass
# Saved viewpoints also make the generated map easy to inspect in the editor.
for name, p, target in [('Bay overlook', (12000, -14000, 15000), (-12000, 10000, 0)),
                        ('Tidewalk', (-1500, -500, 650), (1000, -2700, 250)),
                        ('Basalt cove', (-22000, 12000, 5000), (-37000, 26000, 500))]:
    a = u.EditorLevelLibrary.spawn_actor_from_class(u.CameraActor, u.Vector(*p),
                                                    u.MathLibrary.find_look_at_rotation(u.Vector(*p), u.Vector(*target)))
    a.set_actor_label(name)
    a.set_editor_property('tags', [name])
    a.camera_component.set_editor_property('field_of_view', 70.)
u.EditorLevelLibrary.set_level_viewport_camera_info(u.Vector(12000, -14000, 15000),
                                                    u.MathLibrary.find_look_at_rotation(u.Vector(12000, -14000, 15000), u.Vector(-12000, 10000, 0)))
u.EditorAssetLibrary.make_directory('/Game/Shiomori/Maps')
assert u.EditorLoadingAndSavingUtils.save_map(world, '/Game/Shiomori/Maps/SHIOMORI')
u.log('SHIOMORI_MAP_READY actors=' + str(count))