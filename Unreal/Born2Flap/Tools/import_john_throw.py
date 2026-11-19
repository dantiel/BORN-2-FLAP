"""John's throw uses the founder GLB's custom 2.2s Throw clip (imported
2026-11-15) which reads as crippled; every other pilot plays the Mixamo
Throw from Clips_Colon. John's skeleton is standard mixamorig (FBX frame),
so retarget the same Mixamo Throw onto it and replace SK_FounderThrow.

Run (editor closed, headless commandlet):
    UnrealEditor-Cmd Born2Flap.uproject -ExecutePythonScript="V:/BORN 2 FLAP/Unreal/Born2Flap/Tools/import_john_throw.py" -unattended -nop4 -nosplash -stdout
"""
from pathlib import Path
import unreal as u

TOOLS = u.AssetToolsHelpers.get_asset_tools()
LIB = u.EditorAssetLibrary

_HERE = Path(__file__).resolve()
_PROJECT = _HERE.parents[1]
_CLIP_DIR = _PROJECT / 'SourceAssets/Mixamo/Clips_Underscore'

SKEL = '/Game/Actor/SK_Founder/SkeletalMeshes/SK_Founder_Skeleton.SK_Founder_Skeleton'
DEST_DIR = '/Game/Actor/SK_Founder/SkeletalMeshes'
NAME = 'SK_FounderThrow'


def main():
    skeleton = u.load_asset(SKEL)
    if skeleton is None:
        u.log_error('import_john_throw: skeleton missing %s' % SKEL)
        return
    # purge the old custom throw (and any Take_001 leftovers)
    for p in ['%s/%s' % (DEST_DIR, NAME), '%s/%s_Take_001' % (DEST_DIR, NAME),
              '%s/%s_mixamo_com' % (DEST_DIR, NAME)]:
        if LIB.does_asset_exist(p):
            LIB.delete_asset(p)
            print('DELETED_%s' % p)
    u.SystemLibrary.collect_garbage()

    src = _CLIP_DIR / 'Throw.fbx'
    options = u.FbxImportUI()
    options.automated_import_should_detect_type = False
    options.import_mesh = False
    options.import_as_skeletal = True
    options.mesh_type_to_import = u.FBXImportType.FBXIT_ANIMATION
    options.original_import_type = u.FBXImportType.FBXIT_ANIMATION
    options.import_animations = True
    options.skeleton = skeleton

    task = u.AssetImportTask()
    task.filename = str(src)
    task.destination_path = DEST_DIR
    task.destination_name = NAME
    task.automated = True
    task.replace_existing = True
    task.save = True
    task.options = options
    task.factory = u.FbxFactory()
    TOOLS.import_asset_tasks([task])

    expected = DEST_DIR + '/' + NAME
    seq_pkg = None
    for p in task.imported_object_paths:
        a = u.load_asset(p)
        if a is not None and isinstance(a, u.AnimSequence):
            seq_pkg = p.rsplit('.', 1)[0]
            break
    if seq_pkg is None:
        u.log_error('import_john_throw: no AnimSequence produced (%s)' % task.imported_object_paths)
        return
    if seq_pkg != expected:
        try:
            LIB.rename_asset(seq_pkg, expected)
            seq_pkg = expected
        except Exception as e:
            u.log_error('import_john_throw: rename failed %s' % e)
            return
    seq = u.load_asset(seq_pkg)
    if seq is not None and isinstance(seq, u.AnimSequence):
        LIB.save_asset(seq_pkg)
        print('JOHN_THROW_OK %s dur=%.2fs' % (seq_pkg, seq.get_editor_property('sequence_length')))
    else:
        u.log_error('import_john_throw: asset not an AnimSequence: %s' % seq_pkg)


if __name__ == '__main__':
    try:
        main()
    finally:
        u.SystemLibrary.execute_console_command(None, 'quit')