"""Import the user's born2flap-splash.png as a UMG background texture.

Run once via the editor commandlet (same invocation as create_shiomori_map.py).
Idempotent: a re-run updates settings and re-saves the existing asset.

The C++ splash widget (UI/Born2FlapSplash) resolves the resulting asset at
/Game/Splash/born2flap-splash via ConstructorHelpers.
"""
import unreal as u
from pathlib import Path

assets = u.AssetToolsHelpers.get_asset_tools()
ela = u.EditorAssetLibrary

SRC = Path.home() / 'Downloads' / 'born2flap-splash.png'
DEST_PATH = '/Game/Splash'
DEST_NAME = 'born2flap-splash'
FULL = DEST_PATH + '/' + DEST_NAME

if not SRC.exists():
    u.log_warning('SPLASH_IMPORT: source missing ' + str(SRC))
    raise SystemExit(1)

ela.make_directory(DEST_PATH)
if not ela.does_asset_exist(FULL):
    task = u.AssetImportTask()
    task.filename = str(SRC)
    task.destination_path = DEST_PATH
    task.destination_name = DEST_NAME
    task.automated = True
    task.replace_existing = True
    task.save = True
    assets.import_asset_tasks([task])

tex = u.load_asset(FULL)
if not isinstance(tex, u.Texture2D):
    u.log_warning('SPLASH_IMPORT: failed to load ' + FULL)
    raise SystemExit(1)

# sRGB (it is authored as a display image) and always resident (no streaming
# pop on the very first frame the splash is visible).
tex.set_editor_property('srgb', True)
tex.set_editor_property('never_stream', True)
assert ela.save_asset(FULL), 'SPLASH_IMPORT: failed to save ' + FULL
u.log('SPLASH_IMPORTED ' + FULL)
