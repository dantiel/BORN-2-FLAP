"""Import the Desert Queen (Isis) as SK_Isis: GLB mesh + skeleton + materials +
textures (bones pre-renamed to the mixamorig: convention in SK_Isis.glb), then
retarget the shared Mixamo locomotion clips onto her skeleton — same layout as
the other pilots, so the avatar code loads /Game/Actor/SK_Isis/SkeletalMeshes/
SK_Isis<Idle|Walk|Run|Throw|Catch|Standing|StrafeRight|WalkBackward>.

Run (editor closed, headless commandlet):
    UnrealEditor-Cmd Born2Flap.uproject -ExecutePythonScript="V:/BORN 2 FLAP/Unreal/Born2Flap/Tools/import_queen.py" -unattended -nop4 -nosplash -stdout
"""
from pathlib import Path
import unreal as u

TOOLS = u.AssetToolsHelpers.get_asset_tools()
LIB = u.EditorAssetLibrary

_HERE = Path(__file__).resolve()
_PROJECT = _HERE.parents[1]
_CHAR_DIR = _PROJECT / 'SourceAssets/Mixamo/Characters'
_CLIP_DIR = _PROJECT / 'SourceAssets/Mixamo/Clips_Colon'

BASE = 'SK_Isis'
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
        u.log_error('import_queen: %s mesh landed at %s (expected %s)' % (base, mesh_path, wanted_mesh))
    mesh = u.load_asset(mesh_path)
    try:
        b = mesh.get_bounds()
        ext = b.box_extent
        print('BOUNDS_%s extent=(%.1f, %.1f, %.1f) height=%.1f' % (base, ext.x, ext.y, ext.z, ext.z * 2.0))
    except Exception as e:
        print('BOUNDS_%s ERR %s' % (base, e))
    print('MESH_%s=%s' % (base, mesh_path))
    print('SKELETON_%s=%s' % (base, skeleton_path))
    try:
        mats = mesh.get_editor_property('materials')
        print('MATS_%s=%d [%s]' % (base, len(mats), ', '.join(
            (m.material_interface.get_name() if getattr(m, 'material_interface', None) else 'NULL') for m in mats)))
    except Exception as e:
        print('MATS_%s=ERR %s' % (base, e))
    return skeleton_path


def _import_clips(base, skeleton_path):
    dest_dir = '/Game/Actor/%s/SkeletalMeshes' % base
    skeleton = u.load_asset(skeleton_path)
    if skeleton is None:
        u.log_error('import_queen: skeleton missing %s' % skeleton_path)
        return []
    imported = []
    for fbx_name, suffix in CLIPS:
        src = _CLIP_DIR / fbx_name
        if not src.exists():
            u.log_warning('import_queen: missing clip %s' % src)
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
            u.log_warning('import_queen: %s produced no AnimSequence (%s)' % (asset_name, task.imported_object_paths))
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
                    u.log_warning('import_queen: rename %s -> %s failed: %s' % (seq_pkg, expected, e))
        seq = u.load_asset(seq_pkg)
        if seq is not None and isinstance(seq, u.AnimSequence):
            LIB.save_asset(seq_pkg)
            imported.append(asset_name)
            print('CLIP_%s ok' % asset_name)
    return imported


def main():
    src = _CHAR_DIR / (BASE + '.glb')
    if not src.exists():
        u.log_error('import_queen: missing %s' % src)
        return
    try:
        skeleton_path = _import_mesh(BASE, src)
    except Exception as e:
        u.log_error('import_queen: mesh import failed: %s' % e)
        return
    clips = _import_clips(BASE, skeleton_path)
    print('IMPORT_QUEEN_DONE clips=%d [%s]' % (len(clips), ', '.join(clips)))


if __name__ == '__main__':
    main()
