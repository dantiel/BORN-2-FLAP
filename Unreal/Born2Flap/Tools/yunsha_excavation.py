"""The Bone Archive: actual excavated terrain and retained fossil scans."""
import math
import random
import unreal as u
from yunsha_sand import HLSL, RELIEF, DRY_COLOUR, normal_code


def build(mesh, actor, material, ground, imported, expr, custom, out):
    strata=material('BoneArchiveStrata',(1.,.77,.48),'/Game/Shiomori/Textures/T_SandBeach_Diffuse')
    out(custom(strata,HLSL+RELIEF+'''float3 w=pow(abs(N),4);w/=max(.0001,w.x+w.y+w.z);
float3 rock=Texture2DSample(T,TSampler,P.yz/580).rgb*w.x+Texture2DSample(T,TSampler,P.xz/580).rgb*w.y+Texture2DSample(T,TSampler,P.xy/580).rgb*w.z;
float3 broad=Texture2DSample(T,TSampler,P.yz/1900+3.7).rgb*w.x+Texture2DSample(T,TSampler,P.xz/1900+3.7).rgb*w.y+Texture2DSample(T,TSampler,P.xy/1900+3.7).rgb*w.z;
rock=lerp(rock,broad,.45);
float z=P.z+38*sin(P.x*.0006)+19*sin(P.y*.0011);
float layer=frac((z+2200)/440);
float3 bed=layer<.16?float3(.29,.24,.19):(layer<.52?float3(.77,.57,.36):float3(.94,.83,.61));
bed*=.88+.12*sin(z*.16);
'''+DRY_COLOUR+'''
return lerp(rock*bed,dry,smoothstep(-180,80,P.z));''',{
        'P':expr(strata,'WorldPosition'),'N':expr(strata,'VertexNormalWS'),'C':expr(strata,'CameraVectorWS'),
        'T':expr(strata,'TextureObject',texture=u.load_asset('/Game/Shiomori/Cliffs/Textures/T_Cliff_rock_face_03_Diffuse')),
        'S':expr(strata,'TextureObject',texture=u.load_asset('/Game/Shiomori/Textures/T_SandBeach_Diffuse'))}),'BASE_COLOR')
    out(custom(strata,normal_code(),{
        'P':expr(strata,'WorldPosition'),'N':expr(strata,'VertexNormalWS')}),'NORMAL')
    strata.set_editor_property('tangent_space_normal',False)
    u.MaterialEditingLibrary.recompile_material(strata);u.EditorAssetLibrary.save_loaded_asset(strata)
    # Replace coarse terrain, including every bench and the east access ramp.
    verts=[];tris=[];nx=231;ny=191
    for j in range(ny):
        for i in range(nx):
            x=24000+i*200;y=-30000+j*200;z=ground(x,y)
            # Match the adjacent coarse edge exactly to avoid boundary cracks.
            if i in (0,nx-1):
                y0=math.floor(y/1000)*1000;t=(y-y0)/1000;z=ground(x,y0)*(1-t)+ground(x,y0+1000)*t
            if j in (0,ny-1):
                x0=math.floor(x/1000)*1000;t=(x-x0)/1000;z=ground(x0,y)*(1-t)+ground(x0+1000,y)*t
            verts.append((x,y,z))
    for j in range(ny-1):
        for i in range(nx-1):
            k=j*nx+i;tris.extend(((k,k+1,k+nx),(k+1,k+nx+1,k+nx)))
    terrain=actor('Bone Archive excavation',mesh('BoneArchiveTerrain',verts,tris,strata),tags=['YunshaExcavation'])
    terrain.static_mesh_component.set_editor_property('affect_distance_field_lighting',False)
    # Evaluate the exact two triangles of the saved 2m grid, not the smooth
    # native height function between its vertices. Tiny eggs need this too.
    sampled_ground=ground
    def ground(x,y):
        x0=math.floor(x/200)*200;y0=math.floor(y/200)*200
        tx=(x-x0)/200;ty=(y-y0)/200
        a=sampled_ground(x0,y0);b=sampled_ground(x0+200,y0)
        c=sampled_ground(x0,y0+200);d=sampled_ground(x0+200,y0+200)
        return a+(b-a)*tx+(c-a)*ty if tx+ty<=1 else d+(c-d)*(1-tx)+(b-d)*(1-ty)
    placements={}
    def specimen(name,kind,x,y,span,yaw,burial):
        source=(x,y);sm=imported(kind);b=sm.get_bounds()
        scale=span/(2*max(b.box_extent.x,b.box_extent.y));angle=math.radians(yaw)
        # Prefer the authored location; search nearby only if it crosses a
        # terrace break. Fit the scan plane to nine points under its footprint.
        def fit(px,py):
            h=ground(px,py);radius=max(4,span*.48)
            dx=(ground(px+radius,py)-ground(px-radius,py))/(2*radius)
            dy=(ground(px,py+radius)-ground(px,py-radius))/(2*radius)
            error=max(abs(ground(px+rx*radius,py+ry*radius)-(h+dx*rx*radius+dy*ry*radius)) for rx in (-1,0,1) for ry in (-1,0,1))
            return h,dx,dy,error
        candidates=[(x,y)]
        for radius in (100,200,400,800,1400,2200):
            candidates.extend((x+radius*math.cos(i*math.tau/16),y+radius*math.sin(i*math.tau/16)) for i in range(16))
        for px,py in candidates:
            h,dx,dy,error=fit(px,py)
            if error<=max(.8,span*.012) and math.hypot(dx,dy)<.22:
                x,y=px,py;break
        else:raise AssertionError('No supported fossil footprint: '+name)
        normal=u.Vector(-dx,-dy,1).normal()
        forward=u.Vector(math.cos(angle),math.sin(angle),dx*math.cos(angle)+dy*math.sin(angle)).normal()
        rotation=u.MathLibrary.make_rotation_from_axes(forward,normal.cross(forward),normal)
        # Sink the scan's sediment matrix, leaving its fossil-bearing relief
        # exposed. Previously its lowest bounding-box point merely touched sand.
        thickness=2*b.box_extent.z*scale
        inset=max(burial,thickness*({'PterosaurSlab':.12,'PteranodonBones':.12,'FossilFish':.45,'Herbstosaurus':.10}.get(kind,.28)))
        local=u.Vector(b.origin.x*scale,b.origin.y*scale,(b.origin.z-b.box_extent.z)*scale+inset)
        offset=forward*local.x+normal.cross(forward)*local.y+normal*local.z
        a=actor(name,sm,(x-offset.x,y-offset.y,h-offset.z),(scale,scale,scale),tags=['YunshaFossil','YunshaFossilScan',kind])
        a.set_actor_rotation(rotation,False)
        placements[source]=(x,y,h)
        u.log('YUNSHA_FOSSIL_SEATED %s error_cm=%.3f inset_cm=%.3f slope=%.3f'%(name,error,inset,math.hypot(dx,dy)))
    # Five to seven metre specimens, spread across a 460 metre research field.
    specimen('Anhanguera main exposure','PterosaurSlab',46000,-12000,680,28,4)
    specimen('Western Pteranodon limb and claw exposure','PteranodonBones',36500,-10500,190,133,2)
    specimen('Surface discovery','PterosaurSlab',61000,-20500,620,247,35)
    specimen('Satellite trench Pteranodon bones','PteranodonBones',30000,-15600,175,75,3)
    r=random.Random(61026)
    for i,(x,y) in enumerate(((46700,-11700),(45400,-12400),(36500,-11200),(62000,-20400),(30000,-17000),(50500,-12500),(41000,-10000),(62500,-18000))):
        specimen('Pterosaur jaw fragment %02d'%i,'PterosaurJaw',x,y,r.uniform(55,110),r.uniform(0,360),2)
    for i,(x,y) in enumerate(((41100,-14000),(60300,-21000))):
        specimen('Pteranodon disarticulated bones %d'%i,'PteranodonBones',x,y,165+i*12,45+i*115,3+i)
    for i,(x,y) in enumerate(((36660,-10400),(30200,-15700))):
        specimen('Herbstosaurus fossil mould %d'%i,'Herbstosaurus',x,y,16,20+i*130,.08)
    for i,(x,y) in enumerate(((40900,-13700),(41200,-14200),(60800,-20700))):
        specimen('Chinese dinosaur egg cluster %d'%i,'DinosaurEggs',x,y,65,40+i*115,12)
    for i,(x,y) in enumerate(((40090,-12920),(40820,-13720),(61200,-20300))):
        specimen('Ancient lake fish impression %d'%i,'FossilFish',x,y,22,75+i*50,.15)
    for i,(x,y) in enumerate(((36740,-10600),(30240,-15500),(61500,-20700))):
        specimen('Ammonite exposure %d'%i,'Ammonite',x,y,28,15+i*65,4)
    from yunsha_fossil_eggs import build as build_egg_beds
    build_egg_beds(mesh,actor,material,ground,expr,custom,out)
    # Loose natural spoil at the outer edge leaves the descent and fossils clear.
    scan=imported('rock_face_01');b=scan.get_bounds()
    for i in range(18):
        a=r.uniform(.4,2.8);x=47000+18000*math.cos(a);y=-11000+13500*math.sin(a)
        s=r.uniform(100,280)/(2*b.box_extent.z)
        actor('Excavation spoil %02d'%i,scan,(x,y,ground(x,y)-s*(b.origin.z-b.box_extent.z)-30),(s,s,s),r.uniform(0,360))

    # Small canvas camp on the apron, with ample space between shelters.
    tent=imported('FieldTent');b=tent.get_bounds()
    for i,(x,y,height,yaw) in enumerate(((47800,-21600,290,12),(49000,-21800,250,-8),(47600,-22800,225,95))):
        scale=height/(2*b.box_extent.z)
        actor('Canvas research tent %d'%i,tent,(x,y,ground(x,y)-scale*(b.origin.z-b.box_extent.z)-3),(scale,scale,scale),yaw,tags=['YunshaFieldTent'])
    wood=material('DigWeatheredWood',(.22,.15,.085))
    rope=material('DigHempRope',(.37,.29,.18))
    out(custom(wood,"return float3(.24,.17,.10)*(.78+.22*sin(P.z*.08+sin(P.x*.41)*2+sin(P.y*.37)));",{'P':expr(wood,'WorldPosition')}),'BASE_COLOR')
    u.MaterialEditingLibrary.recompile_material(wood);u.EditorAssetLibrary.save_loaded_asset(wood)
    def tube(verts,tris,points,radius,sides=7):
        start=len(verts)
        for j,p in enumerate(points):
            tangent=u.Vector(*points[min(j+1,len(points)-1)])-u.Vector(*points[max(j-1,0)])
            tangent=tangent.normal();ref=u.Vector(0,0,1) if abs(tangent.z)<.9 else u.Vector(1,0,0)
            right=tangent.cross(ref).normal();up=tangent.cross(right).normal()
            for k in range(sides):
                q=u.Vector(*p)+(right*math.cos(k*math.tau/sides)+up*math.sin(k*math.tau/sides))*radius
                verts.append((q.x,q.y,q.z))
        for j in range(len(points)-1):
            for k in range(sides):
                a=start+j*sides+k;b=start+j*sides+(k+1)%sides
                tris.extend(((a,b,a+sides),(b,b+sides,a+sides)))
        for k in range(1,sides-1):
            tris.append((start,start+k+1,start+k))
            end=start+(len(points)-1)*sides;tris.append((end,end+k,end+k+1))
    # Four isolated lengths: entrances, most pit edges and the ramp stay open.
    for i,(x,y,dx,dy,count) in enumerate(((53200,-23500,320,0,10),(48200,-26300,300,0,8),(60300,-18800,0,300,6),(46200,-22200,0,290,5))):
        pv=[];pt=[];rv=[];rt=[];tops=[]
        for j in range(count):
            px=x+dx*j;py=y+dy*j;z=ground(px,py);h=100+r.uniform(-10,12)
            top=(px+r.uniform(-5,5),py+r.uniform(-5,5),z+h)
            tube(pv,pt,[(px,py,z-20),top],5);tops.append(top)
        for a,b in zip(tops,tops[1:]):
            points=[]
            for j in range(13):
                t=j/12;points.append(tuple(a[k]*(1-t)+b[k]*t-(18+24*4*t*(1-t) if k==2 else 0) for k in range(3)))
            tube(rv,rt,points,1.5,6)
        actor('Weathered dig posts %d'%i,mesh('DigPosts%d'%i,pv,pt,wood),tags=['YunshaDigFence'])
        actor('Slack excavation rope %d'%i,mesh('DigRope%d'%i,rv,rt,rope,False),collision=False)

    return placements