"""Reproducible Neon Abyss — the Aether Nursery void level.

Builds /Game/NeonAbyss/Maps/NEONABYSS: a bounded, buoyant aether volume with a
rich procedural cosmos — a gradient indigo-to-violet backdrop, a palette of soft
radial-glow nebulae (teal/magenta/gold/violet/cyan/emerald/rose + rainbow),
dense star clusters, scattered stars, white-hot quasars with relativistic jets
and coloured point lights, and gradient ringed planets with glowing atmospheres.

There is no terrain — a single invisible aether floor far below bounds the fall.

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
MAT_AETHER = _mat('M_Aether')
MAT_PLANET_RING = _mat('M_PlanetRing')
MAT_NEBULA_RAINBOW = _mat('M_NebulaRainbow')
MAT_QUASAR_CORE = _mat('M_QuasarCore')
MAT_QUASAR_ACCRETION = _mat('M_QuasarAccretion')
MAT_QUASAR_JET = _mat('M_QuasarJet')

NEBULA_SPRITES = [_mat('M_NebulaTeal'), _mat('M_NebulaMagenta'), _mat('M_NebulaGold'),
                  _mat('M_NebulaViolet'), _mat('M_NebulaCyan'), _mat('M_NebulaEmerald'),
                  _mat('M_NebulaRose')]
STAR_SPRITES = [_mat('M_StarWhite'), _mat('M_StarCyan'), _mat('M_StarGold'), _mat('M_StarMagenta')]
PLANET_MATS = [_mat('M_Planet0'), _mat('M_Planet1'), _mat('M_Planet2'), _mat('M_Planet3')]

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


def _face_origin(p):
    """Rotator that points a Plane's +Z normal toward the world origin, so glow
    billboards face the play volume (the ring) instead of the far backdrop."""
    v = u.Vector(-p[0], -p[1], -p[2])
    if v.length() < 1e-4:
        return (0.0, 0.0, 0.0)
    try:
        r = u.MathLibrary.make_rot_from_z(v)
        return (float(r.pitch), float(r.yaw), float(r.roll))
    except Exception:
        r = u.MathLibrary.find_look_at_rotation(u.Vector(*p), u.Vector(0, 0, 0))
        return (float(r.pitch), float(r.yaw), float(r.roll))


def place_sprite(label, p, mat, size):
    """A soft additive glow billboard facing the play volume."""
    place(label, p, meshes['Plane'], mat, rot=_face_origin(p), scale=(size, size, 1))


def point_light(label, p, colour, intensity, radius):
    pl = u.EditorLevelLibrary.spawn_actor_from_class(u.PointLight, u.Vector(*p), u.Rotator())
    pl.set_actor_label(label)
    c = pl.point_light_component
    c.set_mobility(u.ComponentMobility.MOVABLE)
    c.set_editor_property('intensity_units', u.LightUnits.LUMENS)
    c.set_intensity(intensity)
    c.set_attenuation_radius(radius)
    c.set_light_color(u.LinearColor(colour[0], colour[1], colour[2], 1.0))
    c.set_cast_shadows(False)


def _unit_sphere():
    while True:
        x = rng.uniform(-1, 1)
        y = rng.uniform(-1, 1)
        z = rng.uniform(-1, 1)
        m = x * x + y * y + z * z
        if 0.001 < m < 1.0:
            s = 1.0 / (m ** 0.5)
            return (x * s, y * s, z * s)


# --------------------------------------------------------------------------- #
# Cosmos backdrop                                                              #
# --------------------------------------------------------------------------- #
# A huge inverted sphere reads as the aether: deep indigo floor -> violet zenith.
# Two-sided so the inner face is visible from inside the volume. Scaled well past
# every cosmos object so nothing is occluded by the opaque shell.
place('AetherBackdrop', (0, 0, 0), meshes['Sphere'], MAT_AETHER, scale=(600, 600, 600))

# Nebula clouds: large overlapping additive sprites clumped into soft rainbow clouds.
for i in range(16):
    az = rng.uniform(0, 2 * math.pi)
    el = rng.uniform(-0.55, 0.75)
    r = rng.uniform(7000, 15000)
    cx = r * math.cos(az) * math.cos(el)
    cy = r * math.sin(az) * math.cos(el)
    cz = r * math.sin(el)
    for j in range(rng.randint(3, 5)):
        p = (cx + rng.uniform(-900, 900), cy + rng.uniform(-900, 900), cz + rng.uniform(-600, 600))
        place_sprite('Nebula%d_%d' % (i, j), p, rng.choice(NEBULA_SPRITES), rng.uniform(1300, 2200))

# A few large rainbow nebulae: teal heart -> magenta mid -> violet rim.
for i in range(5):
    az = rng.uniform(0, 2 * math.pi)
    el = rng.uniform(-0.4, 0.6)
    r = rng.uniform(7500, 14000)
    p = (r * math.cos(az) * math.cos(el), r * math.sin(az) * math.cos(el), r * math.sin(el))
    place_sprite('RainbowNebula%d' % i, p, MAT_NEBULA_RAINBOW, rng.uniform(1600, 2400))

# Star clusters: dense gaussian blobs of a single tint per cluster.
for i in range(5):
    az = rng.uniform(0, 2 * math.pi)
    el = rng.uniform(-0.6, 0.7)
    r = rng.uniform(6000, 14000)
    ccx = r * math.cos(az) * math.cos(el)
    ccy = r * math.sin(az) * math.cos(el)
    ccz = r * math.sin(el)
    tint = rng.choice(STAR_SPRITES)
    for j in range(45):
        p = (ccx + rng.gauss(0, 550), ccy + rng.gauss(0, 550), ccz + rng.gauss(0, 420))
        place_sprite('Cluster%dStar%d' % (i, j), p, tint, rng.uniform(14, 34))

# Scattered field stars across the whole dome.
for i in range(130):
    d = _unit_sphere()
    r = rng.uniform(6000, 18000)
    p = (d[0] * r, d[1] * r, d[2] * r)
    place_sprite('Star%d' % i, p, rng.choice(STAR_SPRITES), rng.uniform(10, 26))

# Space-dust / plankton motes drifting close to the ring — near-field parallax that
# makes the aether read as a thick luminous medium, not empty space.
for i in range(240):
    d = _unit_sphere()
    r = rng.uniform(700, 5200)
    p = (d[0] * r, d[1] * r, d[2] * r)
    place_sprite('Mote%d' % i, p, rng.choice(STAR_SPRITES), rng.uniform(6, 16))


# Quasars: white-hot core + soft accretion glow + twin relativistic jets + a
# coloured point light that throws "quasar light" onto the nearby aether.
def quasar(p, colour, label):
    place_sprite(label + '_Core', p, MAT_QUASAR_CORE, 42)
    place_sprite(label + '_Accretion', p, MAT_QUASAR_ACCRETION, 950)
    jet_len = 2600.0
    place(label + '_JetA', (p[0], p[1], p[2] + jet_len * 0.5), meshes['Cylinder'],
          MAT_QUASAR_JET, scale=(1.6, 1.6, jet_len / 100.0))
    place(label + '_JetB', (p[0], p[1], p[2] - jet_len * 0.5), meshes['Cylinder'],
          MAT_QUASAR_JET, scale=(1.6, 1.6, jet_len / 100.0))
    point_light(label + '_Light', p, colour, intensity=150000.0, radius=9000.0)


for i, (p, col) in enumerate([
        ((12000, -5000, 6000), (0.55, 0.82, 1.00)),
        ((-9000, 7000, -3000), (1.00, 0.55, 0.95)),
        ((3000, 11000, 4500), (0.45, 0.95, 0.80)),
        ((-13000, -6000, -2500), (1.00, 0.85, 0.45))]):
    quasar(p, col, 'Quasar%d' % i)


# Ringed planets: gradient body with a fresnel atmosphere rim + a glowing ring.
for i, (p, body_r, ring_r, tilt) in enumerate([
        ((9000, 3000, 2500), 900, 1600, 24),
        ((-11000, -2000, 4500), 1200, 2200, -18),
        ((4500, -8000, 1500), 700, 1300, 30),
        ((-2500, 10000, -1500), 1500, 2600, 12)]):
    place('Planet%d' % i, p, meshes['Sphere'], PLANET_MATS[i],
          scale=(body_r / 50.0, body_r / 50.0, body_r / 50.0))
    place('Planet%dRing' % i, p, meshes['Cylinder'], MAT_PLANET_RING,
          rot=(90 + tilt, 0, 0), scale=(ring_r / 50.0, ring_r / 50.0, 0.12))


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


def mask(az, radius, yaw, idx):
    """A monumental Sanxingdui mask: a broad slab with two protruding pillar-eyes.
    Local axes after yaw: +X = tangential (wide), +Y = radial (thin), +Z = up (tall)."""
    x = radius * math.cos(az)
    y = radius * math.sin(az)
    face = (16.0, 2.0, 22.0)  # wide, thin, tall — a monumental bronze slab (metres)
    place('Mask%dFace' % idx, (x, y, 1100), meshes['Cube'], MAT_BRONZE,
          rot=(0, yaw, 0), scale=face)
    for side in (-1, 1):
        place('Mask%dEye' % idx, (x + side * 900 * math.cos(az + math.pi / 2),
                                  y + side * 900 * math.sin(az + math.pi / 2), 2000),
              meshes['Cylinder'], MAT_BRONZE, rot=(90, yaw, 0), scale=(1.4, 1.4, 7.0))
    sun_wheel((x - math.cos(az) * 2200, y - math.sin(az) * 2200, 2400), 900, yaw)


for k in range(MASK_COUNT):
    az = 2 * math.pi * k / MASK_COUNT
    mask(az, RING_R, math.degrees(az) + 90, k)

# The sacred tree — the axis mundi rising through the ring's centre.
trunk = place('SacredTreeTrunk', (0, 0, 1500), meshes['Cylinder'], MAT_BRONZE,
              scale=(1.6, 1.6, 30.0))
for level, (h, arm_len) in enumerate([(700, 1200), (1400, 1000), (2100, 800)]):
    for a in (0, math.pi / 2, math.pi, 3 * math.pi / 2):
        place('SacredTreeArm', (math.cos(a) * arm_len * 0.5, math.sin(a) * arm_len * 0.5, h),
              meshes['Cylinder'], MAT_BRONZE, rot=(90, math.degrees(a), 0),
              scale=(0.5, 0.5, arm_len / 50.0))
        place('SacredTreeBird', (math.cos(a) * arm_len, math.sin(a) * arm_len, h - 150),
              meshes['Cone'], MAT_BRONZE, rot=(90, 0, 0), scale=(0.6, 0.6, 1.2))
    sun_wheel((0, 0, h + 120), 260, 0)


# --------------------------------------------------------------------------- #
# Atmosphere + lights + safety floor                                           #
# --------------------------------------------------------------------------- #
# The aether medium: a luminous, faintly refractive volumetric fog — the level's
# "volumetric cloud" read. Density sits ~2.5x Shiomori's coastal haze so the void
# glows violet and depth-cues scale, while the near ring/jellyfish stay legible.
fog = u.EditorLevelLibrary.spawn_actor_from_class(u.ExponentialHeightFog, u.Vector(0, 0, 0))
fog.component.set_editor_property('fog_density', 0.002)
try:
    fog.component.set_editor_property('fog_inscattering_color', u.LinearColor(0.32, 0.20, 0.58))
    fog.component.set_editor_property('fog_height_falloff', 0.0)
    fog.component.set_editor_property('volumetric_fog', True)
    fog.component.set_editor_property('fog_max_opacity', 0.9)
except Exception:
    pass

# Skylight recaptures the emissive cosmos -> a soft neon ambient for the bronze
# ring and the turtle. Must be movable (the level has no precomputed lighting).
sky = u.EditorLevelLibrary.spawn_actor_from_class(u.SkyLight, u.Vector(0, 0, 0))
sky.light_component.set_mobility(u.ComponentMobility.MOVABLE)
sky.light_component.set_editor_property('real_time_capture', True)
sky.light_component.set_editor_property('intensity', 1.5)
sky.light_component.set_editor_property('light_color', u.Color(150, 110, 220))

# A soft aether key light so the bronze reads as lit metal, not flat emissive.
sun = u.EditorLevelLibrary.spawn_actor_from_class(u.DirectionalLight, u.Vector(0, 0, 10000),
                                                  u.Rotator(pitch=-30, yaw=40, roll=0))
sun.light_component.set_mobility(u.ComponentMobility.MOVABLE)
sun.light_component.set_editor_property('intensity', 30000.0)
sun.light_component.set_editor_property('atmosphere_sun_light', False)
try:
    sun.light_component.set_editor_property('light_color', u.Color(150, 145, 225))
except Exception:
    pass

# Festival neon lights around the bronze ring (colour the masks and jellyfish).
for i, (ang, col, h) in enumerate([
        (0.0, (1.00, 0.25, 0.80), 400),
        (1.57, (0.20, 0.90, 1.00), -100),
        (3.14, (1.00, 0.85, 0.30), 300),
        (4.71, (0.60, 0.30, 1.00), 700)]):
    px = math.cos(ang) * 3000.0
    py = math.sin(ang) * 3000.0
    point_light('FestivalLight%d' % i, (px, py, h), col, intensity=60000.0, radius=5000.0)


# The neon read needs bloom: an unbound post-process volume (camera-side optics).
def apply_post_process():
    vol = u.EditorLevelLibrary.spawn_actor_from_class(u.PostProcessVolume, u.Vector(0, 0, 0))
    vol.set_actor_label('NeonAbyss PostProcess')
    vol.set_editor_property('unbound', True)
    s = vol.settings
    for k, v in (('bloom_intensity', 2.5),
                 ('bloom_threshold', 0.6),
                 ('auto_exposure_min_brightness', 1.0),
                 ('auto_exposure_max_brightness', 1.0)):
        try:
            s.set_editor_property(k, v)
        except Exception:
            pass
    try:
        vol.set_editor_property('settings', s)
    except Exception as e:
        u.log_warning('NeonAbyss post-process settings failed: ' + str(e))


apply_post_process()

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