"""Stage 1d: Isis clip set re-import (all 8, fresh skeleton)."""
from pathlib import Path
import unreal as u

TOOLS = u.AssetToolsHelpers.get_asset_tools()
LIB = u.EditorAssetLibrary

_HERE = Path(__file__).resolve()
_PROJECT = _HERE.parents[1]
_CLIP_DIR = _PROJECT / 'SourceAssets/Mixamo/Clips_Underscore'

CLIPS = [
    ('Idle.fbx',          'Idle'),
    ('Walk.fbx',          'Walk'),
    ('Run.fbx',           'Run'),
    ('Throw.fbx',         'Throw'),
    ('Catch.fbx',         'Catch'),
    ('Standing.fbx',      'Standing'),
    ('StrafeRight.fbx',   'StrafeRight'),
    ('WalkBackward.fbx',  'WalkBackward'),
]


def main():
    print('STAGE1D_BEGIN')
    sk_path = '/Game/Actor/SK_Isis/SkeletalMeshes/SK_Isis_Skeleton.SK_Isis_Skeleton'
    sk = u.load_asset(sk_path)
    if sk is None:
        print('ISIS_SKEL_MISSING')
        return
    dest_dir = '/Game/Actor/SK_Isis/SkeletalMeshes'
    # drop the lowercase GLB anim extras and any stale junk
    for p in LIB.list_assets(dest_dir, recursive=False, include_folder=False):
        nm = Path(p).stem
        if nm in ('SK_Isisrun', 'SK_Isiswalk') or 'mixamo_com' in nm:
            try:
                LIB.delete_asset(p)
                print('DELETE', p)
            except Exception as e:
                print('DELETE_FAIL', p, str(e)[:100])
    ok = []
    for fbx_name, suffix in CLIPS:
        asset_name = 'SK_Isis' + suffix
        src = _CLIP_DIR / fbx_name
        if not src.exists():
            print('CLIP_MISSING', src)
            continue
        options = u.FbxImportUI()
        options.automated_import_should_detect_type = False
        options.import_mesh = False
        options.import_as_skeletal = True
        options.mesh_type_to_import = u.FBXImportType.FBXIT_ANIMATION
        options.original_import_type = u.FBXImportType.FBXIT_ANIMATION
        options.import_animations = True
        options.skeleton = sk
        task = u.AssetImportTask()
        task.filename = str(src)
        task.destination_path = dest_dir
        task.destination_name = asset_name
        task.automated = True
        task.replace_existing = True
        task.save = True
        task.options = options
        task.factory = u.FbxFactory()
        TOOLS.import_asset_tasks([task])
        expected = dest_dir + '/' + asset_name
        seq_pkg = None
        for p in task.imported_object_paths:
            a = u.load_asset(p)
            if a is not None and isinstance(a, u.AnimSequence):
                seq_pkg = p.rsplit('.', 1)[0]
                break
        if seq_pkg is None:
            print('CLIP_NOANIM', asset_name)
            continue
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
                    print('CLIP_RENAME_FAIL', seq_pkg, '->', expected, str(e)[:100])
        seq = u.load_asset(seq_pkg)
        if seq is not None and isinstance(seq, u.AnimSequence):
            LIB.save_asset(seq_pkg)
            ok.append(asset_name)
    print('ISIS_CLIPS=%s' % ok)
    # verify each clip is bound to the current skeleton
    for asset_name in ok:
        seq = u.load_asset(dest_dir + '/' + asset_name)
        try:
            skel = seq.get_editor_property('skeleton')
        except Exception:
            skel = None
        print('BIND', asset_name, '->', skel.get_path_name() if skel else 'NONE')
    print('STAGE1D_DONE')


if __name__ == '__main__':
    main()
