"""Import the Founder character (CC BY 4.0, Mixamo rig) as a SkeletalMesh.

Run from the editor's Python commandlet, e.g.
    UnrealEditor-Cmd Born2Flap.uproject -ExecutePythonScript=Tools/create_avatar_assets.py

The on-foot pilot (ABorn2FlapAvatar) loads /Game/Actor/SK_Founder at runtime and
procedurally poses its bones (walk / hold / throw). This script only imports the
source rig; it does not touch the baked bronze founder statue.
"""
from pathlib import Path
import unreal as u

TOOLS = u.AssetToolsHelpers.get_asset_tools()
LIB = u.EditorAssetLibrary

# The canonical rig lives under SourceAssets; bake_founder.py copies it into
# Saved/NatureSource/Founder when it first runs. Accept either location.
_SAVED = Path(__file__).resolve().parents[1] / 'Saved/NatureSource/Founder/Founder_rigged.glb'
_ARCHIVED = Path(__file__).resolve().parents[2] / 'SourceAssets/Founder/Founder_rigged.glb'


def import_avatar():
    source = _SAVED if _SAVED.exists() else _ARCHIVED
    if not source.exists():
        raise RuntimeError('Missing Founder_rigged.glb. Run Tools/bake_founder.py or git lfs pull.')

    folder = '/Game/Actor'
    task = u.AssetImportTask()
    task.filename = str(source)
    task.destination_path = folder
    task.destination_name = 'SK_Founder'
    task.automated = True
    task.replace_existing = True
    task.save = True
    TOOLS.import_asset_tasks([task])

    imported = [u.load_asset(p) for p in task.imported_object_paths]
    meshes = [a for a in imported if isinstance(a, u.SkeletalMesh)]
    if not meshes:
        raise RuntimeError('No SkeletalMesh produced; imported=%s' % task.imported_object_paths)

    for a in imported:
        if a:
            LIB.save_asset(a.get_path())

    skel = meshes[0]
    u.log('Avatar skeletal mesh ready: %s (%d bones)' % (skel.get_path(), skel.get_bone_count()))
    for i in range(min(skel.get_bone_count(), 40)):
        u.log('  bone %d: %s' % (i, skel.get_bone_name(i)))
    return skel


if __name__ == '__main__':
    import_avatar()
