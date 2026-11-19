"""Repair avatar materials end-to-end:

1. Mixamo characters: (re)assign the GLB-imported PBR materials to the FBX
   mesh slots BY NAME with aliasing (slot "Ch02_body1" matches material
   "Ch02_body"). The plain exact-name match in assemble_avatars.py left
   Sophie/Kate/Adam/Brian with NULL slots.
2. Force BODY materials to BLEND_OPAQUE. Several GLBs declare
   alphaMode=BLEND on the body (e.g. James Ch06_body), which makes the whole
   body render translucent — "transparent textures so inside can be seen".
   The two-sided flag is left as authored (True from the GLB).
3. John (SK_Founder): force his three glTF material instances opaque
   (slot 2 was translucent too).
4. Erika: her FBX slots (EyeSpec_MAT / Akai_MAT / eyelash_MAT / Body_MAT)
   are all NULL and she has no GLB — build the four materials from
   SourceAssets/Mixamo/Characters/Erika_Archer.fbm and assign by name.

Run (editor closed, headless commandlet):
    UnrealEditor-Cmd Born2Flap.uproject -ExecutePythonScript="V:/BORN 2 FLAP/Unreal/Born2Flap/Tools/fix_avatar_materials.py" -unattended -nop4 -nosplash -stdout
"""
import re
import sys
from pathlib import Path

import unreal as u

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from project_guard import require_project

require_project()

TOOLS = u.AssetToolsHelpers.get_asset_tools()
LIB = u.EditorAssetLibrary
MEL = u.MaterialEditingLibrary

_CHAR_DIR = HERE.parent / 'SourceAssets/Mixamo/Characters'

MIXAMO = [
    ('SK_Amy', 'SK_Amy.glb'),
    ('SK_Michelle', 'SK_Michelle.glb'),
    ('SK_Sophie', 'SK_Sophie.glb'),
    ('SK_Kate', 'SK_Kate.glb'),
    ('SK_James', 'SK_James.glb'),
    ('SK_Adam', 'SK_Adam.glb'),
    ('SK_Brian', 'SK_Brian.glb'),
    ('SK_Alex', 'SK_Alex.glb'),
]


def _alias(name):
    """Normalise a slot/material name: casefold + strip trailing digits
    (Ch02_body1 -> ch02_body) so FBX slot names match GLB material names."""
    n = name.casefold()
    n = re.sub(r'[_\s]*\d+$', '', n)
    n = re.sub(r'[_\s]+', '_', n)
    return n


def _is_hairy(name):
    n = name.casefold()
    return any(k in n for k in ('hair', 'eyelash', 'lash', 'beard', 'brow'))


def _is_body(name):
    n = name.casefold()
    return 'body' in n and not _is_hairy(name)


def _force_opaque(mi):
    """Force an opaque, keep two-sided as authored (GLB bodies are authored
    double-sided). Works on MICs via BasePropertyOverrides and on base
    Materials via direct properties."""
    if isinstance(mi, u.MaterialInstanceConstant):
        bo = mi.get_editor_property('base_property_overrides')
        bo.set_editor_property('override_blend_mode', True)
        bo.set_editor_property('blend_mode', u.BlendMode.BLEND_OPAQUE)
        try:
            bo.set_editor_property('override_two_sided', True)
            bo.set_editor_property('two_sided', True)
        except Exception:
            pass
        mi.set_editor_property('base_property_overrides', bo)
    else:
        mi.set_editor_property('blend_mode', u.BlendMode.BLEND_OPAQUE)
        try:
            mi.set_editor_property('two_sided', True)
        except Exception:
            pass
    return mi


def _fix_mixamo(base):
    mesh_path = '/Game/Actor/%s/SkeletalMeshes/%s.%s' % (base, base, base)
    mesh = u.load_asset(mesh_path)
    if mesh is None:
        print('FIX_%s MESH_MISSING' % base)
        return
    # Collect candidate materials: everything MIC/Material under the folder.
    cands = {}
    for p in LIB.list_assets('/Game/Actor/%s/Materials' % base, recursive=False, include_folder=False):
        a = u.load_asset(p)
        if a is not None:
            cands[a.get_name()] = a
    mats = list(mesh.get_editor_property('materials'))
    assigned = []
    for i, sm in enumerate(mats):
        slot = str(sm.get_editor_property('material_slot_name'))
        pick = cands.get(slot)
        if pick is None:
            al = _alias(slot)
            for name, m in cands.items():
                if _alias(name) == al:
                    pick = m
                    break
        if pick is None and i < len(cands):
            keys = [k for k in cands if k not in assigned]
            pick = cands.get(keys[0]) if keys else None
        if pick is None:
            assigned.append('%s:NO_MAT' % slot)
            continue
        # Bodies must never be translucent (GLB alphaMode=BLEND on some).
        forced = False
        if _is_body(pick.get_name()):
            pick = _force_opaque(pick)
            forced = True
        sm.set_editor_property('material_interface', pick)
        # Array elements are STRUCT COPIES in UE python — mutating `sm` alone
        # silently discards the change. Write the struct back into the array
        # or the mesh assignment below is a no-op.
        mats[i] = sm
        if forced:
            # The opaque override lives on the MIC, not the mesh — persist it
            # or the body reloads translucent on the next session.
            LIB.save_loaded_asset(pick, only_if_is_dirty=False)
        assigned.append('%s->%s' % (slot, pick.get_name()))
    mesh.set_editor_property('materials', mats)
    # save_asset() skips packages the python API did not mark dirty (property
    # edits via set_editor_property do NOT dirty) — force the write or the
    # assignment silently evaporates.
    LIB.save_loaded_asset(mesh, only_if_is_dirty=False)
    print('FIX_%s=%s' % (base, assigned))


def _fix_founder():
    for name in ('Material_001', 'Material_002', 'Material_003'):
        mi = u.load_asset('/Game/Actor/SK_Founder/Materials/%s.%s' % (name, name))
        if mi is None:
            print('FIX_John %s MISSING' % name)
            continue
        _force_opaque(mi)
        LIB.save_loaded_asset(mi, only_if_is_dirty=False)
        print('FIX_John %s opaque' % name)


def _import_tex(png, dest_dir, is_normal):
    if not png.exists():
        u.log_warning('fix_avatar_materials: missing %s' % png)
        return None
    expected = dest_dir + '/' + png.stem
    if LIB.does_asset_exist(expected):
        return u.load_asset(expected)
    factory = u.TextureFactory()
    try:
        factory.set_editor_property('no_alpha', False)
    except Exception:
        pass
    try:
        if is_normal:
            factory.set_editor_property('compression_settings', u.TextureCompressionSettings.TC_NORMALMAP)
        factory.set_editor_property('srgb', not is_normal)
    except Exception:
        pass
    task = u.AssetImportTask()
    task.filename = str(png)
    task.destination_path = dest_dir
    task.destination_name = png.stem
    task.automated = True
    task.replace_existing = True
    task.save = True
    task.factory = factory
    TOOLS.import_asset_tasks([task])
    return u.load_asset(expected)


def _tex_sample(mat, tex, x, y):
    expr = MEL.create_material_expression(mat, u.MaterialExpressionTextureSample, x, y)
    expr.set_editor_property('texture', tex)
    return expr


def _build_erika_mat(name, base_tex, normal_tex, masked, two_sided):
    folder = '/Game/Actor/SK_Erika/Materials'
    mat_path = folder + '/' + name
    if LIB.does_asset_exist(mat_path):
        mat = u.load_asset(mat_path)
        if isinstance(mat, u.Material):
            return mat
        LIB.delete_asset(mat_path)
    mat = u.AssetToolsHelpers.get_asset_tools().create_asset(
        name, folder, u.Material, u.MaterialFactoryNew())
    if mat is None:
        u.log_error('fix_avatar_materials: create_asset failed for %s' % name)
        return None
    mat.set_editor_property('two_sided', two_sided)
    if masked:
        mat.set_editor_property('blend_mode', u.BlendMode.BLEND_MASKED)
    else:
        mat.set_editor_property('blend_mode', u.BlendMode.BLEND_OPAQUE)
    if base_tex is not None:
        s = _tex_sample(mat, base_tex, -600, 0)
        MEL.connect_material_property(s, 'RGB', u.MaterialProperty.MP_BASE_COLOR)
        if masked:
            MEL.connect_material_property(s, 'A', u.MaterialProperty.MP_OPACITY_MASK)
    if normal_tex is not None:
        n = _tex_sample(mat, normal_tex, -600, -260)
        MEL.connect_material_property(n, 'RGB', u.MaterialProperty.MP_NORMAL)
    c = MEL.create_material_expression(mat, u.MaterialExpressionConstant, -600, -780)
    c.set_editor_property('r', 0.0)
    MEL.connect_material_property(c, '', u.MaterialProperty.MP_METALLIC)
    c2 = MEL.create_material_expression(mat, u.MaterialExpressionConstant, -600, -900)
    c2.set_editor_property('r', 0.75)
    MEL.connect_material_property(c2, '', u.MaterialProperty.MP_ROUGHNESS)
    LIB.save_loaded_asset(mat, only_if_is_dirty=False)
    return mat


def _fix_erika():
    fbm = _CHAR_DIR / 'Erika_Archer.fbm'
    tex_dir = '/Game/Actor/SK_Erika/Textures'
    body = _import_tex(fbm / 'FemaleFitA_Body_diffuse.png', tex_dir, False)
    body_n = _import_tex(fbm / 'FemaleFitA_StdNM.png', tex_dir, True)
    clothes = _import_tex(fbm / 'Erika_Archer_Clothes_diffuse.png', tex_dir, False)
    clothes_n = _import_tex(fbm / 'Erika_Archer_Clothes_normal.png', tex_dir, True)
    lash = _import_tex(fbm / 'FemaleFitA_eyelash_diffuse.png', tex_dir, False)

    # EyeSpec: plain dark glossy eye material (no eye texture shipped).
    eye = _build_erika_mat('EyeSpec_MAT', None, None, False, False)
    if eye is not None:
        c = MEL.create_material_expression(eye, u.MaterialExpressionConstant3Vector, -600, -120)
        c.set_editor_property('constant', u.LinearColor(0.02, 0.015, 0.012, 1.0))
        MEL.connect_material_property(c, '', u.MaterialProperty.MP_BASE_COLOR)
        r = MEL.create_material_expression(eye, u.MaterialExpressionConstant, -600, -960)
        r.set_editor_property('r', 0.1)
        MEL.connect_material_property(r, '', u.MaterialProperty.MP_ROUGHNESS)
        LIB.save_loaded_asset(eye, only_if_is_dirty=False)

    mats = {
        'EyeSpec_MAT': eye,
        'Akai_MAT': _build_erika_mat('Akai_MAT', clothes, clothes_n, False, False),
        'eyelash_MAT': _build_erika_mat('eyelash_MAT', lash, None, True, True),
        'Body_MAT': _build_erika_mat('Body_MAT', body, body_n, False, False),
    }
    mesh_path = '/Game/Actor/SK_Erika/SkeletalMeshes/SK_Erika.SK_Erika'
    mesh = u.load_asset(mesh_path)
    if mesh is None:
        print('FIX_Erika MESH_MISSING')
        return
    sms = list(mesh.get_editor_property('materials'))
    assigned = []
    for i, sm in enumerate(sms):
        slot = str(sm.get_editor_property('material_slot_name'))
        mat = mats.get(slot)
        if mat is not None:
            sm.set_editor_property('material_interface', mat)
            # Struct copies: write back into the array (see _fix_mixamo).
            sms[i] = sm
            assigned.append('%s->%s' % (slot, mat.get_name()))
        else:
            assigned.append('%s:NO_MAT' % slot)
    mesh.set_editor_property('materials', sms)
    LIB.save_loaded_asset(mesh, only_if_is_dirty=False)
    print('FIX_Erika=%s' % assigned)


def main():
    for base, _ in MIXAMO:
        _fix_mixamo(base)
    _fix_founder()
    _fix_erika()
    print('FIX_AVATAR_MATERIALS_DONE')


if __name__ == '__main__':
    try:
        main()
    finally:
        u.SystemLibrary.execute_console_command(None, 'quit')