"""Import Alexei Ostapenko's CC BY crow from .setup/crow (original Sketchfab ZIP).

Run with UnrealEditor-Cmd -run=pythonscript -script=<this file> -nullrhi.
"""
from pathlib import Path
import unreal as u

SRC = Path(__file__).resolve().parents[3] / '.setup/crow'
DEST = '/Game/Shiomori/HarvestWatch/Crow'
tools = u.AssetToolsHelpers.get_asset_tools()
ela = u.EditorAssetLibrary
u.SystemLibrary.execute_console_command(None, 'Interchange.FeatureFlags.Import.FBX False')
ela.make_directory(DEST)

def task(filename, name, options=None):
    t = u.AssetImportTask()
    t.filename = str(filename)
    t.destination_path = DEST
    t.destination_name = name
    t.automated = True
    t.replace_existing = True
    t.save = True
    if options:
        t.options = options
    tools.import_asset_tasks([t])
    u.log('CROW_IMPORTED ' + str(t.imported_object_paths))
    return u.load_asset(DEST + '/' + name)

opts = u.FbxImportUI()
opts.automated_import_should_detect_type = False
opts.mesh_type_to_import = u.FBXImportType.FBXIT_SKELETAL_MESH
opts.import_as_skeletal = True
opts.import_mesh = True
opts.import_animations = True
opts.import_materials = False
opts.import_textures = False
opts.create_physics_asset = False
opts.override_animation_name = 'A_Crow'
opts.anim_sequence_import_data.set_editor_property('use_default_sample_rate', True)
opts.anim_sequence_import_data.set_editor_property('snap_to_closest_frame_boundary', True)
mesh = task(SRC / 'source/Crow.fbx', 'SK_Crow', opts)
assert isinstance(mesh, u.SkeletalMesh)
anim_opts = u.FbxImportUI()
anim_opts.automated_import_should_detect_type = False
anim_opts.mesh_type_to_import = u.FBXImportType.FBXIT_ANIMATION
anim_opts.import_mesh = False
anim_opts.import_animations = True
anim_opts.skeleton = mesh.get_editor_property('skeleton')
anim_opts.override_animation_name = 'A_Crow'
anim_opts.anim_sequence_import_data.set_editor_property('use_default_sample_rate', True)
anim_opts.anim_sequence_import_data.set_editor_property('snap_to_closest_frame_boundary', True)
task(SRC / 'source/Crow.fbx', 'A_Crow', anim_opts)
lib = u.MaterialEditingLibrary
mat = u.load_asset(DEST + '/M_Crow') or tools.create_asset('M_Crow', DEST, u.Material, u.MaterialFactoryNew())
lib.delete_all_material_expressions(mat)
mat.set_editor_property('two_sided', True)
mat.set_editor_property('blend_mode', u.BlendMode.BLEND_MASKED)
for suffix, name, prop, normal in [
    ('BaseColor', 'T_CrowColor', u.MaterialProperty.MP_BASE_COLOR, False),
    ('N', 'T_CrowNormal', u.MaterialProperty.MP_NORMAL, True),
    ('R', 'T_CrowRoughness', u.MaterialProperty.MP_ROUGHNESS, False),
]:
    tex = task(SRC / ('textures/Crow_' + suffix + '.TGA.png'), name)
    tex.set_editor_property('srgb', suffix == 'BaseColor')
    tex.set_editor_property('max_texture_size', 2048)
    if normal:
        tex.set_editor_property('compression_settings', u.TextureCompressionSettings.TC_NORMALMAP)
    sample = lib.create_material_expression(mat, u.MaterialExpressionTextureSample)
    sample.texture = tex
    if normal:
        sample.sampler_type = u.MaterialSamplerType.SAMPLERTYPE_NORMAL
    elif suffix == 'R':
        sample.sampler_type = u.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR
    lib.connect_material_property(sample, 'RGB' if suffix != 'R' else 'R', prop)
    if suffix == 'BaseColor':
        lib.connect_material_property(sample, 'A', u.MaterialProperty.MP_OPACITY_MASK)
    ela.save_loaded_asset(tex)
lib.recompile_material(mat)
slots = list(mesh.get_editor_property('materials'))
if not slots:
    slots = [u.SkeletalMaterial()]
for i in range(len(slots)):
    slot = slots[i]
    slot.set_editor_property('material_interface', mat)
    slots[i] = slot
mesh.set_editor_property('materials', slots)
lib.set_material_usage(mat, u.MaterialUsage.MATUSAGE_SKELETAL_MESH)
ela.save_loaded_asset(mat)
ela.save_loaded_asset(mesh)
u.EditorLoadingAndSavingUtils.save_dirty_packages(False, True)
for path in ela.list_assets(DEST):
    obj = u.load_asset(path)
    if isinstance(obj, u.AnimSequence):
        u.log('CROW_ANIMATION ' + path + ' length=' + str(obj.get_play_length()))
        for fraction in [0, .25, .5, .75]:
            pose = u.AnimPoseExtensions.get_anim_pose_at_time(obj, obj.get_play_length()*fraction, u.AnimPoseEvaluationOptions())
            parts = []
            for bone in ['crow_-head', 'crow_-l-hand', 'crow_-l-foot', 'crow_-pelvis']:
                p = u.AnimPoseExtensions.get_bone_pose(pose, bone, u.AnimPoseSpaces.WORLD).translation
                parts.append('%s=(%.1f,%.1f,%.1f)' % (bone,p.x,p.y,p.z))
            u.log('CROW_SAMPLE ' + str(fraction) + ' ' + ' '.join(parts))
ela.save_directory(DEST, only_if_is_dirty=False, recursive=True)
# The short source takes have a stray 0.01 root scale. Normalize it in our
# derived clips so switching from the full-size eating take never shrinks birds.
for source, target in [
    ('A_Crow_root_root_001_Unreal_Take_Base_Layer_002', 'A_Fly'),
    ('A_Crow_root_root_002_Unreal_Take_Base_Layer_001', 'A_Glide'),
]:
    if ela.does_asset_exist(DEST + '/' + target):
        ela.delete_asset(DEST + '/' + target)
    clip = ela.duplicate_asset(DEST + '/' + source, DEST + '/' + target)
    assert clip
    # Root has no locomotion, only the erroneous constant scale; the mesh's
    # reference root has the correct identity transform.
    u.AnimationLibrary.remove_bone_animation(clip, 'root', False, True)
    pose = u.AnimPoseExtensions.get_anim_pose_at_time(clip, 0.0, u.AnimPoseEvaluationOptions())
    head = u.AnimPoseExtensions.get_bone_pose(pose, 'crow_-head', u.AnimPoseSpaces.WORLD).translation
    assert 8 < head.z < 50, 'Incorrect normalized crow scale: ' + str(head)
    ela.save_loaded_asset(clip)
u.log('CROW_BOUNDS ' + str(mesh.get_bounds()))
u.log('CROW_IMPORT_PASS')
