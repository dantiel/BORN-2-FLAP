"""Import the Avian Wind (Character Creator 4) pilots as SkeletalMeshes, then
retarget the shared Mixamo locomotion clips onto each character's own skeleton.

The source FBX files have had their CC4 bones renamed to the `mixamorig:`
skeleton by Tools/fbx_rename_bones.py (map_cc4_mixamorig.json), so the shared
Clips_Colon set retargets 1:1 by bone name — the same path the 8 Mixamo humans
use (import_avatar_fbx.py). Extra CC4 bones (twists, toes, face, breast) have
no Mixamo equivalent and stay in ref pose, which is invisible for a pilot.

Asset layout mirrors what ABorn2FlapAvatar::LoadPerson expects:
    /Game/Actor/{Base}/SkeletalMeshes/{Mesh}.{Mesh}            <- skeletal mesh
    /Game/Actor/{Base}/SkeletalMeshes/{Clip}{Suffix}.{...}     <- baked clip set

Run (editor closed, headless commandlet):
    UnrealEditor-Cmd Born2Flap.uproject -ExecutePythonScript="V:/BORN 2 FLAP/Unreal/Born2Flap/Tools/import_avatar_avianwind.py" -unattended -nop4 -nosplash -stdout
"""
from pathlib import Path
import unreal as u

TOOLS = u.AssetToolsHelpers.get_asset_tools()
LIB = u.EditorAssetLibrary

_HERE = Path(__file__).resolve()
_PROJECT = _HERE.parents[1]
_CHAR_DIR = _PROJECT / 'SourceAssets/NewAvatars/AvianWind'
_CLIP_DIR = _PROJECT / 'SourceAssets/Mixamo/Clips_Colon'

# (asset base, source character FBX) — the FBX files are the renamed CC4 rigs.
CHARACTERS = [
    ('SK_AvianWindM', 'AvianWind-male.fbx'),
    ('SK_AvianWindF', 'AvianWind-female.fbx'),
]

# (source clip FBX, LoadPerson suffix)
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

    skeleton = None
    for p in task.imported_object_paths:
        a = u.load_asset(p)
        if a is None:
            continue
        if isinstance(a, u.SkeletalMesh):
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

    print('MESH_%s skeleton=%s' % (mesh_name, skeleton.get_path_name() if skeleton else None))
    return skeleton


def _import_clips(base, clip_prefix, skeleton):
    dest_dir = '/Game/Actor/%s/SkeletalMeshes' % base
    imported = []
    for fbx_name, suffix in CLIPS:
        src = _CLIP_DIR / fbx_name
        if not src.exists():
            u.log_warning('import_avatar_avianwind: missing clip %s' % src)
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
            u.log_warning('import_avatar_avianwind: %s produced no AnimSequence (%s)'
                          % (asset_name, task.imported_object_paths))
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
                    u.log_warning('import_avatar_avianwind: rename %s -> %s failed: %s'
                                  % (seq_pkg, expected, e))
        seq = u.load_asset(seq_pkg)
        if seq is not None and isinstance(seq, u.AnimSequence):
            LIB.save_asset(seq_pkg)
            imported.append(asset_name)
    return imported


def main():
    for base, fbx in CHARACTERS:
        _delete_folder('/Game/Actor/' + base)
        src = _CHAR_DIR / fbx
        if not src.exists():
            u.log_error('import_avatar_avianwind: missing %s' % src)
            continue
        skeleton = _import_mesh(base, base, src)
        if skeleton is None:
            u.log_error('import_avatar_avianwind: %s has no skeleton' % base)
            continue
        mesh = u.load_asset('/Game/Actor/%s/SkeletalMeshes/%s' % (base, base))
        if mesh is not None and isinstance(mesh, u.SkeletalMesh):
            b = mesh.get_bounds()
            ext = b.box_extent
            print('BOUNDS_%s extent=(%.1f, %.1f, %.1f) height=%.1f'
                  % (base, ext.x, ext.y, ext.z, ext.z * 2.0))
        clips = _import_clips(base, base, skeleton)
        print('CHARACTER_%s clips=%d' % (base, len(clips)))
    print('IMPORT_AVATAR_AVIANWIND_DONE')


if __name__ == '__main__':
    main()
