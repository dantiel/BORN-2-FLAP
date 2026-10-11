"""Import photographed ornamentals with correct cutouts; reuse existing PBR scenery."""
import unreal as u
from create_nature_assets import ROOT, import_file, newmat, node, output, constant, save, LIB, MESH

def prepare():
    folder='/Game/Nature/periwinkle_plant'
    paths=LIB.list_assets(folder) if LIB.does_directory_exist(folder) else []
    if not any(isinstance(u.load_asset(p),u.StaticMesh) for p in paths):
        import_file(ROOT/'periwinkle_plant/periwinkle_plant.gltf',folder)
        paths=LIB.list_assets(folder)
    meshes=[u.load_asset(p) for p in paths if isinstance(u.load_asset(p),u.StaticMesh)]
    assert meshes,'Periwinkle meshes were not imported'
    textures=[u.load_asset(p) for p in paths if isinstance(u.load_asset(p),u.Texture2D)]
    diffuse=next(t for t in textures if 'diff' in t.get_name().lower())
    alpha=import_file(next((ROOT/'periwinkle_plant').glob('opacity.*')),'/Game/Nature','T_CampusPeriwinkleAlpha')[0]
    alpha.set_editor_property('srgb',False);LIB.save_loaded_asset(alpha)
    mat=newmat('M_CampusPeriwinkle')
    mat.set_editor_property('two_sided',True);mat.set_editor_property('blend_mode',u.BlendMode.BLEND_MASKED)
    mat.set_editor_property('shading_model',u.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE)
    mat.set_editor_property('opacity_mask_clip_value',.3)
    output(node(mat,'TextureSample',texture=diffuse),'BASE_COLOR','RGB')
    output(node(mat,'TextureSample',texture=alpha,sampler_type=u.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR),'OPACITY_MASK','R')
    normal=next((t for t in textures if 'nor' in t.get_name().lower()),None)
    if normal:
        normal.set_editor_property('compression_settings',u.TextureCompressionSettings.TC_NORMALMAP)
        normal.set_editor_property('srgb',False);normal.set_editor_property('flip_green_channel',True);LIB.save_loaded_asset(normal)
        output(node(mat,'TextureSample',texture=normal,sampler_type=u.MaterialSamplerType.SAMPLERTYPE_NORMAL),'NORMAL','RGB')
    output(node(mat,'Constant3Vector',constant=u.LinearColor(.12,.17,.07)),'SUBSURFACE_COLOR')
    output(constant(mat,.85),'ROUGHNESS');save(mat)
    for mesh in meshes:
        for i in range(len(mesh.static_materials)):mesh.set_material(i,mat)
        opts=u.StaticMeshReductionOptions(auto_compute_lod_screen_size=False,reduction_settings=[u.StaticMeshReductionSettings(percent_triangles=p,screen_size=s) for p,s in [(1.,1.),(.5,.3),(.18,.12),(.06,.04)]])
        MESH.set_lods(mesh,opts);LIB.save_loaded_asset(mesh)
    return meshes
