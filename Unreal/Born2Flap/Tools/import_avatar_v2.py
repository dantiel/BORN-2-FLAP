"""Import the flattened avatar characters (single mesh, single 65-bone skeleton).

The flattened GLBs (SourceAssets/Avatars/SK_{Soldier,Scout}.glb) are already
Z-up, ~180cm, one merged mesh + one skeleton with the wrapper nodes baked away,
so the UE glTF importer produces a normal standing human. This script:

  1. deletes the old SK_Pirate / SK_Soldier / SK_Scout packages
  2. imports the flattened GLBs
  3. discovers the resulting SkeletalMesh + Skeleton and writes their paths to
     SourceAssets/Avatars/import_paths.json
  4. retargets the Mixamo FBX clips onto each new skeleton (AM_* AnimSequences)

Run:
  UnrealEditor-Cmd Born2Flap.uproject -ExecutePythonScript=Tools/import_avatar_v2.py
"""
from pathlib import Path
import json
import unreal as u

TOOLS = u.AssetToolsHelpers.get_asset_tools()
LIB = u.EditorAssetLibrary

_HERE = Path(__file__).resolve()
_PROJECT = _HERE.parents[1]

_CHARACTERS = [
    ('SK_Soldier', _PROJECT / 'SourceAssets/Avatars/SK_Soldier.glb'),
    ('SK_Scout',   _PROJECT / 'SourceAssets/Avatars/SK_Scout.glb'),
]

CLIPS = [
    ('Idle.fbx',          'AM_Idle'),
    ('Standing.fbx',      'AM_Standing'),
    ('Walk.fbx',          'AM_Walk'),
    ('Run.fbx',           'AM_Run'),
    ('RunMedium.fbx',     'AM_RunMedium'),
    ('WalkBackward.fbx',  'AM_WalkBackward'),
    ('StrafeRight.fbx',   'AM_StrafeRight'),
    ('StartWalk.fbx',     'AM_StartWalk'),
    ('StopWalk.fbx',      'AM_StopWalk'),
    ('Throw.fbx',         'AM_Throw'),
    ('ThrowIn.fbx',       'AM_ThrowIn'),
    ('Catch.fbx',         'AM_Catch'),
    ('Lift.fbx',          'AM_Lift'),
]

_SOURCE_DIRS = [
    _PROJECT / 'SourceAssets/Mixamo',
    _PROJECT / 'Saved/Mixamo',
    _PROJECT / 'Saved/NatureSource/Mixamo',
]


def _find_fbx(name):
    for d in _SOURCE_DIRS:
        if not d.exists():
            continue
        for f in d.rglob('*'):
            if f.is_file() and f.name.lower() == name.lower() and f.suffix.lower() == '.fbx':
                return f
    return None


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


def _find_skeletal_meshes(root):
    out = []
    if not LIB.does_directory_exist(root):
        return out
    for p in LIB.list_assets(root, recursive=True, include_folder=False):
        a = u.load_asset(p)
        if a and isinstance(a, u.SkeletalMesh):
            out.append(p)
    return out


def _skeleton_of(mesh):
    try:
        skel = mesh.get_editor_property('skeleton')
        if skel is not None:
            return skel
    except Exception:
        pass
    return None


def import_avatar():
    results = {}

    # 1. delete old packages
    for name, _ in _CHARACTERS:
        _delete_folder('/Game/Actor/%s' % name)
    _delete_folder('/Game/Actor/SK_Pirate')

    for name, source in _CHARACTERS:
        if not source.exists():
            u.log_warning('import_avatar_v2: no source %s' % source)
            continue

        task = u.AssetImportTask()
        task.filename = str(source)
        task.destination_path = '/Game/Actor'
        task.destination_name = name
        task.automated = True
        task.replace_existing = True
        task.save = True
        TOOLS.import_asset_tasks([task])

        meshes = _find_skeletal_meshes('/Game/Actor/%s' % name)
        mesh_path = meshes[0] if meshes else None
        skeleton_path = None
        if mesh_path:
            mesh = u.load_asset(mesh_path)
            if mesh:
                LIB.save_asset(mesh_path)
                skel = _skeleton_of(mesh)
                if skel:
                    skeleton_path = skel.get_path_name()

        u.log('import_avatar_v2: %s -> mesh=%s skeleton=%s (meshes=%s)'
              % (name, mesh_path, skeleton_path, meshes))
        results[name] = {'mesh': mesh_path, 'skeleton': skeleton_path}

        # retarget animations
        if skeleton_path:
            skel_obj = u.load_asset(skeleton_path)
            if skel_obj and isinstance(skel_obj, u.Skeleton):
                dest = '/Game/Actor/%s/Animations' % name
                LIB.make_directory(dest)
                imported = 0
                for fbx_name, asset_name in CLIPS:
                    fbx = _find_fbx(fbx_name)
                    if not fbx:
                        continue
                    options = u.FbxImportUI()
                    options.automated_import_should_detect_type = False
                    options.import_mesh = False
                    options.import_as_skeletal = True
                    options.mesh_type_to_import = u.FBXImportType.FBXIT_ANIMATION
                    options.original_import_type = u.FBXImportType.FBXIT_ANIMATION
                    options.import_animations = True
                    options.skeleton = skel_obj
                    atask = u.AssetImportTask()
                    atask.filename = str(fbx)
                    atask.destination_path = dest
                    atask.destination_name = asset_name
                    atask.automated = True
                    atask.replace_existing = True
                    atask.save = True
                    atask.options = options
                    atask.factory = u.FbxFactory()
                    TOOLS.import_asset_tasks([atask])
                    imported += 1
                u.log('import_avatar_v2: %s -> %d animation clips' % (name, imported))

    out_file = _PROJECT / 'SourceAssets/Avatars/import_paths.json'
    out_file.write_text(json.dumps(results, indent=2))
    print('AVATAR_IMPORT_V2_DONE')
    print(json.dumps(results, indent=2))
    return results


if __name__ == '__main__':
    import_avatar()
