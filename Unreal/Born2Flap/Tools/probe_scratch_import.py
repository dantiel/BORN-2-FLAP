"""Probe: import the PRE-repair clean GLB into a scratch folder. Which classes?"""
import json
import unreal as u

TOOLS = u.AssetToolsHelpers.get_asset_tools()
LIB = u.EditorAssetLibrary

SRC = r'V:\BORN 2 FLAP\scratch\Founder_rigged_clean.pre-repair.glb'
DEST = '/Game/Actor/ScratchProbe'

task = u.AssetImportTask()
task.filename = SRC
task.destination_path = DEST
task.destination_name = 'PreRepair'
task.automated = True
task.replace_existing = True
task.save = True

TOOLS.import_asset_tasks([task])

out = dict(objects=[str(p) for p in task.imported_object_paths], classes=[])
for p in task.imported_object_paths:
    a = u.load_asset(p)
    out['classes'].append([str(p), str(a.get_class().get_name()) if a else 'MISSING'])
s = 'PROBE_SCRATCH ' + json.dumps(out)
u.log(s)
print(s)
u.SystemLibrary.execute_console_command(None, 'quit')
