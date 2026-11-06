"""Headless: play the WALK animation live (loop), let it tick, then sample bones
and screenshot to confirm the legs actually move.

Run:
  UnrealEditor-Cmd Born2Flap.uproject -ExecutePythonScript=Tools/verify_walk_live.py -unattended -nop4 -nosplash -RenderOffscreen -stdout
"""
import json, math, time, traceback
from pathlib import Path
import unreal as u

OUT_IMG = Path('V:/BORN 2 FLAP/_walk_live.png')
OUT_DIAG = Path('V:/BORN 2 FLAP/_walk_live.json')
diag = {}

world = u.EditorLoadingAndSavingUtils.load_map('/Game/Shiomori/Maps/SHIOMORI')
sub = u.get_editor_subsystem(u.EditorActorSubsystem)
cls = u.load_class(None, '/Script/Born2Flap.Born2FlapAvatar')
actor = sub.spawn_actor_from_class(cls, u.Vector(0, 0, 0), u.Rotator(0, 0, 0))
comp = actor.get_components_by_class(u.SkeletalMeshComponent)[0]
mesh = u.load_asset('/Game/Actor/SK_Founder/SkeletalMeshes/SK_Founder.SK_Founder')
walk = u.load_asset('/Game/Actor/SK_Founder/Animations/AM_Walk.AM_Walk')
comp.set_skeletal_mesh_asset(mesh)
comp.set_animation_mode(u.AnimationMode.ANIMATION_SINGLE_NODE)
comp.set_animation(walk)
try:
    comp.set_update_animation_in_editor(True)
except Exception as e:
    diag['update_editor_err'] = str(e)[:120]
try:
    comp.play(True)   # loop
    diag['playing'] = comp.is_playing()
except Exception as e:
    diag['play_err'] = str(e)[:160]

BONES = ['mixamorig_LeftFoot', 'mixamorig_RightFoot', 'mixamorig_LeftHand',
         'mixamorig_RightHand', 'mixamorig_Hips', 'mixamorig_Head']

def sample_bones():
    out = {}
    for b in BONES:
        try:
            loc = comp.get_socket_location(b)
            out[b] = [round(loc.x, 2), round(loc.y, 2), round(loc.z, 2)]
        except Exception as e:
            out[b] = 'ERR:' + str(e)[:50]
    return out

def zpos(samples, b):
    v = samples.get(b)
    return v[2] if isinstance(v, list) else 0.0

diag['t0_bones'] = sample_bones()

cam_loc = u.Vector(260.0, -180.0, 95.0)
target = u.Vector(0.0, 0.0, 85.0)
cam = sub.spawn_actor_from_class(u.CineCameraActor, cam_loc, u.Rotator(0, 0, 0))
dx = target.x - cam_loc.x; dy = target.y - cam_loc.y; dz = target.z - cam_loc.z
yaw = math.degrees(math.atan2(dy, dx)); horiz = math.sqrt(dx*dx + dy*dy)
pitch = math.degrees(math.atan2(dz, horiz))
cam.set_actor_rotation(u.Rotator(pitch, yaw, 0.0), False)
try:
    cam.get_cine_camera_component().set_editor_property('current_focal_length', 50.0)
except Exception:
    pass

state = dict(task=None, after=time.monotonic() + 5, started=time.monotonic())
u.EditorPythonScripting.set_keep_python_script_alive(True)

def finish(ok=True):
    diag['t_end_bones'] = sample_bones()
    try:
        diag['anim_pos'] = round(comp.get_position(), 3)
    except Exception as e:
        diag['anim_pos'] = 'ERR:' + str(e)[:60]
    # motion heuristic
    for b in ['mixamorig_LeftFoot', 'mixamorig_RightFoot']:
        z0 = zpos(diag['t0_bones'], b)
        z1 = zpos(diag['t_end_bones'], b)
        diag[b + '_delta_z'] = round(z1 - z0, 3)
    diag['passed'] = ok
    OUT_DIAG.write_text(json.dumps(diag, indent=2), encoding='utf-8')
    try: u.unregister_slate_post_tick_callback(state['handle'])
    except Exception: pass
    u.EditorPythonScripting.set_keep_python_script_alive(False)
    u.SystemLibrary.execute_console_command(world, 'quit')

def tick(delta):
    try:
        now = time.monotonic()
        if now - state['started'] > 300: raise RuntimeError('timeout')
        if now < state['after']: return
        task = state['task']
        if task is not None:
            if not task.is_task_done(): return
            diag['screenshot'] = str(OUT_IMG)
            diag['screenshot_size'] = OUT_IMG.stat().st_size if OUT_IMG.is_file() else 0
            finish(OUT_IMG.is_file() and OUT_IMG.stat().st_size > 5000)
            return
        u.EditorLevelLibrary.set_level_viewport_camera_info(cam.get_actor_location(), cam.get_actor_rotation())
        state['task'] = u.AutomationLibrary.take_high_res_screenshot(1400, 1200, str(OUT_IMG), camera=cam, delay=2.0)
        if not state['task'].is_valid_task(): raise RuntimeError('screenshot invalid')
    except Exception:
        diag['error'] = traceback.format_exc()
        finish(False)

state['handle'] = u.register_slate_post_tick_callback(tick)
u.log('WALK_LIVE_STARTED')
