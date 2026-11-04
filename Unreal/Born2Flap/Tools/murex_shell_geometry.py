"""Engine-independent Murex airport geometry, in metres.

The building is one shell laid along +X. Its aperture faces the landside (-Y),
while six habitable spines reach the apron (+Y). Golden growth is an architectural
rule, not a claim that every natural Muricidae shell has these proportions.
"""
import math

PHI = (1 + math.sqrt(5)) / 2
GOLDEN_ANGLE = math.tau / (PHI * PHI)
MAIN = (-1280.0, -525.0)
AXIS_Y = -390.0
TOWER_BASE = (-1207.0, -363.0, 34.0)
TOWER_CAB = (-1218.0, -357.0, 111.0)
TOWER_DECK = (-1218.0, -357.0, 117.0)
GALLERY = (-1213.0, -362.0, 84.0)


def add(a, b):
    return tuple(x+y for x, y in zip(a, b))


def sub(a, b):
    return tuple(x-y for x, y in zip(a, b))


def scale(a, k):
    return tuple(x*k for x in a)


def cross(a, b):
    return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])


def unit(a):
    return scale(a, 1/max(1e-12, math.sqrt(sum(x*x for x in a))))


class Scene:
    def __init__(self):
        self.meshes = []
        self.instances = []
        self.gates = []
        self.landmarks = dict(tower_deck=TOWER_DECK, gallery=GALLERY,
                              entrance=(-1272.0, -543.0, .5), park=(-1280.0, -620.0, .3))

    def mesh(self, name, vertices, triangles, material, tags=(), collision=True):
        # Clipped ground profiles can contain collapsed triangles. Remove those
        # before Unreal cooks complex collision rather than generating bad faces.
        faces = [t for t in triangles if sum(v*v for v in cross(
            sub(vertices[t[1]], vertices[t[0]]), sub(vertices[t[2]], vertices[t[0]]))) > 1e-12]
        self.meshes.append(dict(name=name, vertices=vertices, triangles=faces,
                                material=material, tags=list(tags), collision=collision))

    def grid(self, name, rows, material, tags=(), closed=False, collision=True):
        n = len(rows[0])
        vs = [p for row in rows for p in row]
        ts = []
        for j in range(len(rows)-1):
            for i in range(n if closed else n-1):
                a, b, c, d = j*n+i, j*n+(i+1)%n, (j+1)*n+i, (j+1)*n+(i+1)%n
                ts.extend(((a, b, c), (b, d, c)))
        self.mesh(name, vs, ts, material, tags, collision)

    def tube(self, name, points, radii, material, tags=(), sides=12, collision=True):
        rows = []
        for i, (p, radius) in enumerate(zip(points, radii)):
            tangent = unit(sub(points[min(i+1, len(points)-1)], points[max(0, i-1)]))
            reference = (0, 0, 1) if abs(tangent[2]) < .9 else (0, 1, 0)
            a = unit(cross(tangent, reference))
            b = unit(cross(tangent, a))
            rows.append([add(p, add(scale(a, radius*math.cos(k*math.tau/sides)),
                                    scale(b, radius*math.sin(k*math.tau/sides)))) for k in range(sides)])
        self.grid(name, rows, material, tags, closed=True, collision=collision)
        for index in (0, -1):
            ring = rows[index]
            self.mesh(name+('Base' if index == 0 else 'Tip'), [points[index]]+ring,
                      [(0, k+1, (k+1)%sides+1) for k in range(sides)], material, tags, collision)

    def item(self, kind, p, dimensions, yaw=0, tags=()):
        self.instances.append(dict(kind=kind, position=p, size=dimensions, yaw=yaw, tags=list(tags)))

    def beam(self, kind, a, b, width=.16, height=None):
        delta = sub(b, a)
        self.instances.append(dict(kind=kind, position=scale(add(a, b), .5),
                                  size=(math.sqrt(sum(v*v for v in delta)), width, height or width),
                                  direction=delta, yaw=0, tags=[]))

    def polygon(self, name, points, material, tags=(), collision=True):
        center = tuple(sum(p[k] for p in points)/len(points) for k in range(3))
        self.mesh(name, [center]+points, [(0, 1+i, 1+(i+1)%len(points)) for i in range(len(points))],
                  material, tags, collision)


def mouth_point(v, angle, inner=False):
    rx = 154*(1-.77*v**1.5)+3*math.sin(math.pi*v)
    height = 60*(1-.85*v**1.3)
    # The heavier left lip and extended right shoulder make a resting aperture,
    # while the rear contracts into the coiled axial body.
    x = MAIN[0]+55*v+rx*math.cos(angle)+14*math.sin(angle)**2
    y = MAIN[1]+195*v+6*math.sin(angle)**2*(1-v)
    z = .8+height*math.sin(angle)**.88+4*math.sin(2*angle)*(1-v)
    return (x, y+(1 if inner else 0), z-(1.25 if inner else 0))


def axis_point(t, angle):
    x = -1240+690*t
    radius = 73*PHI**(-5.25*t)
    winding = angle - 5*math.tau*t
    radius *= 1+.095*math.cos(winding)+.026*math.cos(3*winding)
    return (x, AXIS_Y+radius*math.cos(angle), max(.55, 8+radius*.49*math.sin(angle)))


def airport_shell(scene):
    rows = [[mouth_point(j/40, i*math.pi/96) for i in range(97)] for j in range(41)]
    scene.grid('ApertureMainHallShell', rows, 'ShellIvory', ('MurexMainHall', 'MurexShell'))
    scene.grid('AperturePearlSoffit', [[mouth_point(j/40, i*math.pi/96, True) for i in range(97)]
                                    for j in range(10)], 'ShellPearl', ('MurexCanopy',))
    # Broad rolled lip, with a rounded thickened base instead of needle teeth.
    points = [mouth_point(0, i*math.pi/96) for i in range(97)]
    scene.tube('ApertureRolledLip', points, [2.4+2.6*abs(math.cos(i*math.pi/96))**3
                                         for i in range(97)], 'ShellPearl', ('MurexApertureLip',), 16)
    footprint = [mouth_point(j/40, 0) for j in range(41)] + [mouth_point(j/40, math.pi) for j in range(40, -1, -1)]
    scene.polygon('MainHallFloor', [(x, y, .35) for x, y, _ in footprint], 'WarmStone', ('MurexHallFloor',))
    # Recess the glazing 27 m behind the sheltering lip. The upper boundary
    # follows the actual arch, including its asymmetric shoulder.
    v = .14
    curve = [mouth_point(v, i*math.pi/80, True) for i in range(81)]
    glass_vertices, glass_faces = [], []
    entrance_x = MAIN[0]+55*v
    for a, b in zip(curve, curve[1:]):
        for z0 in range(0, 66, 6):
            za, zb = min(z0+6, a[2]), min(z0+6, b[2])
            if za <= z0 or zb <= z0:
                continue
            if z0 == 0 and abs((a[0]+b[0])/2-entrance_x) < 10:
                continue  # actual open entrances under the glass curtain wall
            k = len(glass_vertices)
            glass_vertices.extend(((a[0], a[1], max(.4, z0)), (b[0], b[1], max(.4, z0)),
                                   (a[0], a[1], za), (b[0], b[1], zb)))
            glass_faces.extend(((k, k+1, k+2), (k+1, k+3, k+2)))
    scene.mesh('RecessedApertureGlazing', glass_vertices, glass_faces, 'Glazing', ('MurexEntranceGlazing',))
    for a in curve[::2]:
        if a[2] > 1:
            scene.beam('Bronze', (a[0], a[1]-.1, .4), (a[0], a[1]-.1, a[2]), .18)
    for z in (6, 12, 18, 24, 30, 36, 42, 48, 54):
        for a, b in zip(curve, curve[1:]):
            if min(a[2], b[2]) > z:
                scene.beam('Bronze', (a[0], a[1]-.1, z), (b[0], b[1]-.1, z), .16)
    for j in (0, 3, 5, 8, 13, 21, 34):
        vv = j/40
        ps = [mouth_point(vv, i*math.pi/64) for i in range(65)]
        scene.tube('MainGrowthRib%02d'%j, ps, [.4+.23*(1-vv)]*65, 'ShellSand', sides=8)
    for level, width in ((8, 210), (16, 180), (24, 130)):
        scene.item('WarmStone', (-1262, -447, level), (width, 52, .55))
        scene.item('AmberSoft', (-1262, -474, level+.2), (width, .18, .22))
    # Rear body and canal: the long horizontal shell axis, with one continuous
    # winding whose radius decays geometrically towards the tip.
    scene.grid('HorizontalCoiledBody', [[axis_point(j/180, i*math.tau/64) for i in range(64)]
                                        for j in range(181)], 'ShellIvory', ('MurexShellAxis',), closed=True)
    for rib in range(3):
        ps = [axis_point(j/280, j/280*5*math.tau + rib*GOLDEN_ANGLE) for j in range(281)]
        ps = [(x, y, z+.35) for x, y, z in ps]
        scene.tube('AxialGrowthSeam%d'%rib, ps, [1.15*PHI**(-2*j/280)+.18 for j in range(281)],
                   'ShellSand', sides=8)
    # Nonfunctional spines are short, rounded nodules. None competes with ATC.
    for i in range(26):
        t = .07+.88*i/25
        angle = i*GOLDEN_ANGLE
        if math.sin(angle) < -.05:
            continue
        root = axis_point(t, angle)
        length = (8+7*(1-t))*PHI**(-.45*t)
        direction = unit((.45, math.cos(angle), max(.12, math.sin(angle))))
        ps = [add(root, scale(direction, length*j/12)) for j in range(13)]
        rs = [(4.5*(1-t)+1.0)*math.sqrt(max(.015, 1-(j/12)**1.5)) for j in range(13)]
        scene.tube('RoundedVarix%02d'%i, ps, rs, 'ShellIvory', ('MurexRoundedSpine',), 12)


def tower(scene):
    # One dominant upward spine rooted in the thick shoulder of the same shell.
    ps = []
    rs = []
    for i in range(37):
        t = i/36
        ps.append((-1207-11*t+3*math.sin(math.pi*t), -363+6*t, 31+80*t))
        rs.append(15*PHI**(-2.6*t)+1.5)
    scene.tube('DominantTowerSpine', ps, rs, 'ShellIvory', ('MurexTowerSpine',), 32)
    for k in range(5):
        line = []
        for i, (p, r) in enumerate(zip(ps, rs)):
            angle = k*GOLDEN_ANGLE+.35*i/36
            line.append(add(p, (r*math.cos(angle), r*math.sin(angle), 0)))
        scene.tube('TowerGrowthFlute%d'%k, line, [.28]*len(line), 'ShellSand', sides=8)
    for name, center, radius, height, mat, tags in (
        ('TowerCabGlazing', TOWER_CAB, 8.4, 5.4, 'Glazing', ('MurexTowerCab',)),
        ('TowerCabRoof', TOWER_DECK, 9.3, .75, 'ShellPearl', ('MurexTowerDeck',)),
        ('TowerGallery', GALLERY, 11.0, .65, 'WarmStone', ('MurexTowerGallery',))):
        scene.tube(name, [center, add(center, (0, 0, height))], [radius, radius], mat, tags, 32)
    for i in range(16):
        a = i*math.tau/16
        x, y = TOWER_CAB[0]+8.4*math.cos(a), TOWER_CAB[1]+8.4*math.sin(a)
        scene.item('Bronze', (x, y, 113.7), (.12, .12, 5.4))
    scene.item('Bronze', add(TOWER_DECK, (0, 0, 3)), (.16, .16, 5))
    scene.item('Red', add(TOWER_DECK, (0, 0, 5.7)), (.45, .45, .45))


def terminal_spines(scene):
    root_x = -1158.0
    for i in range(6):
        if i:
            root_x += 60+85*PHI**(-(i-1))
        t = (root_x+1240)/690
        radius = 73*PHI**(-5.25*t)
        start_y = AXIS_Y+.45*radius
        length = 62+100*PHI**(-.75*i)
        width = 7+19*PHI**(-.8*i)
        height = 5+12*PHI**(-.8*i)
        rows = []
        floor_left, floor_right = [], []
        for j in range(33):
            v = j/32
            x = root_x+length*.20*v*v
            y = start_y+length*v
            half = width*(.48*(1-v)**.8+.12)
            h = 3.8+(height-3.8)*(1-v)**.8
            rows.append([(x+half*math.cos(a*math.pi/24), y,
                          .55+h*math.sin(a*math.pi/24)) for a in range(25)])
            floor_left.append((x-half, y, .5))
            floor_right.append((x+half, y, .5))
        name = 'GateSpine%02d'%(i+1)
        scene.grid(name, rows, 'ShellIvory', ('MurexGateSpine', 'MurexGate%d'%(i+1)))
        scene.polygon(name+'Floor', floor_left+list(reversed(floor_right)), 'WarmStone')
        for j in (5, 13, 21, 32):
            scene.tube(name+'Rib%d'%j, rows[j], [.18]*25, 'ShellSand', sides=8)
        end = rows[-1]
        scene.polygon(name+'GlassEnd', [(end[0][0], end[0][1]+.08, .5)]+end+
                      [(end[-1][0], end[-1][1]+.08, .5)], 'Glazing', ('MurexGateGlazing',))
        # Continuous clerestory ribbons on both sides of each habitable spine.
        for side_index in (3, 21):
            ribbon = [[row[side_index], row[side_index+1]] for row in rows[5:]]
            scene.grid(name+'Windows%d'%side_index, [[(x, y, z+.06) for x,y,z in row] for row in ribbon], 'Glazing')
        gx = (end[0][0]+end[-1][0])/2
        gy = end[0][1]
        airplane_length = (62, 52, 44, 36, 29, 24)[i]
        airplane_span = (60, 46, 38, 31, 25, 22)[i]
        plane_center = (gx, gy+airplane_length/2+12, 0)
        scene.gates.append(dict(id=i+1, root=(root_x, start_y, .5), tip=(gx, gy, .5),
                                length=length, width=width, plane_center=plane_center,
                                plane_length=airplane_length, plane_span=airplane_span))
        scene.item('Concrete', (gx, gy+airplane_length*.5+12, .08), (airplane_span+14, airplane_length+16, .16))
        # A short, visibly connected enclosed bridge reaches the forward door.
        bridge_end = (gx-2.3, gy+12+airplane_length*.20, 4.1)
        scene.beam('WarmStone', (gx, gy-1, 4.1), bridge_end, 3.0, .5)
        scene.beam('Glazing', (gx, gy-1, 5.3), add(bridge_end, (0, 0, 1.2)), 2.8, 2.2)
        scene.beam('ShellPearl', (gx, gy-1, 6.6), add(bridge_end, (0, 0, 2.5)), 3.1, .24)
        for dy in range(0, int(airplane_length+18), 5):
            scene.item('YellowPaint', (gx, gy+dy, .20), (.16, 2.5, .025))
        scene.item('YellowPaint', (gx, gy+airplane_length*.75, .21), (7, .18, .025))
        aircraft(scene, i+1, plane_center, airplane_length, airplane_span)


def aircraft(scene, number, center, length, span):
    """Static aircraft in metres: nose toward -Y, with real transport proportions."""
    def world(p):
        x, y, z = p
        return (center[0]+y, center[1]-x, z)
    def put(name, vs, ts, material, tags=()):
        scene.mesh('Aircraft%02d%s'%(number, name), [world(v) for v in vs], ts, material,
                   ('MurexScaleAircraft', 'MurexAircraft%d'%number, *tags))
    body_radius = length*.031
    center_z = 1.5+body_radius
    rows = []
    for j in range(41):
        t = j/40
        radius = body_radius*max(.025, math.sin(math.pi*t)**.42)
        x = length*(t-.5)
        rows.append([(x, radius*math.cos(i*math.tau/24), center_z+radius*math.sin(i*math.tau/24)) for i in range(24)])
    vs = [p for row in rows for p in row]
    ts = []
    for j in range(40):
        for i in range(24):
            a,b,d,e = j*24+i,j*24+(i+1)%24,(j+1)*24+i,(j+1)*24+(i+1)%24
            ts.extend(((a,b,d),(b,e,d)))
    put('Fuselage', vs, ts, 'AircraftWhite')
    for side in (-1, 1):
        wing = [(length*.10, side*body_radius*.7, center_z), (-length*.13, side*span*.5, center_z+1.25),
                (-length*.20, side*span*.5, center_z+1.25), (-length*.16, side*body_radius, center_z)]
        wing_lower = [(x,y,z-.3) for x,y,z in wing]
        faces = [(0,1,2),(0,2,3),(4,6,5),(4,7,6),(0,4,5),(0,5,1),(1,5,6),(1,6,2),(2,6,7),(2,7,3)]
        put('Wing%d'%side, wing+wing_lower, faces, 'AircraftWhite')
        tail = [(-length*.34,side*body_radius*.5,center_z+.5),(-length*.45,side*span*.20,center_z+1),
                (-length*.49,side*span*.20,center_z+1),(-length*.48,side*body_radius*.5,center_z+.5)]
        put('Tailplane%d'%side,tail,[(0,1,2),(0,2,3)],'AircraftWhite')
        for j in range(max(5, int(length/.95))):
            x = -length*.30+j*(length*.63/max(5, int(length/.95)))
            r = body_radius*math.sin(math.pi*(x/length+.5))**.42
            y = side*r*.955
            z = center_z+r*.30
            put('Window%d_%d'%(side,j),[(x-.17,y,z-.21),(x+.17,y,z-.21),(x+.17,y,z+.21),(x-.17,y,z+.21)],
                [(0,1,2),(0,2,3)],'AircraftGlass')
        # Nacelles and dark intakes below the wing.
        ex,ey,ez = -.01*length,side*span*.23,center_z-1.0
        r = max(.48, length*.018)
        ps = [world((ex+offset,ey,ez)) for offset in (-r*1.6,0,r*1.7)]
        scene.tube('Aircraft%02dEngine%d'%(number,side),ps,[r*.8,r,r*.9],'AircraftWhite',('MurexScaleAircraft',),16)
        intake = [world((ex+r*1.72,ey+r*.72*math.cos(i*math.tau/20),ez+r*.72*math.sin(i*math.tau/20))) for i in range(20)]
        scene.polygon('Aircraft%02dIntake%d'%(number,side),intake,'AircraftGlass')
    fin=[(-length*.32,0,center_z+body_radius*.6),(-length*.42,0,center_z+length*.16),
         (-length*.49,0,center_z+length*.16),(-length*.49,0,center_z+body_radius*.4)]
    put('VerticalFin',fin,[(0,1,2),(0,2,3)],'AircraftGold')
    for x,y in ((length*.29,0),(-length*.13,-body_radius*1.6),(-length*.13,body_radius*1.6)):
        p=world((x,y,.60))
        scene.item('Wheel',p,(1.1,.55,1.1),90)
        scene.item('Bronze',world((x,y,1.3)),(.18,.18,1.4))
    # Dark cockpit visor with a sloped top visible from above/front.
    for side in (-1,1):
        put('Cockpit%d'%side,[(length*.40,side*body_radius*.65,center_z+.2),
                             (length*.32,side*body_radius*.90,center_z+.5),
                             (length*.32,side*body_radius*.62,center_z+body_radius*.75),
                             (length*.40,side*body_radius*.34,center_z+body_radius*.50)],
            [(0,1,2),(0,2,3)],'AircraftGlass')


def entrance_park(scene):
    cx,cy=-1285.,-610.
    boundary=[(cx+(208+7*math.sin(3*a))*math.cos(a), cy+(85+5*math.sin(2*a))*math.sin(a), .1)
              for a in [i*math.tau/96 for i in range(96)]]
    scene.polygon('EntranceParkGround',boundary,'ParkGrass',('MurexEntrancePark',))
    # A curved drop-off at the park perimeter leaves the central gardens intact.
    road=[]
    for i in range(97):
        a=i*math.tau/96
        road.append([(cx+r*math.cos(a),cy+(r*.43)*math.sin(a),.18) for r in (193,201)])
    scene.grid('ParkDropoffLoop',road,'Asphalt',('MurexParkAccess',))
    path=[]
    for i in range(65):
        t=i/64
        y=-688+191*t
        x=-1272-22*math.sin(math.pi*t)
        width=8+2*math.sin(math.pi*t)
        path.append([(x-width,y,.28),(x+width,y,.28)])
    scene.grid('AperturePromenade',path,'WarmStone',('MurexEntranceWalk',))
    for sign in (-1,1):
        walk=[]
        for i in range(57):
            a=-math.pi*.55+math.pi*1.12*i/56
            x=cx+sign*(65+48*math.cos(a))
            y=cy+45*math.sin(a)
            walk.append([(x-2.5,y,.24),(x+2.5,y,.24)])
        scene.grid('GardenWalk%d'%sign,walk,'WarmStone')
    # Golden-angle planting, filtered against the entrance route and road.
    count=0
    for i in range(55):
        a=i*GOLDEN_ANGLE
        radius=math.sqrt((i+.5)/55)
        x=cx+174*radius*math.cos(a)
        y=cy+64*radius*math.sin(a)
        path_x=-1272-22*math.sin(math.pi*(y+688)/191)
        if abs(x-path_x)<19 or ((x-cx)/185)**2+((y-cy)/77)**2>.89:
            continue
        h=7.5+2.8*(.5+.5*math.sin(i*GOLDEN_ANGLE))
        scene.item('TreeTrunk',(x,y,h*.32),(.48,.48,h*.64))
        for k in range(3):
            a2=k*GOLDEN_ANGLE+i
            scene.item('TreeLeaf%d'%(i%3),(x+1.2*math.cos(a2),y+1.2*math.sin(a2),h*.73+k*.4),
                       (4.3,4.3,4.8))
        count+=1
    for i in range(13):
        a=i*GOLDEN_ANGLE
        x,y=cx+125*math.cos(a),cy+49*math.sin(a)
        scene.item('Bench',(x,y,.65),(2.4,.65,.15),math.degrees(a))
        scene.item('Bronze',(x-.7,y,.32),(.12,.52,.64))
        scene.item('Bronze',(x+.7,y,.32),(.12,.52,.64))
    for i in range(12):
        x=-1410+23*i
        y=-662-12*math.sin(i*.5)
        scene.item('Bronze',(x,y,2.7),(.12,.12,5.4))
        scene.item('AmberSoft',(x,y,5.45),(.65,.65,.18))
    # A few parked cars and luggage-scale elements establish pedestrian scale.
    for i in range(8):
        a=math.pi*1.13+i*.08
        x,y=cx+197*math.cos(a),cy+85*math.sin(a)
        yaw=math.degrees(math.atan2(85*math.cos(a),-197*math.sin(a)))
        scene.item('CarBody',(x,y,.8),(4.6,1.85,1.25),yaw)
        scene.item('AircraftGlass',(x,y,1.5),(2.5,1.65,.55),yaw)
    scene.landmarks['park_tree_count']=count


def build_scene():
    scene=Scene()
    airport_shell(scene)
    tower(scene)
    terminal_spines(scene)
    entrance_park(scene)
    # One mesh per aircraft/material, not one UObject and draw call per window.
    groups = {}
    remaining = []
    for mesh in scene.meshes:
        if not mesh['name'].startswith('Aircraft'):
            remaining.append(mesh)
            continue
        key = (mesh['name'][:10], mesh['material'])
        if key not in groups:
            groups[key] = dict(name=key[0]+'_'+key[1], vertices=[], triangles=[],
                               material=key[1], tags=['MurexScaleAircraft', 'Murex'+key[0]], collision=True)
        group = groups[key]
        offset = len(group['vertices'])
        group['vertices'].extend(mesh['vertices'])
        group['triangles'].extend(tuple(i+offset for i in t) for t in mesh['triangles'])
    scene.meshes = remaining+list(groups.values())
    return scene


if __name__ == '__main__':
    import json
    s=build_scene()
    print(json.dumps(dict(meshes=len(s.meshes),triangles=sum(len(m['triangles']) for m in s.meshes),
                          instances=len(s.instances),gates=s.gates,landmarks=s.landmarks),indent=2))
