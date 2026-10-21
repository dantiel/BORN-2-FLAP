"""Founder transmitter and neck lanyard, positioned from baked anatomical sockets."""
import json,math
from pathlib import Path
import unreal as u

def pose():return json.loads(Path(__file__).with_name('founder_pose.json').read_text())

def install(scope,origin,materials):
    # The remote rests on the palm-up left hand: centre it just forward of the
    # wrist (socket +Y = forward) so it sits clearly in front of the belly.
    data=pose();hx,hy,hz=data['left_hand'];cx,cy,cz=hx-24,hy+6,hz+6
    batches={}
    def face(mat,ps):
        vs,ts=batches.setdefault(mat,([],[]));k=len(vs);vs.extend(ps)
        ts.extend((k,k+i,k+i+1) for i in range(1,len(ps)-1))
    def box(mat,p,size,bevel=0):
        x,y,z=p;w,d,h=[v/2 for v in size];b=min(bevel,w,d)
        poly=[(-w+b,-d),(w-b,-d),(w,-d+b),(w,d-b),(w-b,d),(-w+b,d),(-w,d-b),(-w,-d+b)]
        rings=[[(x+a,y+b,z+dz) for a,b in poly] for dz in (-h,h)]
        face(mat,rings[0][::-1]);face(mat,rings[1])
        for i in range(8):j=(i+1)%8;face(mat,[rings[0][i],rings[0][j],rings[1][j],rings[1][i]])
    def rod(mat,a,b,width,depth=None):
        delta=[b[i]-a[i] for i in range(3)];ln=math.sqrt(sum(v*v for v in delta));d=[v/ln for v in delta]
        ref=(0,0,1) if abs(d[2])<.95 else (0,1,0)
        side=[d[1]*ref[2]-d[2]*ref[1],d[2]*ref[0]-d[0]*ref[2],d[0]*ref[1]-d[1]*ref[0]]
        sl=math.sqrt(sum(v*v for v in side));side=[v/sl for v in side]
        up=[side[1]*d[2]-side[2]*d[1],side[2]*d[0]-side[0]*d[2],side[0]*d[1]-side[1]*d[0]]
        rings=[[tuple(p[i]+s*side[i]*width/2+t*up[i]*(depth or width)/2 for i in range(3)) for s,t in [(-1,-1),(1,-1),(1,1),(-1,1)]] for p in (a,b)]
        face(mat,rings[0][::-1]);face(mat,rings[1])
        for i in range(4):j=(i+1)%4;face(mat,[rings[0][i],rings[0][j],rings[1][j],rings[1][i]])
    box('Bronze',(cx,cy,cz),(46,29,12),5)
    box('Dark',(cx,cy,cz+6.3),(41,24,.8),4)
    # Two distinct gimbal bezels, sticks, screen and shoulder switches.
    for dx in (-12,12):
        for i in range(32):
            a=i*math.tau/32;b=(i+1)*math.tau/32
            rod('Bronze',(cx+dx+6*math.cos(a),cy+1+6*math.sin(a),cz+7),(cx+dx+6*math.cos(b),cy+1+6*math.sin(b),cz+7),1.1)
        rod('Bronze',(cx+dx,cy+1,cz+7),(cx+dx,cy+1,cz+12),1.5)
        box('Bronze',(cx+dx,cy+1,cz+12),(3,3,1.5),.8)
        rod('Bronze',(cx+dx,cy+10,cz+7),(cx+dx,cy+11,cz+13),1)
    box('Bronze',(cx,cy-8,cz+7),(12,6,1),1)
    box('Dark',(cx,cy-8,cz+7.7),(10,4,.4),.5)
    rod('Bronze',(cx,cy+14,cz+1),(cx,cy+20,cz+19),2.4)
    # A neck lanyard: a bronze collar loops around the neck and two thin
    # straps fall down the chest to the transmitter's top clip.
    nx,ny,nz=data['neck'];clip=(cx,cy,cz+8)
    for i in range(24):
        a=i*math.tau/24;b=(i+1)*math.tau/24
        rod('Bronze',(nx+8*math.cos(a),ny+8*math.sin(a),nz-2),
                     (nx+8*math.cos(b),ny+8*math.sin(b),nz-2),1.4,.5)
    for s in (-1,1):
        points=[(nx+s*6,ny+8,nz-2),(nx+s*4,ny+10,nz-20),(nx+s*2,cy,nz-45),clip]
        for a,b in zip(points,points[1:]):rod('Bronze',a,b,2.2,.55)
    box('Bronze',clip,(6,3,3),.7)
    for mat,(vs,ts) in batches.items():
        mesh=scope['build_mesh']('FounderRC_'+mat,vs,ts);mesh.set_material(0,materials[mat]);u.EditorAssetLibrary.save_loaded_asset(mesh)
        a=scope['place_mesh']('Campus founder RC '+mat,origin,mesh,materials[mat],collision=False)
        a.tags=['ShiomoriCampusV2','CampusFounderRC']
    # Merge pivot is the kestrel's nose: its shoulder is 25.5 cm behind it.
    # The extra +24 cm (socket +Y = forward) carries the whole bird out in
    # front of the founder so it perches on the raised hand as the co-subject.
    rx,ry,rz=data['right_hand']
    return (origin[0]+rx,origin[1]+ry+25.5*.7+24,origin[2]+rz+8)