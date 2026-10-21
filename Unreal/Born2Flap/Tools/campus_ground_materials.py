"""Local coastal garden ground; leaves the beach and distant headlands intact."""
import unreal as u
from create_nature_assets import ROOT, import_file, LIB

def prepare(scope):
    for channel,normal in [('Diffuse',False),('nor_dx',True),('Rough',False)]:
        path='/Game/Nature/T_CampusGrass_'+channel
        tex=u.load_asset(path) if LIB.does_asset_exist(path) else import_file(ROOT/'leafy_grass'/(channel+'.jpg'),'/Game/Nature','T_CampusGrass_'+channel)[0]
        if normal:tex.set_editor_property('compression_settings',u.TextureCompressionSettings.TC_NORMALMAP)
        tex.set_editor_property('srgb',channel=='Diffuse');LIB.save_loaded_asset(tex)
    for name,source in [('PineNeedles','forrest_ground_03'),('MossStone','mossy_rock')]:
        for channel in ('Diffuse','nor_dx','Rough'):
            path='/Game/Nature/T_Campus'+name+'_'+channel
            t=u.load_asset(path) if LIB.does_asset_exist(path) else import_file(ROOT/source/(channel+'.jpg'),'/Game/Nature','T_Campus'+name+'_'+channel)[0]
            if channel=='nor_dx':t.set_editor_property('compression_settings',u.TextureCompressionSettings.TC_NORMALMAP)
            t.set_editor_property('srgb',channel=='Diffuse');LIB.save_loaded_asset(t)
    tex=lambda name:u.load_asset('/Game/Shiomori/Textures/'+name)
    scope['coastal_terrain_material']('Headland',
        tuple(tex('T_Sand_'+x) for x in ('Diffuse','Normal','Rough')),
        tuple(u.load_asset('/Game/Nature/Textures/T_aerial_grass_rock_'+x) for x in ('Diffuse','nor_dx','Rough')),
        tuple(tex('T_Basalt_'+x) for x in ('Diffuse','Normal','Rough')))
    for name,base,scale in [('CampusGardenSoil','/Game/Nature/Textures/T_forest_ground_04_',220),
                            ('CampusLawn','/Game/Nature/T_CampusGrass_',240),
                            ('CampusPineNeedles','/Game/Nature/T_CampusPineNeedles_',200),
                            ('CampusMossStone','/Game/Nature/T_CampusMossStone_',150)]:
        scope['pbr_material'](name,diffuse=u.load_asset(base+'Diffuse'),normal=u.load_asset(base+'nor_dx'),
                              rough_tex=u.load_asset(base+'Rough'),tiling=('world',scale),normal_strength=.35)

def localize(m,pos,bc,nb,node,custom):
    d=u.load_asset('/Game/Nature/T_CampusGrass_Diffuse')
    n=u.load_asset('/Game/Nature/T_CampusGrass_nor_dx')
    if not d or not n:return bc,nb
    from shiomori_campus import SITE_OFFSET
    cx,cy=SITE_OFFSET[0]-59500,SITE_OFFSET[1]-9500
    mask=custom(m,'''float2 p=(P.xy-float2(%f,%f))/float2(16000,23000);
        float edge=length(p)+.035*sin(P.x*.0008)*sin(P.y*.0009);
        return (1-smoothstep(.68,1,edge))*smoothstep(180,350,P.z);'''%(cx,cy),{'P':pos},1)
    dnode=node(m,'TextureObject',texture=d);nnode=node(m,'TextureObject',texture=n)
    color=custom(m,'''float2 uv=P.xy/240.;
        float3 a=Texture2DSample(T,TSampler,uv).rgb;
        float3 b=Texture2DSample(T,TSampler,float2(-uv.y,uv.x)*.731+3.7).rgb;
        float blend=.5+.22*sin(P.x*.00071+sin(P.y*.00048));
        float3 grass=lerp(a,b,blend)*(.91+.09*sin(P.x*.00029)*sin(P.y*.00033));
        return lerp(Old,grass,Mask);''',{'P':pos,'T':dnode,'Old':bc,'Mask':mask},3)
    normal=custom(m,'''float3 v=Texture2DSample(T,TSampler,P.xy/240.).rgb*2-1;
        return normalize(lerp(Old,normalize(float3(v.xy*.4,1)),Mask));''',{'P':pos,'T':nnode,'Old':nb,'Mask':mask},3)
    needles=u.load_asset('/Game/Nature/T_CampusPineNeedles_Diffuse')
    if needles:
        circles='float cover=0; '
        for x,y in [(-59900,-4700),(-60000,-2200),(-59800,500),(-60800,4000),(-60200,6500),(-61500,5200),(-59400,8400)]:
            circles+='cover=max(cover,1-smoothstep(380,1100,length(P.xy-float2(%f,%f)))); '%(x+SITE_OFFSET[0],y+SITE_OFFSET[1])
        color=custom(m,circles+'return lerp(Old,Texture2DSample(T,TSampler,P.xy/200.).rgb,cover*.92);',
                     {'P':pos,'T':node(m,'TextureObject',texture=needles),'Old':color},3)
    return color,normal
