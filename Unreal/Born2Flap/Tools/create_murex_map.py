"""Murex Spire Aerodrome — "the Living Hub". Deterministic procedural airport.

Canonical concept: docs/murex-spire-aerodrome.md. Seed 290926 is the single
source of truth for the whole layout. All geometry is code-authored primitives
(no downloads, no scans) following the established generator pattern
(create_yunsha_map.py). World units are centimetres.

Layout: a 4x4 km airport core and distant horizon; a continuous Murex shell
lying along +X with a park-facing aperture, recessed glazed main hall, one
upward tower spine and six diminishing airside gate spines with scale aircraft.
The architectural proportions follow golden-ratio growth and Fibonacci rhythm.
"""
import math
import re
import random
import sys
from pathlib import Path
import unreal as u

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from murex_shell_geometry import build_scene, TOWER_BASE, TOWER_DECK, GALLERY
from project_guard import require_project
require_project()

A = u.AssetToolsHelpers.get_asset_tools()
E = u.EditorAssetLibrary
M = u.MaterialEditingLibrary
ROOT = '/Game/Murex'
rng = random.Random(290926)

# --- GDD parameter table (docs/murex-spire-aerodrome.md) -------------------
SEED = 290926
RUNWAY_LENGTH = 3200.0      # m, along the baseline
RUNWAY_HALF = 160000.0      # cm
RUNWAY_HALF_W = 3000.0      # cm (60 m)
HOTSPOT_DENSITY = 1.0
CORE = 200000.0             # cm (2 km half-extent -> 4x4 km core)
TOWER_POS = (TOWER_BASE[0]*100, TOWER_BASE[1]*100)
BASINS = [(-150000.0, 150000.0, 32000.0, 22000.0),
          (135000.0, 145000.0, 28000.0, 20000.0),
          (150000.0, -140000.0, 26000.0, 18000.0)]

for folder in ('Maps', 'Meshes', 'Materials'):
    E.make_directory(ROOT + '/' + folder)
world = u.EditorLoadingAndSavingUtils.new_blank_map(False)
world.get_world_settings().set_editor_property('force_no_precomputed_lighting', True)
generated_meshes = set()

# --------------------------------------------------------------------------
# Material graph helpers (pin names per the verified UE5.8 convention)
# --------------------------------------------------------------------------
def expr(mat, kind, **properties):
    n = M.create_material_expression(mat, getattr(u, 'MaterialExpression' + kind))
    for k, v in properties.items():
        n.set_editor_property(k, v)
    return n

def link(a, b, pin, output=''):
    assert M.connect_material_expressions(a, output, b, '' if pin == 'Input' else pin), \
        'Invalid material connection ' + pin

def out(n, prop, output=''):
    M.connect_material_property(n, output, getattr(u.MaterialProperty, 'MP_' + prop))

def scalar(m, value):
    return expr(m, 'Constant', r=value)

def color(m, c):
    return expr(m, 'Constant3Vector', constant=u.LinearColor(*c, 1))

def custom(mat, code, inputs):
    n = expr(mat, 'Custom', code=code, output_type=u.CustomMaterialOutputType.CMOT_FLOAT3)
    pins = []
    for name in inputs:
        p = u.CustomInput()
        p.set_editor_property('input_name', name)
        pins.append(p)
    n.set_editor_property('inputs', pins)
    for name, value in inputs.items():
        link(value, n, name)
    return n

NOISE_HLSL = ('float2 q = P.xy / Scale; float2 p = floor(q); float2 f = frac(q);\n'
              'f = f*f*(3.0-2.0*f);\n'
              'float a = frac(sin(dot(p, float2(12.9898,78.233)))*43758.5453);\n'
              'float b = frac(sin(dot(p+float2(1,0), float2(12.9898,78.233)))*43758.5453);\n'
              'float c = frac(sin(dot(p+float2(0,1), float2(12.9898,78.233)))*43758.5453);\n'
              'float d = frac(sin(dot(p+float2(1,1), float2(12.9898,78.233)))*43758.5453);\n'
              'float n = lerp(lerp(a,b,f.x),lerp(c,d,f.x),f.y);\n'
              'return float3(n, n, n);')

def material(name, tint, metal=0.0, rough=0.9, noise_scale=0.0, noise_amount=0.15,
             emissive=0.0, glass=False, translucent=False, opacity=1.0):
    path = ROOT + '/Materials/M_' + name
    mat = u.load_asset(path) if E.does_asset_exist(path) \
        else A.create_asset('M_' + name, ROOT + '/Materials', u.Material, u.MaterialFactoryNew())
    M.delete_all_material_expressions(mat)
    base = color(mat, tint)
    if noise_scale > 0:
        n = custom(mat, NOISE_HLSL, {'P': expr(mat, 'WorldPosition'), 'Scale': scalar(mat, noise_scale)})
        blend = expr(mat, 'LinearInterpolate')
        link(color(mat, (1 - noise_amount, 1 - noise_amount, 1 - noise_amount)), blend, 'A')
        link(color(mat, (1 + noise_amount, 1 + noise_amount, 1 + noise_amount)), blend, 'B')
        link(n, blend, 'Alpha')
        mul = expr(mat, 'Multiply')
        link(base, mul, 'A')
        link(blend, mul, 'B')
        base = mul
    out(base, 'BASE_COLOR')
    out(scalar(mat, rough), 'ROUGHNESS')
    out(scalar(mat, metal), 'METALLIC')
    if emissive > 0:
        glow = expr(mat, 'Multiply')
        link(base, glow, 'A')
        link(scalar(mat, emissive), glow, 'B')
        out(glow, 'EMISSIVE_COLOR')
    if glass:
        mat.set_editor_property('blend_mode', u.BlendMode.BLEND_TRANSLUCENT)
        f = expr(mat, 'Fresnel', exponent=1.4, base_reflect_fraction=0.0)
        mul = expr(mat, 'Multiply')
        link(f, mul, 'A')
        link(scalar(mat, 0.4), mul, 'B')
        alpha = expr(mat, 'Add')
        link(mul, alpha, 'A')
        link(scalar(mat, 0.45), alpha, 'B')
        out(alpha, 'OPACITY')
        out(scalar(mat, 0.35), 'METALLIC')
        out(scalar(mat, 0.12), 'ROUGHNESS')
    elif translucent:
        mat.set_editor_property('blend_mode', u.BlendMode.BLEND_TRANSLUCENT)
        out(scalar(mat, opacity), 'OPACITY')
    M.set_material_usage(mat, u.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES)
    mat.set_editor_property('two_sided', True)
    M.recompile_material(mat)
    assert E.save_loaded_asset(mat)
    return mat

asphalt = material('Asphalt', (0.16, 0.16, 0.17), rough=0.94, noise_scale=600, noise_amount=0.18)
asphalt_dark = material('AsphaltDark', (0.09, 0.09, 0.10), rough=0.96)
concrete = material('Concrete', (0.66, 0.66, 0.63), rough=0.88, noise_scale=900, noise_amount=0.12)
grass = material('GrassBleached', (0.47, 0.50, 0.31), rough=1.0, noise_scale=1700, noise_amount=0.18)
wetland = material('Wetland', (0.10, 0.15, 0.14), metal=0.15, rough=0.42)
white_paint = material('WhitePaint', (0.86, 0.86, 0.84), rough=0.5, emissive=0.05)
yellow_paint = material('YellowPaint', (0.80, 0.62, 0.15), rough=0.5, emissive=0.05)
steel = material('Steel', (0.42, 0.44, 0.48), metal=0.85, rough=0.38)
glass_mat = material('Glass', (0.55, 0.72, 0.82), glass=True)
tower_shaft = material('TowerShaft', (0.62, 0.60, 0.56), rough=0.85, noise_scale=400, noise_amount=0.10)
skyline = material('Skyline', (0.52, 0.56, 0.66), rough=1.0, emissive=0.12)
horizon_ground = material('HorizonGround', (0.45, 0.48, 0.38), rough=1.0)
hill = material('Hill', (0.42, 0.46, 0.34), rough=1.0, noise_scale=800, noise_amount=0.30)
amber_light = material('AmberLight', (1.0, 0.75, 0.25), rough=0.4, emissive=10)
green_light = material('GreenLight', (0.15, 1.0, 0.4), rough=0.4, emissive=10)
red_light = material('RedLight', (1.0, 0.15, 0.1), rough=0.4, emissive=10)
laser_lens = material('LaserLens', (0.1, 1.0, 0.5), rough=0.3, emissive=14)
beam_mat = material('Beam', (0.2, 1.0, 0.6), rough=0.5, emissive=3.0, translucent=True, opacity=0.18)
orange = material('Orange', (0.9, 0.4, 0.1), rough=0.7, emissive=0.2)
dark_door = material('DarkDoor', (0.08, 0.08, 0.10), rough=0.7)
radar_white = material('RadarWhite', (0.85, 0.85, 0.85), rough=0.5, emissive=0.08)

# --------------------------------------------------------------------------
# Mesh / actor helpers (mirrors create_yunsha_map.py)
# --------------------------------------------------------------------------
def mesh_core(name, verts, tris, mat, collision=True):
    name = re.sub(r'[^A-Za-z0-9_]', '_', name)
    path = ROOT + '/Meshes/SM_' + name
    generated_meshes.add(path)
    sm = u.load_asset(path) if E.does_asset_exist(path) \
        else A.create_asset('SM_' + name, ROOT + '/Meshes', u.StaticMesh, None)
    md = sm.create_static_mesh_description()
    md.reserve_new_vertices(len(verts))
    md.reserve_new_vertex_instances(len(verts))
    md.reserve_new_polygons(len(tris))
    pg = md.create_polygon_group()
    md.set_polygon_group_material_slot_name(pg, 'Surface')
    sm.set_editor_property('static_materials', [u.StaticMaterial(material_interface=mat, material_slot_name='Surface')])
    ids = []
    for x, y, z in verts:
        v = md.create_vertex()
        md.set_vertex_position(v, u.Vector(x, y, z))
        i = md.create_vertex_instance(v)
        md.set_vertex_instance_uv(i, u.Vector2D(x / 400, y / 400), 0)
        ids.append(i)
    for a, b, c in tris:
        md.create_triangle(pg, [ids[a], ids[c], ids[b]])
    sm.set_editor_property('allow_cpu_access', True)
    if collision:
        body = sm.get_editor_property('body_setup')
        if body is None:
            sm.build_from_static_mesh_descriptions([md], False, False)
            body = sm.get_editor_property('body_setup')
        body.set_editor_property('collision_trace_flag', u.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
        # These architectural surfaces must block from above and below, while
        # keeping their actual openings instead of enclosing them in hulls.
        body.set_editor_property('double_sided_geometry', True)
    # Build after the collision flags are configured, so physics is cooked
    # with the same two-sided surface policy on fresh and existing assets.
    sm.build_from_static_mesh_descriptions([md], False, False)
    sm.set_material(0, mat)
    assert E.save_loaded_asset(sm)
    return sm

def loft(name, rings, mat, caps=True, collision=True):
    """rings: [(z, [(x,y), ...])]; same vertex count per ring. Caps via fans."""
    verts = []
    tris = []
    n = len(rings[0][1])
    for z, pts in rings:
        for x, y in pts:
            verts.append((x, y, z))
    for j in range(len(rings) - 1):
        for i in range(n):
            a = j * n + i
            b = j * n + (i + 1) % n
            c = (j + 1) * n + i
            d = (j + 1) * n + (i + 1) % n
            tris.extend([(a, b, c), (b, d, c)])
    if caps:
        top_z, top_pts = rings[-1]
        cx = sum(p[0] for p in top_pts) / n
        cy = sum(p[1] for p in top_pts) / n
        verts.append((cx, cy, top_z))
        top = len(verts) - 1
        base = (len(rings) - 1) * n
        for i in range(n):
            tris.append((top, base + (i + 1) % n, base + i))
        bot_z, bot_pts = rings[0]
        cx = sum(p[0] for p in bot_pts) / n
        cy = sum(p[1] for p in bot_pts) / n
        verts.append((cx, cy, bot_z))
        bot = len(verts) - 1
        for i in range(n):
            tris.append((bot, i, (i + 1) % n))
    return mesh_core(name, verts, tris, mat, collision)

def strip(name, x0, x1, profile, mat, collision=True):
    """Sweep a (y,z) polyline along X from x0 to x1."""
    n = len(profile)
    verts = [(x0, y, z) for y, z in profile] + [(x1, y, z) for y, z in profile]
    tris = []
    for i in range(n - 1):
        a, b, c, d = i, i + 1, n + i, n + i + 1
        tris.extend([(a, b, c), (b, d, c)])
    return mesh_core(name, verts, tris, mat, collision)

def fan(name, z, pts, mat, collision=True):
    cx = sum(p[0] for p in pts) / len(pts)
    cy = sum(p[1] for p in pts) / len(pts)
    verts = [(cx, cy, z)] + [(x, y, z) for x, y in pts]
    tris = [(0, 1 + i, 1 + (i + 1) % len(pts)) for i in range(len(pts))]
    return mesh_core(name, verts, tris, mat, collision)

def annulus(name, z, r_in, r_out, n, mat, collision=True):
    outer = circle(r_out, n)
    inner = circle(r_in, n)
    verts = [(x, y, z) for x, y in outer] + [(x, y, z) for x, y in inner]
    tris = []
    for i in range(n):
        a, b = i, (i + 1) % n
        c, d = n + i, n + (i + 1) % n
        tris.extend([(a, b, c), (b, d, c)])
    return mesh_core(name, verts, tris, mat, collision)

def circle(r, n=24):
    return [(r * math.cos(i * math.tau / n), r * math.sin(i * math.tau / n)) for i in range(n)]

def ellipse(rx, ry, n=32):
    return [(rx * math.cos(i * math.tau / n), ry * math.sin(i * math.tau / n)) for i in range(n)]

def actor(label, sm, p=(0, 0, 0), scale=(1, 1, 1), rot=None, collision=True, tags=()):
    a = u.EditorLevelLibrary.spawn_actor_from_class(u.StaticMeshActor, u.Vector(*p), rot or u.Rotator())
    a.set_actor_label('Murex ' + label)
    a.tags = ['Murex', *tags]
    c = a.static_mesh_component
    c.set_static_mesh(sm)
    c.set_collision_profile_name('BlockAll' if collision else 'NoCollision')
    a.set_actor_scale3d(u.Vector(*scale))
    return a

BATCHES = {}
def batch(kind, mat, collision=True, shape='Cube'):
    a = u.EditorLevelLibrary.spawn_actor_from_class(u.Born2FlapYunshaBatch, u.Vector())
    a.set_actor_label('Murex batch ' + kind)
    a.tags = ['Murex']
    c = a.instances
    c.set_static_mesh(u.load_asset('/Engine/BasicShapes/'+shape))
    c.set_material(0, mat)
    c.set_collision_profile_name('BlockAll' if collision else 'NoCollision')
    BATCHES[kind] = c
    return c

def inst(kind, p, size, rot=None):
    BATCHES[kind].add_instance(
        u.Transform(location=u.Vector(*p),
                    rotation=rot or u.Rotator(),
                    scale=u.Vector(*(v / 100 for v in size))), False)

# --------------------------------------------------------------------------
# 1. Terrain: colliding core (grass + hard surfaces + wetland bowls)
# --------------------------------------------------------------------------
def in_hard(x, y):
    if abs(y) <= 3300 and abs(x) <= 168000:
        return True                                    # runway + shoulders + blast pads
    if -11500 <= y <= -6500 and -155000 <= x <= 155000:
        return True                                    # parallel taxi spine
    for cx in (-130000, -40000, 40000, 123750):
        if abs(x - cx) <= 1250 and -11500 <= y <= 3300:
            return True                                # taxiway connectors
    if -150000 <= x <= -50000 and -44000 <= y <= -11000:
        return True                                    # terminal apron
    for hx in (70000, 91000, 112000, 133000):
        if abs(x - hx) <= 5500 and -53000 <= y <= -40000:
            return True                                # hangar pads
    if (x - TOWER_POS[0]) ** 2 + (y - TOWER_POS[1]) ** 2 <= 5000 ** 2:
        return True                                    # tower pad
    return False

def grass_z(x, y):
    z = 14 * math.sin(x * 1.1e-4 + 1.3) * math.sin(y * 9e-5 + 0.7) \
        + 7 * math.sin(x * 3.1e-4 + 4.1) * math.cos(y * 2.7e-4 + 2.2)
    for cx, cy, rx, ry in BASINS:
        t = ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2
        if t < 1:
            z -= 330 * (1 - t) ** 2
    return z - 25

TILE = 50000.0   # 500 m tiles, 10 m cells
for ti in range(8):
    for tj in range(8):
        x0 = -CORE + ti * TILE
        y0 = -CORE + tj * TILE
        verts = []
        tris = []
        for j in range(51):
            for i in range(51):
                verts.append((x0 + i * 1000, y0 + j * 1000, grass_z(x0 + i * 1000, y0 + j * 1000)))
        for j in range(50):
            for i in range(50):
                if all(in_hard(x0 + (i + di) * 1000, y0 + (j + dj) * 1000) for di in (0, 1) for dj in (0, 1)):
                    continue                            # hard surfaces carry their own slab
                k = j * 51 + i
                tris.extend([(k, k + 1, k + 51), (k + 1, k + 52, k + 51)])
        actor('grass %d %d' % (ti, tj), mesh_core('Grass_%d_%d' % (ti, tj), verts, tris, grass))

def slab(name, x0, x1, y0, y1, mat, z=0.0, cell=1000.0):
    nx = int(math.ceil((x1 - x0) / cell))
    ny = int(math.ceil((y1 - y0) / cell))
    verts = []
    tris = []
    for j in range(ny + 1):
        for i in range(nx + 1):
            verts.append((x0 + (x1 - x0) * i / nx, y0 + (y1 - y0) * j / ny, z))
    for j in range(ny):
        for i in range(nx):
            k = j * (nx + 1) + i
            tris.extend([(k, k + 1, k + nx + 1), (k + 1, k + nx + 2, k + nx + 1)])
    return actor(name, mesh_core(name, verts, tris, mat))

slab('RunwayStrip', -RUNWAY_HALF - 8000, RUNWAY_HALF + 8000, -3300, 3300, asphalt)
slab('TaxiSpine', -155000, 155000, -11500, -6500, asphalt)
for cx in (-130000, -40000, 40000, 123750):
    slab('TaxiConnector%d' % cx, cx - 1250, cx + 1250, -11500, 3300, asphalt)
slab('Apron', -150000, -50000, -44000, -11000, asphalt)
for hx in (70000, 91000, 112000, 133000):
    slab('HangarPad%d' % hx, hx - 5500, hx + 5500, -53000, -40000, concrete, z=1.0)
# Blast pads, darker asphalt, at both runway heads.
slab('BlastWest', -168000, -160000, -3300, 3300, asphalt_dark, z=1.0)
slab('BlastEast', 160000, 168000, -3300, 3300, asphalt_dark, z=1.0)

# Wetland water discs filling the bowls to the -230 shoreline.
for bi, (cx, cy, rx, ry) in enumerate(BASINS):
    actor('wetland %d' % bi,
          fan('WetlandDisc%d' % bi, -230.0, ellipse(rx * 0.5, ry * 0.5, 40), wetland, collision=False),
          p=(cx, cy, 0), collision=False)

# --------------------------------------------------------------------------
# 2. Runway markings + lights + perimeter fence
# --------------------------------------------------------------------------
batch('WhitePaint', white_paint, collision=False)
batch('YellowPaint', yellow_paint, collision=False)
batch('Steel', steel)
batch('Amber', amber_light, collision=False)
batch('Green', green_light, collision=False)
batch('Red', red_light, collision=False)
batch('Dark', dark_door)
batch('LaserLens', laser_lens, collision=False)
batch('Beam', beam_mat, collision=False)

# Centerline dashes every 30 m.
for x in range(-159000, 159000, 3000):
    inst('WhitePaint', (x, 0, 2), (450, 45, 2))
# Continuous edge lines.
for sign in (-1, 1):
    actor('edge line %d' % sign,
          strip('EdgeLine%d' % sign, -RUNWAY_HALF, RUNWAY_HALF, [(sign * 2900, 1.5), (sign * 2930, 1.5)], white_paint, collision=False),
          collision=False)
# Threshold bars at both ends.
for sgn in (-1, 1):
    for i in range(8):
        inst('WhitePaint', (sgn * (158500 - i * 500), 1300 if i % 2 else -1300, 2), (180, 2600, 2))
# Aiming-point bars 300 m inside each threshold.
for sgn in (-1, 1):
    for sy in (-1550, 1550):
        inst('WhitePaint', (sgn * 132250, sy, 2), (4500, 900, 2))
# Taxiway centerlines.
for x in range(-152000, 152000, 3000):
    inst('YellowPaint', (x, -9000, 2), (300, 30, 2))
for cx in (-130000, -40000, 40000, 123750):
    for dy in (-8000, -5000):
        inst('YellowPaint', (cx, dy, 2), (300, 30, 2))

# Edge lights: amber posts along both edges, green/red at the thresholds.
for x in range(-162000, 162001, 6000):
    for sy in (-3100, 3100):
        inst('Steel', (x, sy, 0), (14, 14, 70))
        inst('Amber', (x, sy, 77), (18, 18, 14))
for sgn, mat_kind in ((-1, 'Green'), (1, 'Red')):
    for sy in (-3100, 3100):
        inst('Steel', (sgn * 162000, sy, 0), (14, 14, 70))
        inst(mat_kind, (sgn * 162000, sy, 77), (18, 18, 14))

# Perimeter safety fence at the core edge.
for y in (-195000, 195000):
    for x in range(-195000, 195001, 2500):
        inst('Steel', (x, y, 0), (12, 12, 260))
    inst('Steel', (0, y, 120), (390000, 10, 10))
    inst('Steel', (0, y, 240), (390000, 10, 10))
for x in (-195000, 195000):
    for y in range(-195000, 195001, 2500):
        inst('Steel', (x, y, 0), (12, 12, 260))
    inst('Steel', (x, 0, 120), (10, 390000, 10))
    inst('Steel', (x, 0, 240), (10, 390000, 10))

# --------------------------------------------------------------------------
# 3?4. One sideways shell: aperture hall, tower spine, gate spines and park
# --------------------------------------------------------------------------
shell_materials = {
    'ShellIvory': material('ShellIvory', (.78, .72, .61), rough=.62, noise_scale=1800, noise_amount=.035),
    'ShellPearl': material('ShellPearl', (.92, .85, .72), rough=.39),
    'ShellSand': material('ShellSand', (.57, .45, .31), rough=.65),
    'WarmStone': material('WarmStone', (.68, .62, .50), rough=.77),
    'Bronze': material('Bronze', (.19, .15, .10), metal=.65, rough=.32),
    'Glazing': material('ApertureGlass', (.13, .30, .36), glass=True),
    'AircraftWhite': material('AircraftWhite', (.87, .87, .83), metal=.15, rough=.29),
    'AircraftGlass': material('AircraftGlass', (.025, .065, .085), metal=.4, rough=.15),
    'AircraftGold': material('AircraftGold', (.43, .24, .075), metal=.4, rough=.36),
    'ParkGrass': material('ParkGrass', (.19, .29, .10), rough=1.0, noise_scale=350, noise_amount=.15),
    'TreeTrunk': material('ParkBark', (.20, .13, .075), rough=.95),
    'TreeLeaf0': material('ParkLeaf0', (.12, .25, .065), rough=.95),
    'TreeLeaf1': material('ParkLeaf1', (.22, .34, .10), rough=.95),
    'TreeLeaf2': material('ParkLeaf2', (.30, .37, .12), rough=.95),
    'Bench': material('ParkBench', (.32, .19, .09), rough=.75),
    'Wheel': material('AircraftTyre', (.018, .02, .021), rough=.88),
    'CarBody': material('ParkCar', (.32, .37, .38), metal=.3, rough=.3),
    'AmberSoft': material('WarmLight', (1, .60, .25), rough=.4, emissive=2),
    'Concrete': concrete, 'Asphalt': asphalt, 'YellowPaint': yellow_paint, 'Red': red_light,
}
shell_scene = build_scene()
for item in shell_scene.meshes:
    sm = mesh_core(item['name'], [tuple(v*100 for v in p) for p in item['vertices']],
                   item['triangles'], shell_materials[item['material']], item['collision'])
    actor(item['name'], sm, collision=item['collision'], tags=item['tags'])
for item in shell_scene.instances:
    kind = 'Shell_'+item['kind']
    if kind not in BATCHES:
        shape = 'Sphere' if item['kind'].startswith('TreeLeaf') else 'Cylinder' if item['kind']=='TreeTrunk' else 'Cube'
        batch(kind, shell_materials[item['kind']], collision=item['kind'] not in ('AmberSoft','YellowPaint','Red'), shape=shape)
    rotation = u.MathLibrary.find_look_at_rotation(u.Vector(), u.Vector(*item['direction'])) if 'direction' in item else u.Rotator(yaw=item['yaw'])
    inst(kind, tuple(v*100 for v in item['position']), tuple(v*100 for v in item['size']), rotation)

# The terminal name and gate labels are architectural signs, not HUD overlays.
def sign(label, text, position, size=200, yaw=-90):
    a = u.EditorLevelLibrary.spawn_actor_from_class(u.TextRenderActor, u.Vector(*position), u.Rotator(yaw=yaw))
    a.set_actor_label('Murex '+label)
    a.tags = ['Murex', 'MurexSign']
    a.text_render.set_text(text)
    a.text_render.set_world_size(size)
    a.text_render.set_text_render_color(u.Color(225, 211, 178, 255))
    return a
sign('aperture name', 'M U R E X', (-130000,-50100,1800), 280)
for gate in shell_scene.gates:
    gx,gy,_=gate['tip']
    sign('gate %d'%gate['id'], 'M%02d'%gate['id'], (gx*100-150,gy*100+20,310), 120, 90)

# --------------------------------------------------------------------------
# 5. Hangars, radar masts, laser towers, sound cannons, windsock
# --------------------------------------------------------------------------
for hx in (70000, 91000, 112000, 133000):
    inst('Steel', (hx, -43000, 1000), (9000, 60, 2000))                  # back wall
    for hy in (-46300, -39700):
        inst('Steel', (hx, hy, 1000), (60, 6500, 2000))                  # side walls
    inst('Steel', (hx, -46300, 1900), (9000, 60, 200))                   # front lintel
    inst('Dark', (hx, -46320, 900), (8000, 20, 1800))                    # door
    arch = [( -43000 + 3250 * math.cos(math.pi * f / 16), 2000 + 3250 * math.sin(math.pi * f / 16))
            for f in range(17)]
    actor('hangar roof %d' % hx, strip('HangarRoof%d' % hx, hx, hx + 9000, arch, steel))

# Drone hangars: small steel boxes with dark doors.
for dx, dy in ((30000, -9000), (-100000, -9000), (70000, 10000)):
    inst('Steel', (dx, dy, 300), (1500, 900, 600))
    inst('Dark', (dx, dy - 460, 200), (1200, 20, 400))

# Radar masts: four corner posts, X-bracing, platform, arm, dish, beacon.
batch('RadarWhite', radar_white, collision=False)
for mx, my in ((-150000, -160000), (150000, 175000), (0, 185000)):
    for sx in (-450, 450):
        for sy in (-450, 450):
            inst('Steel', (mx + sx, my + sy, 2100), (30, 30, 4200))
    for s in range(3):
        z = 700 + s * 1400
        inst('Steel', (mx, my, z), (900, 20, 20), u.Rotator(pitch=45))
        inst('Steel', (mx, my, z), (20, 900, 20), u.Rotator(roll=45))
    inst('Steel', (mx, my, 4200), (1500, 1500, 30))
    inst('Steel', (mx + 600, my, 4230), (1200, 30, 30))
    inst('RadarWhite', (mx + 1300, my, 4230), (30, 600, 600), u.Rotator(pitch=15))
    inst('Red', (mx, my, 4300), (40, 40, 40))

# Laser towers: posts with green emitter heads sweeping the flock lanes.
for lx, ly in ((-80000, 7000), (80000, 7000), (-80000, -7000), (80000, -7000)):
    inst('Steel', (lx, ly, 450), (40, 40, 900))
    inst('Steel', (lx, ly, 950), (70, 70, 140))
    inst('LaserLens', (lx + 40, ly, 960), (60, 60, 20))
    inst('Beam', (lx + 2300, ly, 960), (4500, 20, 20), u.Rotator(yaw=45))

# Sound cannons: cone emitters on posts, angled outward.
cone_rings = [(h, circle(130 - 85 * h / 260, 12)) for h in (0, 130, 260)]
cone_mesh = loft('SoundCannon', cone_rings, steel)
for sx_, sy_ in ((-40000, 7000), (40000, 7000), (-40000, -7000), (40000, -7000)):
    inst('Steel', (sx_, sy_, 350), (50, 50, 700))
    actor('sound cannon %d %d' % (sx_, sy_), cone_mesh, p=(sx_, sy_, 700), rot=u.Rotator(pitch=-15))

# Windsock near the runway head.
inst('Steel', (0, 13000, 500), (20, 20, 1000))
inst('Steel', (0, 13000, 1050), (320, 12, 12))
sock_rings = [(h, circle(90 - 60 * h / 500, 12)) for h in (0, 250, 500)]
actor('windsock', loft('Windsock', sock_rings, orange, caps=False), p=(320, 13000, 1050), rot=u.Rotator(pitch=-12))

# --------------------------------------------------------------------------
# 6. Horizon ring (5-10 km, no collision) — the 10 km visual world
# --------------------------------------------------------------------------
batch('Skyline', skyline, collision=False)
batch('Turbine', radar_white, collision=False)
batch('HorizonSteel', steel, collision=False)

actor('horizon ground disc', fan('HorizonGround', -140.0, circle(920000, 96), horizon_ground, collision=False),
      collision=False)

# Distant city: deterministic clusters of skyscraper silhouettes.
for ci in range(20):
    ang = ci * math.tau / 20 + rng.uniform(-0.12, 0.12)
    rad = rng.uniform(520000, 820000)
    cx = rad * math.cos(ang)
    cy = rad * math.sin(ang)
    for _ in range(rng.randint(4, 8)):
        jx = rng.uniform(-40000, 40000)
        jy = rng.uniform(-40000, 40000)
        inst('Skyline', (cx + jx, cy + jy, 0),
             (rng.uniform(6000, 22000), rng.uniform(6000, 22000), rng.uniform(6000, 42000)))

# Wind turbines on the ring.
for ti in range(8):
    ang = ti * math.tau / 8 + 0.35
    rad = rng.uniform(380000, 620000)
    tx = rad * math.cos(ang)
    ty = rad * math.sin(ang)
    inst('Turbine', (tx, ty, 8000), (260, 260, 16000))
    inst('Turbine', (tx + 500, ty, 16400), (800, 300, 260))
    for blade in range(3):
        inst('Turbine', (tx + 500, ty, 16400), (11000, 90, 240), u.Rotator(yaw=blade * 120))

# Water towers.
for wx, wy in ((-420000, 300000), (560000, -260000)):
    for sx in (-900, 900):
        for sy in (-900, 900):
            inst('HorizonSteel', (wx + sx, wy + sy, 3500), (100, 100, 7000))
    tank = [(h, circle(2200, 20)) for h in (7000, 9600)]
    actor('water tower tank', loft('WaterTowerTank', tank, radar_white, caps=True, collision=False),
          p=(wx, wy, 0), collision=False)

# Low hills.
for hi in range(8):
    ang = hi * math.tau / 8 + rng.uniform(-0.2, 0.2)
    rad = rng.uniform(420000, 800000)
    hx = rad * math.cos(ang)
    hy = rad * math.sin(ang)
    dome = [(z, circle(38000 * (1 - z / 12000) + 8000, 20)) for z in (-140, 4000, 8000, 12000)]
    actor('hill %d' % hi, loft('Hill%d' % hi, dome, hill, caps=True, collision=False), p=(hx, hy, 0), collision=False)

# --------------------------------------------------------------------------
# 7. Atmosphere + cameras + save
# --------------------------------------------------------------------------
sun = u.EditorLevelLibrary.spawn_actor_from_class(u.DirectionalLight, u.Vector(0, 0, 80000), u.Rotator(pitch=-30, yaw=-65))
sun.light_component.set_mobility(u.ComponentMobility.MOVABLE)
sun.light_component.set_intensity(85000)
sun.light_component.set_atmosphere_sun_light(True)
sky = u.EditorLevelLibrary.spawn_actor_from_class(u.SkyLight, u.Vector())
sky.light_component.set_mobility(u.ComponentMobility.MOVABLE)
sky.light_component.set_editor_property('real_time_capture', True)
sky.light_component.set_intensity(1.0)
u.EditorLevelLibrary.spawn_actor_from_class(u.SkyAtmosphere, u.Vector())
fog = u.EditorLevelLibrary.spawn_actor_from_class(u.ExponentialHeightFog, u.Vector())
fog.component.set_editor_property('fog_density', 0.0016)

views = [((-167000, -112000, 53000), (-107000, -37000, 1700)),
         ((-139000, -67500, 11500), (-127000, -48800, 2200)),
         ((-86000, 16000, 33000), (-99000, -32500, 1200)),
         ((-117000, -25000, 16000), (-121800, -35700, 9800)),
         ((-70000, -23500, 3800), (-79000, -32000, 500)),
         ((-129000, -62500, 1800), (-126500, -50500, 2200))]
for i, (p, target) in enumerate(views):
    a = u.EditorLevelLibrary.spawn_actor_from_class(u.CameraActor, u.Vector(*p),
                                                    u.MathLibrary.find_look_at_rotation(u.Vector(*p), u.Vector(*target)))
    a.tags = ['MurexView%d' % i]
    a.set_actor_label('Murex view %d' % i)
u.EditorLevelLibrary.set_level_viewport_camera_info(u.Vector(*views[0][0]),
                                                    u.MathLibrary.find_look_at_rotation(u.Vector(*views[0][0]), u.Vector(*views[0][1])))

assert u.EditorLoadingAndSavingUtils.save_map(world, ROOT + '/Maps/MUREX')
# Keep unreferenced legacy meshes available for rollback; the saved map owns
# only this recipe's actors. Never remove unrelated user-authored assets.
import json
report_path = HERE.parents[2] / '.setup' / 'murex-redesign' / 'generated-scene.json'
report_path.parent.mkdir(parents=True, exist_ok=True)
report_path.write_text(json.dumps(dict(seed=SEED, revision='sideways-shell-v2',
    meshes=len(generated_meshes), shell_triangles=sum(len(m['triangles']) for m in shell_scene.meshes),
    gates=shell_scene.gates, landmarks=shell_scene.landmarks, views=views), indent=2), encoding='utf-8')
u.log('MUREX_MAP_READY revision=sideways-shell-v2 seed=290926 runway=3200 gate_spines=6 aircraft=6 tower=117 park_trees=%d' % shell_scene.landmarks['park_tree_count'])
u.log('MUREX_MAP_SAVED /Game/Murex/Maps/MUREX')
if not globals().get('B2F_GENERATION_BATCH'):
    u.SystemLibrary.execute_console_command(None, 'quit')
