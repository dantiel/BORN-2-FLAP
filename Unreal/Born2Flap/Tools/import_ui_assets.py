"""Import the new UI artwork from ~/Downloads and build the painted-gate material.

Run once via the editor commandlet (same invocation style as
create_shiomori_map.py / import_splash.py). Idempotent: re-runs update settings
and re-save existing assets.

Imports:
  born2flap-splash-new.png            -> /Game/Splash/born2flap-splash-new   (startup splash)
  born2flap-background.png            -> /Game/Splash/born2flap-background   (full-page menu backdrop)
  born2flap-painted-circle-gate.png   -> /Game/UI/born2flap-painted-circle-gate (sky gate ring)

Also builds M_Gate (/Game/UI): an unlit translucent two-sided material that
recolours the painted ring through a single Tint vector parameter — the gate
starts amber, turns green once flown through.
"""
import unreal as u
from pathlib import Path

assets = u.AssetToolsHelpers.get_asset_tools()
lib = u.MaterialEditingLibrary
ela = u.EditorAssetLibrary

DOWNLOADS = Path.home() / 'Downloads'


def import_texture(src_name, dest_path, dest_name):
    src = DOWNLOADS / src_name
    full = dest_path + '/' + dest_name
    if not src.exists():
        u.log_warning('UI_IMPORT: source missing ' + str(src))
        raise SystemExit(1)
    ela.make_directory(dest_path)
    if not ela.does_asset_exist(full):
        task = u.AssetImportTask()
        task.filename = str(src)
        task.destination_path = dest_path
        task.destination_name = dest_name
        task.automated = True
        task.replace_existing = True
        task.save = True
        assets.import_asset_tasks([task])
    tex = u.load_asset(full)
    if not isinstance(tex, u.Texture2D):
        u.log_warning('UI_IMPORT: failed to load ' + full)
        raise SystemExit(1)
    # Authored as display images; always resident (no streaming pop on first frame).
    tex.set_editor_property('srgb', True)
    tex.set_editor_property('never_stream', True)
    assert ela.save_asset(full), 'UI_IMPORT: failed to save ' + full
    u.log('UI_IMPORTED ' + full)
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


def gate_material(tex):
    path = '/Game/UI/M_Gate'
    m = u.load_asset(path) if ela.does_asset_exist(path) else assets.create_asset(
        'M_Gate', '/Game/UI', u.Material, u.MaterialFactoryNew())
    lib.delete_all_material_expressions(m)
    m.set_editor_property('material_domain', u.MaterialDomain.MD_SURFACE)
    m.set_editor_property('blend_mode', u.BlendMode.BLEND_TRANSLUCENT)
    m.set_editor_property('shading_model', u.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property('two_sided', True)
    # usage flag: keep the same defensive flag as every programmatically-built
    # material in this project (avoid the grey DefaultMaterial fallback).
    for prop in ('used_with_nanite', 'b_used_with_nanite'):
        try:
            m.set_editor_property(prop, True)
            break
        except Exception:
            pass

    ts = node(m, 'TextureSample', texture=tex)
    tint = node(m, 'VectorParameter', parameter_name='Tint',
                default_value=u.LinearColor(1.0, 0.84, 0.55, 1.0))
    # Recolour via the paint's luminance so the gate changes hue cleanly while
    # keeping the brush-stroke shading: colour = Tint * luma(tex) * 2.
    h = node(m, 'Custom',
             code='return Tint * dot(Tex, float3(0.299, 0.587, 0.114)) * 2.0;',
             output_type=u.CustomMaterialOutputType.CMOT_FLOAT3)
    pins = []
    for nm in ('Tex', 'Tint'):
        pin = u.CustomInput()
        pin.set_editor_property('input_name', nm)
        pins.append(pin)
    h.set_editor_property('inputs', pins)
    wire(ts, h, 'Tex', 'RGB')
    wire(tint, h, 'Tint')
    output(h, 'EMISSIVE_COLOR')
    output(ts, 'OPACITY', 'A')
    lib.recompile_material(m)
    assert ela.save_asset(path), 'Failed to save ' + path
    u.log('UI_GATE_MATERIAL ' + path)
    return m


splash = import_texture('born2flap-splash-new.png', '/Game/Splash', 'born2flap-splash-new')
back = import_texture('born2flap-background.png', '/Game/Splash', 'born2flap-background')
gate_tex = import_texture('born2flap-painted-circle-gate.png', '/Game/UI', 'born2flap-painted-circle-gate')
gate_material(gate_tex)
u.log('UI_IMPORT_READY')
