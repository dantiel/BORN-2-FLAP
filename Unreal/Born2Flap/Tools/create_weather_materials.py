"""Create lightweight rain/snow surfaces and refresh the parameterized sun halo.

Run through Unreal's Python commandlet. Does not regenerate any level.
"""
from pathlib import Path
import runpy
import unreal as u

lib=u.MaterialEditingLibrary
assets=u.AssetToolsHelpers.get_asset_tools()
ela=u.EditorAssetLibrary
ela.make_directory('/Game/Weather')
for name, color, opacity in [('M_Rain',(.55,.66,.75),.35),('M_Snow',(.90,.94,1.0),1.0)]:
    path='/Game/Weather/'+name
    m=u.load_asset(path) if ela.does_asset_exist(path) else assets.create_asset(name,'/Game/Weather',u.Material,u.MaterialFactoryNew())
    lib.delete_all_material_expressions(m)
    m.set_editor_property('shading_model',u.MaterialShadingModel.MSM_DEFAULT_LIT)
    m.set_editor_property('blend_mode',u.BlendMode.BLEND_TRANSLUCENT if opacity<1 else u.BlendMode.BLEND_OPAQUE)
    m.set_editor_property('two_sided',True)
    m.set_editor_property('used_with_instanced_static_meshes',True)
    c=lib.create_material_expression(m,u.MaterialExpressionConstant3Vector)
    c.set_editor_property('constant',u.LinearColor(*color,1))
    assert lib.connect_material_property(c,'',u.MaterialProperty.MP_BASE_COLOR)
    r=lib.create_material_expression(m,u.MaterialExpressionConstant)
    r.set_editor_property('r',.8)
    assert lib.connect_material_property(r,'',u.MaterialProperty.MP_ROUGHNESS)
    # Keep small precipitation readable across the daylight/night exposure range.
    m.set_editor_property('shading_model',u.MaterialShadingModel.MSM_UNLIT)
    exposure=lib.create_material_expression(m,u.MaterialExpressionEyeAdaptation)
    light=lib.create_material_expression(m,u.MaterialExpressionDivide)
    assert lib.connect_material_expressions(c,'',light,'A')
    assert lib.connect_material_expressions(exposure,'',light,'B')
    assert lib.connect_material_property(light,'',u.MaterialProperty.MP_EMISSIVE_COLOR)
    if opacity<1:
        a=lib.create_material_expression(m,u.MaterialExpressionConstant)
        a.set_editor_property('r',opacity)
        assert lib.connect_material_property(a,'',u.MaterialProperty.MP_OPACITY)
    lib.recompile_material(m)
    assert ela.save_asset(path)
runpy.run_path(str(Path(__file__).with_name('repair_shiomori_sky.py')),init_globals={'SKY_ONLY':True})
u.log('WEATHER_MATERIALS_READY')
