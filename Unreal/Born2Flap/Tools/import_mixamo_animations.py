"""Import Mixamo locomotion animations onto the on-foot pilot skeleton.

The on-foot pilot (ABorn2FlapAvatar) uses the Founder character's Mixamo rig
(bone names ``mixamorig:*``). Mixamo clips downloaded from mixamo.com share the
exact same rig, so they retarget 1:1 onto the founder skeleton by bone name.

How to use
----------
1. On mixamo.com (free Adobe account) download, with the default "Original" or
   a Mixamo character, the four clips: Idle, Walk, Run, Throw (each as FBX
   *without* skin, "Animation only" is fine too).
2. Drop them into:  Unreal/Born2Flap/SourceAssets/Mixamo/
   named (case-insensitive):  Idle.fbx  Walk.fbx  Run.fbx  Throw.fbx
3. Run:
   UnrealEditor-Cmd Born2Flap.uproject -ExecutePythonScript=Tools/import_mixamo_animations.py

The script imports each clip as an AnimSequence bound to the founder skeleton
into /Game/Actor/Animations (AM_Idle / AM_Walk / AM_Run / AM_Throw). It does not
change gameplay code; wiring the clips into the avatar is a separate step.
"""
from pathlib import Path
import unreal as u

TOOLS = u.AssetToolsHelpers.get_asset_tools()
LIB = u.EditorAssetLibrary

_HERE = Path(__file__).resolve()
_PROJECT = _HERE.parents[1]

SKELETON_PATH = '/Game/Actor/Clean/Founder_rigged_clean/SkeletalMeshes/Object_72_Skeleton'
DEST_PATH = '/Game/Actor/Clean/Animations'

# source filename -> (asset name, log label)
CLIPS = [
    ('Idle.fbx',          'AM_Idle',          'idle'),
    ('Standing.fbx',      'AM_Standing',      'standing'),
    ('Walk.fbx',          'AM_Walk',          'walk'),
    ('Run.fbx',           'AM_Run',           'run'),
    ('RunMedium.fbx',     'AM_RunMedium',     'run-medium'),
    ('WalkBackward.fbx',  'AM_WalkBackward',  'walk-backward'),
    ('StrafeRight.fbx',   'AM_StrafeRight',   'strafe-right'),
    ('StartWalk.fbx',     'AM_StartWalk',     'start-walk'),
    ('StopWalk.fbx',      'AM_StopWalk',      'stop-walk'),
    ('Throw.fbx',         'AM_Throw',         'throw'),
    ('ThrowIn.fbx',       'AM_ThrowIn',       'throw-in'),
    ('Catch.fbx',         'AM_Catch',         'catch'),
    ('Lift.fbx',          'AM_Lift',          'lift'),
]

_SOURCE_DIRS = [
    _PROJECT / 'SourceAssets/Mixamo',
    _PROJECT / 'Saved/Mixamo',
    _PROJECT / 'Saved/NatureSource/Mixamo',
]


def _find_fbx(name: str):
    for d in _SOURCE_DIRS:
        if not d.exists():
            continue
        for f in d.rglob('*'):
            if f.is_file() and f.name.lower() == name.lower() and f.suffix.lower() == '.fbx':
                return f
    return None


def import_animations():
    skeleton = u.load_asset(SKELETON_PATH)
    if not skeleton or not isinstance(skeleton, u.Skeleton):
        u.log_error('import_mixamo_animations: founder skeleton not found at %s '
                    '(run Tools/create_avatar_assets.py first)' % SKELETON_PATH)
        return []

    LIB.make_directory(DEST_PATH)

    imported = []
    for fbx_name, asset_name, label in CLIPS:
        source = _find_fbx(fbx_name)
        if not source:
            u.log_warning('import_mixamo_animations: no source for %s (%s) — searched %s'
                          % (label, fbx_name, [str(d) for d in _SOURCE_DIRS]))
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
        task.destination_path = DEST_PATH
        task.destination_name = asset_name
        task.automated = True
        task.replace_existing = True
        task.save = True
        task.options = options
        task.factory = u.FbxFactory()

        TOOLS.import_asset_tasks([task])

        anims = [p for p in task.imported_object_paths if p.endswith(asset_name)]
        if not anims:
            u.log_warning('import_mixamo_animations: %s produced no %s asset (%s)'
                          % (source.name, asset_name, task.imported_object_paths))
            continue

        for p in task.imported_object_paths:
            a = u.load_asset(p)
            if a:
                LIB.save_asset(p)

        seq = u.load_asset(DEST_PATH + '/' + asset_name)
        if seq and isinstance(seq, u.AnimSequence):
            frames = -1
            for meth in ('get_number_of_frames', 'get_num_frames'):
                if hasattr(seq, meth):
                    try:
                        frames = int(getattr(seq, meth)())
                        break
                    except Exception:
                        pass
            u.log('Mixamo %s -> %s (frames=%d, %.2fs)'
                  % (label, seq.get_path_name(), frames, seq.get_play_length()))
        imported.append(asset_name)

    u.log('import_mixamo_animations: done, imported %d/%d clips'
          % (len(imported), len(CLIPS)))
    return imported


if __name__ == '__main__':
    import_animations()