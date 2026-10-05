"""Resolution-independent hammered-glass highlights over UMG background blur."""
import unreal as u
path='/Game/UI/M_HammeredGlass'
m=u.load_asset(path) or u.AssetToolsHelpers.get_asset_tools().create_asset('M_HammeredGlass','/Game/UI',u.Material,u.MaterialFactoryNew())
lib=u.MaterialEditingLibrary
lib.delete_all_material_expressions(m)
m.set_editor_property('material_domain',u.MaterialDomain.MD_UI)
m.set_editor_property('blend_mode',u.BlendMode.BLEND_TRANSLUCENT)
uv=lib.create_material_expression(m,u.MaterialExpressionTextureCoordinate)
n=lib.create_material_expression(m,u.MaterialExpressionCustom)
n.set_editor_property('code','''
float2 p=UV*float2(34,46);
float facet=sin(p.x+sin(p.y*1.3))*cos(p.y+sin(p.x*.8));
float rim=pow(saturate(1-min(min(UV.x,1-UV.x),min(UV.y,1-UV.y))*35),3);
float light=smoothstep(.3,1,facet);
return float4(float3(.45,.64,.68)+light*.18, .025+light*.035+rim*.12);
''')
n.set_editor_property('output_type',u.CustomMaterialOutputType.CMOT_FLOAT4)
pin=u.CustomInput();pin.set_editor_property('input_name','UV');n.set_editor_property('inputs',[pin])
lib.connect_material_expressions(uv,'',n,'UV')
rgb=lib.create_material_expression(m,u.MaterialExpressionComponentMask)
rgb.set_editor_property('r',True);rgb.set_editor_property('g',True);rgb.set_editor_property('b',True)
alpha=lib.create_material_expression(m,u.MaterialExpressionComponentMask)
alpha.set_editor_property('r',False);alpha.set_editor_property('g',False);alpha.set_editor_property('a',True)
assert lib.connect_material_expressions(n,'',rgb,'')
assert lib.connect_material_expressions(n,'',alpha,'')
lib.connect_material_property(rgb,'',u.MaterialProperty.MP_EMISSIVE_COLOR)
lib.connect_material_property(alpha,'',u.MaterialProperty.MP_OPACITY)
lib.recompile_material(m)
assert u.EditorAssetLibrary.save_loaded_asset(m)
u.log('GLASS_UI_READY')
