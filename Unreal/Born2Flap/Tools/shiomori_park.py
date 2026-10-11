"""Restrained coastal garden using photographed vegetation and stone assets."""
import math
import random

def install(scope, api):
    import unreal as u
    r=random.Random(261010)
    ground,face,route=api['ground'],api['face'],api['route']
    fm=scope['foliage_meshes'];tag=api['TAG'] if 'TAG' in api else 'ShiomoriCampusV2'
    tx,ty=api['tx'],api['ty'];sight=math.radians(api['sight'])
    direction=(math.cos(sight),math.sin(sight))
    paths=api['path']+api['shrine_path'][1:]
    def segment_distance(x,y,a,b):
        dx,dy=b[0]-a[0],b[1]-a[1];t=max(0,min(1,((x-a[0])*dx+(y-a[1])*dy)/max(1,dx*dx+dy*dy)))
        return math.hypot(x-a[0]-t*dx,y-a[1]-t*dy)
    def clear(x,y,height):
        dx,dy=x-tx,y-ty;along=dx*direction[0]+dy*direction[1];side=abs(dx*direction[1]-dy*direction[0])
        if math.hypot(dx,dy)<(1300 if height>150 else 650):return False
        if height>150 and -1800<along<22000 and side<1400+height*.4:return False
        if any(segment_distance(x,y,a,b)<(550 if height>200 else 200) for a,b in zip(paths,paths[1:])):return False
        if math.hypot(x-api['qx'],y-api['qy'])<650:return False
        return True
    count=0
    def plant(kind,x,y,height,z=None):
        nonlocal count
        if not clear(x,y,height):return None
        pool=fm.get(kind,[])
        if not pool:return None
        z=ground(x,y) if z is None else z
        a=scope['foliage']('Campus park '+kind,(x,y,z),r.choice(pool),height,rot=(0,r.uniform(0,360),0),ground_z=z)
        a.tags=[tag,'CampusPark',kind]
        if kind in ('Flower','Grass','Fern'):
            a.static_mesh_component.set_editor_property('disallow_nanite',True)
            a.static_mesh_component.set_editor_property('ld_max_draw_distance',18000.)
        count+=1;return a
    # Asymmetric groups on the landward side leave the lagoon and island open.
    for cx,cy in [(-64200,-3300),(-62300,500),(-61500,5200),(-59400,8400),(-61000,10600)]:
        for i in range(4):
            x=cx+r.uniform(-650,650);y=cy+r.uniform(-550,550)
            plant('Fir' if i%3 else 'Tree',x,y,r.uniform(500,1050))
    # A modest perimeter grove rather than an evenly scattered artificial forest.
    for i in range(72):
        x=r.uniform(-79000,-70500);y=r.uniform(-20500,8000)
        plant('Fir',x,y,r.uniform(800,1600))
    # Low, terrain-conforming beds: irregular outlines, never raised coloured boxes.
    api['materials']['GardenSoil']=u.load_asset('/Game/Shiomori/Materials/M_CampusGardenSoil')
    bed_centres=[(-61300,-4000,1000,550),(-60700,-1700,850,650),(-60600,2300,900,500),
                 (-59100,4300,650,480),(-59600,7200,650,450),(-57100,8000,700,430)]
    for cx,cy,rx,ry in bed_centres:
        # Concentric 1 m rings follow the terrain without floating flat discs.
        rings=max(2,math.ceil(max(rx,ry)/100));segments=56
        def point(rad,i):
            a=i*math.tau/segments;w=1+.1*math.sin(a*3+cx)
            x=cx+rx*w*rad*math.cos(a);y=cy+ry*w*rad*math.sin(a)
            return x,y,ground(x,y)+3
        for j in range(rings):
            for i in range(segments):
                if j==0:face(('ParkBeds','GardenSoil'),[(cx,cy,ground(cx,cy)+3),point(1/rings,i),point(1/rings,i+1)])
                else:face(('ParkBeds','GardenSoil'),[point(j/rings,i),point((j+1)/rings,i),point((j+1)/rings,i+1),point(j/rings,i+1)])
        for i in range(180):
            a=r.uniform(0,math.tau);d=math.sqrt(r.random())*.9;x=cx+rx*d*math.cos(a);y=cy+ry*d*math.sin(a)
            plant('Flower' if i%6 else 'Fern',x,y,r.uniform(32,52) if i%6 else r.uniform(40,65))
        for i in range(7):
            a=r.uniform(0,math.tau);x=cx+rx*math.cos(a);y=cy+ry*math.sin(a)
            plant('Rock',x,y,r.uniform(25,65),ground(x,y)-12)
    # Natural understorey between the beds; keep maintenance and walking space.
    for i in range(240):
        x=r.uniform(-65000,-57000);y=r.uniform(-5000,10500)
        plant('Grass' if i%3 else 'Fern',x,y,r.uniform(18,48))
    # Selected boulders sit partly in the ground and frame, rather than block, the gate.
    for x,y,h in [(-58400,1700,110),(-58500,3100,75),(-55100,4400,90),(-59700,6300,105),(-57400,7600,70)]:
        plant('Rock',x,y,h,ground(x,y)-h*.24)
    # Low planted pockets on the old forecourt, using the actual terrace height.
    for cx,cy in [(-65000,-9200),(-62200,-8200),(-54000,-11100)]:
        for i in range(24):plant('Flower',cx+r.uniform(-520,520),cy+r.uniform(-200,200),r.uniform(25,40),api['HOTEL_RISE']+190 if 'HOTEL_RISE' in api else 940)
    u.log('CAMPUS_PARK_READY scanned_plants=%d gate_clear_radius_cm=1300 sight_corridor_cm=2800'%count)
