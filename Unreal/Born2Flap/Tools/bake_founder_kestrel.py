"""Restore both static kestrel displays in the actual project map.
Works in the editor or with a correctly quoted project and -ExecutePythonScript.
The file journal affects discovery caching, not /Game mount resolution.
"""
import ast
import json
import math
import sys
from pathlib import Path
import unreal as u

here = Path(__file__).resolve().parent
sys.path.insert(0, str(here))
from project_guard import require_project
require_project()
from founder_assets import prepare as prepare_founder, place as place_founder

# Reconstruct the map-generation scope (waterline, build_mesh, pbr_material).
source = here / 'create_shiomori_map.py'
tree = ast.parse(source.read_text(encoding='utf-8-sig'))
definitions = [n for n in tree.body if isinstance(n, (ast.Import, ast.ImportFrom, ast.FunctionDef))]
scope = dict(__file__=str(source), assets=u.AssetToolsHelpers.get_asset_tools(),
             lib=u.MaterialEditingLibrary, ela=u.EditorAssetLibrary, count=0)
exec(compile(ast.Module(body=definitions, type_ignores=[]), str(source), 'exec'), scope)

world = u.EditorLoadingAndSavingUtils.load_map('/Game/Shiomori/Maps/SHIOMORI')
if not world:
    raise RuntimeError('Shiomori map not found in the verified project')

bronze = u.load_asset('/Game/Shiomori/Materials/M_CampusBronze') or \
    scope['pbr_material']('CampusBronze', color=(.22, .155, .072), rough=.42, metallic=.8)

# prepare() bakes (or reloads) SM_KestrelRaised and returns (founder, kestrel).
founder, kestrel = prepare_founder(scope, bronze)
if not kestrel:
    raise RuntimeError('Kestrel bake failed: check the project and Born2FlapEditor module')

# Placement positions mirror shiomori_campus.install().
HOTEL_CENTER = (-59500., -13700.)
HOTEL_YAW = -18.
HOTEL_RISE = 750.
SITE_OFFSET = (-12000., 24000.)


def _transform(p, center, yaw):
    a = math.radians(yaw); x, y, z = p
    return (center[0] + x * math.cos(a) - y * math.sin(a),
            center[1] + x * math.sin(a) + y * math.cos(a), center[2] + z)


sx, sy, _ = _transform((-600., 4700., 0.), (*HOTEL_CENTER, 170.), HOTEL_YAW)
pose = json.loads((here / 'founder_pose.json').read_text())
rx, ry, rz = pose['right_hand']
origin = (sx, sy, 315. + HOTEL_RISE)
bird_position = (origin[0] + rx, origin[1] + ry + 25.5 * .7, origin[2] + rz + 8)


def _smooth(a, b, x):
    t = max(0., min(1., (x - a) / (b - a)))
    return t * t * (3 - 2 * t)


def _analytic_ground(x, y):
    if y < -3000:
        return 150.
    shore = scope['waterline'](x); d = y - shore
    dune = max(20, 40 + 50 * math.sin(x * .00029 + 1.2) + 30 * math.sin(x * .00091 + 4.1)
               + 15 * math.sin(x * .0023 + .7) + 8 * math.sin(x * .0047 + 2.9))
    base = (dune if d <= -2500 else
            dune * (-d / 2500) - 50 * ((d + 2500) / 2500) ** 2 if d < 0 else max(-1750, -50 - .04 * d))
    if y < -1200:
        base = max(0, min(150, math.ceil((-y - 1200) / 100) * 25))
    inner = 54000 - 3500 * math.exp(-((y - 26000) / 14000) ** 2)
    rise = _smooth(inner - 5000, inner + 14000, abs(x))
    tip = 1 - _smooth(40000, 68000, y)
    hill = base + (-50 + 700 + 220 * math.sin(x * .000065 + y * .00011) - base) * rise * tip
    return max(base - 6, (base - 6) + (hill - (base - 6)) * _smooth(-3000, 5000, y))


def _ground(x, y):
    x += SITE_OFFSET[0]; y += SITE_OFFSET[1]
    ix = math.floor(x / 1000) * 1000; iy = math.floor(y / 1000) * 1000
    fx = (x - ix) / 1000; fy = (y - iy) / 1000
    a = _analytic_ground(ix, iy); b = _analytic_ground(ix + 1000, iy)
    c = _analytic_ground(ix, iy + 1000); d = _analytic_ground(ix + 1000, iy + 1000)
    return a + (b - a) * fx + (c - a) * fy if fx + fy <= 1 else d + (c - d) * (1 - fx) + (b - d) * (1 - fy)


shrine_position = (-58200., 5000., _ground(-58200., 5000.) + 100.)

sub = u.get_editor_subsystem(u.EditorActorSubsystem)
for a in list(sub.get_all_level_actors()):
    if a.get_actor_label() in ('Campus founder kestrel', 'Campus shrine kestrel'):
        sub.destroy_actor(a)

bird_position=(bird_position[0]+SITE_OFFSET[0],bird_position[1]+SITE_OFFSET[1],bird_position[2])
shrine_position=(shrine_position[0]+SITE_OFFSET[0],shrine_position[1]+SITE_OFFSET[1],shrine_position[2])
place_founder(kestrel, 'Campus founder kestrel', bird_position, scale=.70, yaw=90, material=bronze)
place_founder(kestrel, 'Campus shrine kestrel', shrine_position, scale=.46, yaw=100, material=bronze)

assert u.EditorLoadingAndSavingUtils.save_map(world, '/Game/Shiomori/Maps/SHIOMORI')
u.log('KESTREL_BAKED_PLACED')
if '-ExecutePythonScript=' in u.SystemLibrary.get_command_line():
    u.SystemLibrary.execute_console_command(None,'quit')
