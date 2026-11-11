"""Create M_ProbeMagenta: unlit, emissive magenta — a canary for the
terrain probe to prove whether the MP section SMC actually draws the
visible ground."""
import sys
from pathlib import Path

import unreal as u

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from project_guard import require_project

require_project()
E = u.EditorAssetLibrary
M = u.MaterialEditingLibrary
A = u.AssetToolsHelpers.get_asset_tools()

path = '/Game/Yunsha/Materials/M_ProbeMagenta'
mat = u.load_asset(path) if E.does_asset_exist(path) else \
    A.create_asset('M_ProbeMagenta', '/Game/Yunsha/Materials', u.Material, u.MaterialFactoryNew())
M.delete_all_material_expressions(mat)
mat.set_editor_property('used_with_instanced_static_meshes', True)
mat.set_editor_property('used_with_nanite', True)
mat.set_editor_property('shading_model', u.MaterialShadingModel.MSM_UNLIT)

col = M.create_material_expression(mat, u.MaterialExpressionConstant3Vector)
col.set_editor_property('constant', u.LinearColor(1.0, 0.05, 1.0, 1.0))
assert M.connect_material_property(col, '', u.MaterialProperty.MP_EMISSIVE_COLOR)
M.recompile_material(mat)
assert E.save_loaded_asset(mat)
print('PROBE-MAGENTA created')
