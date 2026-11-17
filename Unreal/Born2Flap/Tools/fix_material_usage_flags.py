"""Set the SkeletalMesh usage flag on every avatar material (MIC + base
Material) under /Game/Actor/*/Materials.

Root cause of the "missing texture checker pattern": GLB-imported material
instances are compiled with StaticMesh usage only. When the FBX skeletal
meshes wear them, the engine refuses the material ("missing usage flag
SkeletalMesh! Default Material will be used in game") and the whole pilot
renders with the gray checker Default Material. Erika's four MICs
(Body_MAT / EyeSpec_MAT / Akai_MAT / eyelash_MAT) plus Amy's Ch46 and
James' Ch06 slots are confirmed victims in the runtime log.

Run (editor CLOSED — an open editor holds the asset files locked; the
previous fixer died with MoveFile Error Code 32 on exactly that):
    V:\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe "V:\BORN 2 FLAP\Unreal\Born2Flap\Born2Flap.uproject" -run=pythonscript -script="V:/BORN 2 FLAP/Unreal/Born2Flap/Tools/fix_material_usage_flags.py" -nullrhi -nosound -unattended -nop4 -nosplash -stdout -NoShaderCompile
"""
import sys
from pathlib import Path

import unreal as u

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from project_guard import require_project

require_project()

LIB = u.EditorAssetLibrary

# Every SK_* actor folder plus the shared top-level Materials folder.
folders = [f + '/Materials' for f in LIB.list_assets('/Game/Actor', recursive=False, include_folder=True)]
folders.append('/Game/Actor/Materials')

checked = 0
fixed = 0
for folder in folders:
    for p in LIB.list_assets(folder, recursive=False, include_folder=False):
        a = u.load_asset(p)
        checked += 1
        if a is None:
            print('USAGE_SKIP load-fail %s' % p)
            continue
        try:
            a.set_editor_property('b_used_with_skeletal_mesh', True)
            LIB.save_loaded_asset(a, only_if_is_dirty=False)
            fixed += 1
            print('USAGE_FIX %s' % p)
        except Exception as e:
            print('USAGE_ERR %s %s' % (p, e))

print('USAGE_FLAGS_READY checked=%d fixed=%d' % (checked, fixed))
