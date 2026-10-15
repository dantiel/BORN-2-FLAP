"""Reproducible Neon Abyss — the Aether Nursery void level.

Builds /Game/NeonAbyss/Maps/NEONABYSS: a bounded, buoyant aether volume with a
procedural cosmos backdrop (nebula billboards, ringed planets, star field), a
deep-indigo aether fog, and a floating ring of Sanxingdui bronzes (monumental
masks with protruding pillar-eyes, a sacred tree axis, and sun wheels). There is
no terrain — a single invisible aether floor far below bounds the fall.

Run (after create_neon_assets.py has built the /Game/NeonAbyss materials):

  UnrealEditor-Cmd.exe "Unreal/Born2Flap/Born2Flap.uproject" -run=pythonscript \
    -script="Unreal/Born2Flap/Tools/create_neon_abyss_map.py" -unattended -nosplash -nullrhi -nosound
"""
import math
import random
from pathlib import Path
import unreal as u

import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
from project_guard import require_project
require_project()

rng = random.Random(300913)
ela = u.EditorAssetLibrary

world = u.EditorLoadingAndSavingUtils.new_blank_map(False)
world.get_world_settings().set_editor_property('force_no_precomputed_lighting', True)

# Basic shapes + the neon materials (created by create_neon_assets.py).
def _mesh(name):
    return ela.load_asset('/Engine/BasicShapes/' + name) or u.load_asset('/Engine/BasicShapes/' + name)

def _mat(name):
    return ela.load_asset('/Game/NeonAbyss/' + name) or u.load_asset('/Game/NeonAbyss/' + name)

meshes = {n: _mesh(n) for n in ('Cube', 'Sphere', 'Cylinder', 'Cone', 'Plane')}
MAT_BRONZE = _mat('M_Bronze')
MAT_NEBULA = _mat('M_Nebula')
MAT_STAR = _mat('M_Star')
MAT_AETHER = _mat('M_Aether')
MAT_PLANET = _mat('M_Planet')
count = 0


def place(label, p, mesh, mat, rot=(0, 0, 0), scale=(1, 1, 1), collision=False, visible=True):
    global count
    a = u.EditorLevelLibrary.spawn_actor_from_class(
        u.StaticMeshActor, u.Vector(*p), u.Rotator(pitch=rot[0], yaw=rot[1], roll=rot[2]))
    a.set_actor_label(label)
    c = a.static_mesh_component
    if mesh is not None:
        c.set_static_mesh(mesh)
    if mat is not None:
        c.set_material(0, mat)
    c.set_collision_profile_name('BlockAll' if collision else 'NoCollision')
    c.set_visibility(visible)
    c.set_cast_shadow(False)
    a.set_actor_scale3d(u.Vector(*scale))
    count += 1
    return a


# --------------------------------------------------------------------------- #
# Cosmos backdrop                                                              #
# --------------------------------------------------------------------------- #
# A huge inverted sphere reads as the deep indigo void (two-sided, so the inner
# face is visible from inside the volume).
place('AetherBackdrop', (0, 0, 0), meshes['Sphere'], MAT_AETHER, scale=(200, 200, 200))

# Nebula clouds: large additive billboards at the far edge.
for i in range(14):
    az = rng.uniform(0, 2 * math.pi)
    el = rng.uniform(-0.6, 0.8)
    r = rng.uniform(8000, 11000)
    p = (r * math.cos(az) * math.cos(el), r * math.sin(az) * math.cos(el), r * math.sin(el))
    s = rng.uniform(500, 1000)
    place('Nebula%d' % i, p, meshes['Plane'], MAT_NEBULA,
          rot=(rng.uniform(-40, 40), math.degrees(az), rng.uniform(0, 360)), scale=(s, s, 1))

# Ringed planets: a soft body + a thin disc ring.
for i, (p, body_r, ring_r, tilt) in enumerate([
        ((9000, 3000, 2500), 900, 1600, 24),
        ((-11000, -2000, 4500), 1200, 2200, -18),
        ((4500, -8000, 1500), 700, 1300, 30),
        ((-2500, 10000, -1500), 1500, 2600, 12)]):
    place('Planet%d' % i, p, meshes['Sphere'], MAT_PLANET, scale=(body_r / 50.0, body_r / 50.0, body_r / 50.0))
    place('Planet%dRing' % i, p, meshes['Cylinder'], MAT_BRONZE,
          rot=(90 + tilt, 0, 0), scale=(ring_r / 50.0, ring_r / 50.0, 0.12))

# Star field: sparse additive motes.
for i in range(140):
    az = rng.uniform(0, 2 * math.pi)
    el = rng.uniform(-0.9, 0.9)
    r = rng.uniform(6000, 12000)
    p = (r * math.cos(az) * math.cos(el), r * math.sin(az) * math.cos(el), r * math.sin(el))
    place('Star%d' % i, p, meshes['Sphere'], MAT_STAR, scale=(0.5, 0.5, 0.5))


# --------------------------------------------------------------------------- #
# The bronze ring (Sanxingdui)                                                 #
# --------------------------------------------------------------------------- #
RING_R = 4200.0
MASK_COUNT = 8


def sun_wheel(p, r, rot):
    """A flat bronze disc with radiating spokes — the openwork sun disc."""
    place('SunWheel', p, meshes['Cylinder'], MAT_BRONZE,
          rot=(90, rot, 0), scale=(r / 50.0, r / 50.0, 0.10))
    for s in range(8):
        a = math.radians(s * 45)
        place('SunWheelSpoke', (p[0] + math.cos(a) * r * 0.55, p[1] + math.sin(a) * r * 0.55, p[2]),
              meshes['Cube'], MAT_BRONZE, rot=(0, rot, math.degrees(a)),
              scale=(0.08, r * 0.9 / 100.0, 0.08))


def mask(az, radius, yaw):
    """A monumental Sanxingdui mask: a broad slab with two protruding pillar-eyes."""
    x = radius * math.cos(az)
    y = radius * math.sin(az)
    face = (0.05, 0.60, 0.06)  # wide, tall, thin — the mask plane (metres)
    # The face slab, upright and facing the ring axis.
    place('Mask%dFace' % 0, (x, y, 200), meshes['Cube'], MAT_BRONZE,
          rot=(0, yaw, 0), scale=face)
    # Two protruding pillar eyes (纵目), cylinders jutting forward from the face.
    for side in (-1, 1):
        place('MaskEye', (x + side * 85 * math.cos(az + math.pi / 2),
                          y + side * 85 * math.sin(az + math.pi / 2), 420),
              meshes['Cylinder'], MAT_BRONZE, rot=(90, yaw, 0), scale=(0.28, 0.28, 1.6))
    # A sun-wheel halo behind the mask.
    sun_wheel((x - math.cos(az) * 500, y - math.sin(az) * 500, 520), 320, yaw)


for k in range(MASK_COUNT):
    az = 2 * math.pi * k / MASK_COUNT
    mask(az, RING_R, math.degrees(az) + 90)

# The sacred tree — the axis mundi rising through the ring's centre.
trunk = place('SacredTreeTrunk', (0, 0, 1500), meshes['Cylinder'], MAT_BRONZE,
              scale=(1.6, 1.6, 30.0))
for level, (h, arm_len) in enumerate([(700, 1200), (1400, 1000), (2100, 800)]):
    for a in (0, math.pi / 2, math.pi, 3 * math.pi / 2):
        place('SacredTreeArm', (math.cos(a) * arm_len * 0.5, math.sin(a) * arm_len * 0.5, h),
              meshes['Cylinder'], MAT_BRONZE, rot=(90, math.degrees(a), 0),
              scale=(0.5, 0.5, arm_len / 50.0))
        # A hanging bird / ornament at each arm tip.
        place('SacredTreeBird', (math.cos(a) * arm_len, math.sin(a) * arm_len, h - 150),
              meshes['Cone'], MAT_BRONZE, rot=(90, 0, 0), scale=(0.6, 0.6, 1.2))
    sun_wheel((0, 0, h + 120), 260, 0)


# --------------------------------------------------------------------------- #
# Atmosphere + safety floor                                                     #
# --------------------------------------------------------------------------- #
fog = u.EditorLevelLibrary.spawn_actor_from_class(u.ExponentialHeightFog, u.Vector(0, 0, 0))
fog.component.set_editor_property('fog_density', 0.00035)
try:
    fog.component.set_editor_property('fog_inscattering_color', u.LinearColor(0.18, 0.10, 0.38))
    fog.component.set_editor_property('fog_height_falloff', 0.02)
except Exception:
    pass

sky = u.EditorLevelLibrary.spawn_actor_from_class(u.SkyLight, u.Vector(0, 0, 0))
sky.light_component.set_editor_property('real_time_capture', True)
sky.light_component.set_editor_property('intensity', 0.6)
sky.light_component.set_editor_property('light_color', u.Color(120, 90, 200))

# A soft key light so the bronze reads as lit metal, not flat emissive.
sun = u.EditorLevelLibrary.spawn_actor_from_class(u.DirectionalLight, u.Vector(0, 0, 10000),
                                                  u.Rotator(pitch=-30, yaw=40, roll=0))
sun.light_component.set_editor_property('intensity', 20000.)
sun.light_component.set_editor_property('atmosphere_sun_light', False)
try:
    sun.light_component.set_editor_property('light_color', u.Color(150, 130, 210))
except Exception:
    pass

# The aether floor: an invisible BlockAll slab far below the ring — a safety net
# only. The bird flies weightlessly (neutral buoyancy + aether drag, handled by
# the flight pawn), so this is never reached in normal play.
floor = place('AetherFloor', (0, 0, -4000), meshes['Cube'], None,
              scale=(140, 140, 1), collision=True, visible=False)

# POI camera: the bronze ring is the launch/reset zone (F8).
cam = u.EditorLevelLibrary.spawn_actor_from_class(u.CameraActor, u.Vector(6000, -6000, 2500),
                                                  u.MathLibrary.find_look_at_rotation(
                                                      u.Vector(6000, -6000, 2500), u.Vector(0, 0, 500)))
cam.set_actor_label('Bronze ring')
cam.set_editor_property('tags', ['Bronze ring'])

u.EditorLevelLibrary.set_level_viewport_camera_info(
    u.Vector(6000, -6000, 2500),
    u.MathLibrary.find_look_at_rotation(u.Vector(6000, -6000, 2500), u.Vector(0, 0, 500)))

u.EditorAssetLibrary.make_directory('/Game/NeonAbyss/Maps')
assert u.EditorLoadingAndSavingUtils.save_map(world, '/Game/NeonAbyss/Maps/NEONABYSS')
u.log('NEON_ABYSS_MAP_READY actors=' + str(count))
if '-ExecutePythonScript=' in u.SystemLibrary.get_command_line():
    u.SystemLibrary.execute_console_command(None, 'quit')