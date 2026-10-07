"""Arrange licensed lava photoscans into irregular reefs and beach outcrops."""
import math
import random

def install(scope):
    import unreal as u
    meshes=[u.load_asset('/Game/Shiomori/LavaScans/SM_Scoria'),u.load_asset('/Game/Shiomori/LavaScans/SM_Lava')]
    assert all(meshes), 'Run fetch_lava_assets.py and import_lava_assets.py first'
    world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
    rng=random.Random(61934)
    rocks=[]
    def rock(x,y,z,size,tag,pitch=0,roll=0):
        mesh=meshes[0] if rng.random()<.78 else meshes[1]
        a=scope['place_mesh']('Lava outcrop',(x,y,z),mesh,mesh.get_material(0),rot=(pitch,rng.uniform(0,360),roll),collision=True)
        # Scans are normalized to 300 cm longest dimension; tilt exposes natural
        # jagged silhouettes and pores rather than repeating upright columns.
        scale=size/300
        a.set_actor_scale3d(u.Vector(scale*rng.uniform(.85,1.15),scale*rng.uniform(.8,1.15),scale))
        a.set_editor_property('tags',['ShiomoriVolcanic',tag])
        rocks.append(a)
        return a
    # A small coherent formation plus a few satellites, not sprawling clusters.
    # Principal rocks ~4-6 m, satellites 2-5 m, whole footprint ~25-45 m.
    centres=[(-39500,28500,650),(-36800,24800,520),(-42000,31600,420)]
    for cx,cy,span in centres:
        rock(cx,cy,-550,span,'Volcanic outcrop',rng.uniform(-25,25),rng.uniform(-25,25))
        for i in range(8):
            angle=rng.uniform(0,math.tau);r=span*math.sqrt(rng.random())*.9
            size=rng.uniform(200,480)
            rock(cx+math.cos(angle)*r,cy+math.sin(angle)*r,-rng.uniform(250,520),size,
                 'Volcanic outcrop',rng.uniform(-50,50),rng.uniform(-35,35))
    # The bay's western islet: a spacious, sculpted volcanic island rather than
    # a dot. A proud sea-stack peak faces the open swell; behind it a sheltered
    # lee terrace is the only pocket calm enough to hold a single tree. A broken
    # ring of stacks encloses the hollow with a seaward gap the surf breaks
    # through, so almost nothing else can take root out here.
    islet=(-45200,18200)
    ix,iy=islet
    def stack(ang,rad,size,top,pitch=0.0,roll=0.0):
        a=math.radians(ang)
        rock(ix+math.cos(a)*rad,iy+math.sin(a)*rad,top-size*0.5,size,
             'Volcanic islet',pitch,roll)
    # Proud peak (seaward, north): the wind-and-surf-battered stack.
    stack(90,2600,3200,980,rng.uniform(-12,12),rng.uniform(-12,12))
    # Sheltered lee terrace just behind the peak: the tree's calm pocket.
    stack(110,600,2400,420,rng.uniform(-6,6),rng.uniform(-6,6))
    # Broken enclosing ring, leaving the north sector open to the surf.
    for ang,rad,size,top in ((20,2400,2600,480),(340,2400,2500,500),
                             (300,2400,2300,430),(260,2400,2200,380),
                             (205,2400,2600,470),(165,2400,2200,350)):
        stack(ang,rad,size,top,rng.uniform(-20,20),rng.uniform(-18,18))
    # Lower lee ledges for the sparse contenders to cling to.
    stack(235,1500,1500,260,rng.uniform(-18,18),rng.uniform(-16,16))
    stack(290,1500,1300,220,rng.uniform(-18,18),rng.uniform(-16,16))
    # Outer sea stacks trailing toward the reef line, so the island reads as
    # one broken volcanic chain rather than an isolated lump.
    for i in range(9):
        t=rng.random()
        x=-47000+t*(-36800+47000);y=15000+t*(26500-15000)
        x+=rng.uniform(-800,800);y+=rng.uniform(-800,800)
        rock(x,y,-rng.uniform(340,560),rng.uniform(200,480),'Volcanic islet',rng.uniform(-45,45),rng.uniform(-30,30))
    # Wet rocks, shallow submerged fragments and dry rocks at both beach ends.
    for side in (-1,1):
        for group,offset in enumerate((-800,350,1850)):
            # Kept clear of the coast-test transects at X=±30000 so the beach
            # rocks never break the offshore-monotonic sand check.
            x=side*(31000+group*2200);y=scope['waterline'](x)+offset
            tag='Volcanic beach west' if side<0 else 'Volcanic beach east'
            for i in range(4):
                px=x+rng.uniform(-430,430);py=y+rng.uniform(-400,400)
                rock(px,py,scope['beach_height'](px,py)-rng.uniform(60,120),rng.uniform(150,400),tag,
                     rng.uniform(-35,35),rng.uniform(-35,35))
    # Submerged reef fragments ring the larger island, giving the refraction
    # something to reveal just below the surface around the new landmass.
    for i in range(28):
        angle=rng.uniform(0,math.tau);r=rng.uniform(3000,4400)
        x=ix+math.cos(angle)*r;y=iy+math.sin(angle)*r
        rock(x,y,-rng.uniform(300,460),rng.uniform(150,420),'Island shore rock',rng.uniform(-45,45),rng.uniform(-30,30))
    # The bay's eastern shoal: a low, flat counterpart to the western islet.
    # The west rises into a proud sea-stack; the east stays a broad wind-scrubbed
    # ledge — scattered low slabs, half-submerged rocks and sparse vegetation —
    # so the outer bay's east side no longer reads as bare water.
    eshoal=(45200,18200)
    sx,sy=eshoal
    east_ledges=[]
    def erock(ang,rad,size,top,pitch=0.0,roll=0.0,anchor=False):
        a=math.radians(ang)
        x=sx+math.cos(a)*rad;y=sy+math.sin(a)*rad
        rock(x,y,top-size*0.5,size,'Volcanic shoal east',pitch,roll)
        if anchor:east_ledges.append((x,y))
    # Broad central flat slabs: the calm, flat heart the sparse grass clings to.
    erock(0,300,1600,260,rng.uniform(-4,4),rng.uniform(-4,4),anchor=True)
    erock(180,350,1400,230,rng.uniform(-5,5),rng.uniform(-4,4),anchor=True)
    erock(300,700,1200,215,rng.uniform(-5,5),rng.uniform(-4,4),anchor=True)
    # Low broken ring — no stack tops ~2.5 m, keeping the shoal flat.
    for ang,rad,size,top in ((20,2500,700,160),(70,2500,850,180),
                             (120,2450,750,165),(160,2550,800,175),
                             (210,2500,650,150),(250,2500,820,185),
                             (330,2500,700,155)):
        erock(ang,rad,size,top,rng.uniform(-8,8),rng.uniform(-8,8))
    # Satellites trailing seaward, echoing the western chain but far lower.
    for i in range(6):
        t=rng.random()
        x=47000+t*(36800-47000);y=15000+t*(26500-15000)
        x+=rng.uniform(-700,700);y+=rng.uniform(-700,700)
        rock(x,y,-rng.uniform(220,340),rng.uniform(150,320),'Volcanic shoal east',
             rng.uniform(-30,30),rng.uniform(-25,25))
    # Shallow submerged ledges ring the shoal for the refraction to reveal.
    for i in range(14):
        ang=rng.uniform(0,math.tau);r=rng.uniform(2600,3800)
        x=sx+math.cos(ang)*r;y=sy+math.sin(ang)*r
        rock(x,y,-rng.uniform(260,400),rng.uniform(120,320),'Volcanic shoal east',
             rng.uniform(-35,35),rng.uniform(-25,25))
    def height(x,y):
        hit=u.SystemLibrary.line_trace_single(world,u.Vector(x,y,5000),u.Vector(x,y,-1000),
            u.TraceTypeQuery.TRACE_TYPE_QUERY1,True,[],u.DrawDebugTrace.NONE,True)
        if hit is None:return None
        if isinstance(hit,tuple):hit=next((v for v in hit if isinstance(v,u.HitResult)),None)
        if hit is None:return None
        parts=hit.to_tuple()
        return parts[5].z if parts[0] else None
    # Sparse island vegetation. Surf and wind scrub everything except one proud
    # tree in the sheltered lee terrace behind the peak; a few stunted
    # contenders cling to the lower ledges and a little grass holds in the
    # calmest pockets. Almost nothing else survives out here.
    grass=trees=0
    def plant_tree(x,y,hgt):
        nonlocal trees
        z=height(x,y)
        if z is None or z<=-40:return
        mesh=rng.choice(scope['foliage_meshes']['Fir'])
        if mesh is None:return
        a=scope['foliage']('Island tree',(x,y,0),mesh,hgt,rot=(0,rng.uniform(0,360),0),ground_z=z-6)
        a.set_editor_property('tags',['ShiomoriVolcanic','Island tree'])
        trees+=1
    def plant_grass(x,y):
        nonlocal grass
        h=height(x,y)
        if h is None or h<=-40:return
        mesh=rng.choice(scope['foliage_meshes']['Grass'])
        if mesh is None:return
        a=scope['foliage']('Island grass pocket',(x,y,0),mesh,rng.uniform(35,60),
                           rot=(0,rng.uniform(0,360),0),ground_z=h-2)
        a.set_editor_property('tags',['ShiomoriVolcanic','Island grass pocket'])
        grass+=1
    # The one proud medium tree, sheltered behind the seaward peak.
    plant_tree(ix-205,iy+564,700)
    # A few stunted contenders on the lower lee ledges.
    plant_tree(ix-860,iy-1229,340)
    plant_tree(ix+513,iy-1410,300)
    plant_tree(ix-2175,iy-1014,280)
    # Sparse grass in the calmest pockets only.
    for gx,gy in ((ix-205,iy+564),(ix-300,iy+300),(ix+200,iy+100),
                  (ix-417,iy-1900),(ix-860,iy-1229),(ix+513,iy-1410)):
        plant_grass(gx,gy)
    # Sparse eastern-shoal vegetation anchored to the flat slabs above, so the
    # plants root on rock instead of vanishing into the swell.
    def plant_shoal_tree(x,y,hgt):
        nonlocal trees
        z=height(x,y)
        if z is None or z<=-40:return
        mesh=rng.choice(scope['foliage_meshes']['Fir'])
        if mesh is None:return
        a=scope['foliage']('Shoal tree',(x,y,0),mesh,hgt,rot=(0,rng.uniform(0,360),0),ground_z=z-6)
        a.set_editor_property('tags',['ShiomoriVolcanic','Shoal tree'])
        trees+=1
    def plant_shoal_grass(x,y):
        nonlocal grass
        h=height(x,y)
        if h is None or h<=-40:return
        mesh=rng.choice(scope['foliage_meshes']['Grass'])
        if mesh is None:return
        a=scope['foliage']('Shoal grass pocket',(x,y,0),mesh,rng.uniform(35,60),
                           rot=(0,rng.uniform(0,360),0),ground_z=h-2)
        a.set_editor_property('tags',['ShiomoriVolcanic','Shoal grass pocket'])
        grass+=1
    for gx,gy in east_ledges:
        plant_shoal_grass(gx,gy)
    if len(east_ledges)>1:
        plant_shoal_tree(east_ledges[0][0],east_ledges[0][1]+60,260)
        plant_shoal_tree(east_ledges[2][0]-40,east_ledges[2][1],210)
    u.log('LAVA_REEFS_READY rocks=%d grass=%d trees=%d'%(len(rocks),grass,trees))