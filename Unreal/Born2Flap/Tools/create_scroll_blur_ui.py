"""Progressive refractive scroll-edge dissolve (progressive blur → void).

A translucent MD_UI overlay that sits full-bleed over a ScrollBox. In the safe
zone it is transparent (content stays sharp); toward the top/bottom edge it
progressively froths — refracted like hammered glass / water — then dissolves
organically into the void. Drive it via the dynamic instance's scalar params:

    EdgeSize     0..0.5  normalized half-band (the "safe area")
    Amount       0..1    progressive-blur softness / falloff width
    Refract      0..1    refraction warp strength (hammered glass / water)
    RefractScale >0      organic noise frequency
    Void         0..1    how fully the edge dissolves into the void

Run (see .setup/apply-bird-materials.ps1):
  UnrealEditor-Cmd.exe Born2Flap.uproject -run=pythonscript \
      -script=Unreal/Born2Flap/Tools/create_scroll_blur_ui.py \
      -unattended -nosplash -nullrhi -nosound
"""
import unreal as u

path = '/Game/UI/M_ScrollProgressiveBlur'
m = u.load_asset(path) or u.AssetToolsHelpers.get_asset_tools().create_asset(
    'M_ScrollProgressiveBlur', '/Game/UI', u.Material, u.MaterialFactoryNew())
lib = u.MaterialEditingLibrary
lib.delete_all_material_expressions(m)
m.set_editor_property('material_domain', u.MaterialDomain.MD_UI)
m.set_editor_property('blend_mode', u.BlendMode.BLEND_TRANSLUCENT)


def scalar(name, default):
    p = lib.create_material_expression(m, u.MaterialExpressionScalarParameter)
    p.set_editor_property('parameter_name', name)
    p.set_editor_property('default_value', default)
    return p


uv = lib.create_material_expression(m, u.MaterialExpressionTextureCoordinate)
edge = scalar('EdgeSize', 0.14)
amount = scalar('Amount', 0.65)
refract = scalar('Refract', 0.35)
refract_scale = scalar('RefractScale', 6.0)
void = scalar('Void', 0.9)

n = lib.create_material_expression(m, u.MaterialExpressionCustom)
n.set_editor_property('code', '''
float2 p = UV;
float e = clamp(EdgeSize, 0.001, 0.5);
float s = max(RefractScale, 0.001);
// organic two-octave field — the "hammered glass / water" warp
float n1 = sin(p.y*s*3.7 + sin(p.x*s*2.3)) * cos(p.x*s*2.9 + p.y*s*1.7);
float n2 = sin(p.y*s*8.1 + p.x*s*5.3);
float noise = 0.5 + 0.5*(n1*0.6 + n2*0.4);
// refract the coordinate, so the band edge dissolves unevenly / organically
float warp = (noise - 0.5) * Refract * 0.10;
float y = clamp(p.y + warp, 0.0, 1.0);
// distance into the top/bottom safe band (1 at the outer edge, 0 in the safe zone)
float band = max(saturate(1.0 - y/e), saturate(1.0 - (1.0-y)/e));
// progressive soften: Amount widens the blur falloff
float blur = smoothstep(0.0, 1.0, saturate((band - (1.0-Amount)) / max(Amount, 0.001)));
float alpha = blur * (0.35 + 0.65*Void);
float3 frost = float3(0.40, 0.58, 0.65);
float3 dark  = float3(0.03, 0.04, 0.06);
float3 tint  = lerp(frost, dark, blur*Void) + noise*0.015;
return float4(tint, alpha);
''')
n.set_editor_property('output_type', u.CustomMaterialOutputType.CMOT_FLOAT4)
pins = []
for nm in ['UV', 'EdgeSize', 'Amount', 'Refract', 'RefractScale', 'Void']:
    cpin = u.CustomInput()
    cpin.set_editor_property('input_name', nm)
    pins.append(cpin)
n.set_editor_property('inputs', pins)

lib.connect_material_expressions(uv, '', n, 'UV')
lib.connect_material_expressions(edge, '', n, 'EdgeSize')
lib.connect_material_expressions(amount, '', n, 'Amount')
lib.connect_material_expressions(refract, '', n, 'Refract')
lib.connect_material_expressions(refract_scale, '', n, 'RefractScale')
lib.connect_material_expressions(void, '', n, 'Void')

rgb = lib.create_material_expression(m, u.MaterialExpressionComponentMask)
rgb.set_editor_property('r', True)
rgb.set_editor_property('g', True)
rgb.set_editor_property('b', True)
alpha = lib.create_material_expression(m, u.MaterialExpressionComponentMask)
alpha.set_editor_property('r', False)
alpha.set_editor_property('g', False)
alpha.set_editor_property('a', True)

assert lib.connect_material_expressions(n, '', rgb, '')
assert lib.connect_material_expressions(n, '', alpha, '')
lib.connect_material_property(rgb, '', u.MaterialProperty.MP_EMISSIVE_COLOR)
lib.connect_material_property(alpha, '', u.MaterialProperty.MP_OPACITY)
lib.recompile_material(m)
assert u.EditorAssetLibrary.save_loaded_asset(m)
u.log('SCROLL_BLUR_UI_READY')
