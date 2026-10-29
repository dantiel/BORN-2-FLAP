"""Import the on-foot pilot characters (CC BY 4.0, Mixamo rigs) as SkeletalMeshes.

Run from the editor's Python commandlet, e.g.
    UnrealEditor-Cmd Born2Flap.uproject -ExecutePythonScript=Tools/create_avatar_assets.py

The on-foot pilot (ABorn2FlapAvatar) loads /Game/Actor/SK_Founder (person 0) and
/Game/Actor/SK_Hunter (person 1) at runtime and procedurally poses their bones
(walk / hold / throw). This script only imports the source rigs; it does not
touch the baked bronze founder statue.
"""
from pathlib import Path
import unreal as u

TOOLS = u.AssetToolsHelpers.get_asset_tools()
LIB = u.EditorAssetLibrary

_HERE = Path(__file__).resolve()
_PROJECT = _HERE.parents[1]
_REPO = _HERE.parents[3]

_CHARACTERS = [
    ("SK_Founder", [
        _PROJECT / 'Saved/NatureSource/Founder/Founder_rigged.glb',
        _PROJECT / 'SourceAssets/Founder/Founder_rigged.glb',
    ]),
    ("SK_Hunter", [
        _REPO / '.setup/founder/hunter.glb',
    ]),
]


def _pick_source(candidates):
    for c in candidates:
        if c.exists():
            return c
    return None


def import_avatar():
    imported_all = []
    for asset_name, candidates in _CHARACTERS:
        source = _pick_source(candidates)
        if not source:
            u.log_warning('create_avatar_assets: no source for %s (tried %s)'
                          % (asset_name, [str(c) for c in candidates]))
            continue

        task = u.AssetImportTask()
        task.filename = str(source)
        task.destination_path = '/Game/Actor'
        task.destination_name = asset_name
        task.automated = True
        task.replace_existing = True
        task.save = True
        TOOLS.import_asset_tasks([task])

        imported = [u.load_asset(p) for p in task.imported_object_paths]
        meshes = [a for a in imported if isinstance(a, u.SkeletalMesh)]
        if not meshes:
            u.log_warning('create_avatar_assets: no SkeletalMesh produced for %s (%s)'
                          % (asset_name, task.imported_object_paths))
            continue

        for a in imported:
            if a:
                LIB.save_asset(a.get_path())

        skel = meshes[0]
        imported_all.append(skel)
        u.log('Avatar skeletal mesh ready: %s (%d bones)'
              % (skel.get_path(), skel.get_bone_count()))
        for i in range(min(skel.get_bone_count(), 40)):
            u.log('  bone %d: %s' % (i, skel.get_bone_name(i)))

    return imported_all


if __name__ == '__main__':
    import_avatar()
