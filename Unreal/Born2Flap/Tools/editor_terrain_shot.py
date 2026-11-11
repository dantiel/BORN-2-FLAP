"""Editor-viewport verification of the Yunsha terrain material: load YUNSHA,
wait for the MeshPartition preview mesh, park the viewport camera over the
oasis shore, capture a high-res screenshot. The preview swaps M_WindRippleSand
(now the magenta-checker test material), so the shot proves whether the
texture/checker reaches any render path for this terrain."""
import sys
import time
from pathlib import Path

import unreal as u

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from project_guard import require_project

require_project()
OUT = 'V:/BORN 2 FLAP/_editor_terrain_shot.png'

E = u.EditorLevelLibrary
u.EditorLevelLibrary.load_level('/Game/Yunsha/Maps/YUNSHA')
print('LEVEL loaded; waiting for preview mesh...')
time.sleep(12)

# Park the editor camera over the oasis shore looking down at the dunes.
sub = u.get_editor_subsystem(u.LevelEditorSubsystem)
cam = u.Vector(17500.0, -52000.0, 900.0)
rot = u.Rotator(-38.0, 0.0, 0.0)
key = sub.get_active_viewport_config_key()
sub.set_level_viewport_camera_info(cam, rot, key)
print('CAMERA set', sub.get_level_viewport_camera_info(key))
time.sleep(2)

A = u.AutomationLibrary
A.take_high_res_screenshot(1600, 900, OUT)
print('SHOT requested', OUT)
# High-res captures render over several frames; keep the editor alive
# until the file is flushed.
for _ in range(30):
    time.sleep(1)
    if Path(OUT).exists():
        break
print('SHOT on disk:', Path(OUT).exists())