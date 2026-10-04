"""Import the authored kestrel fuselage + tail into /Game/Birds and build their materials.

Run once via the editor commandlet (close any running game/editor first):

  UnrealEditor-Cmd.exe "Unreal/Born2Flap/Born2Flap.uproject" -run=pythonscript \
    -script="Unreal/Born2Flap/Tools/import_kestrel_fuselage.py" -unattended -nosplash -nullrhi -nosound

Produces:
  /Game/Birds/SM_KestrelFuselage        (static mesh, from build_kestrel_fuselage_obj.py)
  /Game/Birds/T_KestrelFuselage_BaseColor   (4k sRGB)
  /Game/Birds/T_KestrelFuselage_Normal      (4k normal map)
  /Game/Birds/T_KestrelFuselage_Roughness   (4k linear)
  /Game/Birds/T_KestrelTail                 (tailmembrane feather raster, sRGB)
  /Game/Birds/M_KestrelFuselage             (PBR: base/normal/roughness)
  /Game/Birds/M_KestrelTail                 (two-sided tail paint)
"""
import unreal as u
from pathlib import Path

assets = u.AssetToolsHelpers.get_asset_tools()
lib = u.MaterialEditingLibrary
ela = u.EditorAssetLibrary

HERE = Path(__file__).resolve().parent
OUT = HERE / "out"
DL = Path(r"C:\Users\d\Downloads\kestrel_fuselage_unreal_final")

BIRDS = "/Game/Birds"


def import_texture(src, tex_path, srgb, compression=None):
    # Geometry repairs do not change the supplied rasters. Reuse their imports;
    # needless reimports also contend with texture streaming in a running game.
    if ela.does_asset_exist(tex_path):
        tex = u.load_asset(tex_path)
        assert isinstance(tex, u.Texture2D), 'Expected texture at ' + tex_path
        return tex
    if not src.exists():
        u.log_warning("KESTREL_FUSELAGE: missing texture " + str(src))
        raise SystemExit(1)
    task = u.AssetImportTask()
    task.filename = str(src)
    task.destination_path = BIRDS
    task.destination_name = tex_path.rsplit("/", 1)[-1]
    task.automated = True
    task.replace_existing = True
    task.save = True
    assets.import_asset_tasks([task])
    tex = u.load_asset(tex_path)
    if not isinstance(tex, u.Texture2D):
        u.log_warning("KESTREL_FUSELAGE: texture import failed " + tex_path)
        raise SystemExit(1)
    tex.set_editor_property("srgb", srgb)
    tex.set_editor_property("never_stream", True)
    if compression is not None:
        tex.set_editor_property("compression_settings", compression)
    assert ela.save_asset(tex_path), "KESTREL_FUSELAGE: failed to save " + tex_path
    u.log("KESTREL_FUSELAGE_TEX " + tex_path)
    return tex


def import_mesh(obj_path, mesh_path):
    if not obj_path.exists():
        u.log_warning("KESTREL_FUSELAGE: missing mesh " + str(obj_path))
        raise SystemExit(1)
    task = u.AssetImportTask()
    task.filename = str(obj_path)
    task.destination_path = BIRDS
    task.destination_name = "SM_KestrelFuselage"
    task.automated = True
    task.replace_existing = True
    task.save = True
    assets.import_asset_tasks([task])
    mesh = u.load_asset(mesh_path)
    assert isinstance(mesh, u.StaticMesh), 'Fuselage import did not produce a StaticMesh'
    # Preserve the smooth silhouette in conventional rendering as well as Nanite.
    # The old automatic Nanite fallback reduced this bird to only 313 vertices.
    # Headless Python commandlets do not initialize this editor subsystem.
    editor = u.get_editor_subsystem(u.StaticMeshEditorSubsystem) or u.new_object(u.StaticMeshEditorSubsystem)
    nanite = editor.get_nanite_settings(mesh)
    nanite.set_editor_property('enabled', False)
    editor.set_nanite_settings(mesh, nanite, True)
    settings = editor.get_lod_build_settings(mesh, 0)
    settings.set_editor_property('recompute_normals', False)
    settings.set_editor_property('recompute_tangents', True)
    settings.set_editor_property('use_mikk_t_space', True)
    settings.set_editor_property('use_full_precision_u_vs', True)
    settings.set_editor_property('use_high_precision_tangent_basis', True)
    editor.set_lod_build_settings(mesh, 0, settings)
    validate_mesh(mesh)
    assert ela.save_asset(mesh_path), "KESTREL_FUSELAGE: failed to save mesh"
    u.log("KESTREL_FUSELAGE_MESH " + mesh_path)
    return mesh


def validate_mesh(mesh):
    vertices, triangles, normals, uv, _ = u.ProceduralMeshLibrary.get_section_from_static_mesh(mesh, 0, 0)
    assert len(triangles) // 3 >= 30000, 'Fuselage was reduced to a coarse fallback'
    assert all(abs(t.y - (1.0 - p.x / 100.0)) < 0.0001 for p, t in zip(vertices, uv)), 'Head/tail UV reversed'
    upper = max(range(len(vertices)), key=lambda i: vertices[i].z)
    lower = min(range(len(vertices)), key=lambda i: vertices[i].z)
    assert min(abs(uv[upper].x), abs(uv[upper].x - 1)) < .02, 'Back texture is not on top'
    assert abs(uv[lower].x - .5) < .02, 'Belly texture is not underneath'
    assert normals[upper].z > .95 and normals[lower].z < -.95, 'Inverted surface normals'
    assert vertices[upper].z > 8 and vertices[lower].z < -12, 'Inverted body silhouette'
    for i in range(0, len(triangles), 3):
        us = [uv[j].x for j in triangles[i:i+3]]
        assert max(us) - min(us) <= .5, 'Triangle crosses cylindrical paint seam'
    u.log('KESTREL_MESH_VALIDATED: smooth full mesh, dorsal-up silhouette, head/tail UVs and seam')


def node(m, kind, **props):
    n = lib.create_material_expression(m, getattr(u, "MaterialExpression" + kind))
    for k, v in props.items():
        n.set_editor_property(k, v)
    return n


def wire(a, b, pin, output=""):
    assert lib.connect_material_expressions(a, output, b, pin), \
        "Invalid connection: %s.%s -> %s.%s" % (type(a).__name__, output, type(b).__name__, pin)


def output(n, prop, pin=""):
    assert lib.connect_material_property(n, pin, getattr(u.MaterialProperty, "MP_" + prop)), \
        "Invalid output: " + prop


def fuselage_material(base, normal, rough):
    path = BIRDS + "/M_KestrelFuselage"
    m = u.load_asset(path) if ela.does_asset_exist(path) else assets.create_asset(
        "M_KestrelFuselage", BIRDS, u.Material, u.MaterialFactoryNew())
    lib.delete_all_material_expressions(m)
    # Retain compatibility if Nanite is enabled for this material elsewhere.
    m.set_editor_property("used_with_nanite", True)

    base_tex = node(m, "TextureSample", texture=base)
    output(base_tex, "BASE_COLOR", "RGB")

    normal_tex = node(m, "TextureSample", texture=normal)
    normal_tex.set_editor_property('sampler_type', u.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    output(normal_tex, "NORMAL", "RGB")

    rough_tex = node(m, "TextureSample", texture=rough)
    output(rough_tex, "ROUGHNESS", "R")

    lib.recompile_material(m)
    assert ela.save_asset(path), "KESTREL_FUSELAGE: failed to save material"
    u.log("KESTREL_FUSELAGE_MAT " + path)
    return m


def tail_material(tail_tex):
    path = BIRDS + "/M_KestrelTail"
    m = u.load_asset(path) if ela.does_asset_exist(path) else assets.create_asset(
        "M_KestrelTail", BIRDS, u.Material, u.MaterialFactoryNew())
    lib.delete_all_material_expressions(m)
    m.set_editor_property("two_sided", True)
    # Slightly translucent feathers: light passes through the tail fan. TailOpacity
    # (scalar parameter) tunes the translucency from 0 (invisible) to 1 (opaque).
    m.set_editor_property("blend_mode", u.BlendMode.BLEND_TRANSLUCENT)
    m.set_editor_property("shading_model", u.MaterialShadingModel.MSM_DEFAULT_LIT)

    paint = node(m, "TextureSampleParameter2D", parameter_name="TailPaint", texture=tail_tex)
    output(paint, "BASE_COLOR", "RGB")

    rough = node(m, "Constant", r=0.65)
    output(rough, "ROUGHNESS")

    opacity = node(m, "ScalarParameter", parameter_name="TailOpacity", default_value=0.72)
    output(opacity, "OPACITY")

    lib.recompile_material(m)
    assert ela.save_asset(path), "KESTREL_FUSELAGE: failed to save tail material"
    u.log("KESTREL_FUSELAGE_TAIL_MAT " + path)
    return m


ela.make_directory(BIRDS)

base = import_texture(DL / "kestrel_fuselage_basecolor_4k.png",
                      BIRDS + "/T_KestrelFuselage_BaseColor", True)
normal = import_texture(DL / "kestrel_fuselage_normal_4k.png",
                        BIRDS + "/T_KestrelFuselage_Normal", False,
                        u.TextureCompressionSettings.TC_NORMALMAP)
rough = import_texture(DL / "kestrel_fuselage_roughness_4k.png",
                       BIRDS + "/T_KestrelFuselage_Roughness", False,
                       u.TextureCompressionSettings.TC_GRAYSCALE)
tail = import_texture(OUT / "kestrel-tail.png",
                      BIRDS + "/T_KestrelTail", True)

mesh = import_mesh(OUT / "kestrel_fuselage_cm.obj", BIRDS + "/SM_KestrelFuselage")

fuselage_material(base, normal, rough)
tail_material(tail)

u.log("KESTREL_FUSELAGE_READY")