"""Continuous coastal rock/earth/grass surface, blended in world space."""
import sys,ast
from pathlib import Path
import unreal as u
here=Path(__file__).resolve().parent;sys.path.insert(0,str(here))
from project_guard import require_project
require_project()
from create_nature_assets import import_file
for source_name in ('rock_face_03','cliff_side','leafy_grass','forrest_ground_03'):
    for channel in ('Diffuse','nor_dx','Rough'):
        asset_name='T_Cliff_'+source_name+'_'+channel
        asset_path='/Game/Shiomori/Cliffs/Textures/'+asset_name
        tex=u.load_asset(asset_path) if u.EditorAssetLibrary.does_asset_exist(asset_path) else import_file(
            here.parent/'Saved/CliffSource'/source_name/(channel+'.jpg'),
            '/Game/Shiomori/Cliffs/Textures',asset_name)[0]
        tex.set_editor_property('srgb',channel=='Diffuse')
        if channel=='nor_dx':tex.set_editor_property('compression_settings',u.TextureCompressionSettings.TC_NORMALMAP)
        u.EditorAssetLibrary.save_loaded_asset(tex)
source=here/'create_shiomori_map.py';tree=ast.parse(source.read_text(encoding='utf-8-sig'))
scope=dict(__file__=str(source),assets=u.AssetToolsHelpers.get_asset_tools(),lib=u.MaterialEditingLibrary,ela=u.EditorAssetLibrary,count=0)
exec(compile(ast.Module(body=[n for n in tree.body if isinstance(n,(ast.Import,ast.ImportFrom,ast.FunctionDef))],type_ignores=[]),str(source),'exec'),scope)
node,custom,output=[scope[k] for k in ('node','custom','output')]
# Keep the actual existing headland shader outside the small new coastline.
# This generated asset has no authored edits; rebuilding it is deterministic.
path='/Game/Shiomori/Materials/M_BayCliffs'
if u.EditorAssetLibrary.does_asset_exist(path):
    assert u.EditorAssetLibrary.delete_asset(path)
m=u.EditorAssetLibrary.duplicate_asset('/Game/Shiomori/Materials/M_Headland',path)
assert m
lib=u.MaterialEditingLibrary
baseline={key:lib.get_material_property_input_node(m,getattr(u.MaterialProperty,'MP_'+key))
          for key in ('BASE_COLOR','NORMAL','ROUGHNESS')}
p=node(m,'WorldPosition');normal=node(m,'VertexNormalWS')
def texture(path):
    t=u.load_asset(path);assert t,path
    return node(m,'TextureObject',texture=t)
inputs=dict(P=p,N=normal,
    Rock=texture('/Game/Shiomori/Cliffs/Textures/T_Cliff_rock_face_03_Diffuse'),
    Stone=texture('/Game/Shiomori/Cliffs/Textures/T_Cliff_cliff_side_Diffuse'),
    Sand=texture('/Game/Shiomori/Textures/T_Sand_Diffuse'),
    Grass=texture('/Game/Shiomori/Cliffs/Textures/T_Cliff_leafy_grass_Diffuse'),
    Needles=texture('/Game/Shiomori/Cliffs/Textures/T_Cliff_forrest_ground_03_Diffuse'))
bc=custom(m,'''float3 w=pow(abs(normalize(N)),4);w/=max(.0001,w.x+w.y+w.z);
float3 rock=Texture2DSample(Rock,RockSampler,P.yz/310).rgb*w.x
 +Texture2DSample(Rock,RockSampler,P.xz/310).rgb*w.y
 +Texture2DSample(Rock,RockSampler,P.xy/310).rgb*w.z;
float3 stone=Texture2DSample(Stone,StoneSampler,P.yz/540).rgb*w.x
 +Texture2DSample(Stone,StoneSampler,P.xz/540).rgb*w.y
 +Texture2DSample(Stone,StoneSampler,P.xy/540).rgb*w.z;
float broad=.5+.22*sin(P.x*.00037+sin(P.y*.00043))+.15*sin(P.x*.0013-P.y*.0011);
float3 grass=lerp(Texture2DSample(Grass,GrassSampler,P.xy/290).rgb,
 Texture2DSample(Grass,GrassSampler,float2(-P.y,P.x)/417+7.1).rgb,broad);
float forest=smoothstep(32500,35500,P.y)*(1-smoothstep(41700,44500,P.y));
float3 needles=Texture2DSample(Needles,NeedlesSampler,P.xy/310).rgb;
float3 ground=lerp(grass*float3(.38,.57,.31),needles*.65,forest*.7);
float slope=1-smoothstep(.60,.94,abs(N.z));
float wet=1-smoothstep(-45,120,P.z);
float band=.5+.5*sin(P.z*.009+sin(P.x*.0008)*1.7+sin(P.y*.0003));
float3 mineral=rock*float3(.82,.86,.86);
float3 face=lerp(mineral,stone*float3(.69,.75,.79),saturate(.10+band*.15+broad*.24));
face*=.83+.17*band;
float3 shore=Texture2DSample(Sand,SandSampler,P.xy/350).rgb*.67;
float beach=(1-smoothstep(65,190,P.z))*(1-smoothstep(.1,.5,slope));
// Sparse vegetation on ledges, avoiding a continuous green stripe on each bed.
float ledgeRock=(1-smoothstep(1400,1900,P.z))*(1-smoothstep(.60,.83,broad))*smoothstep(44500,46500,P.y);
float3 color=lerp(lerp(ground,face,max(slope,ledgeRock*.85)),shore,beach);
return color*(1-wet*.35);''',inputs,3)
normal_tex=texture('/Game/Shiomori/Cliffs/Textures/T_Cliff_rock_face_03_nor_dx')
grass_normal=texture('/Game/Shiomori/Cliffs/Textures/T_Cliff_leafy_grass_nor_dx')
detail=custom(m,'''float3 w=pow(abs(normalize(N)),4);w/=max(.0001,w.x+w.y+w.z);
float3 x=Texture2DSample(T,TSampler,P.yz/310).rgb*2-1;
float3 y=Texture2DSample(T,TSampler,P.xz/310).rgb*2-1;
float3 z=Texture2DSample(G,GSampler,P.xy/290).rgb*2-1;
float3 perturb=float3(0,x.x,x.y)*w.x+float3(y.x,0,y.y)*w.y+float3(z.x,z.y,0)*w.z;
return normalize(N+perturb*.65);''',{'P':p,'N':normal,'T':normal_tex,'G':grass_normal},3)
m.set_editor_property('tangent_space_normal',False)
rough=custom(m,'''float3 w=pow(abs(normalize(N)),4);w/=max(.0001,w.x+w.y+w.z);
float r=Texture2DSample(R,RSampler,P.yz/310).r*w.x
 +Texture2DSample(R,RSampler,P.xz/310).r*w.y
 +Texture2DSample(G,GSampler,P.xy/290).r*w.z;
return lerp(clamp(r,.48,.95),.35,1-smoothstep(-45,100,P.z));''',
 {'P':p,'N':normal,'R':texture('/Game/Shiomori/Cliffs/Textures/T_Cliff_rock_face_03_Rough'),
  'G':texture('/Game/Shiomori/Cliffs/Textures/T_Cliff_leafy_grass_Rough')},1)
mask=custom(m,'''return smoothstep(-98000,-87500,P.x)*(1-smoothstep(-61000,-53000,P.x))
 *smoothstep(32000,39500,P.y)*(1-smoothstep(54000,58500,P.y));''',{'P':p},1)
for key,value,size in [('BASE_COLOR',bc,3),('NORMAL',detail,3),('ROUGHNESS',rough,1)]:
    code='return normalize(lerp(A,B,W));' if key=='NORMAL' else 'return lerp(A,B,W);'
    output(custom(m,code,{'A':baseline[key],'B':value,'W':mask},size),key)
u.MaterialEditingLibrary.recompile_material(m);assert u.EditorAssetLibrary.save_loaded_asset(m)
u.log('BAY_CLIFF_MATERIAL_SAVED')
u.SystemLibrary.execute_console_command(None,'quit')
