"""Desert Queen (SK_Isis) — FBX-frame swap + clip retarget.

The GLB-imported skeleton fails FBX clip retargeting (Interchange sanitises
`mixamorig:Hips` to `mixamorig_Hips`, so the clip importer cannot find the
root track). The 8 working pilots all use FBX-imported skeletons (same FBX
frame as the Clips_Colon animations). This script converts the queen:

1. Export the imported GLB mesh to ASCII FBX.
2. Rename the bone tokens `mixamorig_X` -> `mixamorig:X` in the file
   (the FBX importer preserves colons, matching the clip tracks).
3. Import that FBX as `SK_IsisFbx` (mesh + skeleton) next to the GLB assets.
4. Delete the GLB mesh/skeleton/physics assets (GLB materials/textures stay).
5. Rename the FBX mesh/skeleton/physics into the canonical SK_Isis names.
6. Assign the GLB Material_0 MIC to the mesh slot.
7. Retarget the 8 shared locomotion clips onto the FBX skeleton.

Run (editor closed, headless commandlet):
    UnrealEditor-Cmd Born2Flap.uproject -ExecutePythonScript="V:/BORN 2 FLAP/Unreal/Born2Flap/Tools/import_queen2.py" -unattended -nop4 -nosplash -stdout
"""
from pathlib import Path
import unreal as u

TOOLS = u.AssetToolsHelpers.get_asset_tools()
LIB = u.EditorAssetLibrary

_HERE = Path(__file__).resolve()
_PROJECT = _HERE.parents[1]
_CLIP_DIR = _PROJECT / 'SourceAssets/Mixamo/Clips_Colon'
_FBX_OUT = _PROJECT / 'SourceAssets/NewChars/SK_Isis_ue.fbx'

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


def _export_fbx(mesh):
    task = u.AssetExportTask()
    task.object = mesh
    task.filename = str(_FBX_OUT)
    task.automated = True
    task.replace_identical = True
    task.prompt = False
    opt = u.FbxExportOption()
    try:
        opt.set_editor_property('ascii', True)
    except Exception as e:
        u.log_warning('import_queen2: ascii export option failed (%s) - binary rename impossible' % e)
    task.options = opt
    ok = u.Exporter.run_asset_export_task(task)
    print('EXPORT_%s ok=%s size=%d' % (BASE, ok, _FBX_OUT.stat().st_size if _FBX_OUT.exists() else -1))
    return ok


def _rename_bones():
    txt = _FBX_OUT.read_text(encoding='utf-8', errors='replace')
    n = txt.count('mixamorig_')
    txt = txt.replace('mixamorig_', 'mixamorig:')
    _FBX_OUT.write_text(txt, encoding='utf-8')
    print('FBX_RENAME tokens=%d' % n)
    return n


def _import_fbx_mesh():
    dest_dir = '/Game/Actor/%s/SkeletalMeshes' % BASE
    LIB.make_directory(dest_dir)
    options = u.FbxImportUI()
    options.automated_import_should_detect_type = False
    options.import_mesh = True
    options.import_as_skeletal = True
    options.mesh_type_to_import = u.FBXImportType.FBXIT_SKELETAL_MESH
    options.original_import_type = u.FBXImportType.FBXIT_SKELETAL_MESH
    options.import_animations = False

    task = u.AssetImportTask()
    task.filename = str(_FBX_OUT)
    task.destination_path = dest_dir
    task.destination_name = BASE + 'Fbx'
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
    return mesh_path, skeleton_path


def _import_clips(skeleton_path):
    dest_dir = '/Game/Actor/%s/SkeletalMeshes' % BASE
    skeleton = u.load_asset(skeleton_path)
    imported = []
    for fbx_name, suffix in CLIPS:
        src = _CLIP_DIR / fbx_name
        if not src.exists():
            u.log_warning('import_queen2: missing clip %s' % src)
            continue
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
            u.log_warning('import_queen2: %s produced no AnimSequence (%s)' % (asset_name, task.imported_object_paths))
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
                    u.log_warning('import_queen2: rename %s -> %s failed: %s' % (seq_pkg, expected, e))
        seq = u.load_asset(seq_pkg)
        if seq is not None and isinstance(seq, u.AnimSequence):
            LIB.save_asset(seq_pkg)
            imported.append(asset_name)
            print('CLIP_%s ok' % asset_name)
    return imported


def main():
    mesh_path = '/Game/Actor/%s/SkeletalMeshes/%s' % (BASE, BASE)
    mesh = u.load_asset(mesh_path)
    if mesh is None or not isinstance(mesh, u.SkeletalMesh):
        u.log_error('import_queen2: GLB mesh missing at %s - run import_queen.py first' % mesh_path)
        return

    # 1-2. export + bone rename
    if not _export_fbx(mesh):
        u.log_error('import_queen2: FBX export failed')
        return
    renamed = _rename_bones()
    if renamed == 0:
        u.log_error('import_queen2: no bone tokens found in exported FBX - aborting')
        return

    # 3. import the FBX next to the GLB assets
    fbx_mesh, fbx_skel = _import_fbx_mesh()
    if fbx_mesh is None or fbx_skel is None:
        u.log_error('import_queen2: FBX reimport produced no mesh/skeleton')
        return

    # 4. delete the GLB mesh/skeleton/physics
    for name in [BASE, BASE + '_Skeleton', BASE + '_PhysicsAsset']:
        p = '/Game/Actor/%s/SkeletalMeshes/%s' % (BASE, name)
        if LIB.does_asset_exist(p):
            LIB.delete_asset(p)
            print('DELETED_%s' % p)

    # 5. rename FBX assets into canonical names
    for src, dst in [(fbx_mesh, mesh_path),
                     (fbx_skel, '/Game/Actor/%s/SkeletalMeshes/%s_Skeleton' % (BASE, BASE)),
                     (fbx_mesh + '_PhysicsAsset', '/Game/Actor/%s/SkeletalMeshes/%s_PhysicsAsset' % (BASE, BASE))]:
        if LIB.does_asset_exist(src):
            try:
                LIB.rename_asset(src, dst)
                print('RENAMED %s -> %s' % (src, dst))
            except Exception as e:
                print('RENAME_FAIL %s -> %s: %s' % (src, dst, e))

    # 6. assign the GLB Material_0 MIC to the mesh slot
    mat = u.load_asset('/Game/Actor/%s/Materials/Material_0.Material_0' % BASE)
    m2 = u.load_asset(mesh_path)
    if m2 is not None and mat is not None:
        mats = list(m2.get_editor_property('materials'))
        assigned = False
        for i, sm in enumerate(mats):
            sm.set_editor_property('material_interface', mat)
            mats[i] = sm
            assigned = True
        m2.set_editor_property('materials', mats)
        LIB.save_asset(mesh_path)
        print('SLOT_MATERIAL assigned=%s' % assigned)
    else:
        print('SLOT_MATERIAL skipped mesh=%s mat=%s' % (m2 is not None, mat is not None))

    # 7. retarget clips
    clips = _import_clips('/Game/Actor/%s/SkeletalMeshes/%s_Skeleton' % (BASE, BASE))
    print('IMPORT_QUEEN2_DONE clips=%d [%s]' % (len(clips), ', '.join(clips)))


if __name__ == '__main__':
    main()
