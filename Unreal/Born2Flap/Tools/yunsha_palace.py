"""Moon Jade Palace and four scholar's gardens, in centimetres.

Imported architectural detail surrounds an unobstructed northbound flight
procession. Target pairs describe actual passages for runtime collision sweeps.
"""
import math
import random
import unreal as u


def build(imported, place, mesh, actor, material, ground, rock, expr, custom, out):
    hall=imported('GoldenHall');gate=imported('Paifang');bonsai=imported('Bonsai')
    stone=material('MoonJadeStone',(.66,.74,.65),'/Game/Nature/T_CampusMossStone_Diffuse')
    paving=material('GardenLimestone',(.74,.72,.62),'/Game/Shiomori/Textures/T_ConcreteWorn_Diffuse')
    moss=material('GardenMoss',(.50,.62,.37),'/Game/Nature/T_CampusGrass_Diffuse')
    rng=random.Random(241024)

    def passage(name,x,y,z,halfdepth,kind):
        for end,dy in [('Start',-halfdepth),('End',halfdepth)]:
            p=u.EditorLevelLibrary.spawn_actor_from_class(u.TargetPoint,u.Vector(x,y+dy,z))
            p.tags=['YunshaPassage'+end,name,kind];p.set_actor_label(name+' '+end)

    # Hall roof heights 45/60/90m, with progressively larger entrance openings
    # beneath much higher vaulted ceilings. Authored columns remain intact.
    halls=[('Hall of Arrival',-58000,4500),('Hall of Still Water',-49500,6000),('Moon Jade Great Hall',-38000,9000)]
    for name,y,h in halls:
        base=240
        place(name,hall,13000,y,base,h,tags=['YunshaArchitecture','YunshaPalaceHall'])
        passage(name,13000,y,base+25/229.254*h,h*.56,'YunshaHallPassage')
        # The museum model is open underneath for viewing its interior. Fit a
        # limestone floor inside the existing plinth, with its own real collision.
        r=78/229.254*h;floor=base+7.7/229.254*h
        actor(name+' limestone floor',mesh(name+'Floor',
            [(13000-r,y-r,floor),(13000+r,y-r,floor),(13000+r,y+r,floor),(13000-r,y+r,floor)],
            [(0,1,2),(0,2,3)],paving),tags=['YunshaHallFloor'])
        # Concealed warm fixtures under the high beams illuminate the flight
        # volume without putting lamps or cables across the axial passage.
        for dx in (-1,1):
            for dy in (-1,1):
                light=u.EditorLevelLibrary.spawn_actor_from_class(u.RectLight,
                    u.Vector(13000+dx*h*.13,y+dy*h*.13,base+h*.34),u.Rotator(pitch=-90))
                light.set_actor_label(name+' concealed beam light');light.tags=['Yunsha']
                c=light.light_component;c.set_mobility(u.ComponentMobility.MOVABLE)
                c.set_editor_property('intensity_units',u.LightUnits.LUMENS)
                c.set_intensity(80000*(h/4500)**2);c.set_attenuation_radius(h*.7)
                c.set_editor_property('source_width',h*.08);c.set_editor_property('source_height',h*.08)
                c.set_editor_property('use_temperature',True);c.set_temperature(4300)
    for i,(y,h) in enumerate([(-65000,2400),(-53800,2700),(-43900,3200)]):
        a=place('Moon Jade paifang %d'%i,gate,13000,y,145,h,tags=['YunshaArchitecture','YunshaPaifang'])
        for slot in range(a.static_mesh_component.get_num_materials()):a.static_mesh_component.set_material(slot,stone)
        passage('Paifang%d'%i,13000,y,145+h*.32,h*.25,'YunshaGatePassage')

    def ribbon(name,points,width,mat):
        verts=[];tris=[]
        for i,(x,y) in enumerate(points):
            a=points[max(0,i-1)];b=points[min(len(points)-1,i+1)]
            dx=b[0]-a[0];dy=b[1]-a[1];length=max(1,math.hypot(dx,dy))
            for side in (-1,1):
                px=x-side*dy/length*width/2;py=y+side*dx/length*width/2
                verts.append((px,py,ground(px,py)+9))
            if i: k=2*i;tris.extend([(k-2,k-1,k),(k-1,k+1,k)])
        actor(name,mesh(name,verts,tris,mat))

    # A calm court between each pair of halls; ground contours remain visible
    # outside the precinct rather than being buried under a square foundation.
    for i,(a,b) in enumerate([(-68000,-60500),(-55500,-52700),(-46300,-42800)]):
        ribbon('Processional court %d'%i,[(13000,a+(b-a)*j/32) for j in range(33)],2500,paving)

    # Broad, irregular garden islands: lake contemplation, bonsai collection,
    # rock-and-pine retreat, and the eastern arrival garden.
    gardens=[(-3500,-50000,2300,2600),(1800,-55100,2600,2300),
             (4000,-46400,2900,2800),(22000,-48000,4100,5700)]
    # Feather planted ground back into the same sand texture as the terrain.
    # The irregular mesh boundary is entirely sandy, avoiding cut-out lawn pads.
    code='float d=100;\n'
    for cx,cy,rx,ry in gardens:
        code+='d=min(d,length((P.xy-float2(%s,%s))/float2(%s,%s)));\n'%(cx,cy,rx,ry)
    code+='''float noise=.035*sin(P.x*.014)*sin(P.y*.011)+.025*sin(P.x*.003+P.y*.004);
float mask=1-smoothstep(.50,.84,d+noise);
float3 sand=Texture2DSample(S,SSampler,P.xy/400).rgb*float3(1,.77,.48);
float3 grass=Texture2DSample(G,GSampler,P.xy/400).rgb*float3(.65,.8,.45);
return lerp(sand,grass,mask);'''
    out(custom(moss,code,{'P':expr(moss,'WorldPosition'),
        'S':expr(moss,'TextureObject',texture=u.load_asset('/Game/Shiomori/Textures/T_SandBeach_Diffuse')),
        'G':expr(moss,'TextureObject',texture=u.load_asset('/Game/Nature/T_CampusGrass_Diffuse'))}),'BASE_COLOR')
    u.MaterialEditingLibrary.recompile_material(moss);u.EditorAssetLibrary.save_loaded_asset(moss)
    for g,(cx,cy,rx,ry) in enumerate(gardens):
        verts=[(cx,cy,ground(cx,cy)+4)];tris=[];n=96
        for j in range(n):
            a=math.tau*j/n;r=1+.08*math.sin(a*3+g)+.04*math.cos(a*7)
            x=cx+rx*r*math.cos(a);y=cy+ry*r*math.sin(a)
            verts.append((x,y,ground(x,y)+4))
        for j in range(n):tris.append((0,j+1,(j+1)%n+1))
        actor('Scholar garden %d'%g,mesh('ScholarGarden%d'%g,verts,tris,moss),tags=['YunshaGarden'])
        points=[]
        for j in range(129):
            a=math.tau*j/128;r=.68+.06*math.sin(a*3+g)
            points.append((cx+rx*r*math.cos(a),cy+ry*r*math.sin(a)))
        ribbon('Winding garden walk %d'%g,points,280,paving)
        for j in range(9):
            a=math.tau*j/9+g*.4;r=.36 if j%3 else .89
            x=cx+rx*r*math.cos(a);y=cy+ry*r*math.sin(a)
            place('Bonsai collection %d %d'%(g,j),bonsai,x,y,ground(x,y)+10,rng.uniform(180,310),rng.uniform(0,360),tags=['YunshaBonsai'])
        for j in range(6):
            a=rng.uniform(0,math.tau);x=cx+rx*.86*math.cos(a);y=cy+ry*.86*math.sin(a)
            place('Garden scholar rock %d %d'%(g,j),rock,x,y,ground(x,y)-30,rng.uniform(140,380),rng.uniform(0,360))

    # Continuous bank walk traces the enlarged crescent, with intimate branches
    # into the gardens and a spacious approach from the desert.
    points=[]
    for j in range(193):
        a=math.radians(55+265*j/192)
        points.append((16700*math.cos(a),-52000+10800*math.sin(a)))
    ribbon('Crescent bank promenade',points,320,paving)
    for g,(cx,cy,rx,ry) in enumerate(gardens):
        target=(8500,cy) if g<3 else (17000,-44000)
        points=[]
        for j in range(65):
            t=j/64;x=cx+(target[0]-cx)*t;y=cy+(target[1]-cy)*t+650*math.sin(math.pi*t)
            points.append((x,y))
        ribbon('Garden connecting walk %d'%g,points,260,paving)
