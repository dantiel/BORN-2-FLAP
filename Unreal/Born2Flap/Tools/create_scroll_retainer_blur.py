"""Progressive content blur for URetainerBox-wrapped ScrollBoxes.

A MD_UserInterface effect material applied to a URetainerBox. The retainer
binds its render target to the material's "Texture" sampler parameter every
frame, so this material post-processes the *actual* scroll content: sharp in
the safe zone, progressively blurred (a real 3x3 gaussian) toward the top/
bottom edge, refracted like hammered glass / water, then dissolved into the
void. Output is pre-multiplied alpha for the retainer's AlphaComposite blend.

Scalar params (drive the dynamic instance):

    EdgeSize     0..0.5  normalized half-band (the "safe area")
    Amount       0..1    progressive-blur falloff width
    BlurRadius   >0      max blur radius in UV units
    Refract      0..1    refraction warp strength (hammered glass / water)
    RefractScale >0      organic noise frequency
    Void         0..1    how fully the edge dissolves into the void

Run:
  UnrealEditor-Cmd.exe Born2Flap.uproject -run=pythonscript \
      -script=Unreal/Born2Flap/Tools/create_scroll_retainer_blur.py \
      -unattended -nosplash -nullrhi -nosound
"""
import unreal as u

path = '/Game/UI/M_ScrollRetainerBlur'
m = u.load_asset(path) or u.AssetToolsHelpers.get_asset_tools().create_asset(
    'M_ScrollRetainerBlur', '/Game/UI', u.Material, u.MaterialFactoryNew())
lib = u.MaterialEditingLibrary
lib.delete_all_material_expressions(m)
m.set_editor_property('material_domain', u.MaterialDomain.MD_UI)
# AlphaComposite + pre-multiplied output (the retainer draws PreMultipliedAlpha).
m.set_editor_property('blend_mode', u.BlendMode.BLEND_ALPHA_COMPOSITE)


def scalar(name, default):
    p = lib.create_material_expression(m, u.MaterialExpressionScalarParameter)
    p.set_editor_property('parameter_name', name)
    p.set_editor_property('default_value', default)
    return p


def expr(kind, **props):
    n = lib.create_material_expression(m, getattr(u, 'MaterialExpression' + kind))
    for k, v in props.items():
        n.set_editor_property(k, v)
    return n


def conn(a, b, pin, output=''):
    assert lib.connect_material_expressions(a, output, b, pin), \
        'Invalid material connection: %s -> %s.%s' % (type(a).__name__, type(b).__name__, pin)


uv = expr('TextureCoordinate')

edge = scalar('EdgeSize', 0.14)
amount = scalar('Amount', 0.65)
blur_radius = scalar('BlurRadius', 0.02)
refract = scalar('Refract', 0.35)
refract_scale = scalar('RefractScale', 6.0)
void = scalar('Void', 0.9)

# Scalar-only math: organic edge band + refractive warp + void dissolve.
# Output float4 = (blur, alpha, warp, unused).
math_n = expr('Custom', code='''
float2 p = UV;
float e = clamp(EdgeSize, 0.001, 0.5);
float s = max(RefractScale, 0.001);
float n1 = sin(p.y*s*3.7 + sin(p.x*s*2.3)) * cos(p.x*s*2.9 + p.y*s*1.7);
float n2 = sin(p.y*s*8.1 + p.x*s*5.3);
float noise = 0.5 + 0.5*(n1*0.6 + n2*0.4);
float warp = (noise - 0.5) * Refract * 0.08;
float band = max(saturate(1.0 - p.y/e), saturate(1.0 - (1.0-p.y)/e));
float blur = smoothstep(0.0, 1.0, saturate((band - (1.0-Amount)) / max(Amount, 0.001)));
float alpha = 1.0 - blur * Void;
return float4(blur, alpha, warp, 0.0);
''', output_type=u.CustomMaterialOutputType.CMOT_FLOAT4)
pins = []
for nm in ['UV', 'EdgeSize', 'Amount', 'Refract', 'RefractScale', 'Void']:
    cpin = u.CustomInput()
    cpin.set_editor_property('input_name', nm)
    pins.append(cpin)
math_n.set_editor_property('inputs', pins)
conn(uv, math_n, 'UV')
conn(edge, math_n, 'EdgeSize')
conn(amount, math_n, 'Amount')
conn(refract, math_n, 'Refract')
conn(refract_scale, math_n, 'RefractScale')
conn(void, math_n, 'Void')

# Split the custom output: blur=R, alpha=G, warp=B.
blur = expr('ComponentMask', r=True, g=False, b=False, a=False)
alpha = expr('ComponentMask', r=False, g=True, b=False, a=False)
warp = expr('ComponentMask', r=False, g=False, b=True, a=False)
conn(math_n, blur, '')
conn(math_n, alpha, '')
conn(math_n, warp, '')

# base UV = UV + float2(warp, warp) — the organic refractive offset.
warp2 = expr('AppendVector')
conn(warp, warp2, 'A')
conn(warp, warp2, 'B')
base_uv = expr('Add')
conn(uv, base_uv, 'A')
conn(warp2, base_uv, 'B')

# Tap radius = blur * BlurRadius (0 in the safe zone, full at the edge).
off_mag = expr('Multiply')
conn(blur, off_mag, 'A')
conn(blur_radius, off_mag, 'B')

# 3x3 gaussian kernel: center weight 4, axis 2, corners 1 (total 16).
KERNEL = [(-1, -1, 1), (0, -1, 2), (1, -1, 1),
          (-1, 0, 2), (0, 0, 4), (1, 0, 2),
          (-1, 1, 1), (0, 1, 2), (1, 1, 1)]
TOTAL = 16

samples = []
for dx, dy, w in KERNEL:
    dirv = expr('Constant2Vector', r=float(dx), g=float(dy))
    off = expr('Multiply')
    conn(off_mag, off, 'A')
    conn(dirv, off, 'B')
    tap_uv = expr('Add')
    conn(base_uv, tap_uv, 'A')
    conn(off, tap_uv, 'B')
    s = expr('TextureSampleParameter2D', parameter_name='Texture')
    conn(tap_uv, s, 'UVs')
    if w != 1:
        sw = expr('Multiply')
        sw.set_editor_property('const_b', float(w))
        conn(s, sw, 'A')
        s = sw
    samples.append(s)

# Sum the weighted taps and normalize (blur=0 collapses every tap to the same
# pixel, so the safe zone reproduces the sharp content exactly).
acc = samples[0]
for s in samples[1:]:
    a = expr('Add')
    conn(acc, a, 'A')
    conn(s, a, 'B')
    acc = a
inv = expr('Constant', r=1.0 / TOTAL)
blurred = expr('Multiply')
conn(acc, blurred, 'A')
conn(inv, blurred, 'B')

# Pre-multiply the color by the dissolve alpha for AlphaComposite.
premul = expr('Multiply')
conn(blurred, premul, 'A')
conn(alpha, premul, 'B')

assert lib.connect_material_property(premul, '', u.MaterialProperty.MP_EMISSIVE_COLOR)
assert lib.connect_material_property(alpha, '', u.MaterialProperty.MP_OPACITY)
lib.recompile_material(m)
assert u.EditorAssetLibrary.save_loaded_asset(m)
u.log('SCROLL_RETAINER_BLUR_READY')
