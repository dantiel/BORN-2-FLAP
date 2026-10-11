"""Thin, irregular, bevelled stepping slabs set into a needle-litter path."""
import math
import random

def route(points,width,ground,face):
    rng=random.Random(round(points[0][0]+points[0][1]))
    for a,b in zip(points,points[1:]):
        dx,dy=b[0]-a[0],b[1]-a[1];length=math.hypot(dx,dy)
        if length<1:continue
        along=(dx/length,dy/length);across=(-along[1],along[0])
        steps=max(1,round(length/92));spacing=length/steps
        def p(t,side,extra=0):
            x=a[0]+dx*t+across[0]*side;y=a[1]+dy*t+across[1]*side
            lift=max(3,(a[2]-ground(a[0],a[1]))*(1-t)+(b[2]-ground(b[0],b[1]))*t)
            return x,y,ground(x,y)+lift+extra
        # Wavering litter margins: a walkable continuous foundation under joints.
        for j in range(steps):
            t0=j/steps;t1=(j+1)/steps
            half=width/2+26+12*math.sin(j*.83)
            face(('ShorePath','PineNeedles'),[p(t0,-half,-3),p(t1,-half,-3),p(t1,half,-3),p(t0,half,-3)])
        columns=2 if width>=150 else 1
        for j in range(steps):
            for col in range(columns):
                t=(j+.5+(.09 if col else -.09))/steps
                centre=p(t,(col-(columns-1)/2)*width/columns+rng.uniform(-3,3))
                w=width/columns-rng.uniform(7,13);l=spacing-rng.uniform(6,13)
                # Clipped, uneven corners rather than rectangular concrete blocks.
                cut=rng.uniform(8,16)
                outline=[(-w/2+cut,-l/2),(w/2-cut,-l/2),(w/2,-l/2+cut),
                         (w/2,l/2-cut),(w/2-cut,l/2),(-w/2+cut,l/2),
                         (-w/2,l/2-cut),(-w/2,-l/2+cut)]
                yaw=rng.uniform(-.025,.025);top=[];rim=[];bottom=[]
                for x,y in outline:
                    x+=rng.uniform(-3,3);y+=rng.uniform(-2,2)
                    xx=x*math.cos(yaw)-y*math.sin(yaw);yy=x*math.sin(yaw)+y*math.cos(yaw)
                    wx=centre[0]+across[0]*xx+along[0]*yy;wy=centre[1]+across[1]*xx+along[1]*yy
                    z=ground(wx,wy)+(centre[2]-ground(centre[0],centre[1]))+rng.uniform(-.35,.35)
                    rim.append((wx,wy,z-.8));bottom.append((wx,wy,z-6))
                    top.append((centre[0]+(wx-centre[0])*.975,centre[1]+(wy-centre[1])*.975,z+.8))
                # across/along basis is clockwise in world XY; reverse the tops.
                face(('ShorePath','MossStone'),top[::-1])
                for i in range(8):
                    k=(i+1)%8
                    face(('ShorePath','MossStone'),[top[i],rim[i],rim[k],top[k]])
                    face(('ShorePath','MossStone'),[rim[i],bottom[i],bottom[k],rim[k]])
