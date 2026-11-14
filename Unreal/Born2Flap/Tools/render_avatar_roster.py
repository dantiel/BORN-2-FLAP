"""Render all 10 avatar persons side by side in the editor world for visual QA.

Spawns 10 SkeletalMeshComponents in a row, applies the same 180cm
normalisation ABorn2FlapAvatar::LoadPerson does, plays each character's Idle
clip, then captures one wide screenshot.
"""
import json
import math
import time
import traceback
from pathlib import Path
import unreal as u

OUT_IMG = Path('V:/BORN 2 FLAP/_avatar_roster_check.png')
OUT_DIAG = Path('V:/BORN 2 FLAP/_avatar_roster_check.json')

PERSONS = [
    ('John', '/Game/Actor/SK_Founder/SkeletalMeshes/SK_Founder.SK_Founder'),
    ('Erika', '/Game/Actor/SK_Erika/SkeletalMeshes/SK_Erika.SK_Erika'),
    ('Amy', '/Game/Actor/SK_Amy/SkeletalMeshes/SK_Amy.SK_Amy'),
    ('Michelle', '/Game/Actor/SK_Michelle/SkeletalMeshes/SK_Michelle.SK_Michelle'),
    ('Sophie', '/Game/Actor/SK_Sophie/SkeletalMeshes/SK_Sophie.SK_Sophie'),
    ('Kate', '/Game/Actor/SK_Kate/SkeletalMeshes/SK_Kate.SK_Kate'),
    ('James', '/Game/Actor/SK_James/SkeletalMeshes/SK_James.SK_James'),
    ('Adam', '/Game/Actor/SK_Adam/SkeletalMeshes/SK_Adam.SK_Adam'),
    ('Brian', '/Game/Actor/SK_Brian/SkeletalMeshes/SK_Brian.SK_Brian'),
    ('Alex', '/Game/Actor/SK_Alex/SkeletalMeshes/SK_Alex.SK_Alex'),
]

world = u.EditorLoadingAndSavingUtils.load_map('/Game/Shiomori/Maps/SHIOMORI')
sub = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
actor_sub = u.get_editor_subsystem(u.EditorActorSubsystem)

diag = {'map_loaded': bool(world)}
spawned = []

TARGET_CM = 180.0
for i, (name, mesh_path) in enumerate(PERSONS):
    mesh = u.load_asset(mesh_path)
    rec = {'name': name, 'mesh': mesh_path}
    if mesh is None or not isinstance(mesh, u.SkeletalMesh):
        rec['error'] = 'mesh missing'
        diag['p%d' % i] = rec
        continue
    b = mesh.get_bounds()
    ext = b.box_extent
    height = ext.z * 2.0
    scale = (TARGET_CM / height) if height > 1.0 else 1.0
    foot_z_asset = b.origin.z - ext.z
    lift = -foot_z_asset * scale

    # Spawn the real avatar actor (its Body SkeletalMeshComponent exists without
    # BeginPlay; editor spawn skips LoadPerson so we replicate it in Python).
    try:
        cls = u.load_class(None, '/Script/Born2Flap.Born2FlapAvatar')
        actor = actor_sub.spawn_actor_from_class(cls, u.Vector(i * 210.0 - 950.0, 0, 0), u.Rotator(0, 0, 0))
        comps = actor.get_components_by_class(u.SkeletalMeshComponent)
        comp = comps[0] if comps else None
        if comp is None:
            raise RuntimeError('no SkeletalMeshComponent on avatar')
        comp.set_skeletal_mesh_asset(mesh)
        comp.set_relative_scale3d(u.Vector(scale, scale, scale))
        comp.set_relative_location(u.Vector(0, 0, lift), False, False)
        # face the camera (Mixamo FBX imports facing +Y; camera sits at -Y)
        actor.set_actor_rotation(u.Rotator(0, 180, 0), False)
        # play the Idle clip (single-node) for a natural stance
        base = mesh_path.rsplit('.', 1)[0]
        obj = mesh_path.rsplit('.', 1)[-1]
        idle = u.load_asset(base + 'Idle.' + obj + 'Idle')
        if idle is not None and isinstance(idle, u.AnimSequence):
            comp.set_animation_mode(u.AnimationMode.ANIMATION_SINGLE_NODE)
            comp.set_animation(idle)
            comp.play(True)
        rec['idle'] = idle is not None
        rec['bounds_height'] = round(height, 1)
        rec['scale'] = round(scale, 3)
        rec['ok'] = True
        spawned.append((actor, comp))
    except Exception as e:
        rec['error'] = str(e)
    diag['p%d' % i] = rec

# aim a wide camera at the row
cam_loc = u.Vector(0.0, -2600.0, 180.0)
target = u.Vector(0.0, 0.0, 90.0)
cam = actor_sub.spawn_actor_from_class(u.CineCameraActor, cam_loc, u.Rotator(0, 0, 0))
dx = target.x - cam_loc.x
dy = target.y - cam_loc.y
dz = target.z - cam_loc.z
yaw = math.degrees(math.atan2(dy, dx))
horiz = math.sqrt(dx * dx + dy * dy)
pitch = math.degrees(math.atan2(dz, horiz))
cam.set_actor_rotation(u.Rotator(pitch, yaw, 0.0), False)
try:
    cam.get_cine_camera_component().set_editor_property('current_focal_length', 28.0)
except Exception:
    pass

state = dict(task=None, after=time.monotonic() + 5, started=time.monotonic())
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
        state['task'] = u.AutomationLibrary.take_high_res_screenshot(2800, 900, str(OUT_IMG), camera=cam, delay=1.0)
        if not state['task'].is_valid_task():
            raise RuntimeError('screenshot task invalid')
    except Exception:
        diag['error'] = traceback.format_exc()
        finish(False)


state['handle'] = u.register_slate_post_tick_callback(tick)
u.log('AVATAR_ROSTER_RENDER_STARTED')