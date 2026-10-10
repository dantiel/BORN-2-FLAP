"""Repeatable adapted hotel campus. Centimetres; west=-X, sea=+Y.
Generated assets are persistent; geometry is batched by material and zone.
The founder is a bronze figure with tricorn, long hair and a kestrel
familiar cast from the authored bird mesh; no real name is used.
"""
import math
import random
from collections import defaultdict

# Architectural drafting coordinates stay stable; translate the whole property
# onto the west headland only once, after construction. Separate elevations
# keep the inland hangar low while the hotel occupies a supported terrace.
SITE_OFFSET = (-6000., 21000.)
HOTEL_RISE = 750.
FIELD_RISE = 180.
HOTEL_CENTER = (-59500., -13700.)
HOTEL_YAW = -18.
FLOORS = 12
FLOOR_HEIGHT = 330.
BAY_WIDTH = 420.
BALCONY_DEPTH = 280.
ISLAND = (-45200., 18200.)
HANGAR_CLEAR_DOOR = (1600., 600.)
EXPERIMENT_SPAN = 600.
TAG = 'ShiomoriCampusV2'


def install(scope):
    import unreal as u
    rng = random.Random(101026)
    prior_actors={a.get_path_name() for a in u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors()}
    batches = defaultdict(lambda: ([], []))
    materials = {}
    specs = {
        'Concrete': ((.57,.55,.48),.88,0),
        'Tile': ((.69,.65,.54),.72,0),
        'Panel': ((.20,.32,.32),.65,.12),
        'Dark': ((.035,.055,.061),.28,.3),
        'Steel': ((.25,.29,.29),.47,.65),
        'Roof': ((.23,.25,.24),.78,.1),
        'Wood': ((.21,.12,.067),.85,0),
        'Bronze': ((.22,.155,.072),.42,.8),
        'Stone': ((.25,.27,.25),.92,0),
        'Vermilion': ((.37,.07,.028),.7,0),
        'Grass': ((.16,.21,.105),.95,0),
        'Paving': ((.34,.33,.29),.91,0),
        'Stain': ((.31,.30,.245),.96,0),
        'Membrane': ((.73,.70,.56),.62,0),
    }
    for key,(color,rough,metal) in specs.items():
        materials[key] = scope['pbr_material']('Campus'+key,color=color,rough=rough,metallic=metal,noise_var=.012 if key not in ('Dark','Steel') else 0)

    def smooth(a,b,x):
        t=max(0,min(1,(x-a)/(b-a)));return t*t*(3-2*t)
    def ground(x,y):
        x+=SITE_OFFSET[0];y+=SITE_OFFSET[1]
        if y < -3000:return 150.
        shore=scope['waterline'](x);d=y-shore
        dune=max(20,40+50*math.sin(x*.00029+1.2)+30*math.sin(x*.00091+4.1)+15*math.sin(x*.0023+.7)+8*math.sin(x*.0047+2.9))
        base=dune if d<=-2500 else dune*(-d/2500)-50*((d+2500)/2500)**2 if d<0 else max(-1750,-50-.04*d)
        if y < -1200:base=max(0,min(150,math.ceil((-y-1200)/100)*25))
        inner=54000-3500*math.exp(-((y-26000)/14000)**2)
        rise=smooth(inner-5000,inner+14000,abs(x));tip=1-smooth(40000,68000,y)
        hill=base+(-50+700+220*math.sin(x*.000065+y*.00011)-base)*rise*tip
        return max(base-6,(base-6)+(hill-(base-6))*smooth(-3000,5000,y))

    materials['Headland']=u.load_asset('/Game/Shiomori/Materials/M_Headland')
    rises={key:HOTEL_RISE for key in ('Hotel','Lab','Net','Statue','HotelGround','ForecourtGarden')}
    rises['LaunchGround']=FIELD_RISE

    def transform(p, center=(0,0,0), yaw=0):
        a=math.radians(yaw);x,y,z=p
        return (center[0]+x*math.cos(a)-y*math.sin(a),center[1]+x*math.sin(a)+y*math.cos(a),center[2]+z)

    def face(key, pts):
        vs,ts=batches[key];i=len(vs);vs.extend((x,y,z+rises.get(key[0],0)) for x,y,z in pts)
        ts.extend((i,i+j,i+j+1) for j in range(1,len(pts)-1))

    def prism(zone,mat,poly,z,h,center=(0,0,0),yaw=0):
        # Normalise all footprints, including mirrored aircraft membranes.
        if sum(poly[i][0]*poly[(i+1)%len(poly)][1]-poly[(i+1)%len(poly)][0]*poly[i][1] for i in range(len(poly)))<0:
            poly=poly[::-1]
        key=(zone,mat);n=len(poly)
        low=[transform((x,y,z),center,yaw) for x,y in poly]
        high=[transform((x,y,z+h),center,yaw) for x,y in poly]
        face(key,low[::-1]);face(key,high)
        for i in range(n):
            j=(i+1)%n;face(key,[low[i],low[j],high[j],high[i]])

    def box(zone,mat,p,size,yaw=0):
        x,y,z=p;sx,sy,sz=size
        prism(zone,mat,[(-sx/2,-sy/2),(sx/2,-sy/2),(sx/2,sy/2),(-sx/2,sy/2)],-sz/2,sz,p,yaw)

    def beam(zone,mat,a,b,width,depth=None):
        # Arbitrary 3D rectangular beam with stable perpendicular basis.
        v=[b[i]-a[i] for i in range(3)];length=math.sqrt(sum(t*t for t in v))
        if length<.001:return
        d=[t/length for t in v];ref=(0,0,1) if abs(d[2])<.95 else (0,1,0)
        side=[d[1]*ref[2]-d[2]*ref[1],d[2]*ref[0]-d[0]*ref[2],d[0]*ref[1]-d[1]*ref[0]]
        sl=math.sqrt(sum(t*t for t in side));side=[t/sl for t in side]
        up=[side[1]*d[2]-side[2]*d[1],side[2]*d[0]-side[0]*d[2],side[0]*d[1]-side[1]*d[0]]
        ends=[]
        for p in (a,b):
            ends.append([tuple(p[i]+s*side[i]*width/2+t*up[i]*(depth or width)/2 for i in range(3)) for s,t in ((-1,-1),(1,-1),(1,1),(-1,1))])
        face((zone,mat),ends[0]);face((zone,mat),ends[1][::-1])
        for i in range(4):j=(i+1)%4;face((zone,mat),[ends[1][i],ends[1][j],ends[0][j],ends[0][i]])

    def text(label,content,p,size=35,yaw=90):
        rise=FIELD_RISE if label=='launch field' else HOTEL_RISE if label in ('university sign','retained hotel sign','founder plaque') else 0
        actor=u.EditorLevelLibrary.spawn_actor_from_class(u.TextRenderActor,u.Vector(p[0],p[1],p[2]+rise),u.Rotator(yaw=yaw))
        actor.set_actor_label('Campus '+label);actor.tags=[TAG,'CampusText']
        c=actor.text_render;c.set_text(content);c.set_world_size(size)
        c.set_text_render_color(u.Color(215,216,193,255))
        return actor

    hc=(*HOTEL_CENTER,170.)
    def hp(x,y,z):return transform((x,y,z),hc,HOTEL_YAW)
    def hb(mat,p,size):box('Hotel',mat,hp(*p),size,HOTEL_YAW)

    # Twelve structural floor plates. Their decreasing length makes the wedge;
    # a chamfered east end carries the balcony bands around the corner.
    for floor in range(FLOORS):
        z=floor*FLOOR_HEIGHT
        end=6000-max(0,floor-2)*480
        poly=[(-6000,-1250),(end-650,-1250),(end,-600),(end,600),(end-650,1250),(-6000,1250)]
        prism('Hotel','Concrete',poly,z,25,hc,HOTEL_YAW)
        # A genuine room block set back from balcony edge; the top restaurant
        # and ground ballroom instead have open, visible interior volumes.
        if floor not in (0,10):
            front=1250-BALCONY_DEPTH
            side=end-BALCONY_DEPTH
            chamfer=end+600-BALCONY_DEPTH*math.sqrt(2)
            inner=[(-5750,-front),(chamfer-front,-front),(side,-(chamfer-side)),(side,chamfer-side),(chamfer-front,front),(-5750,front)]
            prism('Hotel','Tile',inner,z+25,FLOOR_HEIGHT-25,hc,HOTEL_YAW)
        else:
            hb('Tile',((-6000+end)/2,-1120,z+165),(end+6000,130,280))
            hb('Tile',(-5920,0,z+165),(160,2400,280))
        # Close the blunt rear and west elevations with real walls, including
        # the gap behind the recessed room core. Keep the bevel unobstructed.
        hb('Tile',((-6000+end-650)/2,-1185,z+175),(end+5350,130,300))
        hb('Tile',(-5950,0,z+175),(100,2500,300))
        # Rear elevation: narrow repeated windows, plain service wall.
        for x in range(-5400,int(end-900),850):
            hb('Dark',(x,-1254,z+175),(460,12,135))
        # The occupied front and bevel edges: floor slab, deep returns,
        # recessed doors, parapet infill and continuous handrail.
        edges=[((-6000,1250),(end-650,1250)),((end-650,1250),(end,600)),((end,600),(end,-600)),((end,-600),(end-650,-1250))]
        for a,b in edges:
            dx,dy=b[0]-a[0],b[1]-a[1];length=math.hypot(dx,dy)
            # These edges run clockwise; right normal points into building.
            nx,ny=dy/length,-dx/length
            count=max(1,round(length/BAY_WIDTH));step=length/count
            ang=math.degrees(math.atan2(dy,dx))+HOTEL_YAW
            for j in range(count):
                t=(j+.5)/count;px=a[0]+dx*t;py=a[1]+dy*t
                q=hp(px,py,z+88)
                box('Hotel','Panel',q,(step-24,16,100),ang)
                # thick party wall along depth, leaving the full bay cavity
                ex=a[0]+dx*j/count;ey=a[1]+dy*j/count
                aa=hp(ex,ey,z+163);bb=hp(ex+nx*BALCONY_DEPTH,ey+ny*BALCONY_DEPTH,z+163)
                beam('Hotel','Concrete',aa,bb,20,276)
                if floor not in (0,10):
                    dp=hp(px+nx*(BALCONY_DEPTH-5),py+ny*(BALCONY_DEPTH-5),z+150)
                    box('Hotel','Dark',dp,(step-65,10,234),ang)
                    beam('Hotel','Steel',hp(px+nx*(BALCONY_DEPTH-5),py+ny*(BALCONY_DEPTH-5),z+32),hp(px+nx*(BALCONY_DEPTH-5),py+ny*(BALCONY_DEPTH-5),z+267),4)
                else:
                    beam('Hotel','Steel',hp(px,py,z+25),hp(px,py,z+325),9)
                if (j+floor)%11==0:
                    # Selective narrow drainage/salt streak beneath joints.
                    box('Hotel','Stain',hp(ex+22,ey,z+65),(10,4,92),ang)
            beam('Hotel','Steel',hp(a[0],a[1],z+148),hp(b[0],b[1],z+148),6)
        # Every stepped-back floor leaves a usable roof terrace.
        if floor>2:
            hb('Roof',(end+240,0,z+28),(420,1900,5))
    top_end=6000-(FLOORS-3)*480
    prism('Hotel','Concrete',[(-6000,-1250),(top_end-650,-1250),(top_end,-600),(top_end,600),(top_end-650,1250),(-6000,1250)],FLOORS*FLOOR_HEIGHT,30,hc,HOTEL_YAW)
    # Exposed rear service stair/core, rather than a glamorous glass atrium.
    hb('Concrete',(-3700,-1430,1980),(820,420,3960))
    for z in range(300,3800,330):hb('Dark',(-3700,-1644,z),(370,8,90))
    # Heavy old hotel canopy and modest new institutional sign.
    hb('Concrete',(-600,2150,470),(4200,1800,80))
    for x in (-2400,1200):hb('Tile',(x,2720,220),(90,90,440))
    hb('Panel',(-600,3060,465),(2800,16,95))
    text('university sign','SHIOMORI  /  ORNITHOPTER UNIVERSITY',hp(-1860,3075,445),42,HOTEL_YAW+90)
    text('retained hotel sign','SHIOMORI SEASIDE  1988',hp(-5300,1268,3650),48,HOTEL_YAW+90)
    for x in (-5350,-4100):hb('Steel',(x,1300,3650),(12,12,280))
    for z in (3550,3770):hb('Steel',(-4725,1300,z),(1300,12,12))

    # Experimental study models. Labelled prototypes because no large complete
    # aircraft asset exists; scaled to a 6 m span for hangar route validation.
    def aircraft(zone,p,span=600,yaw=0,mat='Membrane'):
        def ap(q):return transform(q,p,yaw)
        beam(zone,'Steel',ap((0,-span*.18,0)),ap((0,span*.28,0)),12)
        for s in (-1,1):
            pts=[(0,span*.03,0),(s*span*.22,span*.08,span*.025),(s*span*.5,-span*.05,0),(s*span*.31,-span*.16,-span*.012),(0,-span*.10,0)]
            # Thin closed membrane, readable from both above and below.
            prism(zone,mat,[(q[0],q[1]) for q in pts] if s==1 else [(q[0],q[1]) for q in pts][::-1],-2,4,p,yaw)
            for j in (1,2,3):beam(zone,'Steel',ap((0,0,0)),ap(pts[j]),4)
        prism(zone,mat,[(-span*.12,-span*.30),(span*.12,-span*.30),(0,-span*.16)],0,4,p,yaw)
    # Former panoramic restaurant: suspended prototypes and long lab benches.
    for x in (-4700,-2400,-200):
        aircraft('Lab',hp(x,100,10*330+180),420,HOTEL_YAW)
        beam('Lab','Steel',hp(x,100,10*330+185),hp(x,100,11*330),3)
        hb('Wood',(x,650,10*330+80),(1100,100,10))
    for x in (-4200,-2500,1000):
        hb('Wood',(x,300,85),(650,160,12))
        for dx in (-280,280):hb('Steel',(x+dx,300,42),(12,130,84))
    aircraft('Lab',hp(-400,0,195),600,HOTEL_YAW)
    # Selected balcony research rigs, sparse enough to retain hotel repetition.
    for x,z in ((-4700,680),(-1800,1670),(-3400,2660)):
        aircraft('Lab',hp(x,1090,z+170),150,HOTEL_YAW)
        hb('Steel',(x,1130,z+90),(8,8,180))
    # Modest roof net testing cage (actual thin geometry, not opaque walls).
    rz=FLOORS*FLOOR_HEIGHT+35
    for x in (-5200,-3000):
        for y in (-650,650):hb('Steel',(x,y,rz+170),(12,12,340))
    for x in range(-5200,-2999,100):
        beam('Net','Steel',hp(x,-650,rz+340),hp(x,650,rz+340),1.5)
    for y in range(-650,651,100):beam('Net','Steel',hp(-5200,y,rz+340),hp(-3000,y,rz+340),1.5)
    for z in range(int(rz),int(rz+341),85):
        for y in (-650,650):beam('Net','Steel',hp(-5200,y,z),hp(-3000,y,z),1.5)
    for x in range(-5200,-2999,100):
        for y in (-650,650):beam('Net','Steel',hp(x,y,rz),hp(x,y,rz+340),1.5)

    # Independent inherited property: restrained lawns/paving, a forecourt,
    # inland hangar and a short service connection to the road's west end.
    box('HotelGround','Paving',(-59300,-12600,158),(17100,10800,16))
    box('LaunchGround','Grass',(-53500,-19300,162),(4800,4300,16))
    box('HangarGround','Paving',(-60000,-23100,160),(11700,4200,20))
    def route(points,width,mat='Paving',zone='Ground'):
        for a,b in zip(points,points[1:]):
            dx,dy=b[0]-a[0],b[1]-a[1]
            beam(zone,mat,(a[0],a[1],a[2]-12),(b[0],b[1],b[2]-12),width,24)
            if zone=='Ground':
                length=math.hypot(dx,dy);nx=-dy/length;ny=dx/length
                steps=max(1,math.ceil(length/500))
                for j in range(steps):
                    for side in (-1,1):
                        def edge(t):
                            x=a[0]+dx*t;y=a[1]+dy*t;z=a[2]+(b[2]-a[2])*t
                            return (x+nx*side*width/2,y+ny*side*width/2,z-24),(x+nx*side*(width/2+450),y+ny*side*(width/2+450))
                        aa,oa=edge(j/steps);bb,ob=edge((j+1)/steps)
                        pts=[aa,bb,(ob[0],ob[1],ground(*ob)-4),(oa[0],oa[1],ground(*oa)-4)]
                        face(('Ground','Headland'),pts if side<0 else pts[::-1])

    # Switchback service ramp reaches the hotel's rear terrace from inland.
    # (The old road-access dogleg is removed: the campus now sits on the
    # isolated west-headland peninsula, well clear of the coastal road.)
    route([(-61500,-20700,175),(-66000,-20500,190),(-69000,-18100,520),(-68300,-16100,910),(-62500,-16700,920)],500)
    # Solid retaining terrace and a ground-hugging apron of headland material;
    # this prevents a floating platform after moving onto the sloping land.
    box('Terrace','Stone',(-59300,-12600,450),(17100,10800,900))
    inner=[(-67850,-18000),(-50750,-18000),(-50750,-7200),(-67850,-7200)]
    outer=[(-69500,-19200),(-49600,-19200),(-49600,-5300),(-69500,-5300)]
    for k in range(4):
        j=(k+1)%4
        for n in range(24):
            def lerp(a,b,t):return (a[0]+(b[0]-a[0])*t,a[1]+(b[1]-a[1])*t)
            ia=lerp(inner[k],inner[j],n/24);ib=lerp(inner[k],inner[j],(n+1)/24)
            oa=lerp(outer[k],outer[j],n/24);ob=lerp(outer[k],outer[j],(n+1)/24)
            face(('Terrace','Headland'),[(ia[0],ia[1],900),(oa[0],oa[1],ground(*oa)+4),(ob[0],ob[1],ground(*ob)+4),(ib[0],ib[1],900)])
    # The separate launch lawn is a low supported pad, not a floating carpet.
    box('Terrace','Headland',(-53500,-19300,230),(4800,4300,220))
    # Low perimeter stone walls leave entrance/handling gaps.
    for a,b in (((-67500,-7400,185),(-61500,-7400,185)),((-67500,-7400,185),(-67500,-16000,185))):
        beam('ForecourtGarden','Stone',(a[0],a[1],a[2]+35),(b[0],b[1],b[2]+35),65,80)

    # Open hangar: 26 x 22 m, clear 16 x 6 m doorway, two 6 m span bays
    # separated by an 8 m central handling aisle. No invisible solid body.
    hx,hy=-61500,-21900
    for x in (hx-1300,hx+1300):box('Hangar','Tile',(x,hy,490),(35,2200,640))
    box('Hangar','Tile',(hx,hy-1100,490),(2600,35,640))
    for dx in (-1050,1050):box('Hangar','Panel',(hx+dx,hy+1110,460),(480,30,580))
    box('Hangar','Steel',(hx,hy+1110,800),(2640,50,70))
    # Corrugated roof slopes with two glazed daylight strips. The inherited
    # workshop stays readable in daytime without implausibly powerful lamps.
    materials['RoofGlass']=u.load_asset('/Game/Shiomori/Materials/M_Glass')
    for s in (-1,1):
        for ya,yb,mat in ((-1180,-650,'Roof'),(-650,-250,'RoofGlass'),(-250,200,'Roof'),(200,600,'RoofGlass'),(600,1180,'Roof')):
            aa=(hx,hy+ya,1040);bb=(hx+s*1420,hy+ya,815)
            cc=(hx+s*1420,hy+yb,815);dd=(hx,hy+yb,1040)
            face(('Hangar',mat),[aa,bb,cc,dd] if s>0 else [dd,cc,bb,aa])
            face(('Hangar',mat),[(x,y,z-18) for x,y,z in ([dd,cc,bb,aa] if s>0 else [aa,bb,cc,dd])])
            if mat=='RoofGlass':
                for yy in (ya,yb):beam('Hangar','Steel',(hx,hy+yy,1045),(hx+s*1420,hy+yy,820),10)
    for y in (hy-1000,hy,hy+1000):
        for s in (-1,1):
            beam('Hangar','Steel',(hx+s*1260,y,170),(hx+s*1260,y,800),18)
            beam('Hangar','Steel',(hx+s*1260,y,800),(hx,y,1000),16)
    # Corrugation and parked sliding doors, visually legible from above/front.
    for y in range(hy-1050,hy+1051,90):
        for s in (-1,1):box('Hangar','Steel',(hx+s*1321,y,490),(5,8,620))
    box('Hangar','Steel',(hx,hy+1150,785),(3500,18,20))
    for dx in (-1150,1150):box('Hangar','Panel',(hx+dx,hy+1165,445),(670,18,550))
    for dx in (-750,750):aircraft('HangarProps',(hx+dx,hy+150,285),EXPERIMENT_SPAN)
    for dx in (-900,0,900):
        box('HangarProps','Wood',(hx+dx,hy-880,255),(600,150,12))
        for sx in (-240,240):box('HangarProps','Steel',(hx+dx+sx,hy-880,210),(12,130,90))
    for z in (300,420,540):beam('HangarProps','Steel',(hx-1120,hy-750,z),(hx-1120,hy+350,z),12)
    text('hangar ID','FLIGHT WORKSHOP  /  02', (hx-950,hy+1190,730),48)
    text('prototype bays','EXPERIMENTAL / 6 m SPAN', (hx-600,hy-1070,510),32)
    route([(hx,hy+1080,175),(hx,-18600,220),(-54800,-17800,175+FIELD_RISE)],1000)
    text('launch field','FLIGHT TEST FIELD',(-55300,-16950,190),40)
    for x in (-55000,-52000):box('LaunchGround','Tile',(x,-18000,173),(45,1400,3))

    # Forecourt fountain with provisional bronze founder, facing the sea.
    sx,sy=hp(-600,4700,0)[:2]
    ring=[(math.cos(i*math.tau/40)*530,math.sin(i*math.tau/40)*530) for i in range(40)]
    prism('Statue','Stone',ring,165,35,(sx,sy,0))
    for i in range(40):
        a=i*math.tau/40;b=(i+1)*math.tau/40
        beam('Statue','Tile',(sx+510*math.cos(a),sy+510*math.sin(a),208),(sx+510*math.cos(b),sy+510*math.sin(b),208),40,55)
    box('Statue','Stone',(sx,sy,255),(190,180,120))
    def limb(a,b,w):beam('Statue','Bronze',(sx+a[0],sy+a[1],315+a[2]),(sx+b[0],sy+b[1],315+b[2]),w)
    # Posed legs, jacket torso, lifted arm, controller arm; shaped head/shoulders.
    limb((-25,0,5),(-24,3,100),26);limb((25,-7,5),(16,0,100),26)
    prism('Statue','Bronze',[(-36,-20),(36,-20),(43,20),(-43,20)],405,96,(sx,sy,0))
    # Smooth ellipsoid head rather than a cube; facial likeness intentionally absent.
    for j in range(10):
        t0=-math.pi/2+j*math.pi/10;t1=t0+math.pi/10
        for i in range(20):
            aa=i*math.tau/20;bb=aa+math.tau/20
            face(('Statue','Bronze'),[(sx+22*math.cos(t)*math.cos(a),sy+23*math.cos(t)*math.sin(a),534+31*math.sin(t)) for t,a in ((t0,aa),(t0,bb),(t1,bb),(t1,aa))])
    limb((-39,0,175),(-70,5,218),24);limb((-70,5,218),(-94,12,282),20)
    limb((39,0,173),(63,20,127),24);limb((63,20,127),(35,58,135),20)
    box('Statue','Bronze',(sx+23,sy+64,450),(56,30,25))
    for dx in (-12,12):beam('Statue','Bronze',(sx+23+dx,sy+65,461),(sx+23+dx,sy+65,476),4)
    beam('Statue','Bronze',(sx-15,sy+20,504),(sx+5,sy+63,450),5)
    beam('Statue','Bronze',(sx+20,sy+20,504),(sx+40,sy+63,450),5)
    # Bronze kestrel perched on the founder's raised hand, cast from the
    # authored bird mesh (SM_KestrelFuselage) so the familiar reads as a bird.
    kestrel = u.load_asset('/Game/Birds/SM_KestrelFuselage')
    if kestrel:
        kb = kestrel.get_bounds()
        ks = 36.0 / max(kb.box_extent.x * 2.0, 1.0)
        ka = u.EditorLevelLibrary.spawn_actor_from_class(
            u.StaticMeshActor, u.Vector(sx - 94, sy + 12, 620), u.Rotator(pitch=0, yaw=90, roll=0))
        ka.set_actor_label('Campus founder kestrel')
        ka.set_editor_property('tags', [TAG, 'CampusKestrel'])
        kc = ka.static_mesh_component
        kc.set_static_mesh(kestrel)
        kc.set_material(0, materials['Bronze'])
        kc.set_collision_profile_name('NoCollision')
        ka.set_actor_scale3d(u.Vector(ks, ks, ks))
    text('founder plaque','OBI-WAN DA VINCI ANAKIN CHRONISTER RODRIGUEZ\nFOUNDER  /  ORNITHOPTER PIONEER',(sx-84,sy+92,278),12)

    # Inherited resort planting beds break up the forecourt without filling
    # the flight field or turning the campus into a forest.
    for i,(x,y) in enumerate(((-65000,-9200),(-62200,-8200),(-54000,-11100))):
        box('ForecourtGarden','Grass',(x,y,177),(1300,600,18))
        for dy in (-320,320):box('ForecourtGarden','Stone',(x,y+dy,188),(1400,35,35))
        for dx in (-700,700):box('ForecourtGarden','Stone',(x+dx,y,188),(35,640,35))
        tree=scope['foliage']('Campus garden tree',(x,y,180),scope['foliage_meshes']['Fir'][1],1000,ground_z=180+HOTEL_RISE)
        tree.tags=[TAG,'CampusPine']
    # Practical workshop/lab lighting makes the visible interiors readable.
    for p in ((hx-650,hy,650),(hx+650,hy,650),hp(-3200,300,10*330+270),hp(-1300,300,280)):
        lamp=u.EditorLevelLibrary.spawn_actor_from_class(u.PointLight,u.Vector(p[0],p[1],p[2]+(HOTEL_RISE if p[1]>-18000 else 0)),u.Rotator())
        lamp.set_actor_label('Campus workshop light');lamp.tags=[TAG,'CampusLighting']
        c=lamp.point_light_component;c.set_mobility(u.ComponentMobility.MOVABLE)
        c.set_editor_property("intensity_units",u.LightUnits.LUMENS)
        c.set_intensity(24000);c.set_attenuation_radius(1900)
        c.set_light_color(u.LinearColor(1,.91,.77));c.set_cast_shadows(False)

    # Mainland coastal garden: ground follows the existing runtime headland.
    path=[(-58500,-7200),(-58500,-5300),(-58400,-2000),(-57800,900),(-56500,3000),(-58000,6200)]
    # Dense path samples keep slopes grounded and continuous.
    for a,b in zip(path,path[1:]):
        points=[]
        for i in range(11):
            t=i/10;x=a[0]+(b[0]-a[0])*t;y=a[1]+(b[1]-a[1])*t
            points.append((x,y,(914*(1-t)+(ground(*b)+14)*t) if a==path[0] else ground(x,y)+14))
        route(points,220,'Paving','ShorePath')
    for i,(x,y) in enumerate(((-59200,-5000),(-59300,-3500),(-59100,-1200),(-58500,1200),(-57700,2800),(-58300,4300),(-58500,6000))):
        z=ground(x,y)
        mesh=scope['foliage_meshes']['Fir'][i%3]
        a=scope['foliage']('Campus coastal pine',(x,y,z),mesh,800+(i%3)*120,ground_z=z)
        a.tags=[TAG,'CampusPine']
        if i in (1,3,5):
            box('Garden','Wood',(x+350,y,z+78),(250,100,10))
            for dx in (-100,100):box('Garden','Steel',(x+350+dx,y,z+38),(10,85,76))
            for dy in (-95,95):
                box('Garden','Wood',(x+350,y+dy,z+45),(250,35,10))
                for dx in (-100,100):box('Garden','Steel',(x+350+dx,y+dy,z+20),(10,25,40))
    # Bicycle rack at entrance, simple curved silhouette made of narrow segments.
    for x in range(-56500,-55300,180):
        for y in (-9600,-9520):beam('ForecourtGarden','Steel',(x,y,170),(x,y,255),5)
        beam('ForecourtGarden','Steel',(x,-9600,255),(x,-9520,255),5)
    # Torii normal aims exactly at island. No bridge/causeway or lagoon fill.
    tx,ty=-56500.,3000.;tz=ground(tx,ty)+18
    sight=math.degrees(math.atan2(ISLAND[1]-(ty+SITE_OFFSET[1]),ISLAND[0]-(tx+SITE_OFFSET[0])));yaw=sight-90
    def tp(x,y,z):return transform((x,y,z),(tx,ty,tz),yaw)
    for s in (-1,1):
        beam('Shrine','Vermilion',tp(s*230,0,0),tp(s*205,0,400),30)
        box('Shrine','Stone',tp(s*230,0,12),(65,65,24),yaw)
    beam('Shrine','Vermilion',tp(-310,0,412),tp(310,0,412),42,32)
    beam('Shrine','Roof',tp(-330,0,435),tp(330,0,435),60,15)
    for s in (-1,1):beam('Shrine','Roof',tp(s*330,0,435),tp(s*365,0,450),60,15)
    beam('Shrine','Vermilion',tp(-255,0,312),tp(255,0,312),24)
    beam('Shrine','Wood',tp(0,0,315),tp(0,0,410),28)
    # Small older hokora, offset from framed island and rooted on mainland.
    qx,qy=-58000.,6200.;qz=ground(qx,qy)
    box('Shrine','Stone',(qx,qy,qz+38),(440,390,76))
    for i in range(4):box('Shrine','Stone',(qx,qy-325+i*55,qz+8+i*12),(220,60,16+i*24))
    box('Shrine','Wood',(qx,qy,qz+186),(260,210,220))
    box('Shrine','Dark',(qx,qy-108,qz+171),(125,5,160))
    for dx in (-78,78):box('Shrine','Wood',(qx+dx,qy-120,qz+175),(14,18,185))
    # True pitched, overhanging roof with a modest upturned eave, no pagoda.
    for s in (-1,1):
        face(('Shrine','Roof'),[(qx-185,qy,qz+375),(qx-185,qy+s*170,qz+283),(qx+185,qy+s*170,qz+283),(qx+185,qy,qz+375)][::-s])
        beam('Shrine','Roof',(qx-185,qy+s*170,qz+283),(qx+185,qy+s*170,qz+283),28)
    beam('Shrine','Roof',(qx-195,qy,qz+380),(qx+195,qy,qz+380),25)
    route([(tx,ty,tz),(qx,5000,ground(qx,5000)+14),(qx,qy-380,qz+14)],160,'Stone','ShorePath')

    # Save reusable material-zone meshes. Each actor is tightly scoped/tagged;
    # real triangle collision preserves balconies, hangar openings and paths.
    mesh_count=0;triangle_count=0
    for (zone,mat),(verts,tris) in batches.items():
        if not tris:continue
        # Keep coordinates local for mesh precision and sensible editor pivots.
        origin=tuple(sum(v[i] for v in verts)/len(verts) for i in range(3))
        local=[tuple(v[i]-origin[i] for i in range(3)) for v in verts]
        mesh=scope['build_mesh']('Campus_'+zone+'_'+mat,local,tris)
        body=mesh.get_editor_property('body_setup')
        body.set_editor_property('collision_trace_flag',u.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
        mesh.set_material(0,materials[mat]);u.EditorAssetLibrary.save_loaded_asset(mesh)
        actor=scope['place_mesh']('Campus '+zone+' / '+mat,origin,mesh,materials[mat],collision=zone not in ('Net','Lab','HangarProps'))
        actor.tags=[TAG,'Campus'+zone]
        if mat=='RoofGlass':actor.static_mesh_component.set_editor_property('cast_shadow',False)
        mesh_count+=1;triangle_count+=len(tris)
    for actor in u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors():
        if actor.get_path_name() in prior_actors or TAG not in [str(t) for t in actor.tags]:continue
        loc=actor.get_actor_location()
        actor.set_actor_location(u.Vector(loc.x+SITE_OFFSET[0],loc.y+SITE_OFFSET[1],loc.z),False,False)
    u.log('CAMPUS_READY floors=%d meshes=%d triangles=%d hotel=%s door_cm=%s prototype_span_cm=%s torii=%s'%(FLOORS,mesh_count,triangle_count,(HOTEL_CENTER[0]+SITE_OFFSET[0],HOTEL_CENTER[1]+SITE_OFFSET[1]),HANGAR_CLEAR_DOOR,EXPERIMENT_SPAN,(tx+SITE_OFFSET[0],ty+SITE_OFFSET[1],tz)))