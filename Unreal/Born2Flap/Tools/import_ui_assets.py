"""Import the UI artwork (splash + background) from ~/Downloads.

Run once via the editor commandlet (same invocation style as
create_shiomori_map.py / import_splash.py). Idempotent: re-runs update settings
and re-save existing assets.

Imports:
  born2flap-splash-new.png   -> /Game/Splash/born2flap-splash-new   (startup splash)
  born2flap-background.png   -> /Game/Splash/born2flap-background   (full-page menu backdrop)

NOTE: the training-course flight gates are imported/built by
import_gate_assets.py (M_Gate lives at /Game/UI/M_Gate). This script must NOT
rebuild M_Gate — the two were once split across scripts and rebuilding the old
tinted variant here overwrote the painted-gate material.
"""
import unreal as u
from pathlib import Path

assets = u.AssetToolsHelpers.get_asset_tools()
ela = u.EditorAssetLibrary

DOWNLOADS = Path.home() / 'Downloads'


def import_texture(src_name, dest_path, dest_name):
    src = DOWNLOADS / src_name
    full = dest_path + '/' + dest_name
    if not src.exists():
        u.log_warning('UI_IMPORT: source missing ' + str(src))
        raise SystemExit(1)
    ela.make_directory(dest_path)
    if not ela.does_asset_exist(full):
        task = u.AssetImportTask()
        task.filename = str(src)
        task.destination_path = dest_path
        task.destination_name = dest_name
        task.automated = True
        task.replace_existing = True
        task.save = True
        assets.import_asset_tasks([task])
    tex = u.load_asset(full)
    if not isinstance(tex, u.Texture2D):
        u.log_warning('UI_IMPORT: failed to load ' + full)
        raise SystemExit(1)
    # Authored as display images; always resident (no streaming pop on first frame).
    tex.set_editor_property('srgb', True)
    tex.set_editor_property('never_stream', True)
    assert ela.save_asset(full), 'UI_IMPORT: failed to save ' + full
    u.log('UI_IMPORTED ' + full)
    return tex


splash = import_texture('born2flap-splash-new.png', '/Game/Splash', 'born2flap-splash-new')
back = import_texture('born2flap-background.png', '/Game/Splash', 'born2flap-background')
logo = import_texture('born2flap-logo.png', '/Game/Splash', 'born2flap-logo')
u.log('UI_IMPORT_READY')