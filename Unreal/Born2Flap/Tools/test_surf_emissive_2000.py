"""TEST: scale M_BreakingSurf emissive by 2000x to prove the exposure-crush theory."""
import unreal as u
lib=u.MaterialEditingLibrary
ela=u.EditorAssetLibrary
m=u.load_asset('/Game/Weather/M_BreakingSurf'); assert isinstance(m,u.Material)
# Find the emissive output node and its Multiply chain; simplest: rebuild the
# emissive multiply with a 2000x white constant.
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
m.set_editor_property('blend_mode',u.BlendMode.BLEND_TRANSLUCENT)
m.set_editor_property('shading_model',u.MaterialShadingModel.MSM_UNLIT)
vc=node('VertexColor')
r=node('ComponentMask',r=True,g=False,b=False,a=False)
assert lib.connect_material_expressions(vc,'',r,'')
big=node('Constant',r=2000.0)
col=node('Multiply'); assert lib.connect_material_expressions(big,'',col,'A'); assert lib.connect_material_expressions(r,'',col,'B')
output(col,'EMISSIVE_COLOR')
output(r,'OPACITY')
lib.recompile_material(m); assert ela.save_loaded_asset(m)
u.log('SURF_EMISSIVE_2000_DONE')
