"""Continuous fertile ground and open, mixed oasis planting; no perimeter wall."""
import math
import random
import unreal as u
from yunsha_sand import HLSL, RELIEF, DRY_COLOUR


def ground_material(mat,expr,custom,out):
    out(custom(mat,HLSL+RELIEF+'''struct Field {
float h(float2 p){return frac(sin(dot(p,float2(127.1,311.7)))*43758.5453);}
float n(float2 p){float2 i=floor(p),f=frac(p);f=f*f*(3-2*f);return lerp(lerp(h(i),h(i+float2(1,0)),f.x),lerp(h(i+float2(0,1)),h(i+1),f.x),f.y);}
float f(float2 p){return .57*n(p)+.28*n(p*2.13)+.15*n(p*4.37);}
}; Field F;
float2 q=(P.xy-float2(2500,-51000))/float2(27000,21500);
float n=(F.f(P.xy/2900)-.5)*.24;
float fertile=1-smoothstep(.77,1.12,length(q)+n);
float soil=smoothstep(.38,.67,F.f(P.xy/2400));
'''+DRY_COLOUR+'''
float3 ground=dry;
if (fertile>0.001){
    float3 grass=Texture2DSample(G,GSampler,P.xy/270).rgb*float3(.55,.88,.48);
    float3 earth=Texture2DSample(E,ESampler,P.xy/320).rgb*float3(.30,.22,.13);
    ground=lerp(dry,lerp(grass,earth,soil*.72),fertile);
}
return ground;''',{
        'P':expr(mat,'WorldPosition'),'C':expr(mat,'CameraVectorWS'),
        'S':expr(mat,'TextureObject',texture=u.load_asset('/Game/Shiomori/Textures/T_SandBeach_Diffuse')),
        'G':expr(mat,'TextureObject',texture=u.load_asset('/Game/Nature/T_CampusGrass_Diffuse')),
        'E':expr(mat,'TextureObject',texture=u.load_asset('/Game/Nature/T_CampusMossStone_Diffuse'))}),'BASE_COLOR')
    u.MaterialEditingLibrary.recompile_material(mat);u.EditorAssetLibrary.save_loaded_asset(mat)


def plant(imported,ground):
    rng=random.Random(251025)
    # Preserve broad east-west and north-south approaches, the palace passage,
    # the water itself and the lake pavilion's open flight volume.
    def clear(x,y):
        if u.Born2FlapYunsha.spring_contains(x,y) or ground(x,y)<90:return False
        if abs(x-13000)<2600 and -69000<y<-32500:return False
        if abs(x-13000)<5800 and abs(y+38000)<5800:return False
        if math.hypot(x-2500,y+48500)<2400:return False
        if abs(y+57500)<700 and x>0:return False
        return True
    groves=[(-17300,-52000),(-13500,-60000),(-10000,-43000),
            (-2000,-64000),(0,-41000),(21500,-54000),(23000,-44000),
            (4000,-54000),(5000,-45000),(-4000,-50500)]
    specs=[('Palm',imported('OasisPalm'),135,1000,1900),
           ('Broadleaf',u.load_asset('/Game/Nature/SM_Tree1'),150,700,1400),
           ('Fern',u.load_asset('/Game/Nature/SM_Fern0'),650,65,155),
           ('Grass',u.load_asset('/Game/Nature/SM_Grass1'),1800,35,85),
           ('Meadow',u.load_asset('/Game/Nature/SM_Grass2'),1400,30,65)]
    for name,sm,count,low,high in specs:
        assert sm,name
        a=u.EditorLevelLibrary.spawn_actor_from_class(u.Born2FlapYunshaBatch,u.Vector())
        a.set_actor_label('Yunsha lush oasis '+name);a.tags=['Yunsha','YunshaOasisPlanting']
        c=a.instances;c.set_static_mesh(sm);c.set_collision_profile_name('BlockAll' if name=='Palm' else 'NoCollision')
        bounds=sm.get_bounds();placed=0
        for attempt in range(count*15):
            cx,cy=rng.choice(groves);r=rng.uniform(300,4400);angle=rng.uniform(0,math.tau)
            x=cx+r*math.cos(angle);y=cy+r*math.sin(angle)
            if not clear(x,y):continue
            # Keep planting off the lake promenade and scholar garden walks.
            bank=math.hypot(x/16700,(y+52000)/10800)
            if abs(bank-1)<.025:continue
            h=rng.uniform(low,high);s=h/(2*bounds.box_extent.z)
            c.add_instance(u.Transform(location=u.Vector(x,y,ground(x,y)-s*(bounds.origin.z-bounds.box_extent.z)),
                rotation=u.Rotator(yaw=rng.uniform(0,360)),scale=u.Vector(s,s,s)),False)
            placed+=1
            if placed==count:break
        assert placed==count,(name,placed,count)