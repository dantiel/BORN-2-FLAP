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
    # UE 5.8 DeleteAllMaterialExpressions iterates a live TConstArrayView while
    # RemoveExpression mutates that same array (iterator invalidation) — it only
    # deletes ~half the expressions per call, silently accumulating stale nodes
    # across re-runs. Loop until the expression list is actually empty.
    for _ in range(64):
        if not lib.get_material_expressions(m):
            break
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

# Breaking-surf foam. Mirrors the WORKING M_Foam recipe (create_shiomori_map.py):
# BLEND_TRANSLUCENT + MSM_DEFAULT_LIT (default TLM, no TLM_SURFACE) + white in
# BASE_COLOR + ROUGHNESS. Both TLM_SURFACE (surface forward shading does not
# light here) and MSM_UNLIT+EMISSIVE (translucent unlit renders black) are the
# black-foam regressions — neither is used.
m=material('M_BreakingSurf')
m.set_editor_property('disable_depth_test',False)
m.set_editor_property('translucency_pass',u.MaterialTranslucencyPass.MTP_BEFORE_DOF)
m.set_editor_property('blend_mode',u.BlendMode.BLEND_TRANSLUCENT)
m.set_editor_property('shading_model',u.MaterialShadingModel.MSM_DEFAULT_LIT)
vc=node(m,'VertexColor');pos=node(m,'WorldPosition')
scaled=node(m,'Multiply',const_b=.025);connect(pos,scaled,'A')
noise=node(m,'Noise',levels=3,output_min=0.,output_max=1.);connect(scaled,noise,'')
# Each Custom node takes ONLY built-in inputs (Noise / VertexColor). Chaining a
# Custom output into another Custom input (foam -> color.F) via
# connect_material_expressions silently drops the FExpressionInput in UE 5.8 —
# the SM6 shader then fails ("missing input 1 (F)") and the mesh falls back to
# the grey Default Material checkerboard. Inline the foam mask instead.
color=custom(m,'return lerp(float3(.05,.42,.46),float3(.92,.95,.95),saturate(C.r*(.20+1.35*N)));',{'N':noise,'C':vc},3)
output(color,'BASE_COLOR')
output(node(m,'Constant',r=.35),'ROUGHNESS')
# The VertexColor node's output pin is MCT_Float3 (alpha masked out) — passing
# it into a Custom input and swizzling .a fails ("vector swizzle 'a' is out of
# bounds"). The underlying interpolant is full float4: read the alpha straight
# from Parameters.VertexColor. The vc node stays connected to BASE_COLOR so the
# engine still flags bUsesVertexColor and interpolates the full RGBA.
opacity=custom(m,'return Parameters.VertexColor.a*lerp(.5,1.,saturate(Parameters.VertexColor.r*(.20+1.35*N)));',{'N':noise})
output(opacity,'OPACITY');lib.recompile_material(m);assert ela.save_loaded_asset(m)

m=material('M_RainSplash');m.set_editor_property('blend_mode',u.BlendMode.BLEND_TRANSLUCENT)
m.set_editor_property('shading_model',u.MaterialShadingModel.MSM_UNLIT)
m.set_editor_property('used_with_instanced_static_meshes',True)
uv=node(m,'TextureCoordinate');c=node(m,'Constant3Vector',constant=u.LinearColor(.3,.4,.44,1))
eye=node(m,'EyeAdaptation');light=node(m,'Divide');connect(c,light,'A');connect(eye,light,'B');output(light,'EMISSIVE_COLOR')
ring=custom(m,'float r=length(UV-.5)*2; return smoothstep(.52,.72,r)*(1-smoothstep(.8,1.,r))*.48;',{'UV':uv})
output(ring,'OPACITY');lib.recompile_material(m);assert ela.save_loaded_asset(m)

# Surf droplet: a small, bright, fast-falloff water droplet sprite for the Niagara
# breaking-surf spray (NS_SurfSpray). The engine DefaultSpriteMaterial is a soft,
# wide radial gradient — at Niagara's 50 cm default sprite size it reads as a soft
# blob / heat shimmer, not a projectile. This is unlit translucent with a tight
# pow(1-r) core so it renders as a hard, weighty droplet. bUsedWithNiagaraSprites
# is required or the sprite renderer ignores the material.
m=material('M_SurfDroplet');m.set_editor_property('blend_mode',u.BlendMode.BLEND_TRANSLUCENT)
m.set_editor_property('shading_model',u.MaterialShadingModel.MSM_UNLIT)
m.set_editor_property('used_with_niagara_sprites',True)
m.set_editor_property('used_with_particle_sprites',True)
m.set_editor_property('two_sided',False)
uv=node(m,'TextureCoordinate')
col=custom(m,'float2 c=UV-.5; float r=length(c)*2.; float d=pow(saturate(1.-r),2.5); return float3(.95,.99,1.05)*d;',{'UV':uv},3)
output(col,'EMISSIVE_COLOR')
op=custom(m,'float2 c=UV-.5; float r=length(c)*2.; return pow(saturate(1.-r),2.5);',{'UV':uv})
output(op,'OPACITY');lib.recompile_material(m);assert ela.save_loaded_asset(m)

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