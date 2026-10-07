"""Repair translucent membrane fidelity without changing artwork or opacity."""
import unreal as u
ela=u.EditorAssetLibrary
lib=u.MaterialEditingLibrary
for path in ['/Game/Birds/M_FalconWing','/Game/Birds/M_KestrelTail','/Game/Shiomori/Materials/M_Water','/Game/Shiomori/Materials/M_Shallows','/Game/Shiomori/Materials/M_Foam','/Game/Weather/M_BreakingSurf']:
    m=u.load_asset(path);assert isinstance(m,u.Material),path
    u.log('BIRD_WATER_BEFORE '+path+' lighting='+str(m.get_editor_property('translucency_lighting_mode'))+' pass='+str(m.get_editor_property('translucency_pass')))
    m.set_editor_property('disable_depth_test',False)
    m.set_editor_property('translucency_pass',u.MaterialTranslucencyPass.MTP_BEFORE_DOF)
    # Screen-space refraction samples foreground scene colour and displaces the
    # bird even when water itself passes the depth test. Surface normals,
    # reflection, wave geometry and foam provide the moving water detail.
    # The ocean now uses its own pre-translucency Single Layer Water pass.
    if not path.endswith(('M_Water','M_Shallows')):
        m.set_editor_property('refraction_method',u.RefractionMode.RM_NONE)
    if path.startswith('/Game/Birds/'):
        m.set_editor_property('translucency_lighting_mode',u.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
    lib.recompile_material(m);assert ela.save_loaded_asset(m)
tex=u.load_asset('/Game/Birds/T_KestrelTail');assert isinstance(tex,u.Texture2D)
u.log('TAIL_BEFORE compression='+str(tex.get_editor_property('compression_settings'))+' max='+str(tex.get_editor_property('max_texture_size'))+' bias='+str(tex.get_editor_property('lod_bias')))
tex.set_editor_property('compression_settings',u.TextureCompressionSettings.TC_BC7)
tex.set_editor_property('max_texture_size',4096)
tex.set_editor_property('lod_bias',0)
tex.set_editor_property('never_stream',True)
tex.set_editor_property('filter',u.TextureFilter.TF_TRILINEAR)
assert ela.save_loaded_asset(tex)
u.log('BIRD_WATER_RENDERING_READY')
