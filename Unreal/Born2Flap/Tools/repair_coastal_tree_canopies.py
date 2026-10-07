"""Preserve modeled foliage in Nanite and supply runtime-safe bark materials."""
import ast
from pathlib import Path
import unreal as u
source=Path(__file__).with_name('create_shiomori_map.py')
scope=dict(u=u,assets=u.AssetToolsHelpers.get_asset_tools(),lib=u.MaterialEditingLibrary,ela=u.EditorAssetLibrary)
defs=[n for n in ast.parse(source.read_text(encoding='utf8')).body if isinstance(n,ast.FunctionDef)]
exec(compile(ast.Module(body=defs,type_ignores=[]),str(source),'exec'),scope)
edit=u.get_editor_subsystem(u.StaticMeshEditorSubsystem);assert edit
ela=u.EditorAssetLibrary
bark=scope['pbr_material']('CoastalBark',color=(.13,.105,.075),rough=.92)
bark.set_editor_property('used_with_instanced_static_meshes',True)
ela.save_loaded_asset(bark)
for name in ['Fir0','Fir1','Fir2','Tree0','Tree1']:
    mesh=u.load_asset('/Game/Nature/SM_'+name);assert mesh
    settings=edit.get_nanite_settings(mesh)
    u.log('CANOPY_BEFORE '+name+' '+str(settings))
    settings.set_editor_property('enabled',True)
    settings.set_editor_property('shape_preservation',u.NaniteShapePreservation.PRESERVE_AREA)
    settings.set_editor_property('keep_percent_triangles',1.)
    settings.set_editor_property('trim_relative_error',0.)
    settings.set_editor_property('fallback_relative_error',.05)
    edit.set_nanite_settings(mesh,settings,True)
    for index,slot in enumerate(mesh.static_materials):
        material=slot.material_interface
        if material and 'Foliage' in material.get_name():
            material.set_editor_property('used_with_instanced_static_meshes',True)
            scope['material_usage_flags'](material)
            ela.save_loaded_asset(material)
        else:mesh.set_material(index,bark)
    assert ela.save_loaded_asset(mesh)
u.log('COASTAL_CANOPIES_READY')
u.SystemLibrary.execute_console_command(None,'quit')
