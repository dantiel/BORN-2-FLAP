"""Import the Mixamo FBX clips onto each avatar character's OWN skeleton.

Each character imports its own mixamorig 65-bone skeleton (same bone names as
the founder), so the same FBX clips retarget 1:1 by bone name. Produces
/Game/Actor/{SK_X}/Animations/AM_* for each character.

Run after Tools/create_avatar_assets.py.
"""
from pathlib import Path
import unreal as u

TOOLS = u.AssetToolsHelpers.get_asset_tools()
LIB = u.EditorAssetLibrary

_HERE = Path(__file__).resolve()
_PROJECT = _HERE.parents[1]

# character name -> list of section asset paths (leader first); the skeleton is
# read from the leader mesh at runtime.
_CHARACTERS = [
    ('SK_Pirate',  '/Game/Actor/SK_Pirate/SkeletalMeshes/Section2.Section2'),
    ('SK_Soldier', '/Game/Actor/SK_Soldier/SkeletalMeshes/Section0.Section0'),
    ('SK_Scout',   '/Game/Actor/SK_Scout/SkeletalMeshes/Section0.Section0'),
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


def _skeleton_of(mesh_path):
    mesh = u.load_asset(mesh_path)
    if not mesh or not isinstance(mesh, u.SkeletalMesh):
        return None
    try:
        return mesh.get_editor_property('skeleton')
    except Exception:
        return None


def import_animations():
    for char_name, leader_path in _CHARACTERS:
        skeleton = _skeleton_of(leader_path)
        if not skeleton or not isinstance(skeleton, u.Skeleton):
            u.log_warning('import_avatar_animations: no skeleton for %s (leader %s)' % (char_name, leader_path))
            continue

        dest = '/Game/Actor/%s/Animations' % char_name
        LIB.make_directory(dest)

        imported = 0
        for fbx_name, asset_name in CLIPS:
            source = _find_fbx(fbx_name)
            if not source:
                continue

            options = u.FbxImportUI()
            options.automated_import_should_detect_type = False
            options.import_mesh = False
            options.import_as_skeletal = True
            options.mesh_type_to_import = u.FBXImportType.FBXIT_ANIMATION
            options.original_import_type = u.FBXImportType.FBXIT_ANIMATION
            options.import_animations = True
            options.skeleton = skeleton

            task = u.AssetImportTask()
            task.filename = str(source)
            task.destination_path = dest
            task.destination_name = asset_name
            task.automated = True
            task.replace_existing = True
            task.save = True
            task.options = options
            task.factory = u.FbxFactory()

            TOOLS.import_asset_tasks([task])
            for p in task.imported_object_paths:
                a = u.load_asset(p)
                if a:
                    LIB.save_asset(p)
            imported += 1

        u.log('import_avatar_animations: %s -> %d clips on %s' % (char_name, imported, skeleton.get_path_name()))

    print('AVATAR_ANIM_DONE')


if __name__ == '__main__':
    import_animations()
