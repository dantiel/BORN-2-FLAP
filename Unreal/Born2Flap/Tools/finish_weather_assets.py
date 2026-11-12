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
for name in ['Wetness','SnowCoverage','RainAmount','MistAmount']:
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

# Breaking-surf foam, v2 "volumetric-cloud" shading. The v1 recipe (a single
# static Noise + constant roughness) rendered as a flat, glossy plastic sheet:
# its smooth specular and coherent geometry undulation read as shoreline-
# orthogonal "ribs". Real surf foam is a turbulent 3D field of bubbles — light
# scatters through it (sub-surface tint), it boils over time, and its surface
# glints with fine detail. We reproduce that the same way volumetric clouds do:
# a domain-warped, time-animated FBM "density" field (three offset warp probes
# -> float3 warp -> a 4-octave density Noise), then light it — warm sunlit tops,
# aqua light-transmitted bases, a Fresnel rim on the breaking lip, a density-
# gradient detail normal for bubble sparkle, and density-modulated opacity so
# the foam's edges churn instead of cutting hard.
m=material('M_BreakingSurf')
m.set_editor_property('disable_depth_test',False)
m.set_editor_property('translucency_pass',u.MaterialTranslucencyPass.MTP_BEFORE_DOF)
m.set_editor_property('blend_mode',u.BlendMode.BLEND_TRANSLUCENT)
m.set_editor_property('shading_model',u.MaterialShadingModel.MSM_DEFAULT_LIT)
vc=node(m,'VertexColor');pos=node(m,'WorldPosition');time=node(m,'Time')
geomN=node(m,'VertexNormalWS');fres=node(m,'Fresnel')

# Domain-warped, time-animated density field (the volumetric-cloud FBM).
base=node(m,'Multiply',const_b=.012);connect(pos,base,'A')
tx=node(m,'Multiply',const_b=.20);connect(time,tx,'A')
ty=node(m,'Multiply',const_b=.12);connect(time,ty,'A')
z0=node(m,'Constant',r=0.)
txy=node(m,'AppendVector');connect(tx,txy,'A');connect(z0,txy,'B')
tvec=node(m,'AppendVector');connect(txy,tvec,'A');connect(ty,tvec,'B')
p0=node(m,'Add');connect(base,p0,'A');connect(tvec,p0,'B')
half=node(m,'Constant',r=.5)
offA=node(m,'Constant3Vector',constant=u.LinearColor(0.,0.,0.,0.))
offB=node(m,'Constant3Vector',constant=u.LinearColor(37.1,17.2,0.,0.))
offC=node(m,'Constant3Vector',constant=u.LinearColor(0.,11.3,91.7,0.))
pa=node(m,'Add');connect(p0,pa,'A');connect(offA,pa,'B')
pb=node(m,'Add');connect(p0,pb,'A');connect(offB,pb,'B')
pc=node(m,'Add');connect(p0,pc,'A');connect(offC,pc,'B')
wa=node(m,'Noise',levels=1,output_min=0.,output_max=1.);connect(pa,wa,'')
wb=node(m,'Noise',levels=1,output_min=0.,output_max=1.);connect(pb,wb,'')
wc=node(m,'Noise',levels=1,output_min=0.,output_max=1.);connect(pc,wc,'')
sa=node(m,'Subtract');connect(wa,sa,'A');connect(half,sa,'B')
sb=node(m,'Subtract');connect(wb,sb,'A');connect(half,sb,'B')
sc=node(m,'Subtract');connect(wc,sc,'A');connect(half,sc,'B')
ma=node(m,'Multiply',const_b=1.2);connect(sa,ma,'A')
mb=node(m,'Multiply',const_b=1.2);connect(sb,mb,'A')
mc=node(m,'Multiply',const_b=1.2);connect(sc,mc,'A')
wxy=node(m,'AppendVector');connect(ma,wxy,'A');connect(mb,wxy,'B')
wvec=node(m,'AppendVector');connect(wxy,wvec,'A');connect(mc,wvec,'B')
pw=node(m,'Add');connect(p0,pw,'A');connect(wvec,pw,'B')
dens=node(m,'Noise',levels=4,output_min=0.,output_max=1.);connect(pw,dens,'')
ex=node(m,'Constant3Vector',constant=u.LinearColor(.25,0.,0.,0.))
ey=node(m,'Constant3Vector',constant=u.LinearColor(0.,.25,0.,0.))
px=node(m,'Add');connect(pw,px,'A');connect(ex,px,'B')
py=node(m,'Add');connect(pw,py,'A');connect(ey,py,'B')
dxn=node(m,'Noise',levels=4,output_min=0.,output_max=1.);connect(px,dxn,'')
dyn=node(m,'Noise',levels=4,output_min=0.,output_max=1.);connect(py,dyn,'')

# Base colour: sub-surface foam — warm sunlit top, aqua transmitted base, and a
# Fresnel rim glow on the breaking lip. Density brightens (more bubbles scatter).
color=custom(m,
    'float3 lit=float3(0.97,0.98,0.97);'
    'float3 sss=float3(0.62,0.86,0.83);'
    'float3 col=lerp(sss,lit,Dens);'
    'col=lerp(col,float3(1.0,1.0,1.0),Fres*0.55);'
    'col*=0.92+0.08*(1.0-VCol.g);'
    'return col;',
    {'Dens':dens,'Fres':fres,'VCol':vc},3)
output(color,'BASE_COLOR')
# Dense foam is rough (bubbles scatter); thin film is glossier.
output(custom(m,'return 0.22+0.50*Dens;',{'Dens':dens}),'ROUGHNESS')
# Density-gradient detail normal: high-frequency bubble glint that breaks the
# smooth specular "plastic" mirror (and the rib banding it produced).
normal=custom(m,
    'float3 dn=normalize(float3(-(DX-D0)*0.7,-(DY-D0)*0.7,1.0));'
    'return normalize(GeomN+dn*0.55);',
    {'GeomN':geomN,'D0':dens,'DX':dxn,'DY':dyn},3)
output(normal,'NORMAL')
# The VertexColor node's output pin is MCT_Float3 (alpha masked) — read the full
# RGBA from Parameters.VertexColor. vc stays connected to BASE_COLOR so the
# engine flags bUsesVertexColor and interpolates RGBA. Density churns the foam's
# edge so it boils like a cloud silhouette instead of a hard cut.
opacity=custom(m,'return saturate(Parameters.VertexColor.a*(0.55+0.90*Dens));',{'Dens':dens})
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

# Morning ground mist: soft white feathered sprite for NS_MorningMist. Opacity is
# gated by the MPC MistAmount (day-time + season) so every bank fades in/out as one.
# Unlit + pow(1-r)^3 edge so it reads as air, not hard droplet discs.
m=material('M_MorningMist');m.set_editor_property('blend_mode',u.BlendMode.BLEND_TRANSLUCENT)
m.set_editor_property('shading_model',u.MaterialShadingModel.MSM_UNLIT)
m.set_editor_property('used_with_niagara_sprites',True)
m.set_editor_property('used_with_particle_sprites',True)
m.set_editor_property('two_sided',False)
uv=node(m,'TextureCoordinate')
mist=node(m,'CollectionParameter',collection=mpc,parameter_name='MistAmount')
col=custom(m,'float2 c=UV-.5; float r=length(c)*2.; float soft=pow(saturate(1.-r),3.); return float3(.84,.88,.92)*soft;',{'UV':uv},3)
output(col,'EMISSIVE_COLOR')
op=custom(m,'float2 c=UV-.5; float r=length(c)*2.; float soft=pow(saturate(1.-r),3.); return soft*Mist;',{'UV':uv,'Mist':mist})
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