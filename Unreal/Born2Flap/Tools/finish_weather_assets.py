"""Weather surface layers, breaker/splash shaders, original soundscape import.
Run build_soundscape_audio.py first, then this via Unreal Python commandlet.
Only materials/audio are saved; maps and source textures are left intact.
"""
from pathlib import Path
import unreal as u

lib=u.MaterialEditingLibrary
ela=u.EditorAssetLibrary
assets=u.AssetToolsHelpers.get_asset_tools()
ela.make_directory('/Game/Weather/Audio')

def node(m,kind,**kwargs):
    n=lib.create_material_expression(m,getattr(u,'MaterialExpression'+kind))
    for k,v in kwargs.items(): n.set_editor_property(k,v)
    return n
def connect(a,b,pin,out=''):
    assert lib.connect_material_expressions(a,out,b,pin)
def output(n,prop,out=''):
    assert lib.connect_material_property(n,out,getattr(u.MaterialProperty,'MP_'+prop))
def custom(m,code,inputs,size=1):
    n=node(m,'Custom',code=code,output_type=getattr(u.CustomMaterialOutputType,'CMOT_FLOAT'+(str(size) if size>1 else '1')))
    entries=[]
    for name in inputs:
        entry=u.CustomInput();entry.set_editor_property('input_name',name);entries.append(entry)
    n.set_editor_property('inputs',entries)
    for name,source in inputs.items(): connect(source,n,name)
    return n
def material(name):
    path='/Game/Weather/'+name
    m=u.load_asset(path) if ela.does_asset_exist(path) else assets.create_asset(name,'/Game/Weather',u.Material,u.MaterialFactoryNew())
    lib.delete_all_material_expressions(m)
    m.set_editor_property('two_sided',True)
    return m

mpath='/Game/Weather/MPC_Weather'
mpc=u.load_asset(mpath) if ela.does_asset_exist(mpath) else assets.create_asset('MPC_Weather','/Game/Weather',u.MaterialParameterCollection,u.MaterialParameterCollectionFactoryNew())
params=[]
existing={str(p.get_editor_property('parameter_name')):p for p in mpc.get_editor_property('scalar_parameters')}
for name in ['Wetness','SnowCoverage','RainAmount']:
    p=existing.get(name,u.CollectionScalarParameter());p.set_editor_property('parameter_name',name);p.set_editor_property('default_value',0);params.append(p)
mpc.set_editor_property('scalar_parameters',params);ela.save_loaded_asset(mpc)

# Wrap existing material outputs, preserving textures and normal detail.
# Description markers make re-running this additive migration idempotent.
allowed={'Meadow','Grass','DryGrass','Concrete','Plaster','Roof','Rust','Slate','Stone','Timber','Basalt','Earth','Road','Sand','WetSand','Wood','MossRock','ValleyGround','FirBark'}
count=0
for folder in ['/Game/Ravenstonefield/Materials','/Game/Shiomori/Materials','/Game/Nature','/Game/Training']:
    for path in ela.list_assets(folder,recursive=True,include_folder=False):
        stem=path.split('/')[-1].split('.')[0]
        if not stem.startswith('M_') or stem[2:] not in allowed: continue
        m=u.load_asset(path)
        if not isinstance(m,u.Material): continue
        if any(e.get_editor_property('desc')=='B2F Weather Surface v1' for e in lib.get_material_expressions(m)): continue
        pos=node(m,'WorldPosition');normal=node(m,'VertexNormalWS')
        wet=node(m,'CollectionParameter',collection=mpc,parameter_name='Wetness')
        snow=node(m,'CollectionParameter',collection=mpc,parameter_name='SnowCoverage')
        mask=custom(m,'float patch=.72+.16*sin(P.x*.007)*sin(P.y*.009)+.12*sin(P.x*.019+P.y*.013); return saturate(Snow*patch)*smoothstep(.35,.85,N.z);',{'P':pos,'N':normal,'Snow':snow})
        for prop in ['BASE_COLOR','ROUGHNESS']:
            enum=getattr(u.MaterialProperty,'MP_'+prop)
            original=lib.get_material_property_input_node(m,enum)
            out=lib.get_material_property_input_node_output_name(m,enum)
            if original is None: original=node(m,'Constant',r=.8);out=''
            code=('return lerp(Base*(1-.36*Wet),float3(.83,.89,.95),Cover);' if prop=='BASE_COLOR' else
                  'float puddle=smoothstep(.1,.8,sin(P.x*.003)*sin(P.y*.004)); return lerp(lerp(Base,.10+.15*(1-puddle),Wet*smoothstep(.2,.9,N.z)),.92,Cover);')
            # Connect manually so texture output pins (RGB/R) are preserved.
            layer=custom(m,code,{'Base':original,'Wet':wet,'Cover':mask,'P':pos,'N':normal},3 if prop=='BASE_COLOR' else 1)
            connect(original,layer,'Base',out)
            layer.set_editor_property('desc','B2F Weather Surface v1')
            output(layer,prop)
        lib.recompile_material(m);assert ela.save_loaded_asset(m);count+=1

m=material('M_BreakingSurf')
m.set_editor_property('disable_depth_test',False)
m.set_editor_property('translucency_pass',u.MaterialTranslucencyPass.MTP_BEFORE_DOF)
m.set_editor_property('blend_mode',u.BlendMode.BLEND_TRANSLUCENT)
m.set_editor_property('shading_model',u.MaterialShadingModel.MSM_DEFAULT_LIT)
m.set_editor_property('translucency_lighting_mode',u.TranslucencyLightingMode.TLM_SURFACE)
vc=node(m,'VertexColor');pos=node(m,'WorldPosition');time=node(m,'Time')
scaled=node(m,'Multiply',const_b=.025);connect(pos,scaled,'A')
noise=node(m,'Noise',levels=3,output_min=0.,output_max=1.);connect(scaled,noise,'')
foam=custom(m,'return saturate(C.r*(.20+1.35*N));',{'N':noise,'C':vc})
color=custom(m,'return lerp(float3(.025,.18,.20),float3(.88,.94,.94),F);',{'F':foam},3)
output(color,'BASE_COLOR');output(node(m,'Constant',r=.34),'ROUGHNESS')
eye=node(m,'EyeAdaptation')
glow=custom(m,'return float3(.14,.16,.17)*F/max(Eye,.000001);',{'F':foam,'Eye':eye},3)
output(glow,'EMISSIVE_COLOR')
opacity=custom(m,'return Alpha*lerp(.65,1.,F);',{'Alpha':vc,'F':foam})
connect(vc,opacity,'Alpha','A')
output(opacity,'OPACITY');lib.recompile_material(m);assert ela.save_loaded_asset(m)

m=material('M_RainSplash');m.set_editor_property('blend_mode',u.BlendMode.BLEND_TRANSLUCENT)
m.set_editor_property('shading_model',u.MaterialShadingModel.MSM_UNLIT)
m.set_editor_property('used_with_instanced_static_meshes',True)
uv=node(m,'TextureCoordinate');c=node(m,'Constant3Vector',constant=u.LinearColor(.3,.4,.44,1))
eye=node(m,'EyeAdaptation');light=node(m,'Divide');connect(c,light,'A');connect(eye,light,'B');output(light,'EMISSIVE_COLOR')
ring=custom(m,'float r=length(UV-.5)*2; return smoothstep(.52,.72,r)*(1-smoothstep(.8,1.,r))*.48;',{'UV':uv})
output(ring,'OPACITY');lib.recompile_material(m);assert ela.save_loaded_asset(m)

source=Path(__file__).resolve().parents[1]/'Saved/GeneratedSoundscape'
for wav in sorted(source.glob('*.wav')):
    task=u.AssetImportTask();task.set_editor_property('filename',str(wav));task.set_editor_property('destination_path','/Game/Weather/Audio')
    task.set_editor_property('automated',True);task.set_editor_property('replace_existing',True);task.set_editor_property('save',True)
    assets.import_asset_tasks([task])
    sound=u.load_asset('/Game/Weather/Audio/'+wav.stem);assert isinstance(sound,u.SoundWave)
    sound.set_editor_property('looping',wav.stem in ['CoastalWind','ForestWind','MeadowWind','Surf','Rain'])
    sound.set_editor_property('virtualization_mode',u.VirtualizationMode.PLAY_WHEN_SILENT)
    assert ela.save_loaded_asset(sound)
u.log('WEATHER_EXPERIENCE_ASSETS_READY surfaces='+str(count))
