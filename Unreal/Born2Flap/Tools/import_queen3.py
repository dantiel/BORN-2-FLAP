"""Desert Queen (SK_Isis) — FBX-frame rebuild (v3).

The FBX mesh import matched the existing GLB skeleton; deleting the GLB
mesh+skeleton first forces a fresh FBX-imported skeleton (same frame as the
Clips_Colon animation tracks, which is the configuration the 8 working
pilots use). GLB materials + textures are kept.

Steps:
1. Delete GLB mesh/skeleton/physics + any previous FBX-import attempt.
2. Re-import the bone-renamed FBX as SK_Isis (fresh mesh + skeleton).
3. Assign the GLB Material_0 MIC to the mesh slot.
4. Retarget the 8 shared locomotion clips onto the new skeleton.

Run (editor closed, headless commandlet):
    UnrealEditor-Cmd Born2Flap.uproject -ExecutePythonScript="V:/BORN 2 FLAP/Unreal/Born2Flap/Tools/import_queen3.py" -unattended -nop4 -nosplash -stdout
"""
from pathlib import Path
import unreal as u

TOOLS = u.AssetToolsHelpers.get_asset_tools()
LIB = u.EditorAssetLibrary

_HERE = Path(__file__).resolve()
_PROJECT = _HERE.parents[1]
_CLIP_DIR = _PROJECT / 'SourceAssets/Mixamo/Clips_Colon'
_FBX = _PROJECT / 'SourceAssets/NewChars/SK_Isis_ue.fbx'

BASE = 'SK_Isis'
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


def _import_mesh():
    dest_dir = '/Game/Actor/%s/SkeletalMeshes' % BASE
    LIB.make_directory(dest_dir)
    options = u.FbxImportUI()
    options.automated_import_should_detect_type = False
    options.import_mesh = True
    options.import_as_skeletal = True
    options.mesh_type_to_import = u.FBXImportType.FBXIT_SKELETAL_MESH
    options.original_import_type = u.FBXImportType.FBXIT_SKELETAL_MESH
    options.import_animations = False

    task = u.AssetImportTask()
    task.filename = str(_FBX)
    task.destination_path = dest_dir
    task.destination_name = BASE
    task.automated = True
    task.replace_existing = True
    task.save = True
    task.options = options
    task.factory = u.FbxFactory()
    TOOLS.import_asset_tasks([task])

    mesh_path, skeleton_path = None, None
    for p in task.imported_object_paths:
        a = u.load_asset(p)
        if a is None:
            continue
        if isinstance(a, u.SkeletalMesh) and mesh_path is None:
            mesh_path = p.rsplit('.', 1)[0]
        elif isinstance(a, u.Skeleton) and skeleton_path is None:
            skeleton_path = p.rsplit('.', 1)[0]
    print('FBX_MESH=%s' % mesh_path)
    print('FBX_SKELETON=%s' % skeleton_path)

    # The skeleton may be created but not listed in imported_object_paths —
    # resolve it from the mesh.
    if skeleton_path is None and mesh_path is not None:
        m = u.load_asset(mesh_path)
        try:
            skel = m.get_editor_property('skeleton')
            if skel is not None:
                skeleton_path = skel.get_path_name().rsplit('.', 1)[0]
                print('FBX_SKELETON_FROM_MESH=%s' % skeleton_path)
        except Exception as e:
            print('FBX_SKELETON_ERR %s' % e)
    if mesh_path is not None:
        m = u.load_asset(mesh_path)
        b = m.get_bounds()
        print('BOUNDS_%s height=%.1f' % (BASE, b.box_extent.z * 2.0))
    return mesh_path, skeleton_path


def _import_clips(skeleton_path):
    dest_dir = '/Game/Actor/%s/SkeletalMeshes' % BASE
    skeleton = u.load_asset(skeleton_path)
    imported = []
    for fbx_name, suffix in CLIPS:
        src = _CLIP_DIR / fbx_name
        asset_name = BASE + suffix
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
            u.log_warning('import_queen3: %s produced no AnimSequence (%s)' % (asset_name, task.imported_object_paths))
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
                    u.log_warning('import_queen3: rename %s -> %s failed: %s' % (seq_pkg, expected, e))
        seq = u.load_asset(seq_pkg)
        if seq is not None and isinstance(seq, u.AnimSequence):
            LIB.save_asset(seq_pkg)
            imported.append(asset_name)
            print('CLIP_%s ok' % asset_name)
    return imported


def main():
    if not _FBX.exists():
        u.log_error('import_queen3: missing %s - run import_queen2.py first' % _FBX)
        return

    # 1. delete GLB mesh/skeleton/physics + previous FBX attempts
    for name in [BASE, BASE + 'Fbx', BASE + '_Skeleton', BASE + '_PhysicsAsset']:
        p = '/Game/Actor/%s/SkeletalMeshes/%s' % (BASE, name)
        if LIB.does_asset_exist(p):
            LIB.delete_asset(p)
            print('DELETED_%s' % p)
    u.SystemLibrary.collect_garbage()

    # 2. fresh FBX import
    mesh_path, skeleton_path = _import_mesh()
    if mesh_path is None or skeleton_path is None:
        u.log_error('import_queen3: no mesh/skeleton after reimport')
        return

    # 3. material slot
    mat = u.load_asset('/Game/Actor/%s/Materials/Material_0.Material_0' % BASE)
    m = u.load_asset(mesh_path)
    if m is not None and mat is not None:
        mats = list(m.get_editor_property('materials'))
        for i, sm in enumerate(mats):
            sm.set_editor_property('material_interface', mat)
            mats[i] = sm
        m.set_editor_property('materials', mats)
        LIB.save_asset(mesh_path)
        print('SLOT_MATERIAL assigned slots=%d' % len(mats))

    # 4. clips
    clips = _import_clips(skeleton_path)
    print('IMPORT_QUEEN3_DONE clips=%d [%s]' % (len(clips), ', '.join(clips)))


if __name__ == '__main__':
    main()
