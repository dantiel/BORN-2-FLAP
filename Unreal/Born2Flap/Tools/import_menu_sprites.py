"""Import the vertical main-menu border sprites (cut by cut_menu_sprites.py).

Run once via the editor commandlet. Imports:
  b2f_menu_left.png    -> /Game/UI/Elements/B2FMenuLeft
  b2f_menu_top.png     -> /Game/UI/Elements/B2FMenuTop
  b2f_menu_bottom.png  -> /Game/UI/Elements/B2FMenuBottom
  b2f_menu_divider.png -> /Game/UI/Elements/B2FMenuDivider
"""
import unreal as u
from pathlib import Path

assets = u.AssetToolsHelpers.get_asset_tools()
ela = u.EditorAssetLibrary
DOWNLOADS = Path.home() / 'Downloads'


def import_texture(src_name, dest_name):
    src = DOWNLOADS / src_name
    full = '/Game/UI/Elements/' + dest_name
    if not src.exists():
        u.log_warning('MENU_SPRITE_IMPORT: missing ' + str(src))
        raise SystemExit(1)
    # Always (re)import so the freshly-cut PNG replaces any stale asset.
    task = u.AssetImportTask()
    task.filename = str(src)
    task.destination_path = '/Game/UI/Elements'
    task.destination_name = dest_name
    task.automated = True
    task.replace_existing = True
    task.save = True
    assets.import_asset_tasks([task])
    tex = u.load_asset(full)
    if not isinstance(tex, u.Texture2D):
        u.log_warning('MENU_SPRITE_IMPORT: failed ' + full)
        raise SystemExit(1)
    tex.set_editor_property('srgb', True)
    tex.set_editor_property('never_stream', True)
    assert ela.save_asset(full), 'MENU_SPRITE_IMPORT: save failed ' + full
    u.log('MENU_SPRITE_IMPORTED ' + full)


for src, dst in [
    ('b2f_menu_left.png', 'B2FMenuLeft'),
    ('b2f_menu_top.png', 'B2FMenuTop'),
    ('b2f_menu_bottom.png', 'B2FMenuBottom'),
    ('b2f_menu_divider.png', 'B2FMenuDivider'),
]:
    import_texture(src, dst)

u.log('MENU_SPRITE_IMPORT_READY')