"""Yunsha: deterministic Cloud-Sand Gorge. No downloads or local source folders.
Uses retained licensed architecture, rock scans, CC0 textures and vegetation.
"""
import math
import re
import random
import sys
import wave
import struct
from pathlib import Path
import unreal as u

HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(HERE))
from yunsha_escarpment import noise as erosion_noise
from project_guard import require_project
require_project()
A=u.AssetToolsHelpers.get_asset_tools(); E=u.EditorAssetLibrary; M=u.MaterialEditingLibrary
ROOT='/Game/Yunsha'; rng=random.Random(221022)
for folder in ('Maps','Meshes','Materials','Audio'):E.make_directory(ROOT+'/'+folder)
world=u.EditorLoadingAndSavingUtils.new_blank_map(False)
world.get_world_settings().set_editor_property('force_no_precomputed_lighting',True)


def expr(mat,kind,**properties):
    n=M.create_material_expression(mat,getattr(u,'MaterialExpression'+kind))
    for k,v in properties.items():n.set_editor_property(k,v)
    return n

def link(a,b,pin,output=''):
    assert M.connect_material_expressions(a,output,b,'' if pin=='Input' else pin), 'Invalid material connection '+pin
def out(n,prop,output=''):M.connect_material_property(n,output,getattr(u.MaterialProperty,'MP_'+prop))
def scalar(m,value):return expr(m,'Constant',r=value)
def color(m,c):return expr(m,'Constant3Vector',constant=u.LinearColor(*c,1))

def custom(mat,code,inputs):
    n=expr(mat,'Custom',code=code,output_type=u.CustomMaterialOutputType.CMOT_FLOAT3)
    pins=[]
    for name in inputs:
        p=u.CustomInput();p.set_editor_property('input_name',name);pins.append(p)
    n.set_editor_property('inputs',pins)
    for name,value in inputs.items():link(value,n,name)
    return n

def material(name,tint,texture=None,metal=0.,emission=0.,cloud=False,mural=False):
    path=ROOT+'/Materials/M_'+name
    mat=u.load_asset(path) if E.does_asset_exist(path) else A.create_asset('M_'+name,ROOT+'/Materials',u.Material,u.MaterialFactoryNew())
    M.delete_all_material_expressions(mat)
    base=color(mat,tint)
    if texture:
        tex=u.load_asset(texture);assert tex,texture
        pos=expr(mat,'WorldPosition');div=expr(mat,'Divide');link(pos,div,'A');link(scalar(mat,400),div,'B')
        normal=expr(mat,'VertexNormalWS');absolute=expr(mat,'Abs');link(normal,absolute,'Input')
        projections=[]
        for mask,axis in [((False,True,True),(True,False,False)),((True,False,True),(False,True,False)),((True,True,False),(False,False,True))]:
            uv=expr(mat,'ComponentMask',r=mask[0],g=mask[1],b=mask[2]);link(div,uv,'Input')
            sample=expr(mat,'TextureSample',texture=tex);link(uv,sample,'UVs')
            weight=expr(mat,'ComponentMask',r=axis[0],g=axis[1],b=axis[2]);link(absolute,weight,'Input')
            mul=expr(mat,'Multiply');link(sample,mul,'A','RGB');link(weight,mul,'B');projections.append(mul)
        add=expr(mat,'Add');link(projections[0],add,'A');link(projections[1],add,'B')
        alltex=expr(mat,'Add');link(add,alltex,'A');link(projections[2],alltex,'B')
        mul=expr(mat,'Multiply');link(alltex,mul,'A');link(base,mul,'B');base=mul
    out(base,'BASE_COLOR');out(scalar(mat,.87 if not metal else .32),'ROUGHNESS');out(scalar(mat,metal),'METALLIC')
    if emission: glow=expr(mat,'Multiply');link(base,glow,'A');link(scalar(mat,emission),glow,'B');out(glow,'EMISSIVE_COLOR')
    if cloud:
        mat.set_editor_property('blend_mode',u.BlendMode.BLEND_TRANSLUCENT)
        mat.set_editor_property('two_sided',True)
        # Inverted Fresnel dissolves the silhouette of flattened cloud volumes.
        f=expr(mat,'Fresnel',exponent=1.4,base_reflect_fraction=0.)
        inv=expr(mat,'OneMinus');link(f,inv,'Input');mul=expr(mat,'Multiply');link(inv,mul,'A');link(scalar(mat,.12),mul,'B');out(mul,'OPACITY')
    if mural:
        reveal=expr(mat,'ScalarParameter',parameter_name='Reveal',default_value=1.)
        glow=expr(mat,'ScalarParameter',parameter_name='Afterglow',default_value=0.)
        blend=expr(mat,'LinearInterpolate');link(color(mat,(.31,.13,.07)),blend,'A');link(base,blend,'B');link(reveal,blend,'Alpha');out(blend,'BASE_COLOR')
        em=expr(mat,'Multiply');link(base,em,'A');link(glow,em,'B');out(em,'EMISSIVE_COLOR');mat.set_editor_property('two_sided',True)
    M.set_material_usage(mat,u.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES)
    mat.set_editor_property('two_sided',True)
    M.recompile_material(mat);assert E.save_loaded_asset(mat)
    return mat

sand=material('WindRippleSand',(1.0,.77,.48),'/Game/Shiomori/Textures/T_SandBeach_Diffuse')
out(custom(sand,'''float3 detail=Texture2DSample(T,TSampler,P.xy/180).rgb*2-1;
return normalize(N+float3(detail.xy*.32,0));''',{
    'P':expr(sand,'WorldPosition'),'N':expr(sand,'VertexNormalWS'),
    'T':expr(sand,'TextureObject',texture=u.load_asset('/Game/Shiomori/Textures/T_SandBeach_Normal'))}),'NORMAL')
sand.set_editor_property('tangent_space_normal',False)
M.recompile_material(sand);E.save_loaded_asset(sand)
rock=material('RedSandstone',(1.12,.78,.53),'/Game/Shiomori/Cliffs/Textures/T_Cliff_rock_face_03_Diffuse')
# Photographic mineral variation and relief at two scales break up the smooth
# generated silhouette. World-space normals work across the carved interiors.
pos=expr(rock,'WorldPosition');normal=expr(rock,'VertexNormalWS')
rocktex=expr(rock,'TextureObject',texture=u.load_asset('/Game/Shiomori/Cliffs/Textures/T_Cliff_rock_face_03_Diffuse'))
rocknormal=expr(rock,'TextureObject',texture=u.load_asset('/Game/Shiomori/Cliffs/Textures/T_Cliff_rock_face_03_nor_dx'))
out(custom(rock,'''float3 w=pow(abs(N),4);w/=max(.0001,w.x+w.y+w.z);
float3 a=Texture2DSample(T,TSampler,P.yz/580).rgb*w.x+Texture2DSample(T,TSampler,P.xz/580).rgb*w.y+Texture2DSample(T,TSampler,P.xy/580).rgb*w.z;
float3 b=Texture2DSample(T,TSampler,P.yz/1900+3.7).rgb*w.x+Texture2DSample(T,TSampler,P.xz/1900+3.7).rgb*w.y+Texture2DSample(T,TSampler,P.xy/1900+3.7).rgb*w.z;
float band=.5+.5*sin(P.z*.0018+sin(P.y*.00016)*2+sin(P.x*.00021));
return lerp(a,b,.32)*lerp(float3(.89,.69,.52),float3(1.12,.9,.72),band);''',{'P':pos,'N':normal,'T':rocktex}),'BASE_COLOR')
out(custom(rock,'''float3 w=pow(abs(N),4);w/=max(.0001,w.x+w.y+w.z);
float3 x=Texture2DSample(T,TSampler,P.yz/580).rgb*2-1;
float3 y=Texture2DSample(T,TSampler,P.xz/580).rgb*2-1;
float3 z=Texture2DSample(T,TSampler,P.xy/580).rgb*2-1;
return normalize(N+.7*(float3(0,x.x,x.y)*w.x+float3(y.x,0,y.y)*w.y+float3(z.x,z.y,0)*w.z));''',{'P':pos,'N':normal,'T':rocknormal}),'NORMAL')
rock.set_editor_property('tangent_space_normal',False)
M.recompile_material(rock);E.save_loaded_asset(rock)
pale=material('PaleCarvedStone',(.78,.64,.43),'/Game/Shiomori/Textures/T_ConcreteWorn_Diffuse')
dark=material('DeepCopper',(.19,.075,.038))
jade=material('JadeTiles',(.07,.26,.19),metal=.25)
cinnabar=material('Cinnabar',(.35,.055,.028))
gold=material('SunGold',(.65,.38,.075),metal=.72)
water=material('JadeSpring',(.025,.22,.17),metal=.55)
mirror=material('SilverMirror',(.40,.52,.58),metal=.96)
cloud=material('StandingCloud',(.64,.69,.68),cloud=True)
pigment=material('SunGlass',(.88,.38,.09),mural=True)
lantern=material('Lantern',(.95,.32,.055),emission=5)
material('LiftMote',(.66,.42,.19),emission=.2)
ptero=material('Pterosaur',(.7,.4,.15))
vertex=expr(ptero,'VertexColor');out(vertex,'BASE_COLOR','RGB');ptero.set_editor_property('two_sided',True);M.recompile_material(ptero);E.save_loaded_asset(ptero)


generated_meshes=set()

def mesh(name,verts,tris,mat,collision=True):
    name=re.sub(r'[^A-Za-z0-9_]', '_',name)
    path=ROOT+'/Meshes/SM_'+name
    generated_meshes.add(path)
    sm=u.load_asset(path) if E.does_asset_exist(path) else A.create_asset('SM_'+name,ROOT+'/Meshes',u.StaticMesh,None)
    md=sm.create_static_mesh_description();md.reserve_new_vertices(len(verts));md.reserve_new_vertex_instances(len(verts));md.reserve_new_polygons(len(tris))
    pg=md.create_polygon_group();md.set_polygon_group_material_slot_name(pg,'Surface')
    sm.set_editor_property('static_materials',[u.StaticMaterial(material_interface=mat,material_slot_name='Surface')])
    ids=[]
    for x,y,z in verts:
        v=md.create_vertex();md.set_vertex_position(v,u.Vector(x,y,z));i=md.create_vertex_instance(v)
        md.set_vertex_instance_uv(i,u.Vector2D(x/400,y/400),0);ids.append(i)
    for a,b,c in tris:md.create_triangle(pg,[ids[a],ids[c],ids[b]])
    sm.set_editor_property('allow_cpu_access',True);sm.build_from_static_mesh_descriptions([md],False,False)
    if collision:sm.get_editor_property('body_setup').set_editor_property('collision_trace_flag',u.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
    sm.set_material(0,mat);assert E.save_loaded_asset(sm)
    return sm


def actor(label,sm,p=(0,0,0),scale=(1,1,1),yaw=0,collision=True,tags=()):
    a=u.EditorLevelLibrary.spawn_actor_from_class(u.StaticMeshActor,u.Vector(*p),u.Rotator(yaw=yaw))
    a.set_actor_label('Yunsha '+label);a.tags=['Yunsha',*tags]
    c=a.static_mesh_component;c.set_static_mesh(sm);c.set_collision_profile_name('BlockAll' if collision else 'NoCollision')
    a.set_actor_scale3d(u.Vector(*scale));return a

# Shared primitive meshes are instanced per material, not thousands of actors.
instances={}
for name,mat in [('Rock',rock),('Pale',pale),('Dark',dark),('Jade',jade),('Red',cinnabar),('Gold',gold),('Lantern',lantern)]:
    a=u.EditorLevelLibrary.spawn_actor_from_class(u.Born2FlapYunshaBatch,u.Vector())
    a.set_actor_label('Yunsha masonry '+name);a.tags=['Yunsha']
    c=a.instances
    c.set_static_mesh(u.load_asset('/Engine/BasicShapes/Cube'));c.set_material(0,mat);c.set_collision_profile_name('BlockAll')
    instances[name]=c

def box(kind,p,size,yaw=0):
    instances[kind].add_instance(u.Transform(location=u.Vector(*p),rotation=u.Rotator(yaw=yaw),scale=u.Vector(*(v/100 for v in size))),False)

# Native height sampler is the source of truth for both collision and flight.
ground=u.Born2FlapYunsha.ground_height
# 2.4 x 2.8 km, 10m cells; tiled to keep precision and culling practical.
for row in range(4):
    for col in range(4):
        x0=-120000+col*60000;y0=-110000+row*70000;verts=[];tris=[]
        for j in range(71):
            for i in range(61):
                x=x0+i*1000;y=y0+j*1000;verts.append((x,y,ground(x,y)))
        for j in range(70):
            for i in range(60):
                k=j*61+i;tris.extend([(k,k+1,k+61),(k+1,k+62,k+61)])
        actor('dunes %d %d'%(row,col),mesh('Dunes_%d_%d'%(row,col),verts,tris,sand))

# A distant skirt carries the sand-sea to the horizon, beyond the flight area.
verts=[];tris=[];segments=192
for ring,factor in enumerate((1.,1.5,3.,7.,80.)):
    for i in range(segments):
        ang=math.tau*i/segments;ca=math.cos(ang);sa=math.sin(ang)
        r=min(120000/max(abs(ca),1e-6),140000/max(abs(sa),1e-6))
        x=ca*r;y=30000+sa*r
        z=ground(x,y) if ring==0 else 1600+1100*math.sin(ang*9+ring)*math.sin(ang*3-ring*.7)
        verts.append((x*factor,30000+(y-30000)*factor,z))
for ring in range(4):
    for i in range(segments):
        a=ring*segments+i;b=ring*segments+(i+1)%segments
        tris.extend([(a,b,b+segments),(a,b+segments,a+segments)])
actor('Distant sand sea',mesh('DistantSandSea',verts,tris,sand,False),collision=False)

# Water uses the exact crescent mask: clip a fine grid to the native basin.
verts=[];tris=[]
for y in range(-61700,-42199,150):
    for x in range(-15700,15701,150):
        corners=[(x,y),(x+150,y),(x+150,y+150),(x,y+150)]
        if all(u.Born2FlapYunsha.spring_contains(px,py) for px,py in corners):
            k=len(verts);verts.extend((px,py,0) for px,py in corners);tris.extend([(k,k+1,k+2),(k,k+2,k+3)])
actor('Lunya crescent spring',mesh('Lunya',verts,tris,water,False),collision=False)

# Authored architecture preserves its source geometry, UVs and weathering.
def imported(name):
    objects=[u.load_asset(p) for p in E.list_assets('/Game/WorldAssets/Yunsha/'+name)]
    found=[o for o in objects if isinstance(o,u.StaticMesh)]
    assert len(found)==1, 'Missing retained architectural asset '+name
    return found[0]

hall=imported('TempleHalls');pagoda=imported('Pagoda');gate=imported('Gate');scan=imported('rock_face_01')

def place_asset(name,sm,x,y,z,height,yaw=0,tags=()):
    b=sm.get_bounds();s=height/(2*b.box_extent.z);angle=math.radians(yaw)
    ox=s*(b.origin.x*math.cos(angle)-b.origin.y*math.sin(angle))
    oy=s*(b.origin.x*math.sin(angle)+b.origin.y*math.cos(angle))
    return actor(name,sm,(x-ox,y-oy,z-s*(b.origin.z-b.box_extent.z)),(s,s,s),yaw,tags=tags)

def temple(name,x,y,z,size=1.,summit=False):
    place_asset(name,pagoda if summit else hall,x,y,z,(4200 if summit else 2900)*size,15 if summit else 180,tags=['YunshaArchitecture'])
    if summit:
        perch=u.EditorLevelLibrary.spawn_actor_from_class(u.TargetPoint,u.Vector(x+1600,y-1600,z+10))
        perch.tags=['YunshaPerch'];perch.set_actor_label(name+' flight-only terrace')

# A monumental inhabited oasis, built from retained detailed architecture.
from yunsha_palace import build as build_palace
build_palace(imported,place_asset,mesh,actor,material,ground,scan,expr,custom,out)

# Sandstone pillars: tapered, fluted and striated, never scaled cylinders.
def pillar(name,x,y,height,radius,seed,forest=False,base=None):
    r=random.Random(seed);segments=96;levels=45;z0=ground(x,y)-300 if base is None else base;verts=[];tris=[]
    phase=r.random()*6.28
    for j in range(levels+1):
        t=j/levels;z=z0+(height-z0)*t
        taper=1.50-.50*t+.11*erosion_noise(t*13,phase)+.045*erosion_noise(t*43,phase+7)
        for i in range(segments):
            a=math.tau*i/segments
            crag=1+.32*erosion_noise(math.cos(a)*3+phase,math.sin(a)*3+t*.4)+.11*erosion_noise(math.cos(a)*8+t*4,math.sin(a)*8+phase)+.035*erosion_noise(math.cos(a)*22+t*6,math.sin(a)*22+phase)
            verts.append((x+radius*taper*crag*math.cos(a),y+radius*taper*crag*math.sin(a),z))
    for j in range(levels):
        for i in range(segments):a=j*segments+i;b=j*segments+(i+1)%segments;tris.extend([(a,b,b+segments),(a,b+segments,a+segments)])
    rim=len(verts);verts.extend(verts[-segments:])
    k=len(verts);verts.append((x,y,height))
    for i in range(segments):tris.append((k,rim+i,rim+(i+1)%segments))
    actor(name,mesh(name,verts,tris,rock))
    return height

peaks=[(-16000,39000,23000,4300),(17000,59000,32000,4600),(-19000,81000,41000,5000),(14000,104000,50000,4500),(-6500,126000,61000,5800)]
for i,(x,y,z,r) in enumerate(peaks):
    pillar('Gilded Peak %d'%i,x,y,z,r,420+i,True)
    temple('Sky Temple %d'%i,x,y,z+10,1.,True)
for i in range(14):
    x=rng.choice([-1,1])*rng.uniform(12000,37000);y=rng.uniform(33000,146000)
    if any(math.hypot(x-a,y-b)<r+4000 for a,b,z,r in peaks):continue
    pillar('Mist spire %d'%i,x,y,rng.uniform(14000,42000),rng.uniform(1100,2800),800+i)
# Two eroded buttresses form a real slot with no invisible boundary.
for side in (-1,1):pillar('Narrows buttress %d'%side,4500+side*8500,14000,18000,3600,49+side)

# Grottoes excavated in one continuous mountain, with broad uncarved ribs.
from yunsha_escarpment import build as build_escarpment, front as cliff_front
mouths=build_escarpment(mesh,actor,rock,pigment)
for i in range(54):
    y=rng.uniform(-67000,13000);x=cliff_front(y,0)+rng.uniform(200,3800)
    place_asset('Escarpment fallen rock %d'%i,scan,x,y,ground(x,y)-100,rng.uniform(400,1500),rng.uniform(0,360))
for i,(x,y,z,r) in enumerate(peaks):
    for j in range(9):
        a=math.tau*j/9+rng.uniform(-.15,.15)
        place_asset('Summit weathered shoulder %d %d'%(i,j),scan,x+r*.77*math.cos(a),y+r*.77*math.sin(a),z-rng.uniform(550,1400),rng.uniform(750,1700),math.degrees(a))
for i in range(36):
    a=rng.uniform(0,math.tau);x=17500*math.cos(a);y=-52000+11600*math.sin(a)
    place_asset('Oasis exposed bedrock %d'%i,scan,x,y,ground(x,y)-100,rng.uniform(100,360),rng.uniform(0,360))

# Natural imported trees only where the concept permits them: oasis and summits.
for name in ('Tree0','Tree1','Fir0'):
    sm=u.load_asset('/Game/Nature/SM_'+name)
    assert sm,'Missing retained Nature asset '+name
    a=u.EditorLevelLibrary.spawn_actor_from_class(u.Born2FlapYunshaBatch,u.Vector());a.tags=['Yunsha'];a.set_actor_label('Yunsha canopy '+name)
    c=a.instances;c.set_static_mesh(sm);c.set_collision_profile_name('NoCollision')
    b=sm.get_bounds()
    locations=[]
    if name=='Tree1':
        for j in range(85):
            cx,cy=rng.choice(((1500,-43500),(-18500,-49000),(-2000,-65000),(24500,-46000),(23500,-54000)))
            ang=rng.uniform(0,math.tau);radius=rng.uniform(300,2400)
            x=cx+radius*math.cos(ang);y=cy+radius*math.sin(ang)
            if not u.Born2FlapYunsha.spring_contains(x,y):locations.append((x,y,ground(x,y),rng.uniform(1000,1700)))
    else:
        for x,y,z,r in peaks:
            for j in range(18):
                a0=rng.uniform(0,math.tau);rr=rng.uniform(r*.4,r*.65);locations.append((x+rr*math.cos(a0),y+rr*math.sin(a0),z,rng.uniform(650,1250)))
    for x,y,z,h in locations:
        s=h/(2*b.box_extent.z);c.add_instance(u.Transform(location=u.Vector(x,y,z-s*(b.origin.z-b.box_extent.z)),rotation=u.Rotator(yaw=rng.uniform(0,360)),scale=u.Vector(s,s,s)),False)

# Standing clouds use volumetric fog, so flying into them has depth and no
# translucent sphere surface or disc silhouette. Uniform scales are required.
for i in range(26):
    x=rng.uniform(-30000,30000);y=rng.uniform(35000,150000)
    z=21000+.22*(y-35000)+rng.uniform(-2500,2500)
    a=u.EditorLevelLibrary.spawn_actor_from_class(u.LocalFogVolume,u.Vector(x,y,z))
    a.set_actor_label('Yunsha standing mist %d'%i);a.tags=['Yunsha','YunshaCloud']
    radius=rng.uniform(22,35);a.set_actor_scale3d(u.Vector(radius,radius,radius))
    c=a.get_component_by_class(u.LocalFogVolumeComponent)
    c.set_editor_property('radial_fog_extinction',.018)
    c.set_editor_property('height_fog_extinction',0.)
    c.set_editor_property('height_fog_falloff',3.)
    c.set_editor_property('fog_albedo',u.LinearColor(.80,.86,.9,1))

# Silver mirror-wadis emerge only in monsoon; no second permanent oasis.
for i in range(5):
    x=11000+2300*math.sin(i);y=-18000+i*8500;z=ground(x,y)+6
    rim=[(x+2400*math.cos(j*math.tau/48),y+650*math.sin(j*math.tau/48)) for j in range(48)]
    verts=[(x,y,z)]+[(px,py,ground(px,py)+6) for px,py in rim]
    tris=[(0,1+j,1+(j+1)%48) for j in range(48)]
    actor('Silver mirror wadi',mesh('MirrorWadi%d'%i,verts,tris,mirror,False),collision=False,tags=['YunshaMirror'])

# Original, reproducible audio. No network, numpy or platform-specific encoder.
source=HERE.parent/'Saved/GeneratedYunsha';source.mkdir(parents=True,exist_ok=True)
for name,seconds in [('DesertWind',12),('WindHarp',12),('TempleBell',6)]:
    rate=22050;data=bytearray();noise=0.;rr=random.Random(2200)
    for i in range(rate*seconds):
        t=i/rate;noise=.97*noise+.03*rr.uniform(-1,1)
        if name=='DesertWind':v=.65*noise*(.7+.3*math.sin(math.tau*t/seconds))+.015*math.sin(math.tau*55*t)
        elif name=='WindHarp':v=sum(math.sin(math.tau*f*t+p)*.045 for f,p in [(110,0),(165,.6),(220,1.2),(293,1.8)])*(.65+.35*math.sin(math.tau*t/seconds))+.12*noise
        else:v=sum(math.sin(math.tau*f*t)*math.exp(-t*d)*a for f,d,a in [(440,.7,.3),(1188,1.2,.14),(2376,1.8,.08)])*min(1,t*100)
        fade=min(1,t*10,(seconds-t)*10);data.extend(struct.pack('<h',int(max(-1,min(1,v*fade))*32767)))
    path=source/(name+'.wav')
    with wave.open(str(path),'wb') as f:f.setnchannels(1);f.setsampwidth(2);f.setframerate(rate);f.writeframes(data)
    task=u.AssetImportTask();task.filename=str(path);task.destination_path=ROOT+'/Audio';task.destination_name=name;task.automated=True;task.replace_existing=True;task.save=True;A.import_asset_tasks([task])
    sound=u.load_asset(ROOT+'/Audio/'+name);assert sound;sound.set_editor_property('looping',name!='TempleBell');E.save_loaded_asset(sound)

sun=u.EditorLevelLibrary.spawn_actor_from_class(u.DirectionalLight,u.Vector(0,0,80000),u.Rotator(pitch=-25,yaw=-75))
sun.light_component.set_mobility(u.ComponentMobility.MOVABLE);sun.light_component.set_intensity(80000);sun.light_component.set_atmosphere_sun_light(True)
sky=u.EditorLevelLibrary.spawn_actor_from_class(u.SkyLight,u.Vector());sky.light_component.set_mobility(u.ComponentMobility.MOVABLE);sky.light_component.set_editor_property('real_time_capture',True);sky.light_component.set_intensity(1.)
u.EditorLevelLibrary.spawn_actor_from_class(u.SkyAtmosphere,u.Vector())
fog=u.EditorLevelLibrary.spawn_actor_from_class(u.ExponentialHeightFog,u.Vector());fog.component.set_editor_property('fog_density',.0018)
views=[((33000,-85000,33000),(3000,-50000,1800)),((-22000,-70000,10000),(-43000,-55000,8000)),
       ((24000,18000,17000),(4500,14000,7000)),((45000,17000,48000),(-16000,39000,23000)),
       ((-16000,116000,65000),(-6500,126000,63000)),((17000,-69000,2000),(13000,-49000,1800)),
       ((13000,-39700,1220),(13000,-35500,1600)),
       ((-3500,-56100,1400),(1800,-50500,600))]
for i,(p,target) in enumerate(views):
    a=u.EditorLevelLibrary.spawn_actor_from_class(u.CameraActor,u.Vector(*p),u.MathLibrary.find_look_at_rotation(u.Vector(*p),u.Vector(*target)))
    a.tags=['YunshaView%d'%i];a.set_actor_label('Yunsha view %d'%i)
u.EditorLevelLibrary.set_level_viewport_camera_info(u.Vector(*views[0][0]),u.MathLibrary.find_look_at_rotation(u.Vector(*views[0][0]),u.Vector(*views[0][1])))
assert u.EditorLoadingAndSavingUtils.save_map(world,ROOT+'/Maps/YUNSHA')
# This directory contains only disposable outputs owned by this recipe. Retire
# old roofs and arcade pieces so CookAll does not bundle abandoned prototypes.
for path in E.list_assets(ROOT+'/Meshes',recursive=True,include_folder=False):
    package=path.split('.')[0]
    if package not in generated_meshes:assert E.delete_asset(package),package
u.log('YUNSHA_MAP_READY seed=221022 sky_temples=5 grotto_mouths=60')
if not globals().get('B2F_GENERATION_BATCH'):u.SystemLibrary.execute_console_command(None,'quit')

