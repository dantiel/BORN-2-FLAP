"""TEST SWAP: rebuild M_WindRippleSand with a plain world-space sample of a
DIFFERENT texture (T_Sand_Diffuse) — no antitiling sampler, no explicit
ddx/ddy — plus a 4 km brightness checker to make texture presence obvious
from the air. Decisive test for the Yunsha ground-texture issue: if the
grain shows now, the old antitiling/derivative chain was the culprit; if it
still renders flat, the problem is in the mesh/Nanite path, not the material.
"""
import sys
from pathlib import Path

import unreal as u

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from project_guard import require_project

require_project()
E = u.EditorAssetLibrary
M = u.MaterialEditingLibrary
PATH = '/Game/Yunsha/Materials/M_WindRippleSand'

mat = u.load_asset(PATH)
assert mat, 'M_WindRippleSand not found'

M.delete_all_material_expressions(mat)
mat.set_editor_property('used_with_nanite', True)
mat.set_editor_property('used_with_instanced_static_meshes', True)


def expr(kind, **props):
    n = M.create_material_expression(mat, getattr(u, 'MaterialExpression' + kind))
    for k, v in props.items():
        n.set_editor_property(k, v)
    return n


def custom(code, inputs):
    n = expr('Custom', code=code, output_type=u.CustomMaterialOutputType.CMOT_FLOAT3)
    pins = []
    for name in inputs:
        p = u.CustomInput()
        p.set_editor_property('input_name', name)
        pins.append(p)
    n.set_editor_property('inputs', pins)
    for name, value in inputs.items():
        assert M.connect_material_expressions(value, '', n, name), 'link ' + name
    return n


pos = expr('WorldPosition')
tex = expr('TextureObject',
           texture=u.load_asset('/Game/Shiomori/Textures/T_Sand_Diffuse'))

# Plain sample — the same triplanar-free world-space pattern the working
# RedSandstone rock material uses. 4 km checker flips a warm bright tint so
# even at cruise altitude it is instantly obvious whether the texture shows.
base = custom('''float2 p = P.xy;
float3 tex = Texture2DSample(T, TSampler, p / 60.0).rgb;
float c = fmod(floor(p.x / 500.0) + floor(p.y / 500.0), 2.0);
return lerp(tex, float3(1.6, 0.25, 0.55) * (0.35 + tex.g), c);''',
              {'P': pos, 'T': tex})
assert M.connect_material_property(base, '', u.MaterialProperty.MP_BASE_COLOR)

normal = expr('VertexNormalWS')
assert M.connect_material_property(normal, '', u.MaterialProperty.MP_NORMAL)
mat.set_editor_property('tangent_space_normal', False)

M.recompile_material(mat)
assert E.save_loaded_asset(mat)
print('SWAP-TEST: M_WindRippleSand rebuilt with plain T_Sand_Diffuse sample + 4km checker')