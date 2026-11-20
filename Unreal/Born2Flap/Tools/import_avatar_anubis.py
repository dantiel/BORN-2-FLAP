"""Import Anubis (renamed AutoRig Pro -> mixamorig FBX) as a SkeletalMesh and
retarget the shared Mixamo locomotion clips onto his skeleton.

The source SK_Anubis.fbx was produced by fbx_rename_bones.py + anubis_bone_map.json:
the single-chain deform bones (root.x/spine_01.x/.../arm_stretch.l/forearm_stretch.l/
hand.l/thigh_stretch.l/leg_stretch.l/foot.l/toes_01.l + fingers) were renamed to the
shared mixamorig:* names so the Clips_Colon clip set retargets 1:1 by bone name —
the same mechanism used for the 8 Mixamo humans and Isis.

Layout mirrors ABorn2FlapAvatar::LoadPerson:
    /Game/Actor/SK_Anubis/SkeletalMeshes/SK_Anubis.SK_Anubis            <- mesh
    /Game/Actor/SK_Anubis/SkeletalMeshes/SK_Anubis{Idle,Walk,...}. ...  <- clips

Run (editor closed, headless commandlet):
    UnrealEditor-Cmd Born2Flap.uproject -ExecutePythonScript="V:/BORN 2 FLAP/Unreal/Born2Flap/Tools/import_avatar_anubis.py" -unattended -nop4 -nosplash -stdout
"""
from pathlib import Path
import unreal as u

TOOLS = u.AssetToolsHelpers.get_asset_tools()
LIB = u.EditorAssetLibrary
MEL = u.MaterialEditingLibrary

_HERE = Path(__file__).resolve()
_PROJECT = _HERE.parents[1]

BASE = 'SK_Anubis'
SRC = _PROJECT / 'SourceAssets/NewChars/SK_Anubis.fbx'
_CLIP_DIR = _PROJECT / 'SourceAssets/Mixamo/Clips_Colon'

CLIPS = [
    ('Idle.fbx',         'Idle'),
    ('Walk.fbx',         'Walk'),
    ('Run.fbx',          'Run'),
    ('Throw.fbx',        'Throw'),
    ('Catch.fbx',        'Catch'),
    ('Standing.fbx',     'Standing'),
    ('StrafeRight.fbx',  'StrafeRight'),
    ('WalkBackward.fbx', 'WalkBackward'),
]


def _delete_folder(path):
    if LIB.does_directory_exist(path):
        for a in LIB.list_assets(path, recursive=True, include_folder=False):
            try:
                LIB.delete_asset(a)
            except Exception:
                pass
        try:
            LIB.delete_directory(path)
        except Exception:
            pass


def _import_mesh(base, mesh_name, src):
    dest_dir = '/Game/Actor/%s/SkeletalMeshes' % base
    LIB.make_directory(dest_dir)

    options = u.FbxImportUI()
    options.automated_import_should_detect_type = False
    options.import_mesh = True
    options.import_as_skeletal = True
    options.mesh_type_to_import = u.FBXImportType.FBXIT_SKELETAL_MESH
    options.original_import_type = u.FBXImportType.FBXIT_SKELETAL_MESH
    options.import_animations = False

    task = u.AssetImportTask()
    task.filename = str(src)
    task.destination_path = dest_dir
    task.destination_name = mesh_name
    task.automated = True
    task.replace_existing = True
    task.save = True
    task.options = options
    task.factory = u.FbxFactory()

    TOOLS.import_asset_tasks([task])

    mesh_path = None
    skeleton = None
    for p in task.imported_object_paths:
        a = u.load_asset(p)
        if a is None:
            continue
        if isinstance(a, u.SkeletalMesh):
            mesh_path = p
            LIB.save_asset(p)
            try:
                skel = a.get_editor_property('skeleton')
                if skel is not None:
                    skeleton = skel
            except Exception:
                pass
        elif isinstance(a, u.Skeleton):
            skeleton = a
    if skeleton is not None:
        LIB.save_asset(skeleton.get_path_name())

    print('MESH_%s=%s' % (mesh_name, mesh_path))
    print('SKELETON_%s=%s' % (mesh_name, skeleton.get_path_name() if skeleton else None))
    return skeleton


def _import_clips(base, clip_prefix, skeleton):
    dest_dir = '/Game/Actor/%s/SkeletalMeshes' % base
    imported = []
    for fbx_name, suffix in CLIPS:
        src = _CLIP_DIR / fbx_name
        if not src.exists():
            print('SKIP (missing) %s' % src)
            continue
        asset_name = clip_prefix + suffix

        options = u.FbxImportUI()
        options.automated_import_should_detect_type = False
        options.import_mesh = False
        options.import_as_skeletal = True
        options.mesh_type_to_import = u.FBXImportType.FBXIT_ANIMATION
        options.original_import_type = u.FBXImportType.FBXIT_ANIMATION
        options.import_animations = True
        options.skeleton = skeleton

        task = u.AssetImportTask()
        task.filename = str(src)
        task.destination_path = dest_dir
        task.destination_name = asset_name
        task.automated = True
        task.replace_existing = True
        task.save = True
        task.options = options
        task.factory = u.FbxFactory()

        TOOLS.import_asset_tasks([task])

        expected = dest_dir + '/' + asset_name
        seq_pkg = None
        for p in task.imported_object_paths:
            a = u.load_asset(p)
            if a is not None and isinstance(a, u.AnimSequence):
                seq_pkg = p.rsplit('.', 1)[0]
                break
        if seq_pkg is None:
            print('WARN %s produced no AnimSequence (%s)' % (asset_name, task.imported_object_paths))
            continue
        if seq_pkg != expected:
            if LIB.does_asset_exist(expected):
                try:
                    LIB.delete_asset(seq_pkg)
                except Exception:
                    pass
                seq_pkg = expected
            else:
                try:
                    LIB.rename_asset(seq_pkg, expected)
                    seq_pkg = expected
                except Exception as e:
                    print('WARN rename %s -> %s failed: %s' % (seq_pkg, expected, e))
        seq = u.load_asset(seq_pkg)
        if seq is not None and isinstance(seq, u.AnimSequence):
            LIB.save_asset(seq_pkg)
            imported.append(asset_name)
    return imported


_TEXTURES_DIR = _PROJECT / 'SourceAssets/NewChars/Textures'

# (source .jpg basename, channel) for the body material. The FBX references
# these by RelativeFilename (Textures\Anuphis_Close_Anubis_*.jpg); they are NOT
# embedded, so they must be dropped into SourceAssets/NewChars/Textures first.
_BODY_TEX = [
    ('Anuphis_Close_Anubis_BaseColor', 'base'),
    ('Anuphis_Close_Anubis_Normal', 'normal'),
    ('Anuphis_Close_Anubis_Roughness', 'roughness'),
    ('Anuphis_Close_Anubis_Metallic', 'metallic'),
]


def _build_materials():
    """Import the Anubis PBR texture set and wire a body material to slot 0.

    The FBX meshes ship with unresolved external texture paths (same failure
    mode as the Mixamo FBX set), so without a Textures folder they import
    gray. When the textures are present this builds BaseColor/Normal and
    Roughness/Metallic wiring like the other avatars."""
    tex_dir = '/Game/Actor/%s/Textures' % BASE
    mat_dir = '/Game/Actor/%s/Materials' % BASE
    LIB.make_directory(tex_dir)
    LIB.make_directory(mat_dir)

    tex = {}

    def _import_tex(png, is_normal, is_color):
        if not png.exists():
            return None
        name = png.stem
        expected = tex_dir + '/' + name
        if LIB.does_asset_exist(expected):
            return u.load_asset(expected)
        factory = u.TextureFactory()
        try:
            factory.set_editor_property('no_alpha', True)
        except Exception:
            pass
        try:
            if is_normal:
                factory.set_editor_property('compression_settings', u.TextureCompressionSettings.TC_NORMALMAP)
            factory.set_editor_property('srgb', is_color)
        except Exception:
            pass
        task = u.AssetImportTask()
        task.filename = str(png)
        task.destination_path = tex_dir
        task.destination_name = name
        task.automated = True
        task.replace_existing = True
        task.save = True
        task.factory = factory
        TOOLS.import_asset_tasks([task])
        return u.load_asset(expected)

    for stem, kind in _BODY_TEX:
        png = _TEXTURES_DIR / (stem + '.jpg')
        if not png.exists():
            png = _TEXTURES_DIR / (stem + '.png')
        t = _import_tex(png, is_normal=(kind == 'normal'), is_color=(kind in ('base', 'normal')))
        if t is not None:
            tex[kind] = t

    if not tex.get('base') and not tex.get('normal'):
        print('WARN %s: no textures found in %s — mesh will import gray. '
              'Drop the referenced .jpg/.png files there and re-run.'
              % (BASE, _TEXTURES_DIR))
        return

    mat_path = mat_dir + '/Anubis_Body'
    mat = u.load_asset(mat_path) if LIB.does_asset_exist(mat_path) else None
    if mat is None:
        factory = u.MaterialFactoryNew()
        mat = TOOLS.create_asset('Anubis_Body', mat_dir, u.Material, factory)
    if mat is None:
        print('ERROR %s: create_asset failed for Anubis_Body' % BASE)
        return

    def _sample(t, x, y, stype):
        e = MEL.create_material_expression(mat, u.MaterialExpressionTextureSample, x, y)
        if e is None:
            return None
        e.set_editor_property('texture', t)
        try:
            e.set_editor_property('sampler_type', stype)
        except Exception:
            pass
        return e

    if tex.get('base'):
        s = _sample(tex['base'], -600, 0, u.MaterialSamplerType.SAMPLERTYPE_COLOR)
        if s is not None:
            MEL.connect_material_property(s, 'RGB', u.MaterialProperty.MP_BASE_COLOR)
    if tex.get('normal'):
        s = _sample(tex['normal'], -600, -260, u.MaterialSamplerType.SAMPLERTYPE_NORMAL)
        if s is not None:
            MEL.connect_material_property(s, 'RGB', u.MaterialProperty.MP_NORMAL)
    if tex.get('roughness') and tex.get('metallic'):
        s = _sample(tex['roughness'], -600, -520, u.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
        rough = MEL.create_material_expression(mat, u.MaterialExpressionComponentMask, -260, -560)
        rough.set_editor_property('g', True)
        MEL.connect_material_property(rough, '', u.MaterialProperty.MP_ROUGHNESS)
        if s is not None:
            MEL.connect_material_expressions(s, 'G', rough, '')
        sm = _sample(tex['metallic'], -600, -780, u.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
        metal = MEL.create_material_expression(mat, u.MaterialExpressionComponentMask, -260, -760)
        metal.set_editor_property('b', True)
        MEL.connect_material_property(metal, '', u.MaterialProperty.MP_METALLIC)
        if sm is not None:
            MEL.connect_material_expressions(sm, 'B', metal, '')
    else:
        c = MEL.create_material_expression(mat, u.MaterialExpressionConstant, -600, -780)
        c.set_editor_property('r', 0.0)
        MEL.connect_material_property(c, '', u.MaterialProperty.MP_METALLIC)
        c2 = MEL.create_material_expression(mat, u.MaterialExpressionConstant, -600, -900)
        c2.set_editor_property('r', 0.6)
        MEL.connect_material_property(c2, '', u.MaterialProperty.MP_ROUGHNESS)
    LIB.save_asset(mat_path)

    mesh = u.load_asset('/Game/Actor/%s/SkeletalMeshes/%s' % (BASE, BASE))
    if mesh is not None and isinstance(mesh, u.SkeletalMesh):
        mats = mesh.get_editor_property('materials')
        if mats:
            sm = mats[0]
            sm.set_editor_property('material_interface', mat)
            mats[0] = sm
            mesh.set_editor_property('materials', mats)
            LIB.save_loaded_asset(mesh, only_if_is_dirty=False)
            print('MATS_%s slot0->%s' % (BASE, mat_path))


def main():
    _delete_folder('/Game/Actor/' + BASE)
    if not SRC.exists():
        print('ERROR missing source %s' % SRC)
        return
    skeleton = _import_mesh(BASE, BASE, SRC)
    if skeleton is None:
        print('ERROR %s has no skeleton' % BASE)
        return
    _build_materials()
    mesh = u.load_asset('/Game/Actor/%s/SkeletalMeshes/%s' % (BASE, BASE))
    if mesh is not None and isinstance(mesh, u.SkeletalMesh):
        b = mesh.get_bounds()
        ext = b.box_extent
        print('BOUNDS_%s extent=(%.1f, %.1f, %.1f) height=%.1f'
              % (BASE, ext.x, ext.y, ext.z, ext.z * 2.0))
    clips = _import_clips(BASE, BASE, skeleton)
    print('CHARACTER_%s clips=%d' % (BASE, len(clips)))
    print('IMPORT_ANUBIS_DONE')


if __name__ == '__main__':
    main()