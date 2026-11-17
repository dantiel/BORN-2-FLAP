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
mpc.set_editor_property('scalar_parameters',params)
if not ela.save_loaded_asset(mpc):
    u.log_warning('MPC_Weather save failed — locked by another editor instance')

def repair_breaking_surf():
    """Bulletproof unlit rebuild of M_BreakingSurf — v7. v6's stock foam texture
    sampled so dark that the lace term was ~0 across the strip and the churn
    floor alone read as a faint grey wash over black water. v7 boosts the
    texture signal (3.4x speckle gain, denser blend), raises the emissive floor
    to 0.95 so the strip is WHITE even between foam puffs, and lifts the opacity
    floor (0.55*churn + 1.05*foam) so the band reads as solid white water at
    the crest. Samples from WORLD POSITION, twice (2.3x scale, different pan),
    blended with vertex-colour churn + contrast Power. Engine-standard
    expressions only — no Custom HLSL."""
    m=material('M_BreakingSurf')
    m.set_editor_property('disable_depth_test',False)
    m.set_editor_property('translucency_pass',u.MaterialTranslucencyPass.MTP_BEFORE_DOF)
    m.set_editor_property('blend_mode',u.BlendMode.BLEND_TRANSLUCENT)
    m.set_editor_property('shading_model',u.MaterialShadingModel.MSM_UNLIT)
    vc=node(m,'VertexColor')
    # UE 5.8: the VertexColor node's pin is float3 (alpha is NOT exposed) — mask
    # strictly to R (see v5 notes for the full component-mask gotcha).
    r=node(m,'ComponentMask',r=True,g=False,b=False,a=False)
    connect(vc,r,'')
    foamtex=u.load_asset('/Water/Textures/Foam/T_WaterFlow_01_Foam_Tiled')
    assert foamtex, '/Water/Textures/Foam/T_WaterFlow_01_Foam_Tiled missing (Water plugin disabled?)'
    def param(name,default):
        n=node(m,'ScalarParameter',default_value=default)
        n.set_editor_property('parameter_name',name)
        return n
    def sat(n):
        s=node(m,'Saturate');connect(n,s,'');return s
    scale=param('FoamScale',0.0045)
    speed=param('FoamSpeed',0.030)
    dens=param('FoamDensity',0.92)
    contr=param('FoamContrast',1.45)
    wp=node(m,'WorldPosition')
    rg=node(m,'ComponentMask',r=True,g=True,b=False,a=False);connect(wp,rg,'')
    t=node(m,'Time')
    # Sample A: world-space tile, slow cross-shore pan.
    a=node(m,'Multiply');connect(rg,a,'A');connect(scale,a,'B')
    ta=node(m,'Multiply');connect(t,ta,'A');connect(speed,ta,'B')
    z0=node(m,'Constant',r=0.0)
    panA=node(m,'AppendVector');connect(z0,panA,'A');connect(ta,panA,'B')
    uva=node(m,'Add');connect(a,uva,'A');connect(panA,uva,'B')
    sa=node(m,'TextureSample',texture=foamtex);connect(uva,sa,'UVs')
    # Sample B: 2.3x scale, along-shore pan + UV offset — breaks the tiling.
    b=node(m,'Multiply',const_b=2.3);connect(a,b,'A')
    tb=node(m,'Multiply',const_b=1.6);connect(ta,tb,'A')
    off=node(m,'Constant',r=0.33)
    panB=node(m,'AppendVector');connect(tb,panB,'A');connect(off,panB,'B')
    uvb=node(m,'Add');connect(b,uvb,'A');connect(panB,uvb,'B')
    sb=node(m,'TextureSample',texture=foamtex);connect(uvb,sb,'UVs')
    # Speckle lace: A*B brightened, contrast-boosted, blended with the churn.
    spk=node(m,'Multiply',const_b=3.4);connect(sa,spk,'A');connect(sb,spk,'B')
    lace=sat(spk)
    lerp=node(m,'LinearInterpolate');connect(r,lerp,'A');connect(lace,lerp,'B');connect(dens,lerp,'Alpha')
    pw=node(m,'Power');connect(lerp,pw,'Base');connect(contr,pw,'Exp')
    foam=sat(pw)
    # Emissive: bright white foam with a high floor so the wash never greys out.
    lv=node(m,'Multiply',const_b=0.70);connect(foam,lv,'A')
    lfloor=node(m,'Constant',r=0.95)
    level=node(m,'Add');connect(lfloor,level,'A');connect(lv,level,'B')
    white=node(m,'Constant3Vector',constant=u.LinearColor(1.,1.,1.,1.))
    col=node(m,'Multiply');connect(white,col,'A');connect(level,col,'B')
    output(col,'EMISSIVE_COLOR')
    # Opacity: churn floor + dense foam — opaque white water at the crest.
    opA=node(m,'Multiply',const_b=0.55);connect(r,opA,'A')
    opB=node(m,'Multiply',const_b=1.05);connect(foam,opB,'A')
    op=node(m,'Add');connect(opA,op,'A');connect(opB,op,'B')
    output(sat(op),'OPACITY')
    lib.recompile_material(m)
    if not ela.save_loaded_asset(m):
        u.log_warning('M_BreakingSurf save failed — locked by another editor instance')

# Run the surf rebuild FIRST: it is the highest-value asset here and its save
# must not be aborted by a package locked by an open editor session in the
# surface-wrap loop below (which now tolerates per-material save failures).
repair_breaking_surf()

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
        lib.recompile_material(m)
        if not ela.save_loaded_asset(m):
            u.log_warning('WEATHER_SURFACE_SAVE_FAILED %s (locked by another editor? left unchanged)' % path)
        else:
            count+=1
u.log('WEATHER_ASSETS_READY wrapped=%d' % count)
