"""Import the flattened Founder character as a single SkeletalMesh and retarget
the Mixamo locomotion clips onto its clean 65-bone mixamorig skeleton.

Run:
  UnrealEditor-Cmd Born2Flap.uproject -ExecutePythonScript=Tools/import_founder.py
"""
from pathlib import Path
import json
import unreal as u

TOOLS = u.AssetToolsHelpers.get_asset_tools()
LIB = u.EditorAssetLibrary

_HERE = Path(__file__).resolve()
_PROJECT = _HERE.parents[1]

SOURCE = _PROJECT / 'SourceAssets/Avatars/SK_Founder.glb'
DEST = '/Game/Actor/SK_Founder'

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


def import_founder():
    if not SOURCE.exists():
        raise RuntimeError('Missing %s' % SOURCE)

    _delete_folder(DEST)

    task = u.AssetImportTask()
    task.filename = str(SOURCE)
    task.destination_path = '/Game/Actor'
    task.destination_name = 'SK_Founder'
    task.automated = True
    task.replace_existing = True
    task.save = True
    TOOLS.import_asset_tasks([task])

    # discover mesh + skeleton
    mesh_path = None
    skeleton_path = None
    if LIB.does_directory_exist(DEST):
        for p in LIB.list_assets(DEST, recursive=True, include_folder=False):
            a = u.load_asset(p)
            if a is None:
                continue
            if isinstance(a, u.SkeletalMesh):
                mesh_path = p
                LIB.save_asset(p)
                try:
                    skel = a.get_editor_property('skeleton')
                    if skel is not None:
                        skeleton_path = skel.get_path_name()
                except Exception:
                    pass
            elif isinstance(a, u.Skeleton):
                skeleton_path = p

    u.log('import_founder: mesh=%s skeleton=%s' % (mesh_path, skeleton_path))
    print('MESH=%s' % mesh_path)
    print('SKELETON=%s' % skeleton_path)

    # Animations are baked into the GLB (see bake_mixamo_into_glb.py), so the
    # glTF importer creates them (SK_Founder{Name}) in the same frame as the
    # skeleton — no FBX retarget / axis-conversion mismatch.

    print('IMPORT_FOUNDER_DONE')
    return {'mesh': mesh_path, 'skeleton': skeleton_path}


if __name__ == '__main__':
    import_founder()