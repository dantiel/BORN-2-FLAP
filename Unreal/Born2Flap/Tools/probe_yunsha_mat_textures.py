"""Probe Yunsha ground materials for broken texture bindings (headless).

For each material: dump TextureObject/TextureSample expressions and their
texture references, the BASE_COLOR wiring, and Custom-expression texture
inputs. Exit non-zero is not used; parse the log instead.
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

MATS = [
    'M_WindRippleSand',
    'M_BoneArchiveStrata',
    'M_RedSandstone',
]


def tex_name(tex):
    if tex is None:
        return 'NULL'
    try:
        return tex.get_path_name()
    except Exception:
        return '?'


def walk_pin(mat, prop, name):
    try:
        expr = mat.get_editor_property(prop)
    except Exception as err:
        u.log_warning('MATPROBE %s %s lookup failed: %s' % (name, prop, err))
        return
    if not expr:
        u.log('MATPROBE %s %s <- NONE' % (name, prop))
        return
    u.log('MATPROBE %s %s <- %s (%s)' % (name, prop, expr.get_name(), expr.get_class().get_name()))
    if expr.get_class().get_name() == 'MaterialExpressionCustom':
        for inp in expr.get_editor_property('inputs'):
            src = inp.input
            extra = ''
            if src and src.get_class().get_name() == 'MaterialExpressionTextureObject':
                extra = ' texture=' + tex_name(src.get_editor_property('texture'))
            elif src and src.get_class().get_name() in ('MaterialExpressionTextureSample',
                                                        'MaterialExpressionTextureSampleParameter2D'):
                extra = ' texture=' + tex_name(src.get_editor_property('texture'))
            u.log('MATPROBE %s custom=%s input=%s <- %s (%s)%s'
                  % (name, expr.get_name(), inp.input_name,
                     src.get_name() if src else 'NONE',
                     src.get_class().get_name() if src else '-', extra))


def dump(name):
    path = '/Game/Yunsha/Materials/' + name
    mat = u.load_asset(path)
    if not mat:
        u.log_error('MATPROBE_MISSING ' + path)
        return
    walk_pin(mat, 'base_color', name)
    walk_pin(mat, 'normal', name)
    walk_pin(mat, 'roughness', name)
    # Base color wiring: which expression feeds the final output?
    for prop, mp in (('base_color', 'MP_BASE_COLOR'),
                     ('normal', 'MP_NORMAL'),
                     ('roughness', 'MP_ROUGHNESS')):
        try:
            expr = mat.get_editor_property(prop)
            u.log('MATPROBE %s %s <- %s (%s)' % (name, prop,
                  expr.get_name() if expr else 'NONE',
                  expr.get_class().get_name() if expr else '-'))
        except Exception as err:
            u.log_warning('MATPROBE %s %s lookup failed: %s' % (name, prop, err))
    ok = M.recompile_material(mat)
    u.log('MATPROBE %s recompile=%s' % (name, ok))


for name in MATS:
    dump(name)

u.log('MATPROBE_DONE')
u.SystemLibrary.execute_console_command(None, 'quit')