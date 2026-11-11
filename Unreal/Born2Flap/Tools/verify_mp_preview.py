"""Headless verification of the MeshPartition editor preview proxies.

Run: UnrealEditor-Cmd.exe <uproject> /Game/Yunsha/Maps/YUNSHA
     -ExecutePythonScript=verify_mp_preview.py -nullrhi -nosound
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
u.log('MPPREVIEW world=%s' % world.get_path_name())

subsys = u.get_editor_subsystem(u.Born2FlapMeshPartitionPreview)
count = subsys.force_scan_for_preview(world)
u.log('MPPREVIEW spawned=%d' % count)

proxies = 0
meshes_ok = 0
meshes_bad = 0
for actor in u.EditorLevelLibrary.get_all_level_actors():
    label = actor.get_actor_label() if hasattr(actor, 'get_actor_label') else ''
    if label.startswith('MPPreview_'):
        proxies += 1
        for comp in actor.get_components_by_class(u.StaticMeshComponent):
            sm = comp.static_mesh
            if sm is not None and sm.get_render_data() is not None:
                meshes_ok += 1
            else:
                meshes_bad += 1

u.log('MPPREVIEW proxies=%d smc_with_renderdata=%d smc_missing=%d' % (proxies, meshes_ok, meshes_bad))
u.log('MPPREVIEW_READY')

u.SystemLibrary.execute_console_command(None, 'quit')