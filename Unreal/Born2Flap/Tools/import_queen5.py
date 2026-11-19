"""Desert Queen (SK_Isis) — v5: fresh-name import of the rebased FBX.

v4 failed: importing over the deleted SK_Isis package made the FBX importer
try to reuse the deleted SK_Isis_Skeleton package -> mesh without skeleton.
Fix: import as SK_IsisB (no stale references possible), then rename into the
canonical SK_Isis names, assign Material_0, retarget the 8 clips.

Run (editor closed, headless commandlet):
    UnrealEditor-Cmd Born2Flap.uproject -ExecutePythonScript="V:/BORN 2 FLAP/Unreal/Born2Flap/Tools/import_queen5.py" -unattended -nop4 -nosplash -stdout
"""
import time
from pathlib import Path
import unreal as u

TOOLS = u.AssetToolsHelpers.get_asset_tools()
LIB = u.EditorAssetLibrary

_HERE = Path(__file__).resolve()
_PROJECT = _HERE.parents[1]
_CLIP_DIR = _PROJECT / 'SourceAssets/Mixamo/Clips_Colon'
_FBX = _PROJECT / 'SourceAssets/NewChars/SK_Isis_ue.fbx'

BASE = 'SK_Isis'
TEMP = 'SK_IsisB'
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


def _nuke(name):
    p = '/Game/Actor/%s/SkeletalMeshes/%s' % (BASE, name)
    if LIB.does_asset_exist(p):
        try:
            LIB.delete_loaded_asset(p)
        except Exception as e:
            print('NUKELOAD_%s %s' % (p, e))
        try:
            LIB.delete_asset(p)
            print('DELETED_%s' % p)
        except Exception as e:
            print('DELETEFAIL_%s %s' % (p, e))
    else:
        print('SKIP_%s (absent)' % p)


def _import_mesh():
    dest_dir = '/Game/Actor/%s/SkeletalMeshes' % BASE
    LIB.make_directory(dest_dir)
    options = u.FbxImportUI()
    options.automated_import_should_detect_type = False
    options.import_mesh = True
    options.import_as_skeletal = True
    options.mesh_type_to_import = u.FBXImportType.FBXIT_SKELETAL_MESH
    options.original_import_type = u.FBXImportType.FBXIT_SKELETAL_MESH
    options.import_animations = False
    # The exported FBX carries the queen's body displaced to FBX Y~5.735 m
    # (export round-trip bug). Shift the whole scene uniformly so the body
    # centres on the skeleton root; the bind pose stays internally consistent
    # (uniform scene transform does not affect relative matrices).
    try:
        options.set_editor_property('import_translation', u.Vector(0.0, -5.735, 0.0))
        print('IMPORT_TRANSLATION set (0,-5.735,0)')
    except Exception as e:
        print('IMPORT_TRANSLATION failed: %s' % e)
    # The same export round-trip also stamped the root node with a ~1e-4
    # Lcl Scaling, which collapses the body to a 1 cm sliver (the "Isis is as
    # big as the sky" symptom is the runtime then multiplying that sliver up).
    # Counteract it with a uniform import scale so the mesh lands at authored
    # human size (1 / 1e-4 = 10000).
    try:
        options.set_editor_property('import_uniform_scale', 10000.0)
        print('IMPORT_UNIFORM_SCALE set 10000')
    except Exception as e:
        print('IMPORT_UNIFORM_SCALE failed: %s' % e)

    task = u.AssetImportTask()
    task.filename = str(_FBX)
    task.destination_path = dest_dir
    task.destination_name = TEMP
    task.automated = True
    task.replace_existing = True
    task.save = True
    task.options = options
    task.factory = u.FbxFactory()
    TOOLS.import_asset_tasks([task])

    mesh_path, skeleton_path = None, None
    for p in task.imported_object_paths:
        a = u.load_asset(p)
        if a is None:
            continue
        if isinstance(a, u.SkeletalMesh) and mesh_path is None:
            mesh_path = p.rsplit('.', 1)[0]
        elif isinstance(a, u.Skeleton) and skeleton_path is None:
            skeleton_path = p.rsplit('.', 1)[0]
    print('FBX_MESH=%s' % mesh_path)
    print('FBX_SKELETON=%s' % skeleton_path)
    if mesh_path is not None and skeleton_path is None:
        m = u.load_asset(mesh_path)
        try:
            skel = m.get_editor_property('skeleton')
            if skel is not None:
                skeleton_path = skel.get_path_name().rsplit('.', 1)[0]
                print('FBX_SKELETON_FROM_MESH=%s' % skeleton_path)
        except Exception as e:
            print('FBX_SKELETON_ERR %s' % e)
    if mesh_path is not None:
        m = u.load_asset(mesh_path)
        b = m.get_bounds()
        print('BOUNDS_%s origin=(%.2f,%.2f,%.2f) height=%.1f' % (TEMP, b.origin.x, b.origin.y, b.origin.z, b.box_extent.z * 2.0))
    return mesh_path, skeleton_path


def _import_clips(skeleton_path):
    dest_dir = '/Game/Actor/%s/SkeletalMeshes' % BASE
    skeleton = u.load_asset(skeleton_path)
    imported = []
    for fbx_name, suffix in CLIPS:
        src = _CLIP_DIR / fbx_name
        asset_name = BASE + suffix
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
            u.log_warning('import_queen5: %s produced no AnimSequence (%s)' % (asset_name, task.imported_object_paths))
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
                    u.log_warning('import_queen5: rename %s -> %s failed: %s' % (seq_pkg, expected, e))
        seq = u.load_asset(seq_pkg)
        if seq is not None and isinstance(seq, u.AnimSequence):
            LIB.save_asset(seq_pkg)
            imported.append(asset_name)
            print('CLIP_%s ok' % asset_name)
    return imported


def main():
    if not _FBX.exists():
        u.log_error('import_queen5: missing %s' % _FBX)
        return

    # 1. nuke everything SK_Isis* (old broken imports)
    for name in [BASE, BASE + 'Fbx', TEMP, BASE + '_Skeleton', TEMP + '_Skeleton',
                 BASE + '_PhysicsAsset', TEMP + '_PhysicsAsset']:
        _nuke(name)
    u.SystemLibrary.collect_garbage()
    time.sleep(2.0)
    u.SystemLibrary.collect_garbage()

    # 2. fresh FBX import under the temp name
    mesh_path, skeleton_path = _import_mesh()
    if mesh_path is None or skeleton_path is None:
        u.log_error('import_queen5: no mesh/skeleton after reimport')
        return

    # 3. rename temp -> canonical
    for pair in [(mesh_path, '/Game/Actor/%s/SkeletalMeshes/%s' % (BASE, BASE))]:
        if pair[0] != pair[1]:
            try:
                LIB.rename_asset(pair[0], pair[1])
                print('RENAME %s -> %s' % pair)
            except Exception as e:
                print('RENAMEFAIL %s -> %s : %s' % (pair[0], pair[1], e))
    mesh_path = '/Game/Actor/%s/SkeletalMeshes/%s' % (BASE, BASE)
    if skeleton_path.startswith('/Game/Actor/%s/SkeletalMeshes/%s' % (BASE, TEMP)):
        new_skel = '/Game/Actor/%s/SkeletalMeshes/%s_Skeleton' % (BASE, BASE)
        if not LIB.does_asset_exist(new_skel):
            try:
                LIB.rename_asset(skeleton_path, new_skel)
                print('RENAME %s -> %s' % (skeleton_path, new_skel))
                skeleton_path = new_skel
            except Exception as e:
                print('RENAMEFAIL %s -> %s : %s' % (skeleton_path, new_skel, e))

    # 4. material slot
    mat = u.load_asset('/Game/Actor/%s/Materials/Material_0.Material_0' % BASE)
    m = u.load_asset(mesh_path)
    if m is not None and mat is not None:
        mats = list(m.get_editor_property('materials'))
        for i, sm in enumerate(mats):
            sm.set_editor_property('material_interface', mat)
            mats[i] = sm
        m.set_editor_property('materials', mats)
        LIB.save_asset(mesh_path)
        print('SLOT_MATERIAL assigned slots=%d' % len(mats))
        b = m.get_bounds()
        print('BOUNDS_FINAL origin=(%.2f,%.2f,%.2f) extent=(%.2f,%.2f,%.2f)' % (
            b.origin.x, b.origin.y, b.origin.z, b.box_extent.x, b.box_extent.y, b.box_extent.z))

    # 5. clips
    clips = _import_clips(skeleton_path)
    print('IMPORT_QUEEN5_DONE clips=%d [%s]' % (len(clips), ', '.join(clips)))


if __name__ == '__main__':
    try:
        main()
    finally:
        u.SystemLibrary.execute_console_command(None, 'quit')