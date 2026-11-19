"""Replace the degenerate GLB-baked SK_FounderThrow with the full-resolution
Clips_Colon Throw clip, retargeted 1:1 onto the Founder's own skeleton.

Why: the Throw baked into SK_Founder.glb is degenerate (8 keyframes, arm sweep
~3 degrees), so John's throw wind-up plays almost static. The other eight
pilots play the full-res Clips_Colon/Throw.fbx (67 keys, 2.2 s); this imports
the SAME clip onto the Founder rig.

Naming: the Founder skeleton comes from the glTF import, which sanitised the
`mixamorig:` colon to `mixamorig_` (underscore). Clips_Colon uses colons. Both
tokens are 11 bytes, so the clip is byte-patched to the underscore spelling
(same length -> the FBX structure stays intact) and then imported - the
retarget is 1:1 by bone name, the same mechanism import_avatar_fbx.py uses
for the modern roster.

Run (editor closed, headless commandlet):
    UnrealEditor-Cmd Born2Flap.uproject -ExecutePythonScript="V:/BORN 2 FLAP/Unreal/Born2Flap/Tools/import_founder_throw.py" -unattended -nop4 -nosplash -stdout
"""
from pathlib import Path
import tempfile
import unreal as u

TOOLS = u.AssetToolsHelpers.get_asset_tools()
LIB = u.EditorAssetLibrary

_HERE = Path(__file__).resolve()
_PROJECT = _HERE.parents[1]

SRC = _PROJECT / 'SourceAssets/Mixamo/Clips_Colon/Throw.fbx'
SKELETON = '/Game/Actor/SK_Founder/SkeletalMeshes/SK_Founder_Skeleton.SK_Founder_Skeleton'
DEST_DIR = '/Game/Actor/SK_Founder/SkeletalMeshes'
DEST_NAME = 'SK_FounderThrow'


def _patched_fbx(tmpdir):
    raw = SRC.read_bytes()
    patched = raw.replace(b'mixamorig:', b'mixamorig_')
    if patched == raw:
        raise RuntimeError('no colon bone names found in %s' % SRC)
    out = Path(tmpdir) / 'Throw_us.fbx'
    out.write_bytes(patched)
    return out


def main():
    if not SRC.exists():
        raise RuntimeError('Missing %s' % SRC)
    skeleton = u.load_asset(SKELETON)
    if skeleton is None:
        raise RuntimeError('Missing skeleton %s' % SKELETON)

    with tempfile.TemporaryDirectory(prefix='b2f_throw_') as tmp:
        src = _patched_fbx(tmp)

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
        task.destination_name = DEST_NAME
        task.automated = True
        task.replace_existing = True
        task.save = True
        task.options = options
        task.factory = u.FbxFactory()
        TOOLS.import_asset_tasks([task])

    # The importer may name the clip `{dest}_{take}` (takes: Take_001 and
    # mixamo.com). Normalise to exactly what ABorn2FlapAvatar::LoadPerson
    # loads, dropping take-suffixed duplicates.
    expected = '%s/%s' % (DEST_DIR, DEST_NAME)
    seq_pkg = None
    for p in task.imported_object_paths:
        a = u.load_asset(p)
        if a is not None and isinstance(a, u.AnimSequence):
            seq_pkg = p.rsplit('.', 1)[0]
            break
    if seq_pkg is None:
        print('FOUNDER_THROW_IMPORT_FAILED imported=%d' % len(task.imported_object_paths))
        return False
    if seq_pkg != expected:
        if LIB.does_asset_exist(expected):
            try:
                LIB.delete_asset(seq_pkg)
            except Exception:
                pass
            seq_pkg = expected
        else:
            try:
                LIB.rename_asset(seq_pkg, expected)
                seq_pkg = expected
            except Exception as e:
                print('FOUNDER_THROW_RENAME_FAILED %s -> %s: %s' % (seq_pkg, expected, e))
    seq = u.load_asset(seq_pkg)
    if seq is not None and isinstance(seq, u.AnimSequence):
        LIB.save_asset(seq_pkg)
    print('FOUNDER_THROW_IMPORT_OK path=%s dur=%.2fs' % (seq_pkg, seq.get_play_length() if seq else -1))
    return seq is not None


if __name__ == '__main__':
    try:
        main()
    finally:
        u.SystemLibrary.execute_console_command(None, 'quit')