"""Import the website UI fonts (Chakra Petch) as Unreal font faces.

Run once via the editor commandlet (same invocation style as
import_ui_assets.py / import_splash.py). Idempotent.

Imports:
  ChakraPetch-Regular.ttf  -> /Game/UI/ChakraPetchRegular   (UI font face)
  ChakraPetch-Bold.ttf     -> /Game/UI/ChakraPetchBold      (UI font face, bold)

The web frontend (web/assets/css/style.css) uses Chakra Petch for all
headings/nav/UI text; these faces let the in-game UMG match that identity.
"""
import unreal as u
from pathlib import Path

assets = u.AssetToolsHelpers.get_asset_tools()
ela = u.EditorAssetLibrary

FONTS_DIR = Path(__file__).resolve().parent / 'fonts'


def import_font(src_name, dest_path, dest_name):
    src = FONTS_DIR / src_name
    full = dest_path + '/' + dest_name
    if not src.exists():
        u.log_warning('FONT_IMPORT: source missing ' + str(src))
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
    obj = u.load_asset(full)
    if obj is None:
        u.log_warning('FONT_IMPORT: failed to load ' + full)
        raise SystemExit(1)
    # The TTF factory hard-codes LoadingPolicy=LazyLoad, which breaks standalone
    # -game (face resolved from SourceFilename -> no glyphs -> tofu). Force Inline
    # so the TTF bytes stay embedded and the UI face renders portably.
    obj.set_editor_property('loading_policy', u.FontLoadingPolicy.INLINE)
    assert ela.save_asset(full, only_if_is_dirty=False), 'FONT_IMPORT: failed to save ' + full
    u.log('FONT_IMPORTED %s -> %s (%s)' % (full, dest_name, type(obj).__name__))
    return obj


import_font('ChakraPetch-Regular.ttf', '/Game/UI', 'ChakraPetchRegular')
import_font('ChakraPetch-Bold.ttf', '/Game/UI', 'ChakraPetchBold')
u.log('FONT_IMPORT_READY')

# When launched via the full editor (-ExecutePythonScript), request an exit so
# the headless/CI caller regains control. Harmless under the commandlet.
try:
    u.SystemLibrary.execute_console_command(None, 'quit')
except Exception:
    pass