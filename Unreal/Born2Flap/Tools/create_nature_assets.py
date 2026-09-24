"""Import the CC0 valley assets and build terrain/water materials.
Run after fetch_nature_assets.py with UnrealEditor-Cmd's Python commandlet.
"""
from pathlib import Path
import unreal as u

ROOT = Path(__file__).resolve().parents[1] / 'Saved/NatureSource'
TOOLS = u.AssetToolsHelpers.get_asset_tools()
EDIT = u.MaterialEditingLibrary
LIB = u.EditorAssetLibrary
MESH = u.get_editor_subsystem(u.StaticMeshEditorSubsystem) or u.new_object(u.StaticMeshEditorSubsystem)


def import_file(source, folder, name=''):
    task = u.AssetImportTask()
    task.filename = str(source)
    task.destination_path = folder
    if name:
        task.destination_name = name
    task.automated = True
    task.replace_existing = True
    task.save = True
    TOOLS.import_asset_tasks([task])
    return [u.load_asset(p) for p in task.imported_object_paths]


def node(material, kind, **props):
    result = EDIT.create_material_expression(material, getattr(u, 'MaterialExpression' + kind))
    for key, value in props.items():
        result.set_editor_property(key, value)
    return result


def wire(a, b, pin, output=''):
    EDIT.connect_material_expressions(a, output, b, pin)


def output(n, prop, pin=''):
    EDIT.connect_material_property(n, pin, getattr(u.MaterialProperty, 'MP_' + prop))


def newmat(name):
    path = '/Game/Nature/' + name
    material = u.load_asset(path) if LIB.does_asset_exist(path) else TOOLS.create_asset(name, '/Game/Nature', u.Material, u.MaterialFactoryNew())
    EDIT.delete_all_material_expressions(material)
    return material


def constant(m, value):
    return node(m, 'Constant', r=value)


def texture(m, path, uv=None, normal=False):
    tex = u.load_asset(path)
    if not tex:
        raise RuntimeError('Missing texture ' + path)
    sample = node(m, 'TextureSample', texture=tex)
    if normal:
        sample.set_editor_property('sampler_type', u.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    if uv:
        wire(uv, sample, 'Coordinates')
    return sample


def save(material):
    if material.get_name() not in ('M_ValleyGround', 'M_River'):
        material.set_editor_property('used_with_instanced_static_meshes', True)
    EDIT.recompile_material(material)
    if not LIB.save_loaded_asset(material):
        raise RuntimeError('Could not save ' + material.get_path_name() + '; close the game before importing.')


def main():
    mesh_sets = {}
    for source in ['fir_sapling_medium', 'grass_medium_01', 'rock_moss_set_01']:
        folder = '/Game/Nature/' + source
        imported = [u.load_asset(p) for p in LIB.list_assets(folder)] if LIB.does_directory_exist(folder) else []
        if not any(isinstance(a, u.StaticMesh) for a in imported):
            imported = import_file(ROOT / source / (source + '.gltf'), folder)
        meshes = sorted([a for a in imported if isinstance(a, u.StaticMesh)], key=lambda a: a.get_name())
        if not meshes:
            meshes = sorted([u.load_asset(p) for p in LIB.list_assets(folder) if isinstance(u.load_asset(p), u.StaticMesh)], key=lambda a: a.get_name())
        if not meshes:
            raise RuntimeError('No imported meshes for ' + source)
        mesh_sets[source] = meshes
        u.log('B2F_IMPORTED ' + source + ' ' + ', '.join(m.get_path_name() for m in meshes))
        for mesh in meshes:
            opts = u.StaticMeshReductionOptions()
            opts.auto_compute_lod_screen_size = False
            levels = [(1., 1.), (.65, .2), (.25, .075), (.06, .018)] if source == 'fir_sapling_medium' else [(1., 1.), (.35, .3), (.12, .12), (.035, .045)]
            opts.reduction_settings = [u.StaticMeshReductionSettings(percent_triangles=p, screen_size=s) for p, s in levels]
            if MESH.get_lod_count(mesh) < 4 or (source == 'fir_sapling_medium' and LIB.get_metadata_tag(mesh, 'B2F_LODVersion') != '2'):
                MESH.set_lods(mesh, opts)
                LIB.set_metadata_tag(mesh, 'B2F_LODVersion', '2')
                LIB.save_loaded_asset(mesh)
    # Stable names consumed by the runtime level; keep imported source names too.
    selections = [('Fir', mesh_sets['fir_sapling_medium'], 3),
                  ('Rock', mesh_sets['rock_moss_set_01'], 4),
                  ('Grass', [m for m in mesh_sets['grass_medium_01'] if 'small' in m.get_name().lower() or 'tall' in m.get_name().lower()], 3)]
    for prefix, meshes, count in selections:
        for i, mesh in enumerate(meshes[:count]):
            if not LIB.does_asset_exist('/Game/Nature/SM_' + prefix + str(i)):
                LIB.duplicate_asset(mesh.get_path_name(), '/Game/Nature/SM_' + prefix + str(i))
            elif prefix == 'Fir':
                alias = u.load_asset('/Game/Nature/SM_' + prefix + str(i))
                options = u.StaticMeshReductionOptions(auto_compute_lod_screen_size=False,
                    reduction_settings=[u.StaticMeshReductionSettings(percent_triangles=p, screen_size=s)
                                        for p, s in [(1., 1.), (.65, .2), (.25, .075), (.06, .018)]])
                if LIB.get_metadata_tag(alias, 'B2F_LODVersion') != '2':
                    MESH.set_lods(alias, options)
                    LIB.set_metadata_tag(alias, 'B2F_LODVersion', '2')
                    LIB.save_loaded_asset(alias)

    # Repair cutout foliage: the glTF's BLEND material is unsuitable for dense foliage.
    for source, alpha_name in [('fir_sapling_medium', 'twigs_alpha.png'), ('grass_medium_01', 'Alpha.png')]:
        alpha = import_file(ROOT / source / alpha_name, '/Game/Nature', 'T_' + source + '_Alpha')[0]
        alpha.set_editor_property('srgb', False)
        alpha.set_editor_property('do_scale_mips_for_alpha_coverage', True)
        alpha.set_editor_property('alpha_coverage_thresholds', u.Vector4(.25, 0, 0, 0))
        LIB.save_loaded_asset(alpha)
        for path in LIB.list_assets('/Game/Nature/' + source):
            material = u.load_asset(path)
            if isinstance(material, u.Texture2D) and 'diff' in material.get_name().lower():
                if source == 'grass_medium_01' or 'twigs' in material.get_name().lower():
                    diffuse = material
        foliage = newmat('M_' + source + '_Foliage')
        # The full-detail fir has explicitly modelled needles. The extra alpha
        # atlas belongs to card-based source variants and clips those needles.
        foliage.set_editor_property('blend_mode', u.BlendMode.BLEND_OPAQUE if source == 'fir_sapling_medium' else u.BlendMode.BLEND_MASKED)
        foliage.set_editor_property('two_sided', True)
        foliage.set_editor_property('opacity_mask_clip_value', .25)
        foliage.set_editor_property('shading_model', u.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE)
        sample = node(foliage, 'TextureSample', texture=alpha, sampler_type=u.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
        if source != 'fir_sapling_medium':
            output(sample, 'OPACITY_MASK', 'R')
        output(node(foliage, 'TextureSample', texture=diffuse), 'BASE_COLOR', 'RGB')
        output(node(foliage, 'Constant3Vector', constant=u.LinearColor(.12, .19, .055)), 'SUBSURFACE_COLOR')
        output(constant(foliage, .8), 'ROUGHNESS')
        save(foliage)
        # Stable aliases and originals both receive the cutout material.
        for path in LIB.list_assets('/Game/Nature'):
            mesh = u.load_asset(path)
            if not isinstance(mesh, u.StaticMesh):
                continue
            for i, slot in enumerate(mesh.static_materials):
                old = slot.material_interface
                if old and source in old.get_path_name() and (source == 'grass_medium_01' or 'twigs' in old.get_name().lower()):
                    mesh.set_material(i, foliage)
            LIB.save_loaded_asset(mesh)

    for source in ['aerial_grass_rock', 'forest_ground_04', 'rock_04']:
        for kind in ['Diffuse', 'nor_dx', 'Rough']:
            tex = import_file(ROOT / source / (kind + '.jpg'), '/Game/Nature/Textures', 'T_' + source + '_' + kind)[0]
            if kind != 'Diffuse':
                tex.set_editor_property('srgb', False)
            if kind == 'nor_dx':
                tex.set_editor_property('compression_settings', u.TextureCompressionSettings.TC_NORMALMAP)
            LIB.save_loaded_asset(tex)

    terrain = newmat('M_ValleyGround')
    vc = node(terrain, 'VertexColor')
    ground, forest, rock = [texture(terrain, '/Game/Nature/Textures/T_' + n + '_Diffuse') for n in ['aerial_grass_rock', 'forest_ground_04', 'rock_04']]
    blend = node(terrain, 'LinearInterpolate')
    wire(ground, blend, 'A', 'RGB'); wire(forest, blend, 'B', 'RGB'); wire(vc, blend, 'Alpha', 'R')
    slope = node(terrain, 'LinearInterpolate')
    wire(blend, slope, 'A'); wire(rock, slope, 'B', 'RGB'); wire(vc, slope, 'Alpha', 'G')
    macro = node(terrain, 'Multiply'); wire(slope, macro, 'A'); wire(vc, macro, 'B', 'B')
    tint = node(terrain, 'Multiply')
    wire(macro, tint, 'A')
    wire(node(terrain, 'Constant3Vector', constant=u.LinearColor(.63, .88, .62)), tint, 'B')
    output(tint, 'BASE_COLOR')
    output(constant(terrain, .9), 'ROUGHNESS')
    output(texture(terrain, '/Game/Nature/Textures/T_aerial_grass_rock_nor_dx', normal=True), 'NORMAL', 'RGB')
    save(terrain)

    water = newmat('M_River')
    colour = node(water, 'Constant3Vector', constant=u.LinearColor(.026, .105, .115))
    output(colour, 'BASE_COLOR'); output(constant(water, .15), 'ROUGHNESS'); output(constant(water, .8), 'SPECULAR')
    panner = node(water, 'Panner', speed_x=.012, speed_y=.02)
    ripples = texture(water, '/Game/Nature/Textures/T_rock_04_nor_dx', panner, True)
    flat = node(water, 'Constant3Vector', constant=u.LinearColor(0, 0, 1))
    normal = node(water, 'LinearInterpolate', const_alpha=.12)
    wire(flat, normal, 'A'); wire(ripples, normal, 'B', 'RGB'); output(normal, 'NORMAL')
    save(water)
    # Standalone game materials avoid dependencies on editor glTF templates.
    for source, material_name, token in [('fir_sapling_medium', 'M_FirBark', 'branches_diff'),
                                         ('rock_moss_set_01', 'M_MossRock', 'diff')]:
        candidates = [p for p in LIB.list_assets('/Game/Nature/' + source)
                      if token in p and '/Textures/' in p]
        material = newmat(material_name)
        output(texture(material, sorted(candidates)[0]), 'BASE_COLOR', 'RGB')
        output(constant(material, .88), 'ROUGHNESS')
        save(material)
        for path in LIB.list_assets('/Game/Nature'):
            mesh = u.load_asset(path)
            if not isinstance(mesh, u.StaticMesh):
                continue
            for i, slot in enumerate(mesh.static_materials):
                old = slot.material_interface
                if old and source in old.get_path_name() and 'Foliage' not in old.get_name():
                    mesh.set_material(i, material)
            LIB.save_loaded_asset(mesh)
    LIB.save_directory('/Game/Nature', only_if_is_dirty=True, recursive=True)
    u.log('B2F_NATURE_ASSETS_READY')


main()
