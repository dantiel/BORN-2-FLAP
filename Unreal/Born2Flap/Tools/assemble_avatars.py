"""Assemble each avatar from the best of both imports:

1. GLB import  -> materials + textures (+ a lying placeholder mesh we overwrite)
2. FBX import  -> correct Z-up cm mesh + skeleton (overwrites the GLB mesh)
3. FBX clips   -> the 8 locomotion clips on the FBX skeleton
4. Material assignment: FBX mesh slot names == GLB material names (same source),
   so map by name and assign to the mesh slots.

Run (editor closed, headless commandlet):
    UnrealEditor-Cmd Born2Flap.uproject -ExecutePythonScript="V:/BORN 2 FLAP/Unreal/Born2Flap/Tools/assemble_avatars.py" -unattended -nop4 -nosplash -stdout
"""
import unreal as u

TOOLS = u.AssetToolsHelpers.get_asset_tools()
LIB = u.EditorAssetLibrary

_HERE = __import__('pathlib').Path(__file__).resolve()
_PROJECT = _HERE.parents[1]
_CHAR_DIR = _PROJECT / 'SourceAssets/Mixamo/Characters'
_CLIP_DIR = _PROJECT / 'SourceAssets/Mixamo/Clips_Colon'

CHARACTERS = [
    ('SK_Amy', 'SK_Amy.glb', 'Amy.fbx'),
    ('SK_Michelle', 'SK_Michelle.glb', 'Michelle.fbx'),
    ('SK_Sophie', 'SK_Sophie.glb', 'Sophie.fbx'),
    ('SK_Kate', 'SK_Kate.glb', 'Kate.fbx'),
    ('SK_James', 'SK_James.glb', 'James.fbx'),
    ('SK_Adam', 'SK_Adam.glb', 'Adam.fbx'),
    ('SK_Brian', 'SK_Brian.glb', 'Brian.fbx'),
    ('SK_Alex', 'SK_Alex.glb', 'Alex.fbx'),
]

CLIPS = [
    ('Idle.fbx', 'Idle'), ('Walk.fbx', 'Walk'), ('Run.fbx', 'Run'),
    ('Throw.fbx', 'Throw'), ('Catch.fbx', 'Catch'), ('Standing.fbx', 'Standing'),
    ('StrafeRight.fbx', 'StrafeRight'), ('WalkBackward.fbx', 'WalkBackward'),
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


def _import_glb(base, glb_name):
    src = _CHAR_DIR / glb_name
    task = u.AssetImportTask()
    task.filename = str(src)
    task.destination_path = '/Game/Actor'
    task.destination_name = base
    task.automated = True
    task.replace_existing = True
    task.save = True
    TOOLS.import_asset_tasks([task])
    # discover materials/textures created under /Game/Actor/<base>
    materials = {}
    textures = []
    if LIB.does_directory_exist('/Game/Actor/%s' % base):
        for p in LIB.list_assets('/Game/Actor/%s' % base, recursive=True, include_folder=False):
            a = u.load_asset(p)
            if a is None:
                continue
            cn = a.get_class().get_name()
            if cn in ('Material', 'MaterialInstanceConstant'):
                materials[a.get_name()] = a
            elif cn == 'Texture2D':
                textures.append(p)
    print('GLB_%s materials=%s textures=%d' % (base, sorted(materials.keys()), len(textures)))
    return materials


def _import_fbx_mesh(base, fbx_name):
    src = _CHAR_DIR / fbx_name
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
    task.destination_name = base
    task.automated = True
    task.replace_existing = True
    task.save = True
    task.options = options
    task.factory = u.FbxFactory()
    TOOLS.import_asset_tasks([task])
    mesh = None
    skeleton = None
    for p in task.imported_object_paths:
        a = u.load_asset(p)
        if a is None:
            continue
        if isinstance(a, u.SkeletalMesh):
            mesh = a
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
    print('FBX_%s mesh=%s skeleton=%s' % (base, mesh.get_path_name() if mesh else None,
                                          skeleton.get_path_name() if skeleton else None))
    return mesh, skeleton


def _import_clips(base, skeleton):
    dest_dir = '/Game/Actor/%s/SkeletalMeshes' % base
    imported = []
    for fbx_name, suffix in CLIPS:
        src = _CLIP_DIR / fbx_name
        if not src.exists():
            continue
        asset_name = base + suffix
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
            print('CLIP_%s_%s FAILED (%s)' % (base, suffix, len(task.imported_object_paths)))
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
                except Exception:
                    pass
        seq = u.load_asset(seq_pkg)
        if seq is not None and isinstance(seq, u.AnimSequence):
            LIB.save_asset(seq_pkg)
            imported.append(asset_name)
    return imported


def _assign_materials(base, mesh, materials):
    if mesh is None or not materials:
        return []
    mats = mesh.get_editor_property('materials')
    assigned = []
    for i, sm in enumerate(mats):
        slot = str(sm.material_slot_name)
        mat = materials.get(slot) or materials.get(slot.casefold())
        if mat is None and i < len(materials):
            # order fallback (GLB material JSON order)
            keys = [k for k in materials.keys() if k not in assigned]
            mat = materials.get(keys[0]) if keys else None
        if mat is not None:
            sm.material_interface = mat
            assigned.append(slot)
        else:
            print('SLOT_%s_%d=%s NO MATERIAL' % (base, i, slot))
    mesh.set_editor_property('materials', mats)
    LIB.save_asset(mesh.get_path_name().rsplit('.', 1)[0])
    # verify
    mats2 = mesh.get_editor_property('materials')
    verify = [str(sm.material_interface) for sm in mats2]
    print('ASSIGN_%s=%s' % (base, verify))
    return verify


def main():
    for base, glb_name, fbx_name in CHARACTERS:
        _delete_folder('/Game/Actor/%s' % base)
        u.SystemLibrary.collect_garbage()
        try:
            materials = _import_glb(base, glb_name)
        except Exception as e:
            print('GLB_%s_ERROR %s' % (base, e))
            materials = {}
        try:
            mesh, skeleton = _import_fbx_mesh(base, fbx_name)
        except Exception as e:
            print('FBX_%s_ERROR %s' % (base, e))
            continue
        if skeleton is None:
            print('FBX_%s_NO_SKELETON' % base)
            continue
        clips = _import_clips(base, skeleton)
        b = mesh.get_bounds() if mesh else None
        if b:
            ext = b.box_extent
            print('BOUNDS_%s height=%.1f' % (base, ext.z * 2.0))
        _assign_materials(base, mesh, materials)
        print('CHARACTER_%s clips=%d' % (base, len(clips)))
    print('ASSEMBLE_DONE')


if __name__ == '__main__':
    main()
