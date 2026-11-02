"""Original Hamipterus-inspired egg exposures, explicitly not specimen scans.

Small, compressed, wrinkled eggs in sediment, rather than hard bird eggs in
straw nests. Reference: Wang et al. 2017, Science, DOI 10.1126/science.aan2329.
"""
import math
import random
import unreal as u


def build(mesh,actor,material,ground,expr,custom,out):
    shell=material('PterosaurEggMineral',(.57,.49,.36),'/Game/Shiomori/Textures/T_ConcreteWorn_Diffuse')
    out(custom(shell,'''float f=sin(P.x*3.1+sin(P.y*1.4))+sin(P.y*4.7+P.z*2.1);
float crack=1-smoothstep(.025,.10,abs(sin(P.x*.77+P.y*.29+sin(P.z*1.3))));
return lerp(float3(.40,.32,.21)*(1+f*.13),float3(.14,.10,.06),crack*.6);''',{'P':expr(shell,'WorldPosition')}),'BASE_COLOR')
    u.MaterialEditingLibrary.recompile_material(shell);u.EditorAssetLibrary.save_loaded_asset(shell)
    for bed,(cx,cy) in enumerate(((40000,-13000),(40600,-13500),(40500,-14200))):
        rng=random.Random(140020+bed);z=ground(cx,cy)
        # Bury directly into the terrain; no separate display slab or nest mat.
        verts=[];tris=[];centres=[]
        for egg in range(18):
            for attempt in range(100):
                x=rng.uniform(-36,36);y=rng.uniform(-24,24)
                if all(math.hypot(x-a,y-b)>7 for a,b in centres):break
            centres.append((x,y));length=rng.uniform(5.0,6.4);width=length*rng.uniform(.44,.53)
            yaw=rng.uniform(0,math.tau);phase=rng.uniform(0,math.tau);flatten=rng.uniform(.47,.72)
            n=48;rows=24;k=len(verts)
            for j in range(rows+1):
                theta=math.pi*j/rows
                for i in range(n):
                    phi=math.tau*i/n
                    wrinkle=1+.065*math.sin(phi*9+math.sin(theta*5)+phase)*math.sin(theta)**2
                    ex=length*.5*math.cos(theta)
                    ey=width*.5*math.sin(theta)*math.cos(phi)*wrinkle
                    ez=width*.5*math.sin(theta)*math.sin(phi)*wrinkle*flatten
                    # Slightly sunken shell with asymmetric compression folds.
                    ez-=.10*math.sin(theta*3+phase)*math.sin(phi*2)
                    verts.append((cx+x+ex*math.cos(yaw)-ey*math.sin(yaw),cy+y+ex*math.sin(yaw)+ey*math.cos(yaw),ground(cx+x,cy+y)+.10+ez))
            for j in range(rows):
                for i in range(n):
                    a=k+j*n+i;b=k+j*n+(i+1)%n
                    if j>0:tris.append((a,a+n,b))
                    if j<rows-1:tris.append((b,a+n,b+n))
        actor('Hamipterus-inspired egg exposure %d'%bed,mesh('PterosaurEggExposure%d'%bed,verts,tris,shell),tags=['YunshaFossil','YunshaPterosaurEggReconstruction'])
