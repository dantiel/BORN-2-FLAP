"""One-shot headless repair of all three avatar regressions:

1. JOHN THROW — re-import SK_Founder.glb so the glTF importer regenerates a
   correct SK_FounderThrow (glTF frame). This overwrites the broken
   FBX-retargeted clip that import_founder_throw.py left (the note-5777
   FBX-on-glTF axis mismatch -> swapped/crippled limbs).
2. ISIS SCALE — import the rebaked SK_Isis.glb (correct Y-up meters, 196 cm,
   24-bone mixamorig: skeleton + the 8 baked clips) instead of the mangled
   SK_Isis_ue.fbx (root 1e-4 scale + scattered armature = "as big as the sky").
3. TEXTURES — flip srgb=False on every linear (Glossiness/Metallic/Roughness/ORM)
   texture so their LinearColor samplers compile, then recompile materials
   (the "no textures" gray-default look).

Run (editor closed, headless):
    UnrealEditor-Cmd Born2Flap.uproject -ExecutePythonScript="V:/BORN 2 FLAP/Unreal/Born2Flap/Tools/repair_avatars_commandlet.py" -unattended -nop4 -nosplash -stdout
"""
from pathlib import Path
import unreal as u

TOOLS = u.AssetToolsHelpers.get_asset_tools()
LIB = u.EditorAssetLibrary
MEL = u.MaterialEditingLibrary

_PROJECT = Path(__file__).resolve().parent.parent


def _delete_folder(path):
    if LIB.does_directory_exist(path):
        for a in LIB.list_assets(path, recursive=True, include_folder=False):
            try:
                LIB.delete_asset(a)
            except Exception:
                pass
        try:
            LIB.delete_directory(path)
        except Exception:
            pass


def _import_glb(src, dest_name, dest_path='/Game/Actor'):
    task = u.AssetImportTask()
    task.filename = str(src)
    task.destination_path = dest_path
    task.destination_name = dest_name
    task.automated = True
    task.replace_existing = True
    task.save = True
    TOOLS.import_asset_tasks([task])
    return list(task.imported_object_paths)


def _fix_john():
    print('=== JOHN: re-import GLB ===')
    src = _PROJECT / 'SourceAssets/Avatars/SK_Founder.glb'
    if not src.exists():
        print('JOHN_MISSING_GLB', src)
        return
    _delete_folder('/Game/Actor/SK_Founder')
    imported = _import_glb(src, 'SK_Founder')
    print('JOHN_IMPORTED %d assets' % len(imported))
    # John's glTF slot 2 was translucent historically; force opaque.
    for p in LIB.list_assets('/Game/Actor/SK_Founder/Materials', recursive=False, include_folder=False):
        m = u.load_asset(p)
        if m is None:
            continue
        try:
            if isinstance(m, u.MaterialInstanceConstant):
                bo = m.get_editor_property('base_property_overrides')
                bo.set_editor_property('override_blend_mode', True)
                bo.set_editor_property('blend_mode', u.BlendMode.BLEND_OPAQUE)
                m.set_editor_property('base_property_overrides', bo)
            else:
                m.set_editor_property('blend_mode', u.BlendMode.BLEND_OPAQUE)
            LIB.save_asset(p)
        except Exception as e:
            print('JOHN_MAT_FIX_FAIL %s %s' % (p, e))


def _fix_isis():
    print('=== ISIS: import rebaked GLB ===')
    src = _PROJECT / 'SourceAssets/Mixamo/Characters/SK_Isis.glb'
    if not src.exists():
        print('ISIS_MISSING_GLB', src)
        return
    _delete_folder('/Game/Actor/SK_Isis')
    imported = _import_glb(src, 'SK_Isis')
    print('ISIS_IMPORTED %d assets' % len(imported))
    # Log the bounds of the imported mesh for the scale sanity check.
    for p in imported:
        a = u.load_asset(p)
        if a is not None and isinstance(a, u.SkeletalMesh):
            b = a.get_bounds()
            print('ISIS_BOUNDS height=%.1f extent=(%.1f,%.1f,%.1f)' % (
                b.box_extent.z * 2.0, b.box_extent.x, b.box_extent.y, b.box_extent.z))


def _fix_textures():
    print('=== TEXTURES: sRGB repair ===')
    chars = ['SK_Erika', 'SK_Amy', 'SK_Michelle', 'SK_Sophie', 'SK_Kate',
             'SK_James', 'SK_Adam', 'SK_Brian', 'SK_Alex', 'SK_Founder', 'SK_Isis']
    hints = ('glossiness', 'metallic', 'roughness', 'orm', 'mr_', '_mr', 'metalness')

    def is_linear(name):
        n = name.casefold()
        return any(h in n for h in hints)

    fixed = 0
    for c in chars:
        tex_dir = '/Game/Actor/%s/Textures' % c
        if not LIB.does_directory_exist(tex_dir):
            continue
        for p in LIB.list_assets(tex_dir, recursive=True, include_folder=False):
            if not is_linear(Path(p).stem):
                continue
            tex = u.load_asset(p)
            if tex is None:
                continue
            try:
                if tex.get_editor_property('srgb') is True:
                    tex.set_editor_property('srgb', False)
                    LIB.save_asset(p)
                    fixed += 1
                    print('TEXFIX', p)
            except Exception as e:
                print('TEXFIX_FAIL', p, e)
        mat_dir = '/Game/Actor/%s/Materials' % c
        if not LIB.does_directory_exist(mat_dir):
            continue
        for p in LIB.list_assets(mat_dir, recursive=True, include_folder=False):
            m = u.load_asset(p)
            if m is None:
                continue
            try:
                MEL.recompile_material(m)
                LIB.save_asset(p)
            except Exception as e:
                print('MATRECOMPILE_FAIL', p, e)
    print('TEXTURES_DONE fixed=%d' % fixed)


def main():
    _fix_john()
    _fix_isis()
    _fix_textures()
    print('REPAIR_AVATARS_COMMANDLET_DONE')


if __name__ == '__main__':
    try:
        main()
    finally:
        u.SystemLibrary.execute_console_command(None, 'quit')
