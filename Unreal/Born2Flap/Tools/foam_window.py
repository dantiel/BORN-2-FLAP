"""Park the editor camera over the Shiomori shoreline foam and keep the
viewport alive so an external window capture can grab the pixels."""
import sys
import time
from pathlib import Path

import unreal as u

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from project_guard import require_project

require_project()

sub = u.get_editor_subsystem(u.LevelEditorSubsystem)

u.EditorLevelLibrary.load_level('/Game/Shiomori/Maps/SHIOMORI')
u.log('LEVEL loaded; waiting for meshes...')
time.sleep(15)

key = sub.get_active_viewport_config_key()

# Phase 1: island-beach shoreline close-up.
cam = u.Vector(-45600.0, 13500.0, 1400.0)
tgt = u.Vector(-43400.0, 13500.0, -100.0)
rot = u.MathLibrary.find_look_at_rotation(cam, tgt)
sub.set_level_viewport_camera_info(cam, rot, key)
u.log('CAMERA_PHASE1_SHORELINE')
time.sleep(75)

# Phase 2: bay overlook.
cam2 = u.Vector(12000.0, -14000.0, 15000.0)
tgt2 = u.Vector(-12000.0, 10000.0, 0.0)
rot2 = u.MathLibrary.find_look_at_rotation(cam2, tgt2)
sub.set_level_viewport_camera_info(cam2, rot2, key)
u.log('CAMERA_PHASE2_BAY')
time.sleep(75)

u.log('FOAM_WINDOW_DONE')
