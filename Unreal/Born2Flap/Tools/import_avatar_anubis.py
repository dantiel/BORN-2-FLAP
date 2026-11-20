"""Import Anubis (renamed AutoRig Pro -> mixamorig FBX) as a SkeletalMesh and
retarget the shared Mixamo locomotion clips onto his skeleton.

The source SK_Anubis.fbx was produced by fbx_rename_bones.py + anubis_bone_map.json:
the single-chain deform bones (root.x/spine_01.x/.../arm_stretch.l/forearm_stretch.l/
hand.l/thigh_stretch.l/leg_stretch.l/foot.l/toes_01.l + fingers) were renamed to the
shared mixamorig:* names so the Clips_Colon clip set retargets 1:1 by bone name —
the same mechanism used for the 8 Mixamo humans and Isis.

Layout mirrors ABorn2FlapAvatar::LoadPerson:
    /Game/Actor/SK_Anubis/SkeletalMeshes/SK_Anubis.SK_Anubis            <- mesh
    /Game/Actor/SK_Anubis/SkeletalMeshes/SK_Anubis{Idle,Walk,...}. ...  <- clips

Run (editor closed, headless commandlet):
    UnrealEditor-Cmd Born2Flap.uproject -ExecutePythonScript="V:/BORN 2 FLAP/Unreal/Born2Flap/Tools/import_avatar_anubis.py" -unattended -nop4 -nosplash -stdout
"""
from pathlib import Path
import unreal as u

TOOLS = u.AssetToolsHelpers.get_asset_tools()
LIB = u.EditorAssetLibrary

_HERE = Path(__file__).resolve()
_PROJECT = _HERE.parents[1]

BASE = 'SK_Anubis'
SRC = _PROJECT / 'SourceAssets/NewChars/SK_Anubis.fbx'
_CLIP_DIR = _PROJECT / 'SourceAssets/Mixamo/Clips_Colon'

CLIPS = [
    ('Idle.fbx',         'Idle'),
    ('Walk.fbx',         'Walk'),
    ('Run.fbx',          'Run'),
    ('Throw.fbx',        'Throw'),
    ('Catch.fbx',        'Catch'),
    ('Standing.fbx',     'Standing'),
    ('StrafeRight.fbx',  'StrafeRight'),
    ('WalkBackward.fbx', 'WalkBackward'),
]


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


def _import_mesh(base, mesh_name, src):
    dest_dir = '/Game/Actor/%s/SkeletalMeshes' % base
    LIB.make_directory(dest_dir)

    options = u.FbxImportUI()
    options.automated_import_should_detect_type = False
    options.import_mesh = True
    options.import_as_skeletal = True
    options.mesh_type_to_import = u.FBXImportType.FBXIT_SKELETAL_MESH
    options.original_import_type = u.FBXImportType.FBXIT_SKELETAL_MESH
    options.import_animations = False

    task = u.AssetImportTask()
    task.filename = str(src)
    task.destination_path = dest_dir
    task.destination_name = mesh_name
    task.automated = True
    task.replace_existing = True
    task.save = True
    task.options = options
    task.factory = u.FbxFactory()

    TOOLS.import_asset_tasks([task])

    mesh_path = None
    skeleton = None
    for p in task.imported_object_paths:
        a = u.load_asset(p)
        if a is None:
            continue
        if isinstance(a, u.SkeletalMesh):
            mesh_path = p
            LIB.save_asset(p)
            try:
                skel = a.get_editor_property('skeleton')
                if skel is not None:
                    skeleton = skel
            except Exception:
                pass
        elif isinstance(a, u.Skeleton):
            skeleton = a
    if skeleton is not None:
        LIB.save_asset(skeleton.get_path_name())

    print('MESH_%s=%s' % (mesh_name, mesh_path))
    print('SKELETON_%s=%s' % (mesh_name, skeleton.get_path_name() if skeleton else None))
    return skeleton


def _import_clips(base, clip_prefix, skeleton):
    dest_dir = '/Game/Actor/%s/SkeletalMeshes' % base
    imported = []
    for fbx_name, suffix in CLIPS:
        src = _CLIP_DIR / fbx_name
        if not src.exists():
            print('SKIP (missing) %s' % src)
            continue
        asset_name = clip_prefix + suffix

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
            print('WARN %s produced no AnimSequence (%s)' % (asset_name, task.imported_object_paths))
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
                    print('WARN rename %s -> %s failed: %s' % (seq_pkg, expected, e))
        seq = u.load_asset(seq_pkg)
        if seq is not None and isinstance(seq, u.AnimSequence):
            LIB.save_asset(seq_pkg)
            imported.append(asset_name)
    return imported


def main():
    _delete_folder('/Game/Actor/' + BASE)
    if not SRC.exists():
        print('ERROR missing source %s' % SRC)
        return
    skeleton = _import_mesh(BASE, BASE, SRC)
    if skeleton is None:
        print('ERROR %s has no skeleton' % BASE)
        return
    mesh = u.load_asset('/Game/Actor/%s/SkeletalMeshes/%s' % (BASE, BASE))
    if mesh is not None and isinstance(mesh, u.SkeletalMesh):
        b = mesh.get_bounds()
        ext = b.box_extent
        print('BOUNDS_%s extent=(%.1f, %.1f, %.1f) height=%.1f'
              % (BASE, ext.x, ext.y, ext.z, ext.z * 2.0))
    clips = _import_clips(BASE, BASE, skeleton)
    print('CHARACTER_%s clips=%d' % (BASE, len(clips)))
    print('IMPORT_ANUBIS_DONE')


if __name__ == '__main__':
    main()
