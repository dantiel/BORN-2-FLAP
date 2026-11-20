"""One-shot water visual repair (run via editor commandlet):
  (1) M_Water / M_Shallows re-bake — gentler opacity, softer fresnel (insurance
      fallback surface under the native ocean);
  (2) M_BreakingSurf -> unlit emissive foam (no lighting => no black bands);
  (3) MI_OceanNative — MIC of the engine Water_Material_Ocean with the ocean
      foam + waves static switches enabled and bright shallow-bay tuning.
"""
import ast
import sys
from pathlib import Path
import unreal as u

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

# --- (1) Shiomori water materials ------------------------------------------
source = HERE / 'create_shiomori_map.py'
tree = ast.parse(source.read_text(encoding='utf-8'))
defs = [n for n in tree.body if isinstance(n, (ast.Import, ast.ImportFrom, ast.FunctionDef))]
scope = dict(__file__=str(source), assets=u.AssetToolsHelpers.get_asset_tools(),
             lib=u.MaterialEditingLibrary, ela=u.EditorAssetLibrary)
exec(compile(ast.Module(body=defs, type_ignores=[]), str(source), 'exec'), scope)
scope['water_material']('Water', (0.08, 0.52, 0.66), rough=0.03, specular=1.0,
                        shore_color=(0.12, 0.54, 0.60))
scope['water_material']('Shallows', (.08, .50, .60), shore_color=(.12, .56, .58))
u.log('SHIOMORI_WATER_REPAIR_READY')

# --- (2) Unlit breaking-surf foam ------------------------------------------
source2 = HERE / 'finish_weather_assets.py'
tree2 = ast.parse(source2.read_text(encoding='utf-8'))
defs2 = [n for n in tree2.body if isinstance(n, (ast.Import, ast.ImportFrom, ast.FunctionDef))]
scope2 = dict(__file__=str(source2), assets=u.AssetToolsHelpers.get_asset_tools(),
              lib=u.MaterialEditingLibrary, ela=u.EditorAssetLibrary)
exec(compile(ast.Module(body=defs2, type_ignores=[]), str(source2), 'exec'), scope2)
scope2['repair_breaking_surf']()
u.log('BREAKING_SURF_UNLIT_READY')

# --- (3) MI_OceanNative -----------------------------------------------------
lib = u.MaterialEditingLibrary
ela = u.EditorAssetLibrary
assets = u.AssetToolsHelpers.get_asset_tools()
PARENT = '/Water/Materials/WaterSurface/Water_Material_Ocean'
PATH = '/Game/Shiomori/Materials/MI_OceanNative'
parent = u.load_asset(PARENT)
assert parent, 'Water_Material_Ocean missing'
mic = u.load_asset(PATH)
if not mic:
    mic = assets.create_asset('MI_OceanNative', '/Game/Shiomori/Materials',
                              u.MaterialInstanceConstant,
                              u.MaterialInstanceConstantFactoryNew())
lib.set_material_instance_parent(mic, parent)
for sw in ('Enable Ocean Foam', 'Enable Waves'):
    try:
        lib.set_material_instance_static_switch_parameter_value(mic, sw, True)
    except Exception as ex:
        u.log_warning('SWITCH SKIP %s %s' % (sw, ex))
for name, val in [('Foam Boost', 1.2), ('Water Roughness', 0.05),
                  ('Water Specular', 1.0), ('Refraction', 1.12)]:
    try:
        lib.set_material_instance_scalar_parameter_value(mic, name, val)
    except Exception as ex:
        u.log_warning('SCALAR SKIP %s %s' % (name, ex))
for name, val in [('Water Albedo', (0.03, 0.20, 0.26)),
                  ('Scattering', (0.05, 0.22, 0.28)),
                  ('Absorption', (0.0012, 0.0028, 0.0050))]:
    try:
        lib.set_material_instance_vector_parameter_value(
            mic, name, u.LinearColor(val[0], val[1], val[2], 1.0))
    except Exception as ex:
        u.log_warning('VECTOR SKIP %s %s' % (name, ex))
ela.save_asset(PATH)
u.log('MI_OCEAN_NATIVE_BAKED')

# --- (4) Splash particles -----------------------------------------------
# M_SurfDroplet gains the instanced-mesh usage flag so the pawn's billboard
# droplet pool (the water mirror of the sand-burst simulation's ISM path) can
# render it.
md = u.load_asset('/Game/Weather/M_SurfDroplet')
if md:
    md.set_editor_property('used_with_instanced_static_meshes', True)
    ela.save_asset('/Game/Weather/M_SurfDroplet')
    u.log('SURF_DROPLET_ISM_READY')
else:
    u.log_warning('M_SurfDroplet missing')