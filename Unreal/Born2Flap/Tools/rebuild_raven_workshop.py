"""Refresh the saved valley actor, preserving all independently placed map actors."""
import unreal as u

MAP = '/Game/Ravenstonefield/Maps/RAVENSTONEFIELD'
lib = u.MaterialEditingLibrary
def surface_material(name, rgb, roughness=.9, foliage=False):
    asset_path='/Game/Ravenstonefield/Materials/M_'+name
    mat=u.load_asset(asset_path)
    if mat: lib.delete_all_material_expressions(mat)
    else: mat=u.AssetToolsHelpers.get_asset_tools().create_asset('M_'+name, '/Game/Ravenstonefield/Materials', u.Material, u.MaterialFactoryNew())
    mat.set_editor_property('two_sided', foliage)
    mat.set_editor_property('used_with_instanced_static_meshes', True)
    mat.set_editor_property('used_with_nanite', True)
    color=lib.create_material_expression(mat,u.MaterialExpressionCustom)
    expression='float n=.93+.045*sin(P.x*.037+sin(P.y*.029))+.025*sin(P.y*.16); return float3(%s)*n;'%','.join(str(x) for x in rgb)
    if name=='FieldTurf':
        expression='float n=.84+.10*sin(P.x*.0017+sin(P.y*.0021)); float cut=step(-1500,P.x)*step(P.x,14000)*step(abs(P.y),550); return G*lerp(float3(.43,.58,.22),float3(.58,.67,.30),cut)*n;'
    if name=='Footpath': expression='return G*float3(.62,.53,.39);'
    color.set_editor_property('code',expression)
    inp=u.CustomInput();inp.set_editor_property('input_name','P');color.set_editor_property('inputs',[inp])
    if name in ['FieldTurf','Footpath']:
        grain=u.CustomInput();grain.set_editor_property('input_name','G');color.set_editor_property('inputs',[inp,grain])
        tex=lib.create_material_expression(mat,u.MaterialExpressionTextureSample)
        tex.set_editor_property('texture',u.load_asset('/Game/Nature/Textures/'+('T_aerial_grass_rock_Diffuse' if name=='FieldTurf' else 'T_forest_ground_04_Diffuse')))
        lib.connect_material_expressions(tex,'RGB',color,'G')
    color.set_editor_property('output_type',u.CustomMaterialOutputType.CMOT_FLOAT3)
    pos=lib.create_material_expression(mat,u.MaterialExpressionWorldPosition)
    lib.connect_material_expressions(pos,'',color,'P');lib.connect_material_property(color,'',u.MaterialProperty.MP_BASE_COLOR)
    if name=='FieldTurf':
        mat.set_editor_property('blend_mode',u.BlendMode.BLEND_MASKED)
        mat.set_editor_property('opacity_mask_clip_value',0.0)
        edge=lib.create_material_expression(mat,u.MaterialExpressionCustom)
        edge.set_editor_property('output_type',u.CustomMaterialOutputType.CMOT_FLOAT1)
        edge.set_editor_property('inputs',[inp])
        edge.set_editor_property('code','float e=min(min(P.x+18000,36500-P.x),P.y+12000); e=min(e,abs(P.x-18500)-2800); e=min(e,length(P.xy-float2(-14300,-14400))-4300); float r=5500+6000*sin(P.x*.000025)+1500*sin(P.x*.00006); e=min(e,r-P.y-1300); float a=saturate(e/650); return a-frac(sin(dot(floor(P.xy/4),float2(12.9898,78.233)))*43758.5453);')
        lib.connect_material_expressions(pos,'',edge,'P');lib.connect_material_property(edge,'',u.MaterialProperty.MP_OPACITY_MASK)
    rough=lib.create_material_expression(mat,u.MaterialExpressionConstant);rough.set_editor_property('r',roughness)
    lib.connect_material_property(rough,'',u.MaterialProperty.MP_ROUGHNESS)
    lib.recompile_material(mat);u.EditorAssetLibrary.save_loaded_asset(mat)
    return mat
for name,color in [('CivilConcrete',(.36,.37,.38)),('RoadConcrete',(.27,.28,.29)),('Footpath',(.22,.16,.085)),('FieldTurf',(.12,.22,.035)),('FlowerWhite',(.84,.81,.68)),('FlowerGold',(.72,.47,.035)),('FlowerPurple',(.34,.12,.46)),('FlowerStem',(.075,.13,.025))]:
    surface_material(name,color,foliage=name.startswith('Flower'))
path = '/Game/Ravenstonefield/Materials/M_ManagedGrass'
material = u.load_asset(path)
if not material:
    material = u.AssetToolsHelpers.get_asset_tools().create_asset('M_ManagedGrass', '/Game/Ravenstonefield/Materials', u.Material, u.MaterialFactoryNew())
    material.set_editor_property('two_sided', True)
    material.set_editor_property('blend_mode', u.BlendMode.BLEND_MASKED)
    material.set_editor_property('shading_model', u.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE)
    alpha = lib.create_material_expression(material, u.MaterialExpressionTextureSample)
    alpha.set_editor_property('texture', u.load_asset('/Game/Nature/T_grass_medium_01_Alpha'))
    lib.connect_material_property(alpha, 'R', u.MaterialProperty.MP_OPACITY_MASK)
    color = lib.create_material_expression(material, u.MaterialExpressionCustom)
    color.set_editor_property('code', 'return lerp(float3(.075,.14,.025),float3(.22,.27,.065),R);')
    inp = u.CustomInput(); inp.set_editor_property('input_name', 'R')
    color.set_editor_property('inputs', [inp])
    color.set_editor_property('output_type', u.CustomMaterialOutputType.CMOT_FLOAT3)
    random = lib.create_material_expression(material, u.MaterialExpressionPerInstanceRandom)
    lib.connect_material_expressions(random, '', color, 'R')
    lib.connect_material_property(color, '', u.MaterialProperty.MP_BASE_COLOR)
    lib.connect_material_property(color, '', u.MaterialProperty.MP_SUBSURFACE_COLOR)
    rough = lib.create_material_expression(material, u.MaterialExpressionConstant)
    rough.set_editor_property('r', .9)
    lib.connect_material_property(rough, '', u.MaterialProperty.MP_ROUGHNESS)
    lib.recompile_material(material)
    u.EditorAssetLibrary.save_loaded_asset(material)
material.set_editor_property('used_with_instanced_static_meshes', True)
material.set_editor_property('used_with_nanite', True)
lib.recompile_material(material)
u.EditorAssetLibrary.save_loaded_asset(material)
# These existing foliage materials also fell back to grey in the rendered audit.
for name in ['M_DryGrass', 'M_tree_small_02_branch_diff', 'M_AutumnLeaves', 'M_tree_small_02_diff', 'M_dead_tree_trunk_diff']:
    foliage = u.load_asset('/Game/Ravenstonefield/Materials/' + name)
    if foliage:
        foliage.set_editor_property('used_with_nanite', True)
        foliage.set_editor_property('used_with_instanced_static_meshes', True)
        lib.recompile_material(foliage)
        u.EditorAssetLibrary.save_loaded_asset(foliage)
world = u.EditorLoadingAndSavingUtils.load_map(MAP)
actors = u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors()
valleys = [a for a in actors if isinstance(a, u.Born2FlapValley)]
assert len(valleys) == 1, 'Expected exactly one existing valley generator'
valleys[0].build_world()
assert u.EditorLoadingAndSavingUtils.save_map(world, MAP), 'Map save failed'
u.log('RAVEN_WORKSHOP_MAP_SAVED')
u.SystemLibrary.quit_editor()
