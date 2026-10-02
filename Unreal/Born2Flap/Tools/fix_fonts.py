"""Fix the Chakra Petch UI font faces: force Inline loading policy.

The TTF import factory (UFontFileImportFactory -> InitializeFromBulkData)
hard-codes LoadingPolicy = EFontLoadingPolicy::LazyLoad. In a standalone
-game session LazyLoad resolves the face from SourceFilename (a Tools/ path
that no longer exists at runtime), so the face has no glyphs and every
character falls back to LastResort (tofu). Inline embeds the TTF bytes in
the asset and is the correct, portable choice for a UI face.

Run once via the full editor (not the commandlet — font faces need Slate):
  UnrealEditor.exe Born2Flap.uproject -ExecutePythonScript=fix_fonts.py -unattended -nosound
"""
import unreal as u

ela = u.EditorAssetLibrary
FACES = ('/Game/UI/ChakraPetchRegular', '/Game/UI/ChakraPetchBold')

for path in FACES:
    obj = u.load_asset(path)
    if obj is None:
        u.log_warning('FIX_FONTS: missing ' + path)
        continue
    obj.set_editor_property('loading_policy', u.FontLoadingPolicy.INLINE)
    after = obj.get_editor_property('loading_policy')
    u.log('FIX_FONTS: policy now %s (was set to INLINE)' % str(after))
    assert ela.save_asset(path, only_if_is_dirty=False), 'FIX_FONTS: save failed ' + path
    u.log('FIX_FONTS: inline %s (%s)' % (path, type(obj).__name__))

u.log('FIX_FONTS_READY')

try:
    u.SystemLibrary.execute_console_command(None, 'quit')
except Exception:
    pass