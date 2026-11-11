"""Strip Mesh Partition authoring actors (modifier actors, interactive/
preview sections) from a World Partition map, then save.

Why: UModifierComponent is the only plugin class that overrides
CreateClassComponentDesc, so the modifier actors' component descs enter the
World Partition descriptor stream. Their classes live in the editor-only
module; an uncooked -game load deserializes every descriptor and asserts at
WorldPartitionActorDesc.cpp:944 (ComponentNativeClass) because the class
cannot resolve. Cook strips them via bIsEditorOnlyActor, but the standalone
-game flow reads the uncooked map - so strip them explicitly after the
builder has baked them into the compiled sections.

Map-agnostic: the map comes from the boot URL.
Run: UnrealEditor-Cmd.exe <uproject> /Game/.../Map
     -ExecutePythonScript=finalize_mp_authors.py -nullrhi -nosound
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

stripped = u.Born2FlapMeshPartition.strip_authoring_actors(world)

map_path = world.get_path_name()
map_path = map_path[:map_path.rfind('.')]
saved = u.Born2FlapMeshPartition.save_world_external_first(world, map_path)
descs = u.Born2FlapMeshPartition.count_actor_descs(world)

u.log('MP_FINALIZE stripped=%d saved=%s descs=%d map=%s'
      % (stripped, saved, descs, map_path))
u.log('MP_FINALIZE_READY')
u.SystemLibrary.execute_console_command(None, 'quit')
