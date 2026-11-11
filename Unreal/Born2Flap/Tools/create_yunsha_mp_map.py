"""Yunsha Mesh Terrain rebuild: the same Cloud-Sand Gorge world on the
MeshPartition pipeline (the WOSHI-verified route).

Deterministic (seed 221022, identical to create_yunsha_map.py). The native
dune sampler (Born2FlapYunsha.ground_height + sand_relief) stays the source of
truth for flight and gameplay collision; ONE continuous MeshPartition base
surface replaces the retired 6x6 dune tile field. The excavation pit is
expressed as a MeshPartition Boolean carve (the old pipeline skipped tile
triangles there), and the lake shore/excavation get local remesh modifiers.
All dressing (architecture, oasis, excavation, escarpment, audio, views) is
retained verbatim from create_yunsha_map.py.

Map: /Game/Yunsha/Maps/YUNSHA (overwrites the tile-based world; back up first).
Backend: terrain_backend.MeshPartitionBackend, in-process
WorldPartitionMeshPartitionBuilder when B2F_INPROCESS_BUILD=1 (default route).
"""
import json
import math
import re
import random
import struct
import sys
import wave
from pathlib import Path

import unreal as u

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from yunsha_escarpment import noise as erosion_noise
from project_guard import require_project
from terrain_backend import make_backend

require_project()
A = u.AssetToolsHelpers.get_asset_tools()
E = u.EditorAssetLibrary
M = u.MaterialEditingLibrary
ROOT = '/Game/Yunsha'
rng = random.Random(221022)
for folder in ('Maps', 'Meshes', 'Materials', 'Audio'):
    E.make_directory(ROOT + '/' + folder)


def expr(mat, kind, **properties):
    n = M.create_material_expression(mat, getattr(u, 'MaterialExpression' + kind))
    for k, v in properties.items():
        n.set_editor_property(k, v)
    return n


def link(a, b, pin, output=''):
    assert M.connect_material_expressions(a, output, b, '' if pin == 'Input' else pin), 'Invalid material connection ' + pin


def out(n, prop, output=''):
    M.connect_material_property(n, output, getattr(u.MaterialProperty, 'MP_' + prop))


def scalar(m, value):
    return expr(m, 'Constant', r=value)


def color(m, c):
    return expr(m, 'Constant3Vector', constant=u.LinearColor(*c, 1))


def custom(mat, code, inputs):
    n = expr(mat, 'Custom', code=code, output_type=u.CustomMaterialOutputType.CMOT_FLOAT3)
    pins = []
    for name in inputs:
        p = u.CustomInput()
        p.set_editor_property('input_name', name)
        pins.append(p)
    n.set_editor_property('inputs', pins)
    for name, value in inputs.items():
        link(value, n, name)
    return n


def material_usage_flags(m):
    """Enable the UE5.8 usage flags programmatically-built materials need."""
    m.set_editor_property('used_with_nanite', True)
    m.set_editor_property('used_with_instanced_static_meshes', True)


def material(name, tint, texture=None, metal=0., emission=0., cloud=False, mural=False):
    path = ROOT + '/Materials/M_' + name
    mat = u.load_asset(path) if E.does_asset_exist(path) else A.create_asset('M_' + name, ROOT + '/Materials', u.Material, u.MaterialFactoryNew())
    M.delete_all_material_expressions(mat)
    base = color(mat, tint)
    if texture:
        tex = u.load_asset(texture)
        assert tex, texture
        pos = expr(mat, 'WorldPosition')
        div = expr(mat, 'Divide')
        link(pos, div, 'A')
        link(scalar(mat, 400), div, 'B')
        normal = expr(mat, 'VertexNormalWS')
        absolute = expr(mat, 'Abs')
        link(normal, absolute, 'Input')
        projections = []
        for mask, axis in [((False, True, True), (True, False, False)), ((True, False, True), (False, True, False)), ((True, True, False), (False, False, True))]:
            uv = expr(mat, 'ComponentMask', r=mask[0], g=mask[1], b=mask[2])
            link(div, uv, 'Input')
            sample = expr(mat, 'TextureSample', texture=tex)
            link(uv, sample, 'UVs')
            weight = expr(mat, 'ComponentMask', r=axis[0], g=axis[1], b=axis[2])
            link(absolute, weight, 'Input')
            mul = expr(mat, 'Multiply')
            link(sample, mul, 'A', 'RGB')
            link(weight, mul, 'B')
            projections.append(mul)
        add = expr(mat, 'Add')
        link(projections[0], add, 'A')
        link(projections[1], add, 'B')
        alltex = expr(mat, 'Add')
        link(add, alltex, 'A')
        link(projections[2], alltex, 'B')
        mul = expr(mat, 'Multiply')
        link(alltex, mul, 'A')
        link(base, mul, 'B')
        base = mul
    out(base, 'BASE_COLOR')
    out(scalar(mat, .87 if not metal else .32), 'ROUGHNESS')
    out(scalar(mat, metal), 'METALLIC')
    if emission:
        glow = expr(mat, 'Multiply')
        link(base, glow, 'A')
        link(scalar(mat, emission), glow, 'B')
        out(glow, 'EMISSIVE_COLOR')
    if cloud:
        mat.set_editor_property('blend_mode', u.BlendMode.BLEND_TRANSLUCENT)
        mat.set_editor_property('two_sided', True)
        f = expr(mat, 'Fresnel', exponent=1.4, base_reflect_fraction=0.)
        inv = expr(mat, 'OneMinus')
        link(f, inv, 'Input')
        mul = expr(mat, 'Multiply')
        link(inv, mul, 'A')
        link(scalar(mat, .12), mul, 'B')
        out(mul, 'OPACITY')
    if mural:
        reveal = expr(mat, 'ScalarParameter', parameter_name='Reveal', default_value=1.)
        glow = expr(mat, 'ScalarParameter', parameter_name='Afterglow', default_value=0.)
        blend = expr(mat, 'LinearInterpolate')
        link(color(mat, (.31, .13, .07)), blend, 'A')
        link(base, blend, 'B')
        link(reveal, blend, 'Alpha')
        out(blend, 'BASE_COLOR')
        em = expr(mat, 'Multiply')
        link(base, em, 'A')
        link(glow, em, 'B')
        out(em, 'EMISSIVE_COLOR')
        mat.set_editor_property('two_sided', True)
    material_usage_flags(mat)
    M.recompile_material(mat)
    assert E.save_loaded_asset(mat)
    return mat


def yunsha_sky_material():
    """Yunsha desert-sky optics — the gorge's analogue of Shiomori's ice-halo
    (M_SunParhelion). Screen-space post-process, additive over the scene after
    tonemapping. Four effects: a broad warm dust aureole around the sun (dust
    scatters wide, unlike the crisp 22-deg ice ring), a dust sun-pillar at a low
    dry sun, a horizon mirage band (Fata Morgana), and a dark-sky star field at
    night. Driven by SunDirection/Dust/Mirage/Night/SkyWind set at runtime by
    ABorn2FlapWeather, so it follows weather, day, season and wind. The sun disc
    itself is NOT re-drawn here (DirectionalLight + SkyAtmosphere render it)."""
    path = ROOT + '/Materials/M_YunshaSky'
    m = u.load_asset(path) if E.does_asset_exist(path) else A.create_asset('M_YunshaSky', ROOT + '/Materials', u.Material, u.MaterialFactoryNew())
    M.delete_all_material_expressions(m)
    m.set_editor_property('material_domain', u.MaterialDomain.MD_POST_PROCESS)
    m.set_editor_property('blend_mode', u.BlendMode.BLEND_OPAQUE)
    m.set_editor_property('blendable_location', u.BlendableLocation.BL_SCENE_COLOR_AFTER_TONEMAPPING)
    m.set_editor_property('shading_model', u.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property('two_sided', True)
    sd = expr(m, 'VectorParameter', parameter_name='SunDirection', default_value=u.LinearColor(.3, .5, .8, 0.))
    dust = expr(m, 'ScalarParameter', parameter_name='Dust', default_value=.5)
    mirage = expr(m, 'ScalarParameter', parameter_name='Mirage', default_value=0.)
    night = expr(m, 'ScalarParameter', parameter_name='Night', default_value=0.)
    skywind = expr(m, 'VectorParameter', parameter_name='SkyWind', default_value=u.LinearColor(0., 0., 0., 0.))
    view = expr(m, 'CameraVectorWS')
    scene = expr(m, 'SceneTexture', scene_texture_id=u.SceneTextureId.PPI_POST_PROCESS_INPUT0)
    depth = expr(m, 'SceneTexture', scene_texture_id=u.SceneTextureId.PPI_SCENE_DEPTH)
    code = '''float3 R = normalize(-ViewToCamera);
    float3 S = normalize(SunDir);
    float ang = acos(clamp(dot(R, S), -1.0, 1.0));
    float elS = asin(clamp(S.z, -1.0, 1.0));
    float elR = asin(clamp(R.z, -1.0, 1.0));
    float dEl = elR - elS;
    float dAz = atan2(R.y, R.x) - atan2(S.y, S.x);
    dAz = atan2(sin(dAz), cos(dAz));

    // Above-horizon mask: dust optics live in the sky; terrain occludes them.
    float skyMask = smoothstep(0.0, 0.025, R.z) * step(10000000.0, SceneDepth.r);
    float sunDeg = elS * 57.2958;
    float lowSun = 1.0 - smoothstep(4.0, 20.0, sunDeg);
    // Sun optics fade out at night (only the star field remains).
    float dayOptics = 1.0 - Night;

    // (1) Dust aureole: broad warm scattering lobe around the sun. Widens and
    // reddens as the sun lowers into the haze; no crisp ice ring here.
    float aureole = exp(-pow(ang / (0.30 + 0.30 * lowSun), 2.0)) * Dust * dayOptics;
    float3 aureoleCol = lerp(float3(1.00, 0.60, 0.32), float3(1.00, 0.78, 0.50), saturate(ang * 3.0));

    // (2) Dust sun-pillar: vertical light column from horizontally aligned dust
    // grains, strongest at a low dry sun (dawn/sunset).
    float pillar = exp(-pow(abs(dAz) * 26.0, 2.0)) * exp(-pow(dEl * 3.0, 2.0)) * lowSun * Dust * dayOptics;
    float3 pillarCol = lerp(float3(1.00, 0.42, 0.22), float3(1.00, 0.72, 0.40), saturate(dEl * 2.0 + 0.5));

    // (3) Horizon mirage band: a warm silver shimmer along the horizon (the
    // desert Fata Morgana), only in dry clear air; wind drifts and breaks it.
    float horizon = 1.0 - smoothstep(0.0, 0.030, abs(R.z - 0.010));
    float shimmer = 0.55 + 0.45 * sin(R.x * 38.0 + SkyWind.x * 0.6 + SkyWind.w * 0.7)
                            * sin(R.y * 31.0 + SkyWind.y * 0.6);
    float mirageBand = horizon * Mirage * shimmer * dayOptics;
    float3 mirageCol = float3(1.00, 0.88, 0.68);

    // (4) Dark-sky star field (night only): cell-hash stars over the desert's
    // clear black sky, twinkling faintly. The moon is the DirectionalLight.
    float3 starGrid = floor(R * 220.0);
    float starHash = frac(sin(dot(starGrid, float3(12.9898, 78.233, 37.719))) * 43758.5453);
    float star = smoothstep(0.9986, 0.9992, starHash) * step(0.02, R.z);
    float twinkle = 0.65 + 0.35 * sin(SkyWind.w * 2.4 + starHash * 91.0);
    float3 starCol = star * twinkle * float3(0.82, 0.88, 1.00) * Night;

    float3 contrib = aureoleCol * aureole * 0.85
                   + pillarCol * pillar * 0.55
                   + mirageCol * mirageBand * 0.40
                   + starCol;
    return SceneColor.rgb + (1.0 - saturate(SceneColor.rgb)) * (1.0 - exp(-contrib));'''
    h = expr(m, 'Custom', code=code, output_type=u.CustomMaterialOutputType.CMOT_FLOAT3)
    pins = []
    for nm in ('SunDir', 'Dust', 'Mirage', 'Night', 'SkyWind', 'ViewToCamera', 'SceneColor', 'SceneDepth'):
        p = u.CustomInput()
        p.set_editor_property('input_name', nm)
        pins.append(p)
    h.set_editor_property('inputs', pins)
    link(sd, h, 'SunDir')
    link(dust, h, 'Dust')
    link(mirage, h, 'Mirage')
    link(night, h, 'Night')
    # UE5.8: VectorParameter output 0 is RGB (float3) — the float4 (with the .w
    # time channel) lives on the RGBA pin. Connect that, or SkyWind.w breaks.
    link(skywind, h, 'SkyWind', 'RGBA')
    link(view, h, 'ViewToCamera')
    link(scene, h, 'SceneColor', 'Color')
    link(depth, h, 'SceneDepth', 'Color')
    out(h, 'EMISSIVE_COLOR')
    material_usage_flags(m)
    M.recompile_material(m)
    assert E.save_loaded_asset(m)
    return m


sand = material('WindRippleSand', (1.0, .77, .48))
from yunsha_sand import normal_code
out(custom(sand, normal_code(), {
    'P': expr(sand, 'WorldPosition'), 'N': expr(sand, 'VertexNormalWS')}), 'NORMAL')
sand.set_editor_property('tangent_space_normal', False)
M.recompile_material(sand)
E.save_loaded_asset(sand)
from yunsha_oasis import ground_material, plant as plant_oasis, plant_lake
ground_material(sand, expr, custom, out)
rock = material('RedSandstone', (1.12, .78, .53), '/Game/Shiomori/Cliffs/Textures/T_Cliff_rock_face_03_Diffuse')
# Photographic mineral variation and relief at two scales break up the smooth
# generated silhouette. World-space normals work across the carved interiors.
pos = expr(rock, 'WorldPosition')
normal = expr(rock, 'VertexNormalWS')
rocktex = expr(rock, 'TextureObject', texture=u.load_asset('/Game/Shiomori/Cliffs/Textures/T_Cliff_rock_face_03_Diffuse'))
rocknormal = expr(rock, 'TextureObject', texture=u.load_asset('/Game/Shiomori/Cliffs/Textures/T_Cliff_rock_face_03_nor_dx'))
out(custom(rock, '''float3 w=pow(abs(N),4);w/=max(.0001,w.x+w.y+w.z);
float3 a=Texture2DSample(T,TSampler,P.yz/580).rgb*w.x+Texture2DSample(T,TSampler,P.xz/580).rgb*w.y+Texture2DSample(T,TSampler,P.xy/580).rgb*w.z;
float3 b=Texture2DSample(T,TSampler,P.yz/1900+3.7).rgb*w.x+Texture2DSample(T,TSampler,P.xz/1900+3.7).rgb*w.y+Texture2DSample(T,TSampler,P.xy/1900+3.7).rgb*w.z;
float band=.5+.5*sin(P.z*.0018+sin(P.y*.00016)*2+sin(P.x*.00021));
return lerp(a,b,.32)*lerp(float3(.89,.69,.52),float3(1.12,.9,.72),band);''', {'P': pos, 'N': normal, 'T': rocktex}), 'BASE_COLOR')
out(custom(rock, '''float3 w=pow(abs(N),4);w/=max(.0001,w.x+w.y+w.z);
float3 x=Texture2DSample(T,TSampler,P.yz/580).rgb*2-1;
float3 y=Texture2DSample(T,TSampler,P.xz/580).rgb*2-1;
float3 z=Texture2DSample(T,TSampler,P.xy/580).rgb*2-1;
return normalize(N+.7*(float3(0,x.x,x.y)*w.x+float3(y.x,0,y.y)*w.y+float3(z.x,z.y,0)*w.z));''', {'P': pos, 'N': normal, 'T': rocknormal}), 'NORMAL')
rock.set_editor_property('tangent_space_normal', False)
M.recompile_material(rock)
E.save_loaded_asset(rock)
pale = material('PaleCarvedStone', (.78, .64, .43), '/Game/Shiomori/Textures/T_ConcreteWorn_Diffuse')
dark = material('DeepCopper', (.19, .075, .038))
jade = material('JadeTiles', (.07, .26, .19), metal=.25)
cinnabar = material('Cinnabar', (.35, .055, .028))
gold = material('SunGold', (.65, .38, .075), metal=.72)
water = material('JadeSpring', (.025, .22, .17), metal=.55)
mirror = material('SilverMirror', (.40, .52, .58), metal=.96)
cloud = material('StandingCloud', (.64, .69, .68), cloud=True)
pigment = material('SunGlass', (.88, .38, .09), mural=True)
lantern = material('Lantern', (.95, .32, .055), emission=5)
material('LiftMote', (.66, .42, .19), emission=.2)
# Shared runtime bird material is immutable during map-only rebuilds.
if not E.does_asset_exist(ROOT + '/Materials/M_Pterosaur'):
    ptero = material('Pterosaur', (.7, .4, .15))
    vertex = expr(ptero, 'VertexColor')
    out(vertex, 'BASE_COLOR')
    ptero.set_editor_property('two_sided', True)
    M.recompile_material(ptero)
    E.save_loaded_asset(ptero)


generated_meshes = set()


def mesh(name, verts, tris, mat, collision=True, recompute_normals=None):
    name = re.sub(r'[^A-Za-z0-9_]', '_', name)
    path = ROOT + '/Meshes/SM_' + name
    generated_meshes.add(path)
    sm = u.load_asset(path) if E.does_asset_exist(path) else A.create_asset('SM_' + name, ROOT + '/Meshes', u.StaticMesh, None)
    md = sm.create_static_mesh_description()
    md.reserve_new_vertices(len(verts))
    md.reserve_new_vertex_instances(len(verts))
    md.reserve_new_polygons(len(tris))
    pg = md.create_polygon_group()
    md.set_polygon_group_material_slot_name(pg, 'Surface')
    sm.set_editor_property('static_materials', [u.StaticMaterial(material_interface=mat, material_slot_name='Surface')])
    ids = []
    for x, y, z in verts:
        v = md.create_vertex()
        md.set_vertex_position(v, u.Vector(x, y, z))
        i = md.create_vertex_instance(v)
        md.set_vertex_instance_uv(i, u.Vector2D(x / 400, y / 400), 0)
        ids.append(i)
    for a, b, c in tris:
        md.create_triangle(pg, [ids[a], ids[c], ids[b]])
    if recompute_normals is not None:
        editor = u.get_editor_subsystem(u.StaticMeshEditorSubsystem) or u.new_object(u.StaticMeshEditorSubsystem)
        bs = editor.get_lod_build_settings(sm, 0)
        bs.set_editor_property('recompute_normals', recompute_normals)
        bs.set_editor_property('recompute_tangents', True)
        bs.set_editor_property('use_mikk_t_space', True)
        editor.set_lod_build_settings(sm, 0, bs)
    sm.set_editor_property('allow_cpu_access', True)
    sm.build_from_static_mesh_descriptions([md], False, False)
    if collision:
        sm.get_editor_property('body_setup').set_editor_property('collision_trace_flag', u.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
    if name in ('BoneArchiveTerrain', 'DistantSandSea'):
        u.Born2FlapTerrainBuilder.smooth_yunsha_terrain(sm, u.Vector2D(100000 if name == 'BoneArchiveTerrain' else 0, 0), name == 'DistantSandSea')
    sm.set_material(0, mat)
    assert E.save_loaded_asset(sm)
    return sm


def enable_nanite(sm):
    # Best-effort Nanite for dense opaque geometry. Masked foliage or meshes
    # that still need CPU collision are skipped so the world always generates.
    try:
        sm.set_editor_property('nanite_settings', u.MeshNaniteSettings(enabled=True))
        sm.build()
        u.log('YUNSHA_NANITE ' + sm.get_name())
    except Exception as e:
        u.log_warning('YUNSHA_NANITE_SKIP ' + sm.get_name() + ' ' + str(e))


def actor(label, sm, p=(0, 0, 0), scale=(1, 1, 1), yaw=0, collision=True, tags=()):
    a = u.EditorLevelLibrary.spawn_actor_from_class(u.StaticMeshActor, u.Vector(*p), u.Rotator(yaw=yaw))
    a.set_actor_label('Yunsha ' + label)
    a.tags = ['Yunsha', *tags]
    c = a.static_mesh_component
    c.set_static_mesh(sm)
    c.set_collision_profile_name('BlockAll' if collision else 'NoCollision')
    a.set_actor_scale3d(u.Vector(*scale))
    return a


# Shared primitive meshes are instanced per material, not thousands of actors.
instances = {}
for name, mat in [('Rock', rock), ('Pale', pale), ('Dark', dark), ('Jade', jade), ('Red', cinnabar), ('Gold', gold), ('Lantern', lantern)]:
    a = u.EditorLevelLibrary.spawn_actor_from_class(u.Born2FlapYunshaBatch, u.Vector())
    a.set_actor_label('Yunsha masonry ' + name)
    a.tags = ['Yunsha']
    c = a.instances
    c.set_static_mesh(u.load_asset('/Engine/BasicShapes/Cube'))
    c.set_material(0, mat)
    c.set_collision_profile_name('BlockAll')
    instances[name] = c


def box(kind, p, size, yaw=0):
    instances[kind].add_instance(u.Transform(location=u.Vector(*p), rotation=u.Rotator(yaw=yaw), scale=u.Vector(*(v / 100 for v in size))), False)


# Native height sampler is the source of truth for both collision and flight.
ground = u.Born2FlapYunsha.ground_height


def _smoothstep(e0, e1, x):
    t = 0.0 if x <= e0 else (1.0 if x >= e1 else (x - e0) / (e1 - e0))
    return t * t * (3.0 - 2.0 * t)


def _lerp(a, b, t):
    return a + (b - a) * t


def sand_relief(x, y):
    """Wind-aligned dune relief (cm) baked into the mesh so the desert reads as
    real unevenness instead of normal-mapped detail on a flat surface."""
    m = _smoothstep(-0.6, 0.6, math.sin(x * 0.000013 - y * 0.000009))
    wx = _lerp(0.85, -0.46, m)
    wy = _lerp(0.53, 0.89, m)
    L = math.hypot(wx, wy)
    wx /= L
    wy /= L
    along = x * wx + y * wy
    across = -x * wy + y * wx
    ripple = 180.0 * math.sin(along * 0.002513 + 1.7 * math.sin(along * 0.00005))
    undulation = 120.0 * math.sin(across * 0.001047 + 0.8 * math.sin(along * 0.000032))
    return ripple + undulation


def surf(x, y):
    # The native sampler (Born2FlapYunsha.ground_height) bakes the wind
    # relief in — one height source of truth for render, collision, flight
    # and props. Adding sand_relief again here would double the relief.
    return ground(x, y)


# ---------------------------------------------------------------------------
# Mesh Terrain ground: one continuous base surface on the native sampler.
# ---------------------------------------------------------------------------
backend = make_backend('meshpartition', ROOT, 'YUNSHA')
world = backend.begin()
world.get_world_settings().set_editor_property('force_no_precomputed_lighting', True)


def grid_surface(x0, y0, x1, y1, nx, ny, zfn):
    verts = []
    for j in range(ny + 1):
        for i in range(nx + 1):
            x = x0 + (x1 - x0) * i / nx
            y = y0 + (y1 - y0) * j / ny
            verts.append((x, y, zfn(x, y)))
    tris = []
    for j in range(ny):
        for i in range(nx):
            a = j * (nx + 1) + i
            b = a + 1
            c = a + (nx + 1)
            d = c + 1
            tris.append((a, b, c))
            tris.append((b, d, c))
    return verts, tris


def closed_box(cx, cy, cz, sx, sy, sz):
    # Outward-facing closed tool mesh (UE left-handed Z-up: the up-facing top
    # must read clockwise from above). The MeshPartition boolean classifier
    # decides inside/outside by winding: an inside-out box trims NOTHING and,
    # under Subtract, caps the hole with its (now exposed) top face — a
    # floating collision sheet at the box top. The previous winding had every
    # face inverted; Trim then cut nothing and Subtract left the z=40000 sheet.
    x0, y0, z0 = cx - sx / 2, cy - sy / 2, cz - sz / 2
    x1, y1, z1 = cx + sx / 2, cy + sy / 2, cz + sz / 2
    verts = [(x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0),
             (x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)]
    tris = [
        (0, 1, 2), (0, 2, 3),  # bottom (-Z, faces down)
        (4, 6, 5), (4, 7, 6),  # top (+Z, faces up)
        (0, 5, 1), (0, 4, 5),  # south (-Y, faces -Y)
        (3, 6, 7), (3, 2, 6),  # north (+Y, faces +Y)
        (1, 6, 2), (1, 5, 6),  # east (+X, faces +X)
        (0, 7, 4), (0, 3, 7),  # west (-X, faces -X)
    ]
    return verts, tris


# Six-kilometre desert, the same footprint as the retired 6x6 tile field, now
# one continuous base at 15 m cells. The MP builder re-derives the surface at
# its own texel density, so the base only needs to resolve the macro dune
# shapes; the ripple shader (DesertRelief) carries the fine detail.
ground_verts, ground_tris = grid_surface(-300000.0, -180000.0, 300000.0, 420000.0,
                                         400, 400, surf)
backend.commit_surface('Dunes', ground_verts, ground_tris, sand, slot=0)

# Close-up zones get a local remesh (non-destructive): lake shore + excavation.
backend.commit_remesh('LakeRemesh', (0.0, -52500.0, 0.0),
                      (18000.0, 11000.0, 8000.0), 300.0)
backend.commit_remesh('ExcavationRemesh', (148000.0, -11000.0, 0.0),
                      (24000.0, 20000.0, 8000.0), 300.0)

# The excavation replaces the dune surface with authored archive terrain; cut
# the base out there (the old tile pipeline skipped these triangles instead).
# Trim (not Subtract): the authored BoneArchiveTerrain excavation mesh covers
# the hole. Subtract would cap the hole into a closed surface — the closed
# tool box's top face then survives as a floating, collision-bearing sheet at
# z=40000 over the whole excavation (the invisible "shifted terrain" bug).
carve_verts, carve_tris = closed_box(147000.0, -11000.0, -20000.0,
                                     46000.0, 38000.0, 120000.0)
backend.commit_boolean('ExcavationPit', carve_verts, carve_tris, u.Transform(),
                       operation='trim', channel='Sandstone', weight=1.0)
backend.tick('surfaces')

# Water follows the actual terrain zero-contour so the shoreline is smooth and
# matches the crescent basin exactly (the terrain now stays continuous beneath
# the water instead of skipping lake tiles).
verts = []
tris = []
CELL = 120
for y in range(-62000, -42000, CELL):
    for x in range(-15800, 15801, CELL):
        corners = [(x, y), (x + CELL, y), (x + CELL, y + CELL), (x, y + CELL)]
        if all(ground(px, py) < -4 for px, py in corners):
            k = len(verts)
            verts.extend((px, py, 0) for px, py in corners)
            tris.extend([(k, k + 1, k + 2), (k, k + 2, k + 3)])
actor('Lunya crescent spring', mesh('Lunya', verts, tris, water, False), collision=False)

# Lotus: a floating pad and a cup of petals scattered on the crescent so the
# lake reads inhabited and alive. One shared pad + one shared flower mesh are
# instanced via batch actors, not dozens of unique meshes.
lotus_pad_mat = material('LotusPad', (.08, .36, .19))
lotus_petal_mat = material('LotusPetal', (.86, .46, .60))
lotus_petal_mat.set_editor_property('two_sided', True)
M.recompile_material(lotus_pad_mat)
E.save_loaded_asset(lotus_pad_mat)
M.recompile_material(lotus_petal_mat)
E.save_loaded_asset(lotus_petal_mat)


def fan(radius, z, n):
    v = [(0, 0, z)]
    t = []
    for j in range(n):
        a = math.tau * j / n
        v.append((radius * math.cos(a), radius * math.sin(a), z))
    for j in range(n):
        t.append((0, j + 1, (j + 1) % n + 1))
    return v, t


padv, patris = fan(85, 2, 26)
petv = []
pettris = []


def petal(a0, abase, rbase, rtip, ztip):
    b1 = (rbase * math.cos(a0 - abase), rbase * math.sin(a0 - abase), 2)
    b2 = (rbase * math.cos(a0 + abase), rbase * math.sin(a0 + abase), 2)
    tip = (rtip * math.cos(a0), rtip * math.sin(a0), ztip)
    k = len(petv)
    petv.extend([b1, b2, tip])
    pettris.append((k, k + 1, k + 2))


for j in range(7):
    petal(math.tau * j / 7, .34, 16, 42, 32)
for j in range(5):
    petal(math.tau * j / 5 + math.tau / 10, .30, 8, 12, 52)
pad_mesh = mesh('LotusPad', padv, patris, lotus_pad_mat, False)
petal_mesh = mesh('LotusPetal', petv, pettris, lotus_petal_mat, False)
pad_batch = u.EditorLevelLibrary.spawn_actor_from_class(u.Born2FlapYunshaBatch, u.Vector())
pad_batch.set_actor_label('Yunsha lotus pads')
pad_batch.tags = ['Yunsha', 'YunshaLakeFlora']
pad_batch.instances.set_static_mesh(pad_mesh)
pad_batch.instances.set_collision_profile_name('NoCollision')
flower_batch = u.EditorLevelLibrary.spawn_actor_from_class(u.Born2FlapYunshaBatch, u.Vector())
flower_batch.set_actor_label('Yunsha lotus flowers')
flower_batch.tags = ['Yunsha', 'YunshaLakeFlora']
flower_batch.instances.set_static_mesh(petal_mesh)
flower_batch.instances.set_collision_profile_name('NoCollision')
lotus_rng = random.Random(110519)
placed = 0
for attempt in range(6000):
    if placed >= 120:
        break
    x = lotus_rng.uniform(-15000, 15000)
    y = lotus_rng.uniform(-61500, -42500)
    if not u.Born2FlapYunsha.spring_contains(x, y) or ground(x, y) > -8:
        continue
    s = lotus_rng.uniform(.7, 1.4)
    ang = lotus_rng.uniform(0, 360)
    tr = u.Transform(location=u.Vector(x, y, 0), rotation=u.Rotator(yaw=ang), scale=u.Vector(s, s, s))
    pad_batch.instances.add_instance(tr, False)
    flower_batch.instances.add_instance(tr, False)
    placed += 1


# Authored architecture preserves its source geometry, UVs and weathering.
def imported(name):
    objects = [u.load_asset(p) for p in E.list_assets('/Game/WorldAssets/Yunsha/' + name)]
    found = [o for o in objects if isinstance(o, u.StaticMesh)]
    assert len(found) == 1, 'Missing retained architectural asset ' + name
    return found[0]


hall = imported('TempleHalls')
pagoda = imported('Pagoda')
gate = imported('Gate')
scan = imported('rock_face_01')


def place_asset(name, sm, x, y, z, height, yaw=0, tags=()):
    b = sm.get_bounds()
    s = height / (2 * b.box_extent.z)
    angle = math.radians(yaw)
    ox = s * (b.origin.x * math.cos(angle) - b.origin.y * math.sin(angle))
    oy = s * (b.origin.x * math.sin(angle) + b.origin.y * math.cos(angle))
    return actor(name, sm, (x - ox, y - oy, z - s * (b.origin.z - b.box_extent.z)), (s, s, s), yaw, tags=tags)


def temple(name, x, y, z, size=1., summit=False):
    place_asset(name, pagoda if summit else hall, x, y, z, (4200 if summit else 2900) * size, 15 if summit else 180, tags=['YunshaArchitecture'])
    if summit:
        perch = u.EditorLevelLibrary.spawn_actor_from_class(u.TargetPoint, u.Vector(x + 1600, y - 1600, z + 10))
        perch.tags = ['YunshaPerch']
        perch.set_actor_label(name + ' flight-only terrace')


# A monumental inhabited oasis, built from retained detailed architecture.
from yunsha_palace import build as build_palace
build_palace(imported, place_asset, mesh, actor, material, ground, scan, expr, custom, out)
plant_oasis(imported, ground)
plant_lake(imported, ground)
from yunsha_excavation import build as build_excavation


# Regional translation preserves authored dimensions, including tiny fossils.
def shifted_actor(offset):
    def place(label, sm, p=(0, 0, 0), *args, **kwargs):
        return actor(label, sm, (p[0] + offset, p[1], p[2]), *args, **kwargs)
    return place


fossil_placements = build_excavation(mesh, shifted_actor(100000), material, lambda x, y: ground(x + 100000, y), imported, expr, custom, out)


# Sandstone pillars: tapered, fluted and striated, never scaled cylinders.
def pillar(name, x, y, height, radius, seed, forest=False, base=None):
    r = random.Random(seed)
    segments = 96
    levels = 45
    z0 = ground(x, y) - 300 if base is None else base
    verts = []
    tris = []
    phase = r.random() * 6.28
    for j in range(levels + 1):
        t = j / levels
        z = z0 + (height - z0) * t
        taper = 1.50 - .50 * t + .11 * erosion_noise(t * 13, phase) + .045 * erosion_noise(t * 43, phase + 7)
        for i in range(segments):
            a = math.tau * i / segments
            crag = 1 + .32 * erosion_noise(math.cos(a) * 3 + phase, math.sin(a) * 3 + t * .4) + .11 * erosion_noise(math.cos(a) * 8 + t * 4, math.sin(a) * 8 + phase) + .035 * erosion_noise(math.cos(a) * 22 + t * 6, math.sin(a) * 22 + phase)
            verts.append((x + radius * taper * crag * math.cos(a), y + radius * taper * crag * math.sin(a), z))
    for j in range(levels):
        for i in range(segments):
            a = j * segments + i
            b = j * segments + (i + 1) % segments
            tris.extend([(a, b, b + segments), (a, b + segments, a + segments)])
    rim = len(verts)
    verts.extend(verts[-segments:])
    k = len(verts)
    verts.append((x, y, height))
    for i in range(segments):
        tris.append((k, rim + i, rim + (i + 1) % segments))
    actor(name, mesh(name, verts, tris, rock))
    return height


def talus(name, x, y, height, radius, seed):
    # Eroded scree/mesa apron grounding a pillar: the column reads as the
    # resistant core of a weathered mountain, not a spike from bare sand.
    r = random.Random(seed)
    rings = 10
    segments = 56
    z0 = ground(x, y) - 80
    verts = []
    tris = []
    for j in range(rings + 1):
        t = j / rings
        z = z0 + (height - z0) * t
        rr = radius * (1 - t * 0.85)
        for i in range(segments):
            a = math.tau * i / segments
            crag = 1 + .22 * erosion_noise(math.cos(a) * 2.0 + seed, math.sin(a) * 2.0 + t * 2.0) + .05 * erosion_noise(math.cos(a) * 7 + t * 4, math.sin(a) * 7 + seed)
            verts.append((x + rr * crag * math.cos(a), y + rr * crag * math.sin(a), z))
    for j in range(rings):
        for i in range(segments):
            a = j * segments + i
            b = j * segments + (i + 1) % segments
            tris.extend([(a, b, b + segments), (a, b + segments, a + segments)])
    c = len(verts)
    verts.append((x, y, height))
    top = rings * segments
    for i in range(segments):
        tris.append((c, top + i, top + (i + 1) % segments))
    actor(name, mesh(name, verts, tris, rock))
    return height


peaks = [(-16000, 39000, 23000, 4300), (17000, 59000, 32000, 4600), (-19000, 81000, 41000, 5000), (14000, 104000, 50000, 4500), (-6500, 126000, 61000, 5800)]
peaks = [(x * 1.6, y * 1.8 + 140000, z, r) for x, y, z, r in peaks]
spires = []
for i, (x, y, z, r) in enumerate(peaks):
    talus('Gilded Peak %d talus' % i, x, y, z * .34, r * 4.2, 920 + i)
    pillar('Gilded Peak %d' % i, x, y, z, r, 420 + i, True)
    temple('Sky Temple %d' % i, x, y, z + 10, 1., True)
# Hoodoo fields: clustered spire-arcs read as canyon corridors, not lone columns.
clusters = [(-56000, 262000), (54000, 238000), (-30000, 336000), (26000, 372000), (30000, 200000), (-54000, 198000)]
for ci, (cx, cy) in enumerate(clusters):
    for i in range(6):
        ang = ci * .7 + rng.uniform(-.5, .5)
        x = cx + rng.uniform(0, 26000) * math.cos(ang)
        y = cy + rng.uniform(0, 18000) * math.sin(ang)
        if any(math.hypot(x - px, y - py) < pr + 4000 for px, py, pz, pr in peaks):
            continue
        h = rng.uniform(12000, 38000)
        rad = rng.uniform(1000, 2400)
        talus('Mist spire %d %d talus' % (ci, i), x, y, h * .30, rad * 3.6, 2100 + ci * 10 + i)
        pillar('Mist spire %d %d' % (ci, i), x, y, h, rad, 800 + ci * 7 + i)
        spires.append((x, y, h, rad))
# Two eroded buttresses form a real slot with no invisible boundary.
for side in (-1, 1):
    pillar('Narrows buttress %d' % side, 4500 + side * 8500, 74000, 18000, 3600, 49 + side)

# Grottoes excavated in one continuous mountain, with broad uncarved ribs.
from yunsha_escarpment import build as build_escarpment, front as cliff_front
mouths = build_escarpment(mesh, shifted_actor(-80000), rock, pigment)
for i in range(54):
    y = rng.uniform(-67000, 13000)
    x = cliff_front(y, 0) - 80000 + rng.uniform(200, 3800)
    place_asset('Escarpment fallen rock %d' % i, scan, x, y, ground(x, y) - 100, rng.uniform(400, 1500), rng.uniform(0, 360))
for i, (x, y, z, r) in enumerate(peaks):
    for j in range(9):
        a = math.tau * j / 9 + rng.uniform(-.15, .15)
        place_asset('Summit weathered shoulder %d %d' % (i, j), scan, x + r * .77 * math.cos(a), y + r * .77 * math.sin(a), z - rng.uniform(550, 1400), rng.uniform(750, 1700), math.degrees(a))
for i in range(36):
    a = rng.uniform(0, math.tau)
    x = 17500 * math.cos(a)
    y = -52000 + 11600 * math.sin(a)
    place_asset('Oasis exposed bedrock %d' % i, scan, x, y, ground(x, y) - 100, rng.uniform(100, 360), rng.uniform(0, 360))

# Natural imported trees only where the concept permits them: oasis and summits.
# Nanite is requested on foliage so the cloud forest can be dense and cheap.
for name in ('Tree0', 'Tree1', 'Fir0'):
    sm = u.load_asset('/Game/Nature/SM_' + name)
    assert sm, 'Missing retained Nature asset ' + name
    enable_nanite(sm)
    a = u.EditorLevelLibrary.spawn_actor_from_class(u.Born2FlapYunshaBatch, u.Vector())
    a.tags = ['Yunsha']
    a.set_actor_label('Yunsha canopy ' + name)
    c = a.instances
    c.set_static_mesh(sm)
    c.set_collision_profile_name('NoCollision')
    b = sm.get_bounds()
    locations = []
    if name == 'Tree1':
        for j in range(0):
            cx, cy = rng.choice(((1500, -43500), (-18500, -49000), (-2000, -65000), (24500, -46000), (23500, -54000)))
            ang = rng.uniform(0, math.tau)
            radius = rng.uniform(300, 2400)
            x = cx + radius * math.cos(ang)
            y = cy + radius * math.sin(ang)
            if not u.Born2FlapYunsha.spring_contains(x, y):
                locations.append((x, y, ground(x, y), rng.uniform(1000, 1700)))
    else:
        # Cloud forest: tree + fir on peak shoulders and the wider spire crowns,
        # so the cloud band is a real forest rather than five lonely trees.
        for x, y, z, r in peaks:
            for j in range(24):
                a0 = rng.uniform(0, math.tau)
                rr = rng.uniform(r * .38, r * .68)
                locations.append((x + rr * math.cos(a0), y + rr * math.sin(a0), z, rng.uniform(650, 1250)))
        for x, y, z, r in spires:
            if r < 1800:
                continue
            for j in range(rng.randint(2, 5)):
                a0 = rng.uniform(0, math.tau)
                rr = rng.uniform(0, r * .5)
                locations.append((x + rr * math.cos(a0), y + rr * math.sin(a0), z, rng.uniform(500, 950)))
    for x, y, z, h in locations:
        s = h / (2 * b.box_extent.z)
        c.add_instance(u.Transform(location=u.Vector(x, y, z - s * (b.origin.z - b.box_extent.z)), rotation=u.Rotator(yaw=rng.uniform(0, 360)), scale=u.Vector(s, s, s)), False)

# Standing clouds use volumetric fog, so flying into them has depth and no
# translucent sphere surface or disc silhouette. Uniform scales are required.
for i in range(10):
    x = rng.uniform(-48000, 48000)
    y = rng.uniform(203000, 410000)
    z = 21000 + .122 * (y - 203000) + rng.uniform(-2500, 2500)
    a = u.EditorLevelLibrary.spawn_actor_from_class(u.LocalFogVolume, u.Vector(x, y, z))
    a.set_actor_label('Yunsha standing mist %d' % i)
    a.tags = ['Yunsha', 'YunshaCloud']
    radius = rng.uniform(40, 60)
    a.set_actor_scale3d(u.Vector(radius, radius, radius))
    c = a.get_component_by_class(u.LocalFogVolumeComponent)
    c.set_editor_property('radial_fog_extinction', .018)
    c.set_editor_property('height_fog_extinction', 0.)
    c.set_editor_property('height_fog_falloff', 3.)
    c.set_editor_property('fog_albedo', u.LinearColor(.80, .86, .9, 1))

# Silver mirror-wadis emerge only in monsoon; no second permanent oasis.
for i in range(5):
    x = 11000 + 2300 * math.sin(i)
    y = -18000 + i * 23000
    z = ground(x, y) + 6
    rim = [(x + 2400 * math.cos(j * math.tau / 48), y + 650 * math.sin(j * math.tau / 48)) for j in range(48)]
    verts = [(x, y, z)] + [(px, py, ground(px, py) + 6) for px, py in rim]
    tris = [(0, 1 + j, 1 + (j + 1) % 48) for j in range(48)]
    actor('Silver mirror wadi', mesh('MirrorWadi%d' % i, verts, tris, mirror, False), collision=False, tags=['YunshaMirror'])

# Original, reproducible audio. No network, numpy or platform-specific encoder.
source = HERE.parent / 'Saved/GeneratedYunsha'
source.mkdir(parents=True, exist_ok=True)
for name, seconds in [('DesertWind', 12), ('WindHarp', 12), ('TempleBell', 6)]:
    rate = 22050
    data = bytearray()
    noise = 0.
    rr = random.Random(2200)
    for i in range(rate * seconds):
        t = i / rate
        noise = .97 * noise + .03 * rr.uniform(-1, 1)
        if name == 'DesertWind':
            v = .65 * noise * (.7 + .3 * math.sin(math.tau * t / seconds)) + .015 * math.sin(math.tau * 55 * t)
        elif name == 'WindHarp':
            v = sum(math.sin(math.tau * f * t + p) * .045 for f, p in [(110, 0), (165, .6), (220, 1.2), (293, 1.8)]) * (.65 + .35 * math.sin(math.tau * t / seconds)) + .12 * noise
        else:
            v = sum(math.sin(math.tau * f * t) * math.exp(-t * d) * a for f, d, a in [(440, .7, .3), (1188, 1.2, .14), (2376, 1.8, .08)]) * min(1, t * 100)
        fade = min(1, t * 10, (seconds - t) * 10)
        data.extend(struct.pack('<h', int(max(-1, min(1, v * fade)) * 32767)))
    path = source / (name + '.wav')
    with wave.open(str(path), 'wb') as f:
        f.setnchannels(1)
        f.setsampwidth(2)
        f.setframerate(rate)
        f.writeframes(data)
    task = u.AssetImportTask()
    task.filename = str(path)
    task.destination_path = ROOT + '/Audio'
    task.destination_name = name
    task.automated = True
    task.replace_existing = True
    task.save = True
    A.import_asset_tasks([task])
    sound = u.load_asset(ROOT + '/Audio/' + name)
    assert sound
    sound.set_editor_property('looping', name != 'TempleBell')
    E.save_loaded_asset(sound)

sun = u.EditorLevelLibrary.spawn_actor_from_class(u.DirectionalLight, u.Vector(0, 0, 80000), u.Rotator(pitch=-25, yaw=-75))
sun.light_component.set_mobility(u.ComponentMobility.MOVABLE)
sun.light_component.set_intensity(80000)
sun.light_component.set_atmosphere_sun_light(True)
sky = u.EditorLevelLibrary.spawn_actor_from_class(u.SkyLight, u.Vector())
sky.light_component.set_mobility(u.ComponentMobility.MOVABLE)
sky.light_component.set_editor_property('real_time_capture', False)
sky.light_component.set_intensity(1.)
u.EditorLevelLibrary.spawn_actor_from_class(u.SkyAtmosphere, u.Vector())
fog = u.EditorLevelLibrary.spawn_actor_from_class(u.ExponentialHeightFog, u.Vector())
fog.component.set_editor_property('fog_density', .0018)
yunsha_sky_material()
views = [((33000, -85000, 33000), (3000, -50000, 1800)), ((-22000, -70000, 10000), (-43000, -55000, 8000)),
         ((24000, 18000, 17000), (4500, 14000, 7000)), ((45000, 17000, 48000), (-16000, 39000, 23000)),
         ((-16000, 116000, 65000), (-6500, 126000, 63000)), ((17000, -69000, 2000), (13000, -49000, 1800)),
         ((13000, -39700, 1220), (13000, -35500, 1600)),
         ((-3500, -56100, 1400), (1800, -50500, 600)),
         ((74000, -40000, 30000), (46000, -11000, -600)),
         ((46500, -12800, -1000), (46000, -12000, -1650)),
         ((36680, -10830, ground(136500, -10500) + 240), (36500, -10500, ground(136500, -10500) + 12)),
         ((36680, -10435, ground(136660, -10400) + 30), (36660, -10400, ground(136660, -10400) + .5)),
         ((41000, -13820, ground(140900, -13700) + 75), (40900, -13700, ground(140900, -13700) + 20)),
         ((40055, -13065, ground(140000, -13000) + 60), (40000, -13000, ground(140000, -13000) + 1)),
         ((40840, -13750, ground(140820, -13720) + 35), (40820, -13720, ground(140820, -13720) + 1)),
         ((36770, -10640, ground(136740, -10600) + 40), (36740, -10600, ground(136740, -10600) + 3)),
         ((57000, -27500, 4200), (52000, -22500, 100)),
         ((48800, -23800, 650), (48200, -21900, 210))]
# If a scan had to leave a bench edge, keep its inspection camera with it.
for index, source in ((9, (46000, -12000)), (10, (36500, -10500)), (11, (36660, -10400)), (12, (40900, -13700)), (14, (40820, -13720)), (15, (36740, -10600))):
    x, y, z = fossil_placements[source]
    oldz = ground(source[0] + 100000, source[1])
    views[index] = tuple((p[0] + x - source[0], p[1] + y - source[1], p[2] + z - oldz) for p in views[index])
views[1] = tuple((x - 80000, y, z) for x, y, z in views[1])
views[2] = tuple((x, y + 60000, z) for x, y, z in views[2])
for index in (3, 4):
    views[index] = tuple((x * 1.6, y * 1.8 + 140000, z) for x, y, z in views[index])
for index in range(8, 18):
    views[index] = tuple((x + 100000, y, z) for x, y, z in views[index])
views.extend([((95000, -95000, 8000), (125000, -55000, 700)),
              ((-65000, -110000, 18000), (15000, -40000, 0)),
              ((70000, 140000, 8500), (95000, 180000, 800)),
              ((90000, -70000, ground(90000, -70000) + 120), (90500, -69800, ground(90500, -69800) + 20))])
for i, (p, target) in enumerate(views):
    a = u.EditorLevelLibrary.spawn_actor_from_class(u.CameraActor, u.Vector(*p), u.MathLibrary.find_look_at_rotation(u.Vector(*p), u.Vector(*target)))
    a.tags = ['YunshaView%d' % i]
    a.set_actor_label('Yunsha view %d' % i)
u.EditorLevelLibrary.set_level_viewport_camera_info(u.Vector(*views[0][0]), u.MathLibrary.find_look_at_rotation(u.Vector(*views[0][0]), u.Vector(*views[0][1])))
backend.tick('props')

# Externals-first save; with B2F_INPROCESS_BUILD=1 the mesh-partition builder
# runs in this session and a second save persists the compiled-section
# descriptors (the WOSHI-verified headless route).
stats = backend.finalize(ROOT + '/Maps/YUNSHA')
# This directory contains only disposable outputs owned by this recipe. Retire
# old roofs, arcade pieces and the retired tile field so CookAll does not
# bundle abandoned prototypes. The MeshPartition SOURCE meshes (dune base +
# boolean tool) are backend-owned and must survive: the build commandlet
# re-resolves the boolean tool mesh from disk and rebuilds hash dependencies
# on the base source package. Only assets in NEITHER set are retired.
backend_owned = {p.split('.')[0] for p in backend.meshes}
for path in E.list_assets(ROOT + '/Meshes', recursive=True, include_folder=False):
    package = path.split('.')[0]
    if package not in generated_meshes and package not in backend_owned:
        assert E.delete_asset(package), package
stats.update({
    'seed': 221022,
    'world_label': 'YUNSHA',
    'backend': backend.name,
    'world_partition': backend.world_partition,
    'carve_supported': backend.carve_supported,
    'remesh_supported': backend.remesh_supported,
})
metrics_dir = Path(u.Paths.project_saved_dir()) / 'YunshaMetrics'
metrics_dir.mkdir(parents=True, exist_ok=True)
metrics_path = metrics_dir / (backend.name + '.json')
with open(metrics_path, 'w') as handle:
    json.dump(stats, handle, indent=2)
u.log('YUNSHA_MP_MAP_READY backend=%s seed=%d map=%s metrics=%s descs=%s'
      % (backend.name, 221022, stats['map'], metrics_path, stats.get('actor_descs_after_save')))
if not globals().get('B2F_GENERATION_BATCH'):
    # Shutdown flush (UE 5.8 stock teardown bug): PythonScriptPlugin finalizes
    # Python at engine exit and its Py_Finalize pass crashes deep in
    # python311.dll (write AV after QUIT_EDITOR, process exit 3) when the
    # session leaves a large object graph behind -- the finalize GC deallocs
    # thousands of unreal wrapper objects against partially-torn-down module
    # state. Map/assets/metrics are already saved at this point, so shrink the
    # graph while the interpreter is still healthy: the finalize pass then has
    # no dealloc storm left to corrupt.
    import gc as _gc
    _keep = {'u', 'E', '_gc'}
    for _n in list(globals()):
        if not _n.startswith('_') and _n not in _keep:
            globals().pop(_n, None)
    _gc.collect()
    u.SystemLibrary.collect_garbage()
    _gc.collect()
    u.SystemLibrary.execute_console_command(None, 'quit')