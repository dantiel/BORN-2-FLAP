"""TEST 2: emissive 2000 + disable_depth_test=True on M_BreakingSurf."""
import unreal as u
lib=u.MaterialEditingLibrary
ela=u.EditorAssetLibrary
m=u.load_asset('/Game/Weather/M_BreakingSurf'); assert isinstance(m,u.Material)
m.set_editor_property('blend_mode',u.BlendMode.BLEND_TRANSLUCENT)
m.set_editor_property('shading_model',u.MaterialShadingModel.MSM_UNLIT)
m.set_editor_property('disable_depth_test',True)
for _ in range(64):
    exps=lib.get_material_expressions(m)
    if not exps: break
    lib.delete_all_material_expressions(m)
def node(kind,**kw):
    n=lib.create_material_expression(m,getattr(u,'MaterialExpression'+kind))
    for k,v in kw.items(): n.set_editor_property(k,v)
    return n
def output(n,prop,out=''):
    assert lib.connect_material_property(n,out,getattr(u.MaterialProperty,'MP_'+prop))
vc=node('VertexColor')
r=node('ComponentMask',r=True,g=False,b=False,a=False)
assert lib.connect_material_expressions(vc,'',r,'')
big=node('Constant',r=2000.0)
col=node('Multiply'); assert lib.connect_material_expressions(big,'',col,'A'); assert lib.connect_material_expressions(r,'',col,'B')
output(col,'EMISSIVE_COLOR')
output(r,'OPACITY')
lib.recompile_material(m); assert ela.save_loaded_asset(m)
u.log('SURF_NODEPTH_2000_DONE')
