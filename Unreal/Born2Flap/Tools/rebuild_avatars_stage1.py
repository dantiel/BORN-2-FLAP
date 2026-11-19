"""Stage 1c: finalise John + Isis.

- Reimport SK_Founder.glb (replace_existing) so its 13 embedded animations
  land with the C++ names (SK_FounderThrow, SK_FounderWalk, ...). NO rename
  pass this time (StartWalk/StopWalk are extras, never renamed).
- Fill any missing standard clip from the underscore FBX set.
- Isis: her GLB import created SK_IsisWalk/SK_IsisRun (own rig animations).
  Verify them; if invalid, delete + retarget the underscore Walk/Run FBXs.
  Delete stray *_mixamo_com junk.
- Reimport founder + Isis textures from the extracted sources (scratch at
  project root, not Unreal/scratch).
"""
from pathlib import Path
import unreal as u

TOOLS = u.AssetToolsHelpers.get_asset_tools()
LIB = u.EditorAssetLibrary

_HERE = Path(__file__).resolve()
_PROJECT = _HERE.parents[1]
_ROOT = _PROJECT.parents[1]
_CHAR_DIR = _PROJECT / 'SourceAssets/Mixamo/Characters'
_CLIP_DIR = _PROJECT / 'SourceAssets/Mixamo/Clips_Underscore'
_SCRATCH = _ROOT / 'scratch'

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


def import_glb(base, src):
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
    dest = '/Game/Actor/%s' % base
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
    if mesh_path is None or skeleton_path is None:
        raise RuntimeError('import failed for %s (mesh=%s skel=%s)' % (base, mesh_path, skeleton_path))
    print('MESH_%s=%s' % (base, mesh_path))
    print('SKELETON_%s=%s' % (base, skeleton_path))
    return skeleton_path


def list_anims(base):
    dir = '/Game/Actor/%s/SkeletalMeshes' % base
    out = {}
    if not LIB.does_directory_exist(dir):
        return out
    for p in LIB.list_assets(dir, recursive=False, include_folder=False):
        a = u.load_asset(p)
        if a is not None and isinstance(a, u.AnimSequence):
            out[a.get_name()] = p
    return out


def delete_junk(base):
    dir = '/Game/Actor/%s/SkeletalMeshes' % base
    for p in LIB.list_assets(dir, recursive=False, include_folder=False):
        nm = Path(p).stem
        if 'mixamo_com' in nm or nm == base + 'Fbx':
            try:
                LIB.delete_asset(p)
                print('JUNK_DELETE', p)
            except Exception as e:
                print('JUNK_DELETE_FAIL', p, str(e)[:100])


def import_one_clip(base, skeleton_path, fbx_name, suffix):
    asset_name = base + suffix
    dest_dir = '/Game/Actor/%s/SkeletalMeshes' % base
    src = _CLIP_DIR / fbx_name
    if not src.exists():
        print('CLIP_MISSING', src)
        return False
    options = u.FbxImportUI()
    options.automated_import_should_detect_type = False
    options.import_mesh = False
    options.import_as_skeletal = True
    options.mesh_type_to_import = u.FBXImportType.FBXIT_ANIMATION
    options.original_import_type = u.FBXImportType.FBXIT_ANIMATION
    options.import_animations = True
    options.skeleton = u.load_asset(skeleton_path)
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
        print('CLIP_NOANIM', asset_name)
        return False
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
                print('CLIP_RENAME_FAIL', seq_pkg, '->', expected, str(e)[:100])
    seq = u.load_asset(seq_pkg)
    if seq is not None and isinstance(seq, u.AnimSequence):
        LIB.save_asset(seq_pkg)
        print('CLIP_OK', asset_name, 'frames:', str(seq.get_editor_property('number_of_frames')))
        return True
    print('CLIP_LOAD_FAIL', seq_pkg)
    return False


def reimport_texture(src_file, dest_dir, force_srgb=None, force_normal=None):
    name = Path(src_file).stem
    expected = dest_dir + '/' + name
    if LIB.does_asset_exist(expected):
        try:
            LIB.delete_asset(expected)
        except Exception as e:
            print('TEXDEL_FAIL', expected, str(e)[:100])
    if force_srgb is None:
        n = name.casefold()
        if 'normal' in n or 'nm' in n:
            srgb, is_normal = False, True
        elif 'diffuse' in n or 'basecolor' in n:
            srgb, is_normal = True, False
        else:
            srgb, is_normal = False, False
    else:
        srgb, is_normal = force_srgb, bool(force_normal)
    factory = u.TextureFactory()
    try:
        factory.set_editor_property('no_alpha', False)
    except Exception:
        pass
    try:
        if is_normal:
            factory.set_editor_property('compression_settings', u.TextureCompressionSettings.TC_NORMALMAP)
        factory.set_editor_property('srgb', srgb)
    except Exception:
        pass
    task = u.AssetImportTask()
    task.filename = str(src_file)
    task.destination_path = dest_dir
    task.destination_name = name
    task.automated = True
    task.replace_existing = True
    task.save = True
    task.factory = factory
    TOOLS.import_asset_tasks([task])
    tex = u.load_asset(expected)
    if tex is None:
        print('TEXIMPORT_FAIL', expected)
        return
    try:
        tex.set_editor_property('lod_bias', 0)
        tex.set_editor_property('max_texture_size', 0)
    except Exception:
        pass
    LIB.save_loaded_asset(tex, only_if_is_dirty=False)
    try:
        got = (tex.blueprint_get_size_x(), tex.blueprint_get_size_y())
    except Exception:
        got = '?'
    print('TEXREIMPORT %s -> %s size=%s' % (src_file.name, expected, got))


def main():
    print('STAGE1C_BEGIN')
    # John
    try:
        sk = import_glb('SK_Founder', _PROJECT / 'SourceAssets/Avatars/SK_Founder.glb')
        anims = list_anims('SK_Founder')
        print('FOUNDER_ANIMS=%s' % sorted(anims))
        for fbx_name, suffix in CLIPS:
            if 'SK_Founder' + suffix not in anims:
                import_one_clip('SK_Founder', sk, fbx_name, suffix)
    except Exception as e:
        print('JOHN_FAIL', str(e)[:300])
    # Isis
    try:
        sk = import_glb('SK_Isis', _CHAR_DIR / 'SK_Isis.glb')
        delete_junk('SK_Isis')
        anims = list_anims('SK_Isis')
        print('ISIS_ANIMS=%s' % sorted(anims))
        for fbx_name, suffix in CLIPS:
            want = 'SK_Isis' + suffix
            have = anims.get(want)
            if have is not None:
                seq = u.load_asset(have)
                try:
                    skel = seq.get_editor_property('skeleton')
                    frames = seq.get_editor_property('number_of_frames')
                except Exception as e:
                    skel, frames = None, 'ERR'
                if skel is not None and frames not in (0, 'ERR', None):
                    print('ISIS_KEEP', want, 'frames:', frames, 'skel:', skel.get_name())
                    continue
                print('ISIS_REPLACE', want, 'invalid (skel=%s frames=%s)' % (skel, frames))
                try:
                    LIB.delete_asset(have)
                except Exception as e:
                    print('ISIS_DEL_FAIL', have, str(e)[:100])
            import_one_clip('SK_Isis', sk, fbx_name, suffix)
    except Exception as e:
        print('ISIS_FAIL', str(e)[:300])
    # Founder + Isis textures
    try:
        ftex = _SCRATCH / 'founder_tex'
        mapping = {0: (True, False), 1: (False, False), 2: (False, True), 3: (False, True), 4: (True, False)}
        for png in sorted(ftex.glob('*.png')):
            idx = int(png.stem.rsplit('_', 1)[-1])
            srgb, isn = mapping.get(idx, (True, False))
            reimport_texture(png, '/Game/Actor/SK_Founder/Textures', force_srgb=srgb, force_normal=isn)
        itex = _SCRATCH / 'isis_tex'
        imap = {'rig_normal': (False, True), 'rig_basecolor': (True, False), 'rig_metallic-rig_roughness': (False, False)}
        for img in sorted(itex.glob('*')):
            srgb, isn = imap.get(img.stem, (True, False))
            reimport_texture(img, '/Game/Actor/SK_Isis/Textures', force_srgb=srgb, force_normal=isn)
    except Exception as e:
        print('TEXTURE_PASS_FAIL', str(e)[:300])
    print('STAGE1C_DONE')


if __name__ == '__main__':
    main()
