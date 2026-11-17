"""Import the 8 modern Mixamo FBX pilots as SkeletalMeshes, then retarget the
shared locomotion clips onto each character's own mixamorig skeleton.

All 8 are 65-bone `mixamorig:` characters (covers_animation_rig=true) so the
shared clip set retargets 1:1 with no undriven prop bones (the old fantasy
roster had 67-99 bone rigs whose extra prop/hair bones froze in ref pose).

Why FBX (not GLB): these come from the Mixamo library snapshot (FBX 7.7), already
carrying the clean 65-bone `mixamorig:*` skeleton at cm scale.  UE imports them
natively — mesh + skeleton in the SAME FBX frame — so retargeting the FBX clips
by bone name is 1:1 with no glTF/FBX axis mismatch (the note-5777 crumple only
happens when a glTF skeleton meets FBX animation).

Asset layout mirrors what ABorn2FlapAvatar::LoadPerson expects:
    /Game/Actor/{Base}/SkeletalMeshes/{Mesh}.{Mesh}            <- skeletal mesh
    /Game/Actor/{Base}/SkeletalMeshes/{Clip}{Suffix}.{...}     <- baked clip set

Run (editor closed, headless commandlet):
    UnrealEditor-Cmd Born2Flap.uproject -ExecutePythonScript="V:/BORN 2 FLAP/Unreal/Born2Flap/Tools/import_avatar_fbx.py" -unattended -nop4 -nosplash -stdout
"""
from pathlib import Path
import unreal as u

TOOLS = u.AssetToolsHelpers.get_asset_tools()
LIB = u.EditorAssetLibrary

_HERE = Path(__file__).resolve()
_PROJECT = _HERE.parents[1]
_CHAR_DIR = _PROJECT / 'SourceAssets/Mixamo/Characters'
# Clips re-exported with `mixamorig:` (colon) bone names so they retarget 1:1
# onto the character skeletons (the originals use `mixamorig_` underscore).
_CLIP_DIR = _PROJECT / 'SourceAssets/Mixamo/Clips_Colon'

# (asset base / mesh / clip prefix, source character FBX)
CHARACTERS = [
    ('SK_Amy',      'Amy.fbx'),
    ('SK_Michelle', 'Michelle.fbx'),
    ('SK_Sophie',   'Sophie.fbx'),
    ('SK_Kate',     'Kate.fbx'),
    ('SK_James',    'James.fbx'),
    ('SK_Adam',     'Adam.fbx'),
    ('SK_Brian',    'Brian.fbx'),
    ('SK_Alex',     'Alex.fbx'),
]

# Old roster packages removed from /Game/Actor before import.
OBSOLETE = ['SK_Uriel', 'SK_Medea', 'SK_Captain', 'SK_Dreyar', 'SK_Nightshade',
            'SK_Peasant', 'SK_Granny', 'SK_Scout', 'SK_Soldier']

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
            if not mesh_path and False:
                mesh_path = None
    if skeleton is not None:
        LIB.save_asset(skeleton.get_path_name())

    u.log('import_avatar_fbx: %s mesh=%s skeleton=%s'
          % (mesh_name, mesh_path, skeleton.get_path_name() if skeleton else None))
    print('MESH_%s=%s' % (mesh_name, mesh_path))
    print('SKELETON_%s=%s' % (mesh_name, skeleton.get_path_name() if skeleton else None))
    return skeleton


def _load_existing_skeleton(base):
    mesh_path = '/Game/Actor/%s/SkeletalMeshes/%s' % (base, base)
    mesh = u.load_asset(mesh_path)
    if mesh is not None and isinstance(mesh, u.SkeletalMesh):
        try:
            return mesh.get_editor_property('skeleton')
        except Exception:
            pass
    return None


def _import_clips(base, clip_prefix, skeleton):
    dest_dir = '/Game/Actor/%s/SkeletalMeshes' % base
    imported = []
    for fbx_name, suffix in CLIPS:
        src = _CLIP_DIR / fbx_name
        if not src.exists():
            u.log_warning('import_avatar_fbx: missing clip %s' % src)
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

        # UE names the sequence `{dest}_{take}` (Mixamo take = "mixamo.com" ->
        # "_mixamo_com") for clips exported "With Skin". Find the produced
        # AnimSequence package and normalise its name to exactly what LoadPerson
        # expects. `imported_object_paths` carries full object paths
        # (`.../Name.Name`); strip the trailing `.Name` to get the package path.
        expected = dest_dir + '/' + asset_name
        seq_pkg = None
        for p in task.imported_object_paths:
            a = u.load_asset(p)
            if a is not None and isinstance(a, u.AnimSequence):
                seq_pkg = p.rsplit('.', 1)[0]
                break
        if seq_pkg is None:
            u.log_warning('import_avatar_fbx: %s produced no AnimSequence (%s)'
                          % (asset_name, task.imported_object_paths))
            continue
        if seq_pkg != expected:
            if LIB.does_asset_exist(expected):
                # Correctly-named asset already present (from a prior pass) —
                # drop the take-suffixed duplicate produced by this import.
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
                    u.log_warning('import_avatar_fbx: rename %s -> %s failed: %s'
                                  % (seq_pkg, expected, e))
        seq = u.load_asset(seq_pkg)
        if seq is not None and isinstance(seq, u.AnimSequence):
            LIB.save_asset(seq_pkg)
            imported.append(asset_name)
    return imported


def main():
    for base in OBSOLETE:
        _delete_folder('/Game/Actor/' + base)
        print('DELETED_%s' % base)

    for base, fbx in CHARACTERS:
        # Always reimport from scratch: a stray GLB-imported mesh at the
        # expected path would otherwise be reused (wrong skeleton root).
        _delete_folder('/Game/Actor/' + base)
        src = _CHAR_DIR / fbx
        if not src.exists():
            u.log_error('import_avatar_fbx: missing %s' % src)
            continue
        skeleton = _import_mesh(base, base, src)
        if skeleton is None:
            u.log_error('import_avatar_fbx: %s has no skeleton' % base)
            continue
        # Sanity: report the imported mesh bounds so scale problems are visible
        # in the log (expect a ~150-200 unit tall human in Mixamo cm).
        mesh = u.load_asset('/Game/Actor/%s/SkeletalMeshes/%s' % (base, base))
        if mesh is not None and isinstance(mesh, u.SkeletalMesh):
            b = mesh.get_bounds()
            ext = b.box_extent
            print('BOUNDS_%s extent=(%.1f, %.1f, %.1f) height=%.1f'
                  % (base, ext.x, ext.y, ext.z, ext.z * 2.0))
        clips = _import_clips(base, base, skeleton)
        u.log('import_avatar_fbx: %s done (%d clips)' % (base, len(clips)))
        print('CHARACTER_%s clips=%d' % (base, len(clips)))
    print('IMPORT_AVATAR_FBX_DONE')


if __name__ == '__main__':
    main()