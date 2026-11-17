"""Import the 8 modern Mixamo pilots from GLB (mesh + skeleton + materials +
textures) and retarget the shared FBX locomotion clips onto each skeleton.

Why GLB (not FBX): the HF character_refined FBX files reference their embedded
textures by absolute Linux build-machine paths, so UE's FBX importer cannot
resolve them and produces textureless gray meshes. The character_refined_glb
exports embed textures properly and carry the same clean 65-bone mixamorig
skeleton (Blender glTF exporter, Y-up meters -> UE converts to Z-up cm).

Multi-primitive GLBs were pre-merged into ONE mesh node by
Tools/merge_glb_meshes.py so the import yields one SkeletalMesh with one
section per material.

Run (editor closed, headless commandlet):
    UnrealEditor-Cmd Born2Flap.uproject -ExecutePythonScript="V:/BORN 2 FLAP/Unreal/Born2Flap/Tools/import_avatar_glb.py" -unattended -nop4 -nosplash -stdout
"""
from pathlib import Path
import unreal as u

TOOLS = u.AssetToolsHelpers.get_asset_tools()
LIB = u.EditorAssetLibrary

_HERE = Path(__file__).resolve()
_PROJECT = _HERE.parents[1]
_CHAR_DIR = _PROJECT / 'SourceAssets/Mixamo/Characters'
_CLIP_DIR = _PROJECT / 'SourceAssets/Mixamo/Clips_Colon'

CHARACTERS = [
    ('SK_Amy',      'SK_Amy.glb'),
    ('SK_Michelle', 'SK_Michelle.glb'),
    ('SK_Sophie',   'SK_Sophie.glb'),
    ('SK_Kate',     'SK_Kate.glb'),
    ('SK_James',    'SK_James.glb'),
    ('SK_Adam',     'SK_Adam.glb'),
    ('SK_Brian',    'SK_Brian.glb'),
    ('SK_Alex',     'SK_Alex.glb'),
]

CLIPS = [
    ('Idle.fbx',          'Idle'),
    ('Walk.fbx',          'Walk'),
    ('Run.fbx',           'Run'),
    ('Throw.fbx',         'Throw'),
    ('Catch.fbx',         'Catch'),
    ('Standing.fbx',      'Standing'),
    ('StrafeRight.fbx',   'StrafeRight'),
    ('WalkBackward.fbx',  'WalkBackward'),
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


def _import_mesh(base, src):
    dest = '/Game/Actor/%s' % base
    _delete_folder(dest)
    u.SystemLibrary.collect_garbage()
    if LIB.does_directory_exist(dest):
        raise RuntimeError('folder %s still exists after delete' % dest)

    # destination_name + the GLB's own root name decide the asset layout;
    # with the file named SK_X.glb (root renamed to SK_X) Interchange places
    # everything exactly like SK_Founder: /Game/Actor/SK_X/SkeletalMeshes/...
    task = u.AssetImportTask()
    task.filename = str(src)
    task.destination_path = '/Game/Actor'
    task.destination_name = base
    task.automated = True
    task.replace_existing = True
    task.save = True
    TOOLS.import_asset_tasks([task])

    mesh_path = None
    skeleton_path = None
    if LIB.does_directory_exist(dest):
        for p in LIB.list_assets(dest, recursive=True, include_folder=False):
            a = u.load_asset(p)
            if a is None:
                continue
            if isinstance(a, u.SkeletalMesh) and mesh_path is None:
                mesh_path = p
                LIB.save_asset(p)
                try:
                    skel = a.get_editor_property('skeleton')
                    if skel is not None:
                        skeleton_path = skel.get_path_name()
                except Exception:
                    pass
            elif isinstance(a, u.Skeleton) and skeleton_path is None:
                skeleton_path = p

    if mesh_path is None:
        raise RuntimeError('no SkeletalMesh imported for %s' % base)

    wanted_mesh = dest + '/SkeletalMeshes/' + base + '.' + base
    if mesh_path != wanted_mesh:
        u.log_error('import_avatar_glb: %s mesh landed at %s (expected %s) - no rename attempted'
                    % (base, mesh_path, wanted_mesh))
    mesh = u.load_asset(mesh_path)
    b = mesh.get_bounds()
    ext = b.box_extent
    print('MESH_%s=%s' % (base, mesh_path))
    print('SKELETON_%s=%s' % (base, skeleton_path))
    print('BOUNDS_%s extent=(%.1f, %.1f, %.1f) height=%.1f' % (base, ext.x, ext.y, ext.z, ext.z * 2.0))
    try:
        mats = mesh.get_editor_property('materials')
        print('MATS_%s=%d [%s]' % (base, len(mats), ', '.join(
            (m.material_interface.get_name() if getattr(m, 'material_interface', None) else 'NULL') for m in mats)))
    except Exception as e:
        print('MATS_%s=ERR %s' % (base, e))
    skeleton = u.load_asset(skeleton_path) if skeleton_path else None
    return skeleton


def _import_clips(base, clip_prefix, skeleton):
    dest_dir = '/Game/Actor/%s/SkeletalMeshes' % base
    imported = []
    for fbx_name, suffix in CLIPS:
        src = _CLIP_DIR / fbx_name
        if not src.exists():
            u.log_warning('import_avatar_glb: missing clip %s' % src)
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
            u.log_warning('import_avatar_glb: %s produced no AnimSequence (%s)'
                          % (asset_name, task.imported_object_paths))
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
                    u.log_warning('import_avatar_glb: rename %s -> %s failed: %s'
                                  % (seq_pkg, expected, e))
        seq = u.load_asset(seq_pkg)
        if seq is not None and isinstance(seq, u.AnimSequence):
            LIB.save_asset(seq_pkg)
            imported.append(asset_name)
    return imported


def main():
    for base, glb in CHARACTERS:
        src = _CHAR_DIR / glb
        if not src.exists():
            u.log_error('import_avatar_glb: missing %s' % src)
            continue
        try:
            skeleton = _import_mesh(base, src)
        except Exception as e:
            u.log_error('import_avatar_glb: %s mesh import failed: %s' % (base, e))
            continue
        if skeleton is None:
            u.log_error('import_avatar_glb: %s has no skeleton' % base)
            continue
        clips = _import_clips(base, base, skeleton)
        u.log('import_avatar_glb: %s done (%d clips)' % (base, len(clips)))
        print('CHARACTER_%s clips=%d' % (base, len(clips)))
    print('IMPORT_AVATAR_GLB_DONE')


if __name__ == '__main__':
    main()