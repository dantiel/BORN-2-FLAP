"""Re-import the repaired founder GLB into /Game/Actor/Clean (replace existing).

The source Founder_rigged_clean.glb was repaired: Sketchfab's x100 armature
rotation (baked into the skin inverse-bind matrices) was normalized and the
IBMs re-baked from the corrected chain. This re-import replaces the broken
skeletal mesh + skeleton that made the avatar render as a ~210 m blob.

Run AFTER the repair, BEFORE importing the animations again.
"""
from pathlib import Path
import unreal as u

TOOLS = u.AssetToolsHelpers.get_asset_tools()
LIB = u.EditorAssetLibrary

_HERE = Path(__file__).resolve()
_PROJECT = _HERE.parents[1]

SRC = _PROJECT / 'SourceAssets/Founder/Founder_rigged_clean.glb'
assert SRC.exists(), 'repaired GLB missing: %s' % SRC

# Clean up leftovers from a previous (static-fallback) import attempt.
for stale in ('/Game/Actor/ScratchProbe',
              '/Game/Actor/Clean/Founder_rigged_clean/StaticMeshes'):
    if LIB.does_directory_exist(stale):
        LIB.delete_directory(stale)
        u.log('reimport_avatar_assets: deleted stale %s' % stale)

task = u.AssetImportTask()
task.filename = str(SRC)
task.destination_path = '/Game/Actor/Clean'
task.destination_name = 'Founder_rigged_clean'
task.automated = True
task.replace_existing = True
task.save = True

TOOLS.import_asset_tasks([task])

meshes = []
for p in task.imported_object_paths:
    a = u.load_asset(p)
    if a:
        LIB.save_asset(p)
        if isinstance(a, u.SkeletalMesh):
            meshes.append(a)

u.log('reimport_avatar_assets: %d imported objects, %d SkeletalMeshes'
      % (len(task.imported_object_paths), len(meshes)))
for m in meshes:
    b = m.get_bounds()
    u.log('  mesh %s bounds origin=%s extent=%s'
          % (m.get_path_name(), b.origin, b.box_extent))
print('REIMPORT_DONE meshes=%d' % len(meshes))
u.SystemLibrary.execute_console_command(None, 'quit')