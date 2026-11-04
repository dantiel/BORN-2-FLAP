"""Import the RAW Founder_rigged.glb (un-flattened Sketchfab rig) as the avatar
skeletal mesh. The hand-flattened variants crumbled the mesh; the raw rig is
internally self-consistent (correct skinning), so UE renders it as a
recognizable human in reference pose -- the original 'functioning actor'.
"""
from pathlib import Path
import unreal as u

SRC = 'V:/BORN 2 FLAP/Unreal/Born2Flap/SourceAssets/Avatars/SK_Founder.glb'

TOOLS = u.AssetToolsHelpers.get_asset_tools()
LIB = u.EditorAssetLibrary

task = u.AssetImportTask()
task.filename = SRC
task.destination_path = '/Game/Actor'
task.destination_name = 'SK_Founder'
task.automated = True
task.replace_existing = True
task.save = True
TOOLS.import_asset_tasks([task])

u.log('IMPORT_RESULT_PATHS_BEGIN')
for p in task.imported_object_paths:
    a = u.load_asset(p)
    if a:
        u.log('IMPORTED %s | %s' % (p, type(a).__name__))
        try:
            LIB.save_asset(p)
        except Exception as e:
            u.log('SAVE_FAIL %s %s' % (p, e))
u.log('IMPORT_RESULT_PATHS_END')

# Report the skeletal mesh bone count for verification.
for p in task.imported_object_paths:
    a = u.load_asset(p)
    if isinstance(a, u.SkeletalMesh):
        u.log('SKELETAL_MESH %s bones=%d' % (p, a.get_bone_count()))
u.log('IMPORT_DONE')