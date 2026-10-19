"""Small reusable raven/scarecrow material set; geometry is instanced at runtime."""
from pathlib import Path
import sys
import unreal as u
sys.path.insert(0,str(Path(__file__).resolve().parent))
from project_guard import require_project
require_project()
lib=u.MaterialEditingLibrary
for name,colour,rough in [('Raven',(.018,.024,.033),.56),('Feather',(.025,.032,.045),.65),
                           ('Beak',(.028,.027,.025),.7),('Wood',(.19,.115,.055),.93),
                           ('Cloth',(.16,.23,.25),.96),('Straw',(.49,.34,.13),.9)]:
    path='/Game/Shiomori/HarvestWatch/M_'+name
    m=u.load_asset(path) if u.EditorAssetLibrary.does_asset_exist(path) else u.AssetToolsHelpers.get_asset_tools().create_asset('M_'+name,'/Game/Shiomori/HarvestWatch',u.Material,u.MaterialFactoryNew())
    lib.delete_all_material_expressions(m)
    c=lib.create_material_expression(m,u.MaterialExpressionConstant3Vector)
    c.set_editor_property('constant',u.LinearColor(*colour))
    lib.connect_material_property(c,'',u.MaterialProperty.MP_BASE_COLOR)
    r=lib.create_material_expression(m,u.MaterialExpressionConstant);r.set_editor_property('r',rough)
    lib.connect_material_property(r,'',u.MaterialProperty.MP_ROUGHNESS)
    lib.set_material_usage(m,u.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES)
    lib.recompile_material(m)
    assert u.EditorAssetLibrary.save_loaded_asset(m)
u.log('HARVEST_WATCH_ASSETS_READY')
