"""Woshi (the Reclining Stone) - the Mesh Terrain proof ground.

One deterministic generator, two backends (env B2F_TERRAIN_BACKEND):
  meshpartition : World Partition map + AMeshPartition + base/boolean/remesh modifiers
  staticmesh    : the current pipeline (baked StaticMesh assets + actors, no WP)

Geological features (seed 7407, all fictional, all procedural):
  - a sandstone mountain (ridged dome, wind-aligned noise)
  - a canyon corridor with a localized remesh modifier (meshpartition only)
  - a natural arch (torus segment)
  - a carved grotto (Boolean subtract into a cliff, meshpartition only;
    the staticmesh backend gets a solid closed cliff)
  - the Resting Stone (a colossal reclining boulder with a head stone)

Compiled sections are NOT built here; .setup/regen-pocterrain.ps1 runs the
WorldPartitionMeshPartitionBuilder commandlet after generation.
"""
import json
import math
import os
import random
import sys
import time
from pathlib import Path

import unreal as u

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from project_guard import require_project
from terrain_backend import make_backend

require_project()

E = u.EditorAssetLibrary
M = u.MaterialEditingLibrary

SEED = 7407
BACKEND_KIND = os.environ.get('B2F_TERRAIN_BACKEND', 'meshpartition')
if BACKEND_KIND == 'staticmesh':
    ROOT = '/Game/WoshiSM'
    WORLD_LABEL = 'WOSHI_SM'
else:
    ROOT = '/Game/Woshi'
    WORLD_LABEL = 'WOSHI'

rng = random.Random(SEED)

# --------------------------------------------------------------------------
# Deterministic value noise (lattice + smooth interpolation, no numpy)
# --------------------------------------------------------------------------
def _hash2(x, y, s):
    h = (x * 374761393 + y * 668265263 + s * 1274126177) & 0x7FFFFFFF
    h = ((h ^ (h >> 13)) * 1103515245) & 0x7FFFFFFF
    return (h ^ (h >> 16)) / 0x7FFFFFFF


def value_noise(x, y, scale, s, octaves=3, gain=0.5):
    total = 0.0
    amp = 1.0
    norm = 0.0
    for o in range(octaves):
        freq = scale * (2 ** o)
        xi = math.floor(x * freq)
        yi = math.floor(y * freq)
        fx = x * freq - xi
        fy = y * freq - yi
        uu = fx * fx * (3.0 - 2.0 * fx)
        vv = fy * fy * (3.0 - 2.0 * fy)
        n00 = _hash2(xi, yi, s + o)
        n10 = _hash2(xi + 1, yi, s + o)
        n01 = _hash2(xi, yi + 1, s + o)
        n11 = _hash2(xi + 1, yi + 1, s + o)
        total += amp * (n00 * (1 - uu) * (1 - vv) + n10 * uu * (1 - vv)
                        + n01 * (1 - uu) * vv + n11 * uu * vv)
        norm += amp
        amp *= gain
    return total / norm


# --------------------------------------------------------------------------
# Materials
# --------------------------------------------------------------------------
def expr(mat, kind, **properties):
    node = M.create_material_expression(mat, getattr(u, 'MaterialExpression' + kind))
    for key, value in properties.items():
        node.set_editor_property(key, value)
    return node


def make_material(name, color, rough=0.9):
    path = ROOT + '/Materials/M_' + name
    if E.does_asset_exist(path):
        assert E.delete_asset(path)
    mat = u.AssetToolsHelpers.get_asset_tools().create_asset(
        'M_' + name, ROOT + '/Materials', u.Material, None)
    base = expr(mat, 'Constant3Vector', constant=u.LinearColor(*color, 1.0))
    rough_node = expr(mat, 'Constant', r=rough)
    M.connect_material_property(base, '', u.MaterialProperty.MP_BASE_COLOR)
    M.connect_material_property(rough_node, '', u.MaterialProperty.MP_ROUGHNESS)
    mat.set_editor_property('used_with_nanite', True)
    M.recompile_material(mat)
    assert E.save_loaded_asset(mat)
    return mat


# --------------------------------------------------------------------------
# Geometry builders (all return verts/tris in cm, CCW from above)
# --------------------------------------------------------------------------
def grid_surface(x0, y0, x1, y1, nx, ny, zfn):
    verts = []
    for j in range(ny + 1):
        for i in range(nx + 1):
            x = x0 + (x1 - x0) * i / nx
            y = y0 + (y1 - y0) * j / ny
            verts.append((x, y, zfn(x, y)))
    tris = []
    for j in range(ny):
        for i in range(nx):
            a = j * (nx + 1) + i
            b = a + 1
            c = a + (nx + 1)
            d = c + 1
            tris.append((a, b, c))
            tris.append((b, d, c))
    return verts, tris


def closed_box(cx, cy, cz, sx, sy, sz):
    x0, y0, z0 = cx - sx / 2, cy - sy / 2, cz - sz / 2
    x1, y1, z1 = cx + sx / 2, cy + sy / 2, cz + sz / 2
    verts = [(x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0),
             (x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)]
    tris = [
        (0, 2, 1), (0, 3, 2),  # bottom (-Z)
        (4, 5, 6), (4, 6, 7),  # top (+Z)
        (0, 1, 5), (0, 5, 4),  # south (-Y)
        (3, 7, 6), (3, 6, 2),  # north (+Y)
        (1, 2, 6), (1, 6, 5),  # east (+X)
        (0, 4, 7), (0, 7, 3),  # west (-X)
    ]
    return verts, tris


def uv_sphere(cx, cy, cz, rx, ry, rz, seg_theta=28, seg_phi=20):
    verts = [(cx, cy, cz + rz)]
    for j in range(1, seg_theta):
        theta = math.pi * j / seg_theta
        for i in range(seg_phi):
            phi = 2.0 * math.pi * i / seg_phi
            verts.append((cx + rx * math.sin(theta) * math.cos(phi),
                          cy + ry * math.sin(theta) * math.sin(phi),
                          cz + rz * math.cos(theta)))
    verts.append((cx, cy, cz - rz))
    south = len(verts) - 1
    tris = []
    for i in range(seg_phi):
        tris.append((0, 1 + (i + 1) % seg_phi, 1 + i))
        tris.append((south, south - seg_phi + (i + 1) % seg_phi, south - seg_phi + i))
    for j in range(1, seg_theta - 1):
        for i in range(seg_phi):
            a = 1 + (j - 1) * seg_phi + i
            b = 1 + (j - 1) * seg_phi + (i + 1) % seg_phi
            c = 1 + j * seg_phi + i
            d = 1 + j * seg_phi + (i + 1) % seg_phi
            tris.append((a, b, c))
            tris.append((b, d, c))
    return verts, tris


def torus_arc(cx, cy, cz, radius, tube, t0, t1, seg_t, seg_p):
    """Half torus ring lying in the YZ plane (axis +X), arc from angle t0..t1."""
    verts = []
    for j in range(seg_t + 1):
        t = t0 + (t1 - t0) * j / seg_t
        for i in range(seg_p):
            p = 2.0 * math.pi * i / seg_p
            verts.append((cx + tube * math.cos(p),
                          cy + (radius + tube * math.sin(p)) * math.cos(t),
                          cz + (radius + tube * math.sin(p)) * math.sin(t)))
    tris = []
    for j in range(seg_t):
        for i in range(seg_p):
            a = j * seg_p + i
            b = j * seg_p + (i + 1) % seg_p
            c = (j + 1) * seg_p + i
            d = (j + 1) * seg_p + (i + 1) % seg_p
            tris.append((a, b, c))
            tris.append((b, d, c))
    return verts, tris


# --------------------------------------------------------------------------
# Woshi geological features (deterministic, seed 7407)
# --------------------------------------------------------------------------
def canyon_dip(x, y):
    """Ground heightfield: flat sand, dipping into the canyon corridor.
    Floor at -110 m, smooth walls between |x| = 18 m and |x| = 45 m."""
    if abs(y) > 28000.0:
        return 0.0
    t = max(0.0, min(1.0, (abs(x) - 1800.0) / 2700.0))
    return -11000.0 * (1.0 - t * t * (3.0 - 2.0 * t))


def dome(x, y):
    """The gorge dome: a wind-carved sandstone butte standing over the canyon."""
    r = math.hypot(x, y)
    if r > 15000.0:
        return 0.0
    t = 1.0 - r / 15000.0
    ridge = value_noise(x / 1800.0, y / 1800.0, 0.8, 11) * 900.0
    # wind-aligned streaks so the sandstone reads as carved
    streak = value_noise(x / 500.0, (y * 0.35 + x * 0.94) / 500.0, 23, 2) * 260.0
    return 12000.0 * (t ** 1.35) + ridge + streak


def world_surface(x, y):
    """One continuous heightfield: the canyon corridor cuts clean through the
    dome (the gorge); the plains beyond carry the resting stones."""
    canyon = canyon_dip(x, y)
    if canyon < 0.0:
        return canyon
    return max(0.0, dome(x, y))




# --------------------------------------------------------------------------
# Generation
# --------------------------------------------------------------------------
def generate():
    backend = make_backend(BACKEND_KIND, ROOT, WORLD_LABEL)
    for folder in ('Maps', 'Meshes', 'Materials', 'DataAssets'):
        E.make_directory(ROOT + '/' + folder)
    u.log('WOSHI_BACKEND ' + backend.name + ' seed=' + str(SEED))

    world = backend.begin()
    world.get_world_settings().set_editor_property('force_no_precomputed_lighting', True)

    sandstone = make_material('Sandstone', (0.78, 0.58, 0.36), 0.92)
    rock = make_material('Rock', (0.52, 0.36, 0.28), 0.85)
    backend.tick('materials')

    # One continuous heightfield: the canyon corridor cuts through the dome
    # (a gorge through the mountain). The Resting Stone and its head stone
    # ride on the sand east of the canyon as separate smooth meshes.
    ground_verts, ground_tris = grid_surface(-40000, -40000, 40000, 40000, 160, 160,
                                             world_surface)
    backend.commit_surface('Ground', ground_verts, ground_tris, sandstone, slot=0)

    # The world's namesake: a colossal reclining boulder half-buried in
    # the sand, its smaller head stone nestled against the eastern shoulder.
    stone_verts, stone_tris = uv_sphere(14000.0, 22000.0, 900.0, 5000.0, 1400.0, 1500.0)
    backend.commit_surface('RestingStone', stone_verts, stone_tris, sandstone, slot=0)
    head_verts, head_tris = uv_sphere(18500.0, 21800.0, 1500.0, 1600.0, 1200.0, 1100.0)
    backend.commit_surface('HeadStone', head_verts, head_tris, sandstone, slot=0)

    # Natural arch: half torus standing on the ground, rock channel.
    arch_verts, arch_tris = torus_arc(24000.0, 4000.0, 0.0, 3800.0, 800.0,
                                      -0.45, math.pi + 0.45, 44, 14)
    backend.commit_surface('Arch', arch_verts, arch_tris, rock, slot=1)
    backend.tick('surfaces')

    if backend.carve_supported:
        # Carved grotto: closed cliff + Boolean subtract of a cave ellipsoid.
        cliff_verts, cliff_tris = closed_box(30000.0, -16000.0, 4000.0, 12000.0, 6000.0, 8000.0)
        backend.commit_surface('GrottoCliff', cliff_verts, cliff_tris, rock, slot=1)
        cave_verts, cave_tris = uv_sphere(30000.0, -13200.0, 2600.0, 2600.0, 2000.0, 2200.0)
        backend.commit_boolean('GrottoCarve', cave_verts, cave_tris,
                               u.Transform(), subtract=True, channel='Rock', weight=1.0)
    else:
        # Staticmesh backend: a solid closed cliff (the alcove is a mesh-
        # partition Boolean feature; the baked-dent stand-in produced a
        # partial collision trimesh and was removed).
        cliff_verts, cliff_tris = closed_box(30000.0, -16000.0, 4000.0, 12000.0, 6000.0, 8000.0)
        backend.commit_surface('GrottoCliff', cliff_verts, cliff_tris, rock, slot=1)

    if backend.remesh_supported:
        # Localized remesh over the canyon corridor (non-destructive, MP only).
        backend.commit_remesh('CanyonRemesh', (0.0, 0.0, -6000.0),
                              (3000.0, 15000.0, 10000.0), 200.0)
    backend.tick('modifiers')

    # --- shared environmental dressing -------------------------------------------------
    sun = u.EditorLevelLibrary.spawn_actor_from_class(
        u.DirectionalLight, u.Vector(0, 0, 80000), u.Rotator(pitch=-32, yaw=-70))
    sun.light_component.set_mobility(u.ComponentMobility.MOVABLE)
    sun.light_component.set_intensity(80000)
    sun.light_component.set_atmosphere_sun_light(True)
    sun.set_actor_label(WORLD_LABEL + ' Sun')
    sky = u.EditorLevelLibrary.spawn_actor_from_class(u.SkyLight, u.Vector())
    sky.light_component.set_mobility(u.ComponentMobility.MOVABLE)
    sky.light_component.set_editor_property('real_time_capture', True)
    sky.light_component.set_intensity(1.0)
    u.EditorLevelLibrary.spawn_actor_from_class(u.SkyAtmosphere, u.Vector())
    fog = u.EditorLevelLibrary.spawn_actor_from_class(u.ExponentialHeightFog, u.Vector())
    fog.component.set_editor_property('fog_density', 0.0015)

    start = u.EditorLevelLibrary.spawn_actor_from_class(
        u.PlayerStart, u.Vector(0, -32000, 2000), u.Rotator(yaw=90))
    start.set_actor_label(WORLD_LABEL + ' PlayerStart')

    views = [((0, 0, 14000), (0, 0, 12000)),                    # above the gorge dome
             ((0, -10000, -4000), (0, 10000, -9000)),           # inside the gorge, north
             ((24000, 4000, 4000), (24000, 4000, 200)),         # the natural arch
             ((14000, 27000, 5000), (14000, 22000, 1800)),      # the resting stone
             ((30000, -16000, 6000), (30000, -13200, 2600))]    # the grotto alcove
    for index, (pos, target) in enumerate(views):
        cam = u.EditorLevelLibrary.spawn_actor_from_class(
            u.CameraActor, u.Vector(*pos),
            u.MathLibrary.find_look_at_rotation(u.Vector(*pos), u.Vector(*target)))
        cam.set_actor_label('%s View%d' % (WORLD_LABEL, index))
        cam.tags = ['WoshiView%d' % index]
    u.EditorLevelLibrary.set_level_viewport_camera_info(
        u.Vector(*views[0][0]),
        u.MathLibrary.find_look_at_rotation(u.Vector(*views[0][0]), u.Vector(*views[0][1])))
    backend.tick('props')

    stats = backend.finalize(ROOT + '/Maps/' + WORLD_LABEL)
    stats.update({
        'seed': SEED,
        'world_label': WORLD_LABEL,
        'world_partition': backend.world_partition,
        'carve_supported': backend.carve_supported,
        'remesh_supported': backend.remesh_supported,
    })

    metrics_dir = Path(u.Paths.project_saved_dir()) / 'WoshiMetrics'
    metrics_dir.mkdir(parents=True, exist_ok=True)
    metrics_path = metrics_dir / (backend.name + '.json')
    with open(metrics_path, 'w') as handle:
        json.dump(stats, handle, indent=2)
    u.log('WOSHI_MAP_READY backend=%s seed=%d map=%s metrics=%s'
          % (backend.name, SEED, stats['map'], metrics_path))


generate()

if not globals().get('B2F_GENERATION_BATCH'):
    # Shutdown flush (UE 5.8 stock teardown bug): PythonScriptPlugin finalizes
    # Python at engine exit and its Py_Finalize pass crashes deep in
    # python311.dll (write AV after QUIT_EDITOR, process exit 3) when the
    # session leaves a large object graph behind -- the finalize GC deallocs
    # thousands of unreal wrapper objects against partially-torn-down module
    # state. Map/assets/metrics are already saved at this point, so shrink the
    # graph while the interpreter is still healthy: the finalize pass then has
    # no dealloc storm left to corrupt.
    import gc as _gc
    _keep = {'u', 'E', '_gc'}
    for _n in list(globals()):
        if not _n.startswith('_') and _n not in _keep:
            globals().pop(_n, None)
    _gc.collect()
    u.SystemLibrary.collect_garbage()
    _gc.collect()
    u.SystemLibrary.execute_console_command(None, 'quit')