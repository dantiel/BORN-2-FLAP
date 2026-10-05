"""Import the painted gate artwork and rebuild M_Gate as a texture-parameter material.

Run once via the editor commandlet (same invocation style as import_ui_assets.py).
Idempotent: re-runs re-save existing assets.

Imports (from ~/Desktop/born2flap gates):
  born2flap-red-gate-1..4.png     -> /Game/UI/born2flap-red-gate-N    (active gate, random variants)
  born2flap-green-gate-1..3.png   -> /Game/UI/born2flap-green-gate-N  (passed gate, random variants)
  born2flap-quasi-gate-1..4.png   -> /Game/UI/born2flap-quasi-gate-N  (ghost/ahead gates, distance-indexed)

Rebuilds M_Gate (/Game/UI): a lit, translucent, two-sided material whose
TextureObjectParameter ("GateTex") is swapped per gate at runtime so the painted
artwork itself carries the state colour (no manual tinting). BaseColor carries the
art, EmissiveColor keeps the gate bright and readable against any sky.
"""
import unreal as u
from pathlib import Path

assets = u.AssetToolsHelpers.get_asset_tools()
lib = u.MaterialEditingLibrary
ela = u.EditorAssetLibrary

GATE_DIR = Path.home() / 'Desktop' / 'born2flap gates'
DEST = '/Game/UI'

FILES = [
    'born2flap-red-gate-1.png',
    'born2flap-red-gate-2.png',
    'born2flap-red-gate-3.png',
    'born2flap-red-gate-4.png',
    'born2flap-green-gate-1.png',
    'born2flap-green-gate-2.png',
    'born2flap-green-gate-3.png',
    'born2flap-quasi-gate-1.png',
    'born2flap-quasi-gate-2.png',
    'born2flap-quasi-gate-3.png',
    'born2flap-quasi-gate-4.png',
]


def import_texture(src_path, dest_path, dest_name):
    full = dest_path + '/' + dest_name
    if not src_path.exists():
        u.log_warning('GATE_IMPORT: source missing ' + str(src_path))
        raise SystemExit(1)
    ela.make_directory(dest_path)
    if not ela.does_asset_exist(full):
        task = u.AssetImportTask()
        task.filename = str(src_path)
        task.destination_path = dest_path
        task.destination_name = dest_name
        task.automated = True
        task.replace_existing = True
        task.save = True
        assets.import_asset_tasks([task])
    tex = u.load_asset(full)
    if not isinstance(tex, u.Texture2D):
        u.log_warning('GATE_IMPORT: failed to load ' + full)
        raise SystemExit(1)
    tex.set_editor_property('srgb', True)
    tex.set_editor_property('never_stream', True)
    assert ela.save_asset(full), 'GATE_IMPORT: failed to save ' + full
    u.log('GATE_IMPORTED ' + full)
    return tex


def node(m, kind, **props):
    n = lib.create_material_expression(m, getattr(u, 'MaterialExpression' + kind))
    for k, v in props.items():
        n.set_editor_property(k, v)
    return n


def wire(a, b, pin, output=''):
    assert lib.connect_material_expressions(a, output, b, pin), \
        'Invalid material connection: %s.%s -> %s.%s' % (type(a).__name__, output, type(b).__name__, pin)


def output(n, prop, pin=''):
    assert lib.connect_material_property(n, pin, getattr(u.MaterialProperty, 'MP_' + prop)), \
        'Invalid material output: ' + prop


def gate_material(default_tex):
    path = '/Game/UI/M_Gate'
    m = u.load_asset(path) if ela.does_asset_exist(path) else assets.create_asset(
        'M_Gate', '/Game/UI', u.Material, u.MaterialFactoryNew())
    lib.delete_all_material_expressions(m)
    m.set_editor_property('material_domain', u.MaterialDomain.MD_SURFACE)
    m.set_editor_property('blend_mode', u.BlendMode.BLEND_TRANSLUCENT)
    # Lit surface (matches the proven bird materials) with a full emissive glow.
    # BaseColor carries the painted artwork; EmissiveColor keeps the gate bright
    # and readable against any sky.
    m.set_editor_property('shading_model', u.MaterialShadingModel.MSM_DEFAULT_LIT)
    m.set_editor_property('two_sided', True)

    # TextureObjectParameter + TextureSample: the classic runtime-swappable pair.
    # TextureSampleParameter2D can leave its default texture unset through the
    # 5.8 Python API and read as black — the explicit pair is unambiguous.
    tex_param = node(m, 'TextureObjectParameter', parameter_name='GateTex', texture=default_tex)
    ts = node(m, 'TextureSample')
    wire(tex_param, ts, 'Tex')
    output(ts, 'BASE_COLOR', 'RGB')
    output(ts, 'EMISSIVE_COLOR', 'RGB')
    output(ts, 'OPACITY', 'A')
    lib.recompile_material(m)
    assert ela.save_asset(path), 'Failed to save ' + path
    u.log('GATE_MATERIAL ' + path)
    return m


first = None
for name in FILES:
    tex = import_texture(GATE_DIR / name, DEST, name[:-4])  # strip .png
    if first is None:
        first = tex
gate_material(first)
u.log('GATE_IMPORT_READY')
