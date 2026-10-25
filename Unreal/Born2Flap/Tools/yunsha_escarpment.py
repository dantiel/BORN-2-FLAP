"""A continuous mountain surface with excavated grottoes, in centimetres.

Cavities are depressions in the rock volume itself, never stacked arch blocks.
Wide uncarved ribs separate five irregular communities of small caves.
"""
import math
import random


def smooth(a,b,x):
    t=max(0,min(1,(x-a)/(b-a)))
    return t*t*(3-2*t)


def noise(x,y):
    ix,iy=math.floor(x),math.floor(y);fx=x-ix;fy=y-iy
    fx=fx*fx*(3-2*fx);fy=fy*fy*(3-2*fy)
    def h(a,b):return ((a*374761393+b*668265263)^((a*374761393+b*668265263)>>13))%65536/32768-1
    return (h(ix,iy)*(1-fx)+h(ix+1,iy)*fx)*(1-fy)+(h(ix,iy+1)*(1-fx)+h(ix+1,iy+1)*fx)*fy


def erosion(y,z):
    # Different scales of discontinuous erosion replace periodic melted flutes.
    return 2100*noise(y/5100,1.3)+800*noise(y/1700,z/3100)+260*noise(y/420,z/330)+95*noise(y/150,z/170)


def front(y,z):
    return -43000+erosion(y,z)-erosion(-54400,6500)


def crown(y):
    return 16000+3500*noise(y/6500,7.1)+1400*noise(y/1400,3.4)


def caves():
    r=random.Random(8713)
    result=[(-54400,6500,1050,1100,3600)]
    # Poisson-separated openings; gaps and varying elevations read as rock,
    # not a regular arcade. Large weathered piers remain between clusters.
    attempts=0
    while len(result)<60:
        attempts+=1
        assert attempts<100000
        cy=r.choice((-55000,-42000,-26000,-11000,3500))+r.uniform(-3600,3600)
        cz=r.uniform(2800,min(13200,crown(cy)-2600))
        ry=r.uniform(560,1050);rz=r.uniform(650,1250)
        if any(((cy-y)/(ry+wy+450))**2+((cz-z)/(rz+wz+550))**2<1 for y,z,wy,wz,d in result):continue
        result.append((cy,cz,ry,rz,r.uniform(2300,4000)))
    return result


def build(mesh,actor,rock,pigment):
    mouths=caves();vertices=[];triangles=[]
    ny,nz=420,110
    y0,y1=-70000,16000
    for j in range(ny+1):
        y=y0+(y1-y0)*j/ny
        end=smooth(y0,y0+11000,y)*(1-smooth(y1-12000,y1,y))
        top=500+(crown(y)-500)*end
        for k in range(nz+1):
            z=-100+(top+100)*k/nz
            x=front(y,z)-2800*(k/nz)**5
            depth=0
            for cy,cz,ry,rz,d in mouths:
                radius=math.sqrt(((y-cy)/ry)**2+((z-cz)/rz)**2)
                depth=max(depth,d*(1-smooth(.62,1.05,radius)))
            x-=depth
            vertices.append((x,y,z))
    for j in range(ny):
        for k in range(nz):
            a=j*(nz+1)+k;b=a+nz+1
            triangles.extend([(a,b,a+1),(a+1,b,b+1)])
    # A broad eroded mountain continues west from the crest and descends to
    # the surrounding desert. No free-standing facade or artificial top rail.
    base=len(vertices);width=32
    for j in range(ny+1):
        y=y0+(y1-y0)*j/ny;edge=vertices[j*(nz+1)+nz]
        for i in range(width+1):
            t=i/width
            x=edge[0]-t*39000
            z=edge[2]*(1-smooth(.18,1,t))+900*math.sin(t*math.pi)*math.sin(y*.0004+t*7)
            vertices.append((x,y,z-100*t))
    for j in range(ny):
        for i in range(width):
            a=base+j*(width+1)+i;b=a+width+1
            triangles.extend([(a,b,a+1),(a+1,b,b+1)])
    actor('Bronze mountain carved from bedrock',mesh('BronzeMountain',vertices,triangles,rock),tags=['YunshaEscarpment'])
    for i,(y,z,ry,rz,depth) in enumerate(mouths):
        x=front(y,z)-depth+30
        verts=[(x,y,z-rz*.30),(x,y-ry*.48,z+rz*.35),(x,y-ry*.22,z),
               (x,y,z+rz*.05),(x,y+ry*.22,z),(x,y+ry*.48,z+rz*.35)]
        actor('Sun-glass wing mural',mesh('Wing_%d'%i,verts,[(0,1,2),(0,2,3),(0,3,4),(0,4,5)],pigment,False),collision=False,tags=['YunshaMural'])
    return mouths
