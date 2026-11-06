"""Headless render of the in-game avatar to VERIFY it is a recognizable human.

Spawns ABorn2FlapAvatar, dumps diagnostics (mesh, bounds, bone count), then
captures a high-res screenshot with a CineCameraActor aimed at the body.

Run:
  UnrealEditor-Cmd Born2Flap.uproject -ExecutePythonScript=Tools/render_avatar_check.py -RenderOffscreen -unattended -nop4 -nosplash
"""
import json
import math
import time
import traceback
from pathlib import Path
import unreal as u

HERE = Path(__file__).resolve().parent
PROJ = HERE.parents[1]
OUT_IMG = Path('V:/BORN 2 FLAP/_avatar_check.png')
OUT_DIAG = Path('V:/BORN 2 FLAP/_avatar_check.json')

world = u.EditorLoadingAndSavingUtils.load_map('/Game/Shiomori/Maps/SHIOMORI')
editor_world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
sub = u.get_editor_subsystem(u.EditorActorSubsystem)

diag = {'map_loaded': bool(world)}

cls = u.load_class(None, '/Script/Born2Flap.Born2FlapAvatar')
diag['class'] = str(cls)

actor = None
if cls and world:
    actor = sub.spawn_actor_from_class(cls, u.Vector(0, 0, 0), u.Rotator(0, 0, 0))
diag['spawned'] = actor is not None

if actor:
    comps = actor.get_components_by_class(u.SkeletalMeshComponent)
    diag['num_skeletal_comps'] = len(comps)
    for ci, c in enumerate(comps):
        # ApplyMesh() is a plain C++ method (not UFUNCTION) and only runs in
        # BeginPlay (PIE). Editor spawn skips it, so set the mesh explicitly.
        try:
            m = u.load_asset('/Game/Actor/SK_Founder/SkeletalMeshes/SK_Founder.SK_Founder')
            c.set_skeletal_mesh_asset(m)
        except Exception as e:
            diag['set_mesh_error'] = str(e)
        m = c.get_skeletal_mesh_asset()
        if not m:
            diag['comp_%d' % ci] = 'NO_MESH'
            continue
        try:
            bb = m.get_bounds()
            ext = bb.box_extent
            diag['comp_%d' % ci] = dict(
                mesh=m.get_name(),
                extent=[round(v, 2) for v in (ext.x, ext.y, ext.z)],
                origin=[round(v, 2) for v in (bb.origin.x, bb.origin.y, bb.origin.z)],
            )
        except Exception as e:
            diag['comp_%d' % ci] = 'bounds_err=%s' % e

# aim a camera at the avatar torso (~85 cm up)
cam_loc = u.Vector(260.0, -180.0, 95.0)
target = u.Vector(0.0, 0.0, 85.0)
cam = sub.spawn_actor_from_class(u.CineCameraActor, cam_loc, u.Rotator(0, 0, 0))
dx = target.x - cam_loc.x
dy = target.y - cam_loc.y
dz = target.z - cam_loc.z
yaw = math.degrees(math.atan2(dy, dx))
horiz = math.sqrt(dx * dx + dy * dy)
pitch = math.degrees(math.atan2(dz, horiz))
cam.set_actor_rotation(u.Rotator(pitch, yaw, 0.0), False)
try:
    cam.get_cine_camera_component().set_editor_property('current_focal_length', 50.0)
except Exception:
    pass

state = dict(task=None, after=time.monotonic() + 4, started=time.monotonic())
u.EditorPythonScripting.set_keep_python_script_alive(True)


def finish(ok=True):
    diag['passed'] = ok
    OUT_DIAG.write_text(json.dumps(diag, indent=2), encoding='utf-8')
    try:
        u.unregister_slate_post_tick_callback(state['handle'])
    except Exception:
        pass
    u.EditorPythonScripting.set_keep_python_script_alive(False)
    u.SystemLibrary.execute_console_command(world, 'quit')


def tick(delta):
    try:
        now = time.monotonic()
        if now - state['started'] > 300:
            raise RuntimeError('timeout')
        if now < state['after']:
            return
        task = state['task']
        if task is not None:
            if not task.is_task_done():
                return
            diag['screenshot'] = str(OUT_IMG)
            diag['screenshot_size'] = OUT_IMG.stat().st_size if OUT_IMG.is_file() else 0
            finish(OUT_IMG.is_file() and OUT_IMG.stat().st_size > 5000)
            return
        u.EditorLevelLibrary.set_level_viewport_camera_info(cam.get_actor_location(), cam.get_actor_rotation())
        state['task'] = u.AutomationLibrary.take_high_res_screenshot(1400, 1200, str(OUT_IMG), camera=cam, delay=2.0)
        if not state['task'].is_valid_task():
            raise RuntimeError('screenshot task invalid')
    except Exception:
        diag['error'] = traceback.format_exc()
        finish(False)


state['handle'] = u.register_slate_post_tick_callback(tick)
u.log('AVATAR_RENDER_CHECK_STARTED')