"""World Partition direct-authoring test on an ENGINE TEMPLATE map (not a blank map).

Phase GEN (env B2F_WPTEST_PHASE=gen):
  1) create the world via UEditorLoadingAndSavingUtils::NewMapFromTemplate
     (the /Engine/Maps/Templates/OpenWorld template brings a fully initialized
     World Partition: runtime hash, grids, data layers) -- no retrofit
  2) spawn 3 ordinary StaticMeshActors (cubes) at 3 different WP cells
  3) save with the external-packages-first order
  4) dump the actor descriptor container state

Phase PROBE (env B2F_WPTEST_PHASE=probe):
  load the saved map and verify: actors present, descriptors registered,
  external actor packages on disk.

Run: UnrealEditor-Cmd.exe <uproject> -ExecutePythonScript=probe_wp_sma.py
     (map argument for the probe phase: /Game/WPTemplateTest/Maps/WPTEST)
The same script runs unchanged in an interactive editor Python console --
that is the headless-vs-interactive comparison vehicle.
"""
import os
import sys
from pathlib import Path

import unreal as u

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from project_guard import require_project

require_project()

PHASE = os.environ.get('B2F_WPTEST_PHASE', 'gen')
MAP_PATH = '/Game/WPTemplateTest/Maps/WPTEST'
TEMPLATE = '/Engine/Maps/Templates/OpenWorld'
TAG = 'WPTestCube'
CELLS = [(0, 0, 300), (60000, 0, 300), (0, 60000, 300)]  # 3 different WP cells


def log_desc_state(world, prefix):
    u.log('%s_DESC_COUNT=%d' % (prefix, u.Born2FlapMeshPartition.count_actor_descs(world)))
    for line in u.Born2FlapMeshPartition.dump_actor_desc_state(world):
        u.log('%s_DESC %s' % (prefix, line))


def gen():
    u.EditorAssetLibrary.make_directory('/Game/WPTemplateTest/Maps')
    world = u.Born2FlapMeshPartition.new_map_from_template_cpp(TEMPLATE)
    assert world, 'template map creation failed'
    world.get_world_settings().set_editor_property('force_no_precomputed_lighting', True)
    # WP state is reported through the C++ descriptor dump (wp_initialized,
    # external_actors) -- UWorld::GetWorldPartition has no Python binding.

    cube = u.load_asset('/Engine/BasicShapes/Cube')
    assert cube, 'cube mesh missing'
    for index, (x, y, z) in enumerate(CELLS):
        actor = u.EditorLevelLibrary.spawn_actor_from_class(
            u.StaticMeshActor, u.Vector(x, y, z), u.Rotator())
        actor.set_actor_label('WPTest Cube %d' % index)
        actor.tags = [TAG]
        component = actor.static_mesh_component
        component.set_static_mesh(cube)
        component.set_collision_profile_name('BlockAll')
    u.log('WPTEST_ACTORS_SPAWNED=%d' % len(CELLS))

    log_desc_state(world, 'WPTEST_PRESAVE')
    assert u.Born2FlapMeshPartition.save_world_external_first(world, MAP_PATH), 'save failed'
    log_desc_state(world, 'WPTEST_POSTSAVE')
    u.log('WPTEST_GEN_READY map=%s' % MAP_PATH)
    if not os.environ.get('B2F_GENERATION_BATCH'):
        u.SystemLibrary.execute_console_command(None, 'quit')


def probe():
    world = u.EditorLevelLibrary.get_editor_world()
    assert world, 'no editor world'

    ours = [a for a in u.EditorLevelLibrary.get_all_level_actors()
            if TAG in (list(a.tags) if a.tags else [])]
    u.log('WPTEST_PROBE_OWN_ACTORS=%d' % len(ours))
    for actor in ours:
        u.log('WPTEST_PROBE_ACTOR %s @ %s' % (actor.get_name(), actor.get_actor_location()))

    log_desc_state(world, 'WPTEST_PROBE')
    ok = len(ours) == len(CELLS) and \
        u.Born2FlapMeshPartition.count_actor_descs(world) >= len(CELLS)
    u.log('WPTEST_VERDICT=%s own=%d descs=%d' % (
        'PASS' if ok else 'FAIL', len(ours),
        u.Born2FlapMeshPartition.count_actor_descs(world)))
    u.log('WPTEST_PROBE_READY')
    if not os.environ.get('B2F_GENERATION_BATCH'):
        u.SystemLibrary.execute_console_command(None, 'quit')


if PHASE == 'gen':
    gen()
else:
    probe()