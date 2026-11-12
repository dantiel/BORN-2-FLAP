"""Continuous fertile ground and open, mixed oasis planting; no perimeter wall."""
import math
import random
import unreal as u
from yunsha_sand import HLSL, RELIEF, DRY_COLOUR


def ground_material(mat,expr,custom,out):
    # Keep actual oasis plants, but never blend photographs of grass/moss into sand.
    out(custom(mat,RELIEF+DRY_COLOUR+r'''
float outer=length(P.xy/float2(15600,9600)-float2(0,-52000.0/9600.0));
float bank=1.0-smoothstep(0.0,0.10,abs(outer-1.0));
return dry*(1.0-0.12*bank);
''',{'P':expr(mat,'WorldPosition')}),'BASE_COLOR')
    u.MaterialEditingLibrary.recompile_material(mat)
    u.EditorAssetLibrary.save_loaded_asset(mat)


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


def plant_lake(imported,ground):
    """Reeds, ferns and a few palms ring the crescent shore; lotus is on the
    water itself (built in create_yunsha_map). Ground height selects the band,
    so planting hugs the actual waterline rather than an analytic ellipse."""
    rng=random.Random(110520)
    grass=u.load_asset('/Game/Nature/SM_Grass1');meadow=u.load_asset('/Game/Nature/SM_Grass2')
    fern=u.load_asset('/Game/Nature/SM_Fern0');palm=imported('OasisPalm')
    def radius(x,y):return math.hypot(x/15600,(y+52000)/9600)
    def scatter(sm,count,band,ground_lo,ground_hi,height_range,block):
        a=u.EditorLevelLibrary.spawn_actor_from_class(u.Born2FlapYunshaBatch,u.Vector())
        a.set_actor_label('Yunsha lake shore '+sm.get_name());a.tags=['Yunsha','YunshaLakeShore']
        c=a.instances;c.set_static_mesh(sm);c.set_collision_profile_name('BlockAll' if block else 'NoCollision')
        bounds=sm.get_bounds();placed=0
        for attempt in range(count*18):
            if placed>=count:break
            x=rng.uniform(-17500,17500);y=rng.uniform(-62800,-41600)
            r=radius(x,y)
            if not(band[0]<r<band[1]):continue
            gh=ground(x,y)
            if not(ground_lo<gh<ground_hi):continue
            height=rng.uniform(*height_range);sc=height/(2*bounds.box_extent.z)
            c.add_instance(u.Transform(location=u.Vector(x,y,gh-sc*(bounds.origin.z-bounds.box_extent.z)),
                rotation=u.Rotator(yaw=rng.uniform(0,360)),scale=u.Vector(sc,sc,sc)),False)
            placed+=1
    # The oasis term flattens the bank to 140 cm; ground bands must sit inside
    # that plateau or they silently place nothing (the old 2-70 cm reed band
    # never matched the terrain, so the shore read bare). Reeds stand in the
    # shallow water and at the exact waterline; weeds/ferns fill the damp slope
    # above; palms anchor the high rim.
    scatter(grass,1600,(0.93,1.06),-30,45,(70,130),False)   # tall reeds at the waterline
    scatter(meadow,1200,(0.96,1.12),30,150,(30,70),False)   # low weeds on the shore slope
    scatter(fern,560,(1.00,1.30),40,160,(50,140),False)     # leafy weeds up the bank
    scatter(palm,26,(1.06,1.45),90,180,(700,1400),True)     # palms on the high bank