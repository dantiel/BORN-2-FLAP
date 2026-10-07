"""Fix an existing nested HLSL function in the world shelter glass material."""
import unreal as u
lib=u.MaterialEditingLibrary
mat=u.load_asset('/Game/Shiomori/Materials/M_Glass');assert mat
for expression in lib.get_material_expressions(mat):
    if isinstance(expression,u.MaterialExpressionCustom):
        code=expression.get_editor_property('code')
        if 'float vnoise(float2 p){' in code and 'struct GlassNoise' not in code:
            code=code.replace('float vnoise(float2 p){','struct GlassNoise { float vnoise(float2 p){')
            code=code.replace('float n = vnoise','}; GlassNoise noise;\nfloat n = noise.vnoise')
            code=code.replace('+ vnoise','+ noise.vnoise')
            expression.set_editor_property('code',code)
lib.recompile_material(mat)
assert u.EditorAssetLibrary.save_loaded_asset(mat)
u.log('SHIOMORI_GLASS_COMPILE_READY')
