"""Persist the Mesh Partition variant data layers' initial runtime state.

The engine creates every build-variant data layer (MPDL_*) with
InitialRuntimeState=Unloaded and never activates it (cook only strips the
platform-irrelevant variants). The compiled-section actors therefore never
stream in at runtime and the terrain is invisible in-game. This flips the
MeshPartition-owned variant data layers to Loaded and saves the map.

Run: UnrealEditor-Cmd.exe <uproject> /Game/.../Map
     -ExecutePythonScript=finalize_mp_datalayers.py -nullrhi -nosound
"""
import sys
from pathlib import Path

import unreal as u

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from project_guard import require_project

require_project()

world = u.EditorLevelLibrary.get_editor_world()
assert world, 'no editor world'

touched = u.Born2FlapMeshPartition.set_variant_data_layers_initial_runtime_state(world, True)

map_path = world.get_path_name()
map_path = map_path[:map_path.rfind('.')]
saved = u.Born2FlapMeshPartition.save_world_external_first(world, map_path)

for line in u.Born2FlapMeshPartition.dump_data_layer_states(world):
    u.log('MP_DATALAYER ' + line)

u.log('MP_DATALAYERS touched=%d saved=%s map=%s' % (touched, saved, map_path))
u.log('MP_DATALAYERS_READY')
u.SystemLibrary.execute_console_command(None, 'quit')
