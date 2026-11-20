"""Minimal headless re-import of the retargeted SK_Isis.glb.

Run (editor closed, headless):
    "V:/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "V:/BORN 2 FLAP/Unreal/Born2Flap/Born2Flap.uproject" -ExecutePythonScript="V:/BORN 2 FLAP/Unreal/Born2Flap/Tools/reimport_isis_commandlet.py" -unattended -nop4 -nosplash -stdout -NoVerifyGC
"""
from pathlib import Path
import unreal as u

TOOLS = u.AssetToolsHelpers.get_asset_tools()
LIB = u.EditorAssetLibrary

_PROJECT = Path(__file__).resolve().parent.parent
GLB = _PROJECT / 'SourceAssets/Mixamo/Characters/SK_Isis.glb'


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


def main():
    if not GLB.exists():
        print('ISIS_MISSING_GLB', GLB)
        return
    print('=== ISIS: delete + re-import retargeted GLB ===')
    _delete_folder('/Game/Actor/SK_Isis')
    task = u.AssetImportTask()
    task.filename = str(GLB)
    task.destination_path = '/Game/Actor'
    task.destination_name = 'SK_Isis'
    task.automated = True
    task.replace_existing = True
    task.save = True
    TOOLS.import_asset_tasks([task])
    imported = list(task.imported_object_paths)
    print('ISIS_IMPORTED %d assets' % len(imported))
    for p in imported:
        a = u.load_asset(p)
        if a is not None and isinstance(a, u.SkeletalMesh):
            b = a.get_bounds()
            print('ISIS_BOUNDS height=%.1f' % (b.box_extent.z * 2.0))
    # Report clip lengths so we can confirm Throw/Catch imported.
    for p in imported:
        a = u.load_asset(p)
        if a is not None and isinstance(a, u.AnimSequence):
            print('ISIS_ANIM %s %.2fs' % (a.get_name(), a.get_editor_property('sequence_length')))
    print('REIMPORT_ISIS_DONE')


if __name__ == '__main__':
    try:
        main()
    finally:
        u.SystemLibrary.execute_console_command(None, 'quit')
