"""Organic hotel headland and a gently turning, three-flight park stair."""
import math
import random


def install(api):
    ground, face, prism = api['ground'], api['face'], api['prism']
    cx, cy = -59500., -12800.
    top = 916.

    def point(a, r):
        bulge = 1 + .055*math.sin(3*a+.6) + .035*math.sin(5*a)
        return cx+10000*bulge*r*math.cos(a), cy+6200*bulge*r*math.sin(a)

    # The route turns independently of the building axes. Landings interrupt
    # the descent, retaining the quiet rhythm of an old resort garden stair.
    def centre(t):
        return point(1.46-.16*math.sin(math.pi*t), .98+.62*t)
    end = centre(1)
    bottom = ground(*end)+4
    risers = max(3, math.ceil((top-bottom)/15))
    rise = (top-bottom)/risers
    assert 0 < rise <= 15, 'Park stair must descend in ordinary walking risers'
    # Longer flat intervals create two restful landings between flights.
    weights = [3.8 if i in (risers//3, 2*risers//3) else 1 for i in range(risers)]
    total = sum(weights)
    bounds = [0.]
    for weight in weights:
        bounds.append(bounds[-1]+weight/total)
    stair = []
    for i in range(risers):
        a, b = centre(bounds[i]), centre(bounds[i+1])
        z = top-i*rise
        dx, dy = b[0]-a[0], b[1]-a[1]
        length = math.hypot(dx,dy)
        nx, ny = -dy/length, dx/length
        width = 460+100*bounds[i]
        def edge(t,side):
            p=centre(t);before=centre(t-.001);after=centre(t+.001)
            dx,dy=after[0]-before[0],after[1]-before[1]
            length=math.hypot(dx,dy);half=(460+100*t)/2
            return p[0]-side*dy/length*half,p[1]+side*dx/length*half
        poly = [edge(bounds[i],-1),edge(bounds[i+1],-1),
                edge(bounds[i+1],1),edge(bounds[i],1)]
        prism('ParkStairs','MossStone',poly,z-75,75)
        if 'beam' in api:
            for side in (-1,1):
                ea,eb=edge(bounds[i],side),edge(bounds[i+1],side)
                # Low stone cheeks and a quiet continuous metal handrail.
                api['beam']('ParkStairs','Stone',(*ea,z+10),(*eb,z+10),18,20)
                next_z=z-rise
                api['beam']('ParkStairs','Steel',(*ea,z+90),(*eb,next_z+90),4,4)
                api['beam']('ParkStairs','Steel',(*ea,z),(*ea,z+90),4,4)
        stair.append((a,b,z,width))

    def surface(x,y,r):
        blend = max(0,min(1,(r-1)/.6))
        blend = blend*blend*(3-2*blend)
        z = top*(1-blend)+(ground(x,y)-5)*blend
        # Cut the planted embankment beneath the walking surface, never let
        # a continuous terrain ramp bury the visible risers or landings.
        for a,b,sz,width in stair:
            dx,dy=b[0]-a[0],b[1]-a[1]
            t=max(0,min(1,((x-a[0])*dx+(y-a[1])*dy)/(dx*dx+dy*dy)))
            distance=math.hypot(x-a[0]-t*dx,y-a[1]-t*dy)
            influence=max(0,min(1,(distance-width/2)/220))
            z=min(z,sz-35+influence*1000)
        return z

    # Dense radial rings merge into the real terrain with a rounded shoulder;
    # there is no square retaining block underneath this surface.
    rings, segments = 48, 160
    for j in range(rings):
        r0, r1 = 1.6*j/rings, 1.6*(j+1)/rings
        for i in range(segments):
            def vertex(r,k):
                x,y=point(math.tau*k/segments,r)
                return x,y,surface(x,y,r)
            mat='Paving' if r1<=.88 else 'Grass' if r1<=1.12 else 'Headland'
            pts=[vertex(r0,i),vertex(r1,i),vertex(r1,i+1)]
            if j:pts.append(vertex(r0,i+1))
            face(('CampusLandscape',mat),pts)
    # Loose planted drifts soften the shoulder, using the generated surface
    # rather than the lower native terrain for roots and partly buried rocks.
    rng=random.Random(261018)
    scope=api['scope']
    for i in range(220):
        a=rng.uniform(.12,math.pi-.1);r=rng.uniform(1.06,1.55)
        x,y=point(a,r)
        if any(math.hypot(x-p[0],y-p[1])<850 for p in (centre(t/40) for t in range(41))):continue
        kind='Rock' if i%9==0 else 'Fern' if i%3==0 else 'Grass'
        pool=scope['foliage_meshes'].get(kind,[])
        if not pool:continue
        z=surface(x,y,r)-(20 if kind=='Rock' else 0)
        actor=scope['foliage']('Campus terrace '+kind,(x,y,z),rng.choice(pool),
                               rng.uniform(45,100) if kind=='Rock' else rng.uniform(35,65),
                               rot=(0,rng.uniform(0,360),0),ground_z=z)
        actor.tags=[api['TAG'] if 'TAG' in api else 'ShiomoriCampusV2','CampusLandscapePlant']
    return [centre(t) for t in bounds], bottom, surface, point
