"""Author the playable map after compiling Born2FlapEditor and importing assets.

The generated actor components are saved so the editor shows the actual world.
Use the actor's Build World button to regenerate it after changing its generator.
"""
import unreal as u

MAP = '/Game/Ravenstonefield/Maps/RAVENSTONEFIELD'
world = u.EditorLoadingAndSavingUtils.new_blank_map(False)
world.get_world_settings().set_editor_property('force_no_precomputed_lighting', True)
actor = u.EditorLevelLibrary.spawn_actor_from_class(u.Born2FlapValley, u.Vector(0, 0, 0))
if not actor:
    raise RuntimeError('Cannot create the Ravenstonefield world actor')
actor.set_actor_label('RAVENSTONEFIELD / autumn river basin')
actor.build_world()
u.EditorLevelLibrary.set_level_viewport_camera_info(u.Vector(-8500, -17000, 5900), u.Rotator(-4, 29, 0))
if not u.EditorLoadingAndSavingUtils.save_map(world, MAP):
    raise RuntimeError('Cannot save ' + MAP)
u.log('RAVENSTONEFIELD_MAP_READY ' + MAP)
