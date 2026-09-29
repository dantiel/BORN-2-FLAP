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
            wire(wp, mask, '', 'XYZ')
            wire(mask, sc, 'A')
        else:
            sc.set_editor_property('const_b', val)
            uv = node(m, 'TextureCoordinate')
            wire(uv, sc, 'A')
        wire(sc, s, 'Coordinates')
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
        wire(wp, sc, 'A', 'XYZ')
        nz = node(m, 'Noise', levels=2, output_min=0.85, output_max=1.0)
        wire(sc, nz, 'Position')
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


def water_material(name, color, rough, specular, ripple, fine, wave_strength=0.35,
                   fine_strength=0.18, waves=None, wave_repeats=300.0, wave_speed=-0.06):
    path = '/Game/Shiomori/Materials/M_' + name
    m = u.load_asset(path) if ela.does_asset_exist(path) else assets.create_asset(
        'M_' + name, '/Game/Shiomori/Materials', u.Material, u.MaterialFactoryNew())
    lib.delete_all_material_expressions(m)
    output(node(m, 'Constant3Vector', constant=u.LinearColor(*color)), 'BASE_COLOR')
    output(node(m, 'Constant', r=rough), 'ROUGHNESS')
    output(node(m, 'Constant', r=specular), 'SPECULAR')
    flat = node(m, 'Constant3Vector', constant=u.LinearColor(0, 0, 1))
    # Parallel swell: a directional wave normal, tiled and panned toward the
    # shore (-V, i.e. -Y) so long crests roll in as the surf approaches.
    n = flat
    if waves is not None:
        uv = node(m, 'TextureCoordinate')
        rep = node(m, 'Multiply')
        rep.set_editor_property('const_b', wave_repeats)
        wire(uv, rep, 'A')
        pw = node(m, 'Panner', speed_x=0.0, speed_y=wave_speed)
        wire(rep, pw, 'Coordinate')
        sw = node(m, 'TextureSample', texture=waves)
        sw.set_editor_property('sampler_type', u.MaterialSamplerType.SAMPLERTYPE_NORMAL)
        wire(pw, sw, 'Coordinates')
        bw = node(m, 'LinearInterpolate', const_alpha=0.55)
        wire(flat, bw, 'A')
        wire(sw, bw, 'B', 'RGB')
        n = bw
    p1 = node(m, 'Panner', speed_x=0.02, speed_y=0.028)
    s1 = node(m, 'TextureSample', texture=ripple)
    s1.set_editor_property('sampler_type', u.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    wire(p1, s1, 'Coordinates')
    b1 = node(m, 'LinearInterpolate', const_alpha=wave_strength)
    wire(n, b1, 'A')
    wire(s1, b1, 'B', 'RGB')
    p2 = node(m, 'Panner', speed_x=-0.014, speed_y=0.01)
    s2 = node(m, 'TextureSample', texture=fine)
    s2.set_editor_property('sampler_type', u.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    wire(p2, s2, 'Coordinates')
    b2 = node(m, 'LinearInterpolate', const_alpha=fine_strength)
    wire(b1, b2, 'A')
    wire(s2, b2, 'B', 'RGB')
    output(b2, 'NORMAL')
    fres = node(m, 'Fresnel')
    sheen = node(m, 'Multiply')
    tint = node(m, 'Constant3Vector', constant=u.LinearColor(0.55, 0.72, 0.85))
    wire(fres, sheen, 'A')
    wire(tint, sheen, 'B')
    output(sheen, 'EMISSIVE_COLOR')
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
ripple_n = u.load_asset('/Game/Nature/Textures/T_rock_04_nor_dx') or sand_n


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
mats['Sand'] = pbr_material('Sand', diffuse=sand_d, normal=sand_n, rough_tex=sand_r,
                            tiling=('world', 150.0), normal_strength=0.5, noise_var=0.002)
mats['WetSand'] = pbr_material('WetSand', diffuse=sand_d, normal=sand_n, rough=0.35,
                               tiling=('world', 150.0), normal_strength=0.4,
                               tint=(0.52, 0.47, 0.4))
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
mats['Water'] = water_material('Water', (0.02, 0.14, 0.19), 0.12, 0.8, ripple_n, sand_n,
                               waves=wave_n)
mats['Shallows'] = water_material('Shallows', (0.03, 0.3, 0.33), 0.2, 0.6, ripple_n, sand_n,
                                  wave_strength=0.25, fine_strength=0.14, waves=wave_n,
                                  wave_repeats=500.0, wave_speed=-0.09)
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
wire(wp, sc, 'A', 'XYZ')
nz = node(foam, 'Noise', levels=3, output_min=0.0, output_max=1.0)
wire(sc, nz, 'Position')
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
wire(wp, sc, 'A', 'XYZ')
nz = node(cloud, 'Noise', levels=4, output_min=0.2, output_max=1.0)
wire(sc, nz, 'Position')
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
part('Open bay', (0, 55000, -50), (400000, 400000, 1), 'Water', 'Plane', collision=False)
# Gentle sculpted shoreline: wet sand sloping from the beach into the surf.
part('Shoreline wet-sand slope', (0, 10400, -25), (90000, 1000, 4), 'WetSand', rot=(0, 0, -2.86), collision=False)
part('Shallow turquoise margin', (0, 11400, -48), (90000, 2000, 1), 'Shallows', 'Plane', collision=False)
for i in range(40):
    part('Broken surf line', (-44000 + i * 2200 + rng.uniform(-200, 200), 10050 + rng.uniform(-30, 60), -45),
         (rng.uniform(400, 1200), rng.uniform(20, 70), 1), 'Foam', 'Plane', collision=False)
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
# A bushy end of the beach; the main sand stays intentionally uncluttered.
for i in range(65):
    x = rng.uniform(40200, 44900)
    y = rng.uniform(-800, 7500)
    kind = rng.choice(('Grass', 'Grass', 'Fir', 'Fir'))
    mesh = rng.choice(foliage_meshes[kind])
    h = rng.uniform(70, 200)
    foliage('Dune-end bush', (x, y, 0), mesh, h, rot=(0, rng.uniform(0, 360), 0), ground_z=0)
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