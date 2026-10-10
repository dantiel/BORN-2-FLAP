"""Apply the current campus light settings without rebuilding map geometry."""
import unreal as u
world=u.EditorLoadingAndSavingUtils.load_map('/Game/Shiomori/Maps/SHIOMORI')
count=0
for a in u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors():
    if 'CampusLighting' not in [str(t) for t in a.tags]:continue
    c=a.point_light_component
    c.set_editor_property('intensity_units',u.LightUnits.LUMENS)
    c.set_intensity(24000)
    count+=1
assert count==4
assert u.EditorLoadingAndSavingUtils.save_map(world,'/Game/Shiomori/Maps/SHIOMORI')
u.log('CAMPUS_LIGHTING_SAVED lights=%d'%count)
u.SystemLibrary.execute_console_command(None,'quit')
