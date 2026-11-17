"""Visual verification of the Shiomori shoreline foam: load SHIOMORI, park the
editor camera over the island-beach POI and the bay, capture high-res
screenshots. Proves whether the foam renders black or white."""
import sys
import time
from pathlib import Path

import unreal as u

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from project_guard import require_project

require_project()

sub = u.get_editor_subsystem(u.LevelEditorSubsystem)
A = u.AutomationLibrary

u.EditorLevelLibrary.load_level('/Game/Shiomori/Maps/SHIOMORI')
u.log('LEVEL loaded; waiting for meshes...')
time.sleep(15)

key = sub.get_active_viewport_config_key()

# 1. Island-beach POI: strand at (-44800,9500)..(-45200,18200), water on the
#    west/outer side. From altitude, the shoreline foam ring should be visible.
shots = [
    ('island_beach_high', u.Vector(-43000.0, 3000.0, 3000.0), u.Vector(-45200.0, 16000.0, 0.0)),
    ('island_beach_low', u.Vector(-42000.0, -4000.0, 700.0), u.Vector(-45200.0, 14000.0, 0.0)),
    ('bay_overlook', u.Vector(12000.0, -14000.0, 15000.0), u.Vector(-12000.0, 10000.0, 0.0)),
]
for name, cam, tgt in shots:
    out = 'V:/BORN 2 FLAP/_foam_shot_%s.png' % name
    rot = u.MathLibrary.find_look_at_rotation(cam, tgt)
    sub.set_level_viewport_camera_info(cam, rot, key)
    u.log('CAMERA %s set' % name)
    time.sleep(3)
    A.take_high_res_screenshot(1920, 1080, out)
    u.log('SHOT %s requested' % out)
    for _ in range(25):
        time.sleep(1)
        if Path(out).exists():
            break
    u.log('SHOT %s on disk: %s' % (out, Path(out).exists()))
    time.sleep(2)

u.log('FOAM_SHOTS_DONE')
