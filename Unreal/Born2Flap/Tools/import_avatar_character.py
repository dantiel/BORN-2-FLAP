"""Import a flattened Mixamo avatar GLB (mesh + skeleton + baked clips) as a
SkeletalMesh with AnimSequences, mirroring Tools/import_founder.py for any
character.

Run:
  UnrealEditor-Cmd Born2Flap.uproject -ExecutePythonScript="V:/BORN 2 FLAP/Unreal/Born2Flap/Tools/import_avatar_character.py" -unattended -nop4 -nosplash -stdout

Characters are declared below (SOURCE + destination name). Deletes the
existing destination folder first (idempotent re-import).
"""
from pathlib import Path
import unreal as u

TOOLS = u.AssetToolsHelpers.get_asset_tools()
LIB = u.EditorAssetLibrary

_HERE = Path(__file__).resolve()
_PROJECT = _HERE.parents[1]

CHARACTERS = [
    ('SK_Scout',   'SK_Scout_anim.glb'),
    ('SK_Soldier', 'SK_Soldier_anim.glb'),
    ('SK_Xbot',    'SK_Xbot.glb'),
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


def import_character(name, glb):
    src = _PROJECT / 'SourceAssets/Avatars' / glb
    if not src.exists():
        raise RuntimeError('Missing %s' % src)
    dest = '/Game/Actor/' + name

    _delete_folder(dest)

    task = u.AssetImportTask()
    task.filename = str(src)
    task.destination_path = '/Game/Actor'
    task.destination_name = name
    task.automated = True
    task.replace_existing = True
    task.save = True
    TOOLS.import_asset_tasks([task])

    mesh_path = None
    skeleton_path = None
    if LIB.does_directory_exist(dest):
        for p in LIB.list_assets(dest, recursive=True, include_folder=False):
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

    u.log('import_avatar: %s mesh=%s skeleton=%s' % (name, mesh_path, skeleton_path))
    print('MESH_%s=%s' % (name, mesh_path))
    print('SKELETON_%s=%s' % (name, skeleton_path))
    return {'mesh': mesh_path, 'skeleton': skeleton_path}


if __name__ == '__main__':
    for name, glb in CHARACTERS:
        import_character(name, glb)
    print('IMPORT_AVATAR_CHARACTERS_DONE')
