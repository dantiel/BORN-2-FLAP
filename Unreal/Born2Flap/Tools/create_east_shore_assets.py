"""Import CC0 eastern coast scans and build locally blended shore shading."""
import sys,ast
from pathlib import Path
import unreal as u
here=Path(__file__).resolve().parent;sys.path.insert(0,str(here))
from shore_antitiling import HLSL
from project_guard import require_project
require_project()
from create_nature_assets import import_file
root=here.parent/'Saved/EastShoreSource';lib=u.MaterialEditingLibrary;ela=u.EditorAssetLibrary
source=here/'create_shiomori_map.py';tree=ast.parse(source.read_text(encoding='utf-8-sig'))
scope=dict(__file__=str(source),assets=u.AssetToolsHelpers.get_asset_tools(),lib=lib,ela=ela,count=0)
exec(compile(ast.Module(body=[n for n in tree.body if isinstance(n,(ast.Import,ast.ImportFrom,ast.FunctionDef))],type_ignores=[]),str(source),'exec'),scope)
node,custom,output=[scope[k] for k in ('node','custom','output')]
for name in ('coast_sand_rocks_02','dry_river_pebbles','gravelly_sand'):
    for channel in ('Diffuse','nor_dx','Rough'):
        label='T_East_'+name+'_'+channel;path='/Game/Shiomori/EastShore/Textures/'+label
        t=u.load_asset(path) if ela.does_asset_exist(path) else import_file(root/name/(channel+'.jpg'),'/Game/Shiomori/EastShore/Textures',label)[0]
        t.set_editor_property('srgb',channel=='Diffuse')
        if channel=='nor_dx':t.set_editor_property('compression_settings',u.TextureCompressionSettings.TC_NORMALMAP)
        ela.save_loaded_asset(t)
mesh_editor=u.get_editor_subsystem(u.StaticMeshEditorSubsystem)
for name,alias in [('coast_rocks_02','SM_CoastRockA'),('coast_land_rocks_02','SM_CoastRockB'),('shrub_02','SM_CoastalShrub')]:
    folder='/Game/Shiomori/EastShore/Imported/'+name
    if not ela.does_directory_exist(folder):import_file(root/name/(name+'.gltf'),folder)
    objects=[u.load_asset(p) for p in ela.list_assets(folder)]
    meshes=[o for o in objects if isinstance(o,u.StaticMesh)];assert meshes,name
    textures=[o for o in objects if isinstance(o,u.Texture2D)]
    diffuse=next(t for t in textures if 'diff' in t.get_name().lower())
    normal=next(t for t in textures if 'nor' in t.get_name().lower())
    normal.set_editor_property('compression_settings',u.TextureCompressionSettings.TC_NORMALMAP)
    normal.set_editor_property('srgb',False);normal.set_editor_property('flip_green_channel',True);ela.save_loaded_asset(normal)
    mat=scope['pbr_material']('East_'+name,diffuse=diffuse,normal=normal,rough=.84)
    if name=='shrub_02':
        alpha_path='/Game/Shiomori/EastShore/T_ShrubAlpha'
        alpha=u.load_asset(alpha_path) if ela.does_asset_exist(alpha_path) else import_file(next((root/name).glob('Alpha.*')),'/Game/Shiomori/EastShore','T_ShrubAlpha')[0]
        alpha.set_editor_property('srgb',False);ela.save_loaded_asset(alpha)
        mat.set_editor_property('blend_mode',u.BlendMode.BLEND_MASKED);mat.set_editor_property('two_sided',True)
        mat.set_editor_property('shading_model',u.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE)
        output(node(mat,'TextureSample',texture=alpha,sampler_type=u.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR),'OPACITY_MASK','R')
        output(node(mat,'Constant3Vector',constant=u.LinearColor(.14,.19,.07)),'SUBSURFACE_COLOR')
    lib.recompile_material(mat);ela.save_loaded_asset(mat)
    path='/Game/Shiomori/EastShore/'+alias
    mesh=u.load_asset(path) if ela.does_asset_exist(path) else ela.duplicate_asset(meshes[0].get_path_name(),path)
    for i in range(len(mesh.static_materials)):mesh.set_material(i,mat)
    mesh.set_editor_property('allow_cpu_access',True)
    if name!='shrub_02':mesh.get_editor_property('body_setup').set_editor_property('collision_trace_flag',u.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
    if mesh_editor.get_lod_count(mesh)<4:
        mesh_editor.set_lods(mesh,u.StaticMeshReductionOptions(auto_compute_lod_screen_size=False,reduction_settings=[u.StaticMeshReductionSettings(percent_triangles=p,screen_size=s) for p,s in [(1.,1.),(.4,.3),(.12,.12),(.035,.035)]]))
    ela.save_loaded_asset(mesh)

path='/Game/Shiomori/Materials/M_EastShore'
if ela.does_asset_exist(path):assert ela.delete_asset(path)
m=ela.duplicate_asset('/Game/Shiomori/Materials/M_Headland',path);assert m
baseline={key:lib.get_material_property_input_node(m,getattr(u.MaterialProperty,'MP_'+key)) for key in ('BASE_COLOR','NORMAL','ROUGHNESS')}
p=node(m,'WorldPosition');n=node(m,'VertexNormalWS')
def tex(path):
    t=u.load_asset(path);assert t,path
    return node(m,'TextureObject',texture=t)
def surface(name,channel='Diffuse'):return tex('/Game/Shiomori/EastShore/Textures/T_East_'+name+'_'+channel)
args={'P':p,'N':n,'Pebble':surface('dry_river_pebbles'),'Rock':surface('coast_sand_rocks_02'),'Sand':surface('gravelly_sand'),
      'Grass':tex('/Game/Shiomori/Cliffs/Textures/T_Cliff_leafy_grass_Diffuse'),
      'Forest':tex('/Game/Shiomori/Cliffs/Textures/T_Cliff_forrest_ground_03_Diffuse')}
weights=custom(m,'''float edge=51500+1700*sin(P.x*.00022)+650*sin(P.x*.00069+1.8);
float d=P.y-edge;
float patch=.5+.22*sin(P.x*.0017+sin(P.y*.0011)*2)+.17*sin(P.y*.004-P.x*.003);
float inland=(1-smoothstep(-9300,-2700,d))*smoothstep(70,260,P.z);
float bedrock=saturate((1-N.z)*2.6+smoothstep(.54,.82,patch)*.65);
return float3(inland,bedrock,patch);''',{'P':p,'N':n},3)
args['W']=weights
bc=custom(m,HLSL+'''float3 w=pow(abs(normalize(N)),4);w/=max(.0001,w.x+w.y+w.z);
float3 pebbles=shore.sampleSurface(Pebble,PebbleSampler,P.xy/190,false);
float3 sand=shore.sampleSurface(Sand,SandSampler,P.xy/310,false);
float3 stone=shore.sampleSurface(Rock,RockSampler,P.yz/440,false)*w.x+shore.sampleSurface(Rock,RockSampler,P.xz/440,false)*w.y+shore.sampleSurface(Rock,RockSampler,P.xy/440,false)*w.z;
float3 grass=shore.sampleSurface(Grass,GrassSampler,P.xy/290,false)*float3(.53,.69,.44);
float3 forest=shore.sampleSurface(Forest,ForestSampler,P.xy/340,false)*.8;
float3 soil=lerp(grass,forest,(1-smoothstep(18000,32000,P.y))*.65);
soil=lerp(soil,sand*.69,smoothstep(.59,.81,W.z)*.3);
float3 shoreColour=lerp(lerp(pebbles*.86,sand*.83,W.z*.45),stone*.87,W.y);
return lerp(shoreColour,soil,W.x)*(1-(1-smoothstep(-45,75,P.z))*.36);''',args,3)
detail=custom(m,HLSL+'''float3 peb=shore.sampleSurface(T,TSampler,P.xy/190,true);
float3 grass=shore.sampleSurface(G,GSampler,P.xy/290,true);
float3 t=lerp(peb,grass,W.x);
return normalize(N+float3(t.xy,0)*.6);''',{'P':p,'N':n,'W':weights,'T':surface('dry_river_pebbles','nor_dx'),'G':tex('/Game/Shiomori/Cliffs/Textures/T_Cliff_leafy_grass_nor_dx')},3)
rough=custom(m,HLSL+'''float r=shore.sampleSurface(T,TSampler,P.xy/190,false).r;
return lerp(clamp(r,.55,.93),.26,1-smoothstep(-45,70,P.z));''',{'P':p,'T':surface('dry_river_pebbles','Rough')},1)
mask=custom(m,'''return smoothstep(48000,59000,P.x)*(1-smoothstep(94000,108000,P.x))
*smoothstep(9000,24000,P.y)*(1-smoothstep(62000,71000,P.y));''',{'P':p},1)
for key,value,size in [('BASE_COLOR',bc,3),('NORMAL',detail,3),('ROUGHNESS',rough,1)]:
    code='return normalize(lerp(A,B,W));' if key=='NORMAL' else 'return lerp(A,B,W);'
    output(custom(m,code,{'A':baseline[key],'B':value,'W':mask},size),key)
lib.recompile_material(m);assert ela.save_loaded_asset(m)
u.log('EAST_SHORE_ASSETS_READY');u.SystemLibrary.execute_console_command(None,'quit')
