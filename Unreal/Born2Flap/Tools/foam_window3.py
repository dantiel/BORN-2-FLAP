"""Park the editor camera over the Shiomori shoreline foam, then take ENGINE
screenshots via AutomationLibrary (renders the viewport internally — OS window
capture reads the D3D swapchain as black). Two shots: shoreline foam, then the
wide bay for water colour/mirror check."""
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

cam = u.Vector(-45600.0, 13500.0, 1400.0)
tgt = u.Vector(-43400.0, 13500.0, -100.0)
rot = u.MathLibrary.find_look_at_rotation(cam, tgt)
sub.set_level_viewport_camera_info(cam, rot, key)
time.sleep(12)
u.AutomationLibrary.take_high_res_screenshot(1920, 1080, 'V:/BORN 2 FLAP/_foam_shot3_shore.png')
u.log('SHOT1_SAVED')

time.sleep(130)
cam2 = u.Vector(12000.0, -14000.0, 15000.0)
tgt2 = u.Vector(-12000.0, 10000.0, 0.0)
rot2 = u.MathLibrary.find_look_at_rotation(cam2, tgt2)
sub.set_level_viewport_camera_info(cam2, rot2, key)
time.sleep(12)
u.AutomationLibrary.take_high_res_screenshot(1920, 1080, 'V:/BORN 2 FLAP/_foam_shot3_bay.png')
u.log('SHOT2_SAVED')
u.log('FOAM_WINDOW3_DONE')
