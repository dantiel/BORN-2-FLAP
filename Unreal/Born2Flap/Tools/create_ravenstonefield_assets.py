"""Create Ravenstonefield's portable UE materials, foliage and radio assets.

Run using UnrealEditor-Cmd -run=pythonscript -script=<this file>.
Source downloads are CC0; the supplied music is imported locally, not published.
"""
from pathlib import Path
import unreal as u

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'Saved/RavenstonefieldSource'
DEST = '/Game/Ravenstonefield'
TOOLS = u.AssetToolsHelpers.get_asset_tools()
EDIT = u.MaterialEditingLibrary
LIB = u.EditorAssetLibrary
MESH = u.get_editor_subsystem(u.StaticMeshEditorSubsystem) or u.new_object(u.StaticMeshEditorSubsystem)


def node(m, kind, **props):
    n = EDIT.create_material_expression(m, getattr(u, 'MaterialExpression' + kind))
    for k, v in props.items():
        n.set_editor_property(k, v)
    return n


def wire(a, b, pin, output=''):
    if not EDIT.connect_material_expressions(a, output, b, pin):
        raise RuntimeError('Cannot connect ' + b.get_name() + '.' + pin)


def out(n, prop, pin=''):
    EDIT.connect_material_property(n, pin, getattr(u.MaterialProperty, 'MP_' + prop))


def scalar(m, x):
    return node(m, 'Constant', r=x)


def vec(m, x, y, z):
    return node(m, 'Constant3Vector', constant=u.LinearColor(x, y, z))


def mat(name):
    path = DEST + '/Materials/' + name
    m = u.load_asset(path) if LIB.does_asset_exist(path) else TOOLS.create_asset(name, DEST + '/Materials', u.Material, u.MaterialFactoryNew())
    EDIT.delete_all_material_expressions(m)
    m.set_editor_property('used_with_instanced_static_meshes', True)
    return m


def custom(m, code, inputs, size=3):
    n = node(m, 'Custom', code=code, output_type=getattr(u.CustomMaterialOutputType, 'CMOT_FLOAT' + (str(size) if size > 1 else '1')))
    pins = []
    for k in inputs:
        pin = u.CustomInput()
        pin.set_editor_property('input_name', k)
        pins.append(pin)
    n.set_editor_property('inputs', pins)
    for k, v in inputs.items():
        wire(v, n, k)
    return n


def save(m):
    EDIT.layout_material_expressions(m)
    EDIT.recompile_material(m)
    if not LIB.save_loaded_asset(m):
        raise RuntimeError('Failed to save ' + m.get_path_name())


def import_asset(source, folder, name=''):
    task = u.AssetImportTask()
    task.filename = str(source)
    task.destination_path = folder
    task.destination_name = name
    task.automated = True
    task.replace_existing = True
    task.save = True
    TOOLS.import_asset_tasks([task])
    return [u.load_asset(p) for p in task.imported_object_paths]


def texture(m, path, normal=False, uv=None):
    t = u.load_asset(path)
    if not t:
        raise RuntimeError('Missing texture: ' + path)
    n = node(m, 'TextureSample', texture=t)
    if normal:
        n.set_editor_property('sampler_type', u.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    if uv:
        wire(uv, n, 'UVs')
    return n


def pbr(name, source, tint=(1., 1., 1.), metres=3.):
    for kind in ['Diffuse', 'nor_dx', 'Rough']:
        path = DEST + '/Textures/T_' + source + '_' + kind
        if not LIB.does_asset_exist(path):
            import_asset(SOURCE / source / (kind + '.jpg'), DEST + '/Textures', 'T_' + source + '_' + kind)
        t = u.load_asset(path)
        if kind == 'Rough':
            t.set_editor_property('srgb', False)
        if kind == 'nor_dx':
            t.set_editor_property('compression_settings', u.TextureCompressionSettings.TC_NORMALMAP)
            t.set_editor_property('srgb', False)
        LIB.save_loaded_asset(t)
    m = mat(name)
    m.set_editor_property('tangent_space_normal', False)
    pos, norm = node(m, 'WorldPosition'), node(m, 'VertexNormalWS')
    args = {'P': pos, 'N': norm, 'S': scalar(m, 100 * metres)}
    weights = 'float3 w=pow(abs(N),4); w/=max(dot(w,1),0.001); float3 p=P/S; '
    for kind, prop in [('Diffuse', 'BASE_COLOR'), ('Rough', 'ROUGHNESS')]:
        t = node(m, 'TextureObject', texture=u.load_asset(DEST + '/Textures/T_' + source + '_' + kind))
        code = weights + 'float3 c=Texture2DSample(T,TSampler,p.yz).rgb*w.x+Texture2DSample(T,TSampler,p.xz).rgb*w.y+Texture2DSample(T,TSampler,p.xy).rgb*w.z; '
        code += ('return c*float3(%f,%f,%f);' % tint) if kind == 'Diffuse' else 'return max(0.48,c.r);'
        out(custom(m, code, dict(args, T=t), 3 if kind == 'Diffuse' else 1), prop)
    t = node(m, 'TextureObject', texture=u.load_asset(DEST + '/Textures/T_' + source + '_nor_dx'))
    code = weights + '''
float3 a=Texture2DSample(T,TSampler,p.yz).rgb*2-1;
float3 b=Texture2DSample(T,TSampler,p.xz).rgb*2-1;
float3 c=Texture2DSample(T,TSampler,p.xy).rgb*2-1;
float3 detail=float3(0,a.x,a.y)*w.x+float3(b.x,0,b.y)*w.y+float3(c.x,c.y,0)*w.z;
return normalize(N+detail*0.45);'''
    out(custom(m, code, dict(args, T=t)), 'NORMAL')
    save(m)


def basic(name, color, rough=.7, metal=0., glow=0.):
    m = mat(name)
    out(vec(m, *color), 'BASE_COLOR')
    out(scalar(m, rough), 'ROUGHNESS')
    out(scalar(m, metal), 'METALLIC')
    if glow:
        out(vec(m, *(x * glow for x in color)), 'EMISSIVE_COLOR')
    save(m)


def environment():
    m = mat('M_Meadow')
    p, vc = node(m, 'WorldPosition'), node(m, 'VertexColor')
    uv = node(m, 'TextureCoordinate')
    grass = texture(m, '/Game/Nature/Textures/T_aerial_grass_rock_Diffuse', uv=uv)
    earth = texture(m, '/Game/Nature/Textures/T_forest_ground_04_Diffuse', uv=uv)
    stone = texture(m, '/Game/Nature/Textures/T_rock_04_Diffuse', uv=uv)
    # Vertex R: wet banks; G: rock; B: broad ground colour variation.
    code = '''float bank=saturate(C.r); float rock=saturate(C.g);
float macro=0.91+0.06*sin(P.x*0.00037+sin(P.y*0.00021))+0.04*sin(P.y*0.0011);
float3 field=G*float3(0.84,0.78,0.48);
float3 mud=E*float3(0.58,0.55,0.43);
return lerp(lerp(field,mud,bank),R,rock)*(0.83+C.b*0.2)*macro;'''
    out(custom(m, code, {'P': p, 'C': vc, 'G': grass, 'E': earth, 'R': stone}), 'BASE_COLOR')
    out(texture(m, '/Game/Nature/Textures/T_aerial_grass_rock_nor_dx', True), 'NORMAL', 'RGB')
    out(custom(m, 'return lerp(0.93,0.48,C.r);', {'C': vc}, 1), 'ROUGHNESS')
    save(m)
    m = mat('M_LakeWater')
    m.set_editor_property('shading_model', u.MaterialShadingModel.MSM_SINGLE_LAYER_WATER)
    out(vec(m, .018, .046, .037), 'BASE_COLOR')
    out(scalar(m, .085), 'ROUGHNESS')
    out(scalar(m, .5), 'SPECULAR')
    out(scalar(m, .035), 'OPACITY')
    p, t, vc = node(m, 'WorldPosition'), node(m, 'Time'), node(m, 'VertexColor')
    out(custom(m, '''float2 q=P.xy*.018;
float2 n=float2(cos(q.x+q.y*.39+T*1.3),sin(q.y*1.13-q.x*.27-T*.85))*.055;
n+=float2(sin(q.x*2.3+q.y*1.2-T*1.7),cos(q.y*2.1-q.x*.8+T*1.2))*.022;
return normalize(float3(n,1));''', {'P': p, 'T': t}), 'NORMAL')
    out(custom(m, 'return float3(0,0,sin(P.x*.006+P.y*.003+T*.8)*1.6+sin(P.y*.013-T*1.1)*.8);', {'P': p, 'T': t}), 'WORLD_POSITION_OFFSET')
    water = node(m, 'SingleLayerWaterMaterialOutput')
    wire(vec(m, .00065, .0014, .0011), water, 'ScatteringCoefficients')
    wire(vec(m, .0052, .0018, .0009), water, 'AbsorptionCoefficients')
    wire(scalar(m, .25), water, 'PhaseG')
    caustics = custom(m, '''float2 q=P.xy*.032;
q+=float2(sin(q.y*.63+T*.37),sin(q.x*.71-T*.31))*.7;
float a=pow(1-abs(sin(q.x+sin(q.y+T*.5))),16);
float b=pow(1-abs(sin(q.y*.93+sin(q.x-T*.43))),16);
return 1+(a+b)*0.65*saturate(C.r);''', {'P': p, 'T': t, 'C': vc}, 1)
    wire(caustics, water, 'ColorScaleBehindWater')
    save(m)
    m = mat('M_Rust')
    out(custom(m, '''float n=sin(P.x*.14)*sin(P.y*.17)*sin(P.z*.21);
return lerp(float3(.055,.065,.063),float3(.24,.085,.024),smoothstep(-.15,.25,n));''', {'P': node(m, 'WorldPosition')}), 'BASE_COLOR')
    out(scalar(m, .82), 'ROUGHNESS')
    out(scalar(m, .35), 'METALLIC')
    save(m)


def foliage():
    for source, alias in [('tree_small_02', 'SM_AutumnTree'), ('dead_tree_trunk', 'SM_Deadwood')]:
        folder = DEST + '/Source/' + source
        if not LIB.does_directory_exist(folder):
            import_asset(SOURCE / source / (source + '.gltf'), folder)
        assets = [u.load_asset(p) for p in LIB.list_assets(folder)]
        meshes = [a for a in assets if isinstance(a, u.StaticMesh)]
        if not meshes:
            raise RuntimeError('Missing source mesh: ' + source)
        diffuse = [a for a in assets if isinstance(a, u.Texture2D) and 'diff' in a.get_name().lower()]
        for tex in diffuse:
            is_leaf = 'leaves' in tex.get_name().lower()
            name = ('M_AutumnLeaves' if is_leaf else 'M_' + tex.get_name())
            m = mat(name)
            s = node(m, 'TextureSample', texture=tex)
            if is_leaf:
                # Preserve photographic leaf detail; pigment moves from green to
                # mustard/copper through per-instance variation, never neon orange.
                c = custom(m, '''float lum=dot(C,float3(.25,.6,.15));
return lerp(float3(.28,.09,.018),float3(.64,.37,.073),R)*lum*2.0;''',
                           {'C': s, 'R': node(m, 'PerInstanceRandom')})
                out(c, 'BASE_COLOR')
                m.set_editor_property('two_sided', True)
                m.set_editor_property('blend_mode', u.BlendMode.BLEND_MASKED)
                m.set_editor_property('shading_model', u.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE)
                alpha_path = DEST + '/Textures/T_LeavesAlpha'
                if not LIB.does_asset_exist(alpha_path):
                    import_asset(SOURCE / source / 'leaves_alpha.png', DEST + '/Textures', 'T_LeavesAlpha')
                out(texture(m, alpha_path), 'OPACITY_MASK', 'R')
                m.set_editor_property('opacity_mask_clip_value', .38)
                out(vec(m, .22, .12, .025), 'SUBSURFACE_COLOR')
                out(custom(m, '''float height=saturate(P.z/1800);
return float3(sin(T*1.1+P.x*.002)*2.5,sin(T*.83+P.y*.003)*1.7,0)*height;''',
                           {'P': node(m, 'WorldPosition'), 'T': node(m, 'Time')}), 'WORLD_POSITION_OFFSET')
            else:
                out(s, 'BASE_COLOR', 'RGB')
            out(scalar(m, .86), 'ROUGHNESS')
            save(m)
            token = 'leaves' if is_leaf else ('branch' if 'branch' in tex.get_name().lower() else '')
            for mesh in meshes:
                for i, slot in enumerate(mesh.static_materials):
                    old = slot.material_interface
                    n = old.get_name().lower() if old else ''
                    if (token and token in n) or (not token and 'leaves' not in n and 'branch' not in n):
                        mesh.set_material(i, m)
        mesh = sorted(meshes, key=lambda a: a.get_name())[0]
        if LIB.get_metadata_tag(mesh, 'RavenLODs') != '1':
            opts = u.StaticMeshReductionOptions(auto_compute_lod_screen_size=False,
                reduction_settings=[u.StaticMeshReductionSettings(percent_triangles=p, screen_size=s)
                                    for p, s in [(.2, 1.), (.045, .25), (.009, .095), (.002, .035)]])
            MESH.set_lods(mesh, opts)
            LIB.set_metadata_tag(mesh, 'RavenLODs', '1')
        LIB.save_loaded_asset(mesh)
        alias_path = DEST + '/Meshes/' + alias
        if not LIB.does_asset_exist(alias_path):
            LIB.duplicate_asset(mesh.get_path_name(), alias_path)
    # Autumn grass uses existing detailed grass geometry and cutout atlas.
    m = mat('M_DryGrass')
    m.set_editor_property('two_sided', True)
    m.set_editor_property('blend_mode', u.BlendMode.BLEND_MASKED)
    m.set_editor_property('shading_model', u.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE)
    out(texture(m, '/Game/Nature/T_grass_medium_01_Alpha'), 'OPACITY_MASK', 'R')
    out(custom(m, 'return lerp(float3(.15,.19,.065),float3(.37,.28,.12),R);', {'R': node(m, 'PerInstanceRandom')}), 'BASE_COLOR')
    out(vec(m, .13, .15, .035), 'SUBSURFACE_COLOR')
    out(scalar(m, .9), 'ROUGHNESS')
    save(m)


def radio():
    source = Path.home() / 'Downloads/TurboRaven.mp3'
    if not source.exists():
        raise RuntimeError('TURBORAVEN source missing: ' + str(source))
    path = DEST + '/Audio/TURBORAVEN'
    if not LIB.does_asset_exist(path):
        import_asset(source, DEST + '/Audio', 'TURBORAVEN')
    wave = u.load_asset(path)
    if not isinstance(wave, u.SoundWave):
        raise RuntimeError('TURBORAVEN did not import as a SoundWave')
    wave.set_editor_property('looping', True)
    wave.set_editor_property('volume', .55)
    LIB.save_loaded_asset(wave)
    u.log('RAVENSTONEFIELD_AUDIO duration=' + str(wave.duration))


def main():
    for name, source, tint, size in [
        ('M_Stone', 'castle_wall_slates', (.85,.88,.80), 3.),
        ('M_Roof', 'clay_roof_tiles', (.65,.52,.4), 3.),
        ('M_Concrete', 'concrete_moss', (.86,.89,.85), 4.),
        ('M_Plaster', 'clay_plaster', (.93,.88,.72), 3.),
        ('M_Timber', 'weathered_planks', (.59,.49,.38), 2.5)]:
        pbr(name, source, tint, size)
    environment()
    for name, color, rough, metal, glow in [
        ('M_Slate', (.048,.058,.06), .78, 0, 0),
        ('M_Window', (.021,.033,.031), .18, .25, 0),
        ('M_Ivory', (.68,.63,.45), .85, 0, 0),
        ('M_Teal', (.025,.10,.085), .72, .2, 0),
        ('M_RadioDial', (.19,.55,.31), .3, 0, 3),
        ('M_Amber', (.9,.42,.10), .3, 0, 6),
        ('M_Bone', (.42,.36,.24), .94, 0, 0)]:
        basic(name, color, rough, metal, glow)
    foliage()
    radio()
    LIB.save_directory(DEST, only_if_is_dirty=True, recursive=True)
    u.log('RAVENSTONEFIELD_ASSETS_READY')


main()
