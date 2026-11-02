"""Import the on-foot pilot characters (CC BY 4.0, Mixamo rigs) as SkeletalMeshes.

Run from the editor's Python commandlet, e.g.
    UnrealEditor-Cmd Born2Flap.uproject -ExecutePythonScript=Tools/create_avatar_assets.py

Each character is a distinct Mixamo-rigged human normalized to the shared
``mixamorig:*`` 65-bone skeleton. Every character imports with its OWN skeleton
(same bone names/hierarchy as the founder), and the AM_* animation clips are
retargeted 1:1 onto each skeleton by bone name (see import_mixamo_animations.py).
The baked bronze founder STATUE (Founder_posed) is untouched and is not an avatar.
"""
from pathlib import Path
import unreal as u

TOOLS = u.AssetToolsHelpers.get_asset_tools()
LIB = u.EditorAssetLibrary

_HERE = Path(__file__).resolve()
_PROJECT = _HERE.parents[1]

_CHARACTERS = [
    ('SK_Pirate',  _PROJECT / 'SourceAssets/Avatars/SK_Pirate.glb'),
    ('SK_Soldier', _PROJECT / 'SourceAssets/Avatars/SK_Soldier.glb'),
    ('SK_Scout',   _PROJECT / 'SourceAssets/Avatars/SK_Scout.glb'),
]


def _skeleton_of(mesh):
    try:
        skel = mesh.get_editor_property('skeleton')
        if skel is not None:
            return skel.get_path_name()
    except Exception:
        pass
    return None


def import_avatar():
    results = {}
    for asset_name, source in _CHARACTERS:
        if not source.exists():
            u.log_warning('create_avatar_assets: no source for %s (%s)' % (asset_name, source))
            continue

        task = u.AssetImportTask()
        task.filename = str(source)
        task.destination_path = '/Game/Actor'
        task.destination_name = asset_name
        task.automated = True
        task.replace_existing = True
        task.save = True
        TOOLS.import_asset_tasks([task])

        meshes = []
        skeletons = set()
        for p in task.imported_object_paths:
            a = u.load_asset(p)
            if not a:
                continue
            if isinstance(a, u.SkeletalMesh):
                meshes.append(a.get_path_name())
                skel = _skeleton_of(a)
                if skel:
                    skeletons.add(skel)
            LIB.save_asset(p)

        results[asset_name] = meshes
        u.log('create_avatar_assets: %s -> %d SkeletalMeshes' % (asset_name, len(meshes)))
        for m in meshes:
            u.log('  MESH %s' % m)
        for s in sorted(skeletons):
            u.log('  SKELETON %s' % s)

    print('AVATAR_IMPORT_DONE')
    return results


if __name__ == '__main__':
    import_avatar()
