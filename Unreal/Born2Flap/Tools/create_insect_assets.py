"""Generate the tiny procedural insect meshes + tintable materials for the swarm.

Poly Haven (the sole CC0 source for foliage) carries no insect models, so these
are authored here as minimal flat OBJ geometry and two unlit materials, then
imported like the nature assets. Everything is tintable at runtime through a
single VectorParameter ("Color") so the C++ swarm needs no per-type material.

Run after (or without) fetch_nature_assets.py, with UnrealEditor-Cmd:
  -run=pythonscript -script=.../create_insect_assets.py -unattended -nullrhi
"""
from pathlib import Path
import unreal as u

ROOT = Path(__file__).resolve().parents[1] / 'Saved/InsectSource'
TOOLS = u.AssetToolsHelpers.get_asset_tools()
EDIT = u.MaterialEditingLibrary
LIB = u.EditorAssetLibrary
DEST = '/Game/Nature/Insects'


def write_obj(name, verts, faces):
    lines = ['# B2F procedural insect part', 'o ' + name]
    for x, y, z in verts:
        lines.append('v %.4f %.4f %.4f' % (x, y, z))
    # One UV per vertex (unlit materials ignore it, but the OBJ translator
    # asserts UVs exist for every face corner — supply them to stay clean).
    for x, y, _ in verts:
        lines.append('vt %.4f %.4f' % (x, y))
    for f in faces:
        lines.append('f ' + ' '.join('%d/%d' % (i, i) for i in f))
    path = ROOT / (name + '.obj')
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text('\n'.join(lines) + '\n', encoding='utf-8')
    return path


def quad(a, b, c, d):
    return [(a, b, c), (a, c, d)]  # two triangles (1-based indices)


def import_mesh(source, name):
    folder = DEST
    if not LIB.does_directory_exist(folder):
        LIB.make_directory(folder)
    if LIB.does_asset_exist(folder + '/' + name):
        mesh = u.load_asset(folder + '/' + name)
        if isinstance(mesh, u.StaticMesh):
            return mesh
    task = u.AssetImportTask()
    task.filename = str(source)
    task.destination_path = folder
    task.destination_name = name
    task.automated = True
    task.replace_existing = True
    task.save = True
    TOOLS.import_asset_tasks([task])
    for p in task.imported_object_paths:
        asset = u.load_asset(p)
        if isinstance(asset, u.StaticMesh):
            return asset
    raise RuntimeError('No StaticMesh imported for ' + name)


def node(material, kind, **props):
    result = EDIT.create_material_expression(material, getattr(u, 'MaterialExpression' + kind))
    for key, value in props.items():
        result.set_editor_property(key, value)
    return result


def output(n, prop, pin=''):
    EDIT.connect_material_property(n, pin, getattr(u.MaterialProperty, 'MP_' + prop))


def newmat(name):
    path = DEST + '/' + name
    if LIB.does_asset_exist(path):
        material = u.load_asset(path)
    else:
        material = TOOLS.create_asset(name, DEST, u.Material, u.MaterialFactoryNew())
    EDIT.delete_all_material_expressions(material)
    return material


def save(material):
    material.set_editor_property('used_with_instanced_static_meshes', True)
    EDIT.recompile_material(material)
    if not LIB.save_loaded_asset(material):
        raise RuntimeError('Could not save ' + material.get_path_name() + '; close the game before importing.')


def build_meshes():
    # SM_Insect_Wing — a single flat wing. Inboard edge at x=0 (flap pivot), tip
    # at x=+5, chord along Y (±1). One-sided quad, rendered two-sided.
    write_obj('SM_Insect_Wing',
              [(0, -1, 0), (5, -1, 0), (5, 1, 0), (0, 1, 0)],
              [(1, 2, 3), (1, 3, 4)])
    # SM_Insect_Body — elongated lozenge along +X (forward), 4 cm long.
    write_obj('SM_Insect_Body',
              [(-2, -0.6, -0.6), (2, -0.6, -0.6), (2, 0.6, -0.6), (-2, 0.6, -0.6),
               (-2, -0.6, 0.6), (2, -0.6, 0.6), (2, 0.6, 0.6), (-2, 0.6, 0.6)],
              [(1, 3, 2), (1, 4, 3), (5, 6, 7), (5, 7, 8),   # front/back
               (1, 2, 6), (1, 6, 5), (2, 3, 7), (2, 7, 6),   # top/bottom
               (3, 4, 8), (3, 8, 7), (4, 1, 5), (4, 5, 8)])  # left/right
    # SM_Insect_Speck — 4-winged cross silhouette (dragonfly/butterfly star) for
    # the instanced swarm: four thin wings radiating from a tiny body bar.
    write_obj('SM_Insect_Speck',
              # body bar along X
              [(-1.5, -0.2, 0), (1.5, -0.2, 0), (1.5, 0.2, 0), (-1.5, 0.2, 0),
               # east wing
               (1.0, -0.7, 0), (5.5, -0.7, 0), (5.5, 0.7, 0), (1.0, 0.7, 0),
               # west wing
               (-1.0, -0.7, 0), (-5.5, -0.7, 0), (-5.5, 0.7, 0), (-1.0, 0.7, 0),
               # north wing
               (-0.7, 1.0, 0), (-0.7, 5.0, 0), (0.7, 5.0, 0), (0.7, 1.0, 0),
               # south wing
               (-0.7, -1.0, 0), (-0.7, -5.0, 0), (0.7, -5.0, 0), (0.7, -1.0, 0)],
              [(1, 2, 3), (1, 3, 4),                      # body
               (5, 6, 7), (5, 7, 8),                      # east
               (9, 10, 11), (9, 11, 12),                  # west
               (13, 14, 15), (13, 15, 16),                # north
               (17, 18, 19), (17, 19, 20)])               # south
    for name in ('SM_Insect_Wing', 'SM_Insect_Body', 'SM_Insect_Speck'):
        mesh = import_mesh(ROOT / (name + '.obj'), name)
        u.log('B2F_INSECT_MESH ' + mesh.get_path_name())


def build_materials():
    wing = newmat('M_InsectWing')
    wing.set_editor_property('blend_mode', u.BlendMode.BLEND_TRANSLUCENT)
    wing.set_editor_property('two_sided', True)
    wing.set_editor_property('shading_model', u.MaterialShadingModel.MSM_UNLIT)
    color = node(wing, 'VectorParameter', parameter_name='Color',
                 default_value=u.LinearColor(1.0, 1.0, 1.0, 1.0))
    opacity = node(wing, 'ScalarParameter', parameter_name='Opacity', default_value=0.55)
    output(color, 'EMISSIVE_COLOR')
    output(opacity, 'OPACITY')
    save(wing)

    body = newmat('M_InsectBody')
    body.set_editor_property('blend_mode', u.BlendMode.BLEND_OPAQUE)
    body.set_editor_property('two_sided', False)
    body.set_editor_property('shading_model', u.MaterialShadingModel.MSM_UNLIT)
    bcolor = node(body, 'VectorParameter', parameter_name='Color',
                  default_value=u.LinearColor(1.0, 1.0, 1.0, 1.0))
    output(bcolor, 'EMISSIVE_COLOR')
    save(body)
    u.log('B2F_INSECT_MATERIALS M_InsectWing M_InsectBody')


def main():
    build_meshes()
    build_materials()
    print('B2F_INSECT_ASSETS_READY', flush=True)


if __name__ == '__main__':
    main()