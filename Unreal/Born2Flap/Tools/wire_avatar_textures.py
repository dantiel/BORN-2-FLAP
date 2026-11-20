"""Rebuild the classic avatar materials from the GLB texture sets, replacing the
empty FBX placeholder materials (root cause of "characters have no textures").

The FBX placeholder materials are bare `Material` assets with no texture
wiring; reusing them in place does not stick (verified). The reliable path is
to DELETE the placeholder, create a fresh Material, wire BaseColor/Normal/
Roughness/Metallic, then re-assign the mesh slot.

Run (editor closed, headless commandlet):
    UnrealEditor-Cmd Born2Flap.uproject -run=pythonscript -script="V:/BORN 2 FLAP/Unreal/Born2Flap/Tools/wire_avatar_textures.py" -unattended -nop4 -nosplash -stdout -nullrhi
"""
import json
import struct
from pathlib import Path
import unreal as u

TOOLS = u.AssetToolsHelpers.get_asset_tools()
LIB = u.EditorAssetLibrary
MEL = u.MaterialEditingLibrary

_HERE = Path(__file__).resolve()
_PROJECT = _HERE.parents[1]
_CHAR_DIR = _PROJECT / 'SourceAssets/Mixamo/Characters'

# Characters that ship a GLB (material + texture plan). Erika is a separate
# FBX-texture case (no GLB) handled elsewhere.
CHARACTERS = ['SK_Amy', 'SK_Michelle', 'SK_Sophie', 'SK_Kate',
              'SK_James', 'SK_Adam', 'SK_Brian', 'SK_Alex']


def _read_glb_json(name):
    b = (_CHAR_DIR / (name + '.glb')).read_bytes()
    total = struct.unpack_from('<I', b, 8)[0]
    off = 12
    json_bytes = None
    while off + 8 <= total:
        clen, ctype = struct.unpack_from('<II', b, off)
        if ctype == 0x4E4F534A:
            json_bytes = b[off + 8:off + 8 + clen]
        off += 8 + clen
    return json.loads(json_bytes)


def _material_plan(name):
    j = _read_glb_json(name)
    images = [img.get('name', 'img%02d' % i) for i, img in enumerate(j.get('images', []))]
    textures = j.get('textures', [])

    def img_of(tex_ref):
        t = textures[tex_ref['index']]
        src = t.get('source', t.get('extensions', {}).get('KHR_texture_basisu', {}).get('source', 0))
        return images[src] if src < len(images) else None

    plan = {}
    for m in j.get('materials', []):
        mname = m.get('name', 'mat')
        pbr = m.get('pbrMetallicRoughness', {})
        entry = {}
        if 'baseColorTexture' in pbr:
            entry['base'] = img_of(pbr['baseColorTexture'])
        if 'metallicRoughnessTexture' in pbr:
            entry['mr'] = img_of(pbr['metallicRoughnessTexture'])
        if 'normalTexture' in m:
            entry['normal'] = img_of(m['normalTexture'])
        plan[mname] = entry
    return plan


def _load_or_import_texture(png, dest_dir, is_normal, is_color):
    name = png.stem
    expected = dest_dir + '/' + name
    if LIB.does_asset_exist(expected):
        return u.load_asset(expected)
    if not png.exists():
        return None
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
    task.destination_path = dest_dir
    task.destination_name = name
    task.automated = True
    task.replace_existing = True
    task.save = True
    task.factory = factory
    TOOLS.import_asset_tasks([task])
    return u.load_asset(expected)


def _add_tex_sample(mat, tex, x, y, sampler_type):
    expr = MEL.create_material_expression(mat, u.MaterialExpressionTextureSample, x, y)
    if expr is None:
        return None
    expr.set_editor_property('texture', tex)
    try:
        expr.set_editor_property('sampler_type', sampler_type)
    except Exception:
        pass
    return expr


def _wire_material(mat_name, entry, tex_dir, tex_assets, report):
    folder = tex_dir.rsplit('/', 1)[0] + '/Materials'
    mat_path = folder + '/' + mat_name
    LIB.make_directory(folder)
    # Rebuild from scratch: reusing the FBX placeholder does not stick.
    if LIB.does_asset_exist(mat_path):
        LIB.delete_asset(mat_path)
    factory = u.MaterialFactoryNew()
    mat = TOOLS.create_asset(mat_name, folder, u.Material, factory)
    if mat is None:
        report.append('%s: create-fail' % mat_name)
        return None
    two_sided = any(k in mat_name.lower() for k in ('hair', 'eyelash', 'beard', 'lash'))
    try:
        mat.set_editor_property('two_sided', two_sided)
    except Exception:
        pass

    ok = True
    if 'base' in entry and entry['base'] in tex_assets:
        s = _add_tex_sample(mat, tex_assets[entry['base']], -600, 0, u.MaterialSamplerType.SAMPLERTYPE_COLOR)
        if s is not None:
            r = MEL.connect_material_property(s, 'RGB', u.MaterialProperty.MP_BASE_COLOR)
            ok = ok and r
    if 'normal' in entry and entry['normal'] in tex_assets:
        s = _add_tex_sample(mat, tex_assets[entry['normal']], -600, -260, u.MaterialSamplerType.SAMPLERTYPE_NORMAL)
        if s is not None:
            r = MEL.connect_material_property(s, 'RGB', u.MaterialProperty.MP_NORMAL)
            ok = ok and r
    if 'mr' in entry and entry['mr'] in tex_assets:
        s = _add_tex_sample(mat, tex_assets[entry['mr']], -600, -520, u.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
        if s is not None:
            rough = MEL.create_material_expression(mat, u.MaterialExpressionComponentMask, -260, -560)
            rough.set_editor_property('g', True)
            MEL.connect_material_expressions(s, 'G', rough, '')
            MEL.connect_material_property(rough, '', u.MaterialProperty.MP_ROUGHNESS)
            metal = MEL.create_material_expression(mat, u.MaterialExpressionComponentMask, -260, -760)
            metal.set_editor_property('b', True)
            MEL.connect_material_expressions(s, 'B', metal, '')
            MEL.connect_material_property(metal, '', u.MaterialProperty.MP_METALLIC)
    else:
        c = MEL.create_material_expression(mat, u.MaterialExpressionConstant, -600, -780)
        c.set_editor_property('r', 0.0)
        MEL.connect_material_property(c, '', u.MaterialProperty.MP_METALLIC)
        c2 = MEL.create_material_expression(mat, u.MaterialExpressionConstant, -600, -900)
        c2.set_editor_property('r', 0.7)
        MEL.connect_material_property(c2, '', u.MaterialProperty.MP_ROUGHNESS)

    LIB.save_asset(mat_path)
    report.append('%s: wired=%s' % (mat_name, ok))
    return mat


def main():
    report = {}
    for name in CHARACTERS:
        plan = _material_plan(name)
        tex_dir = '/Game/Actor/%s/Textures' % name
        png_dir = _CHAR_DIR / ('Textures_' + name)
        tex_assets = {}
        for mname, entry in plan.items():
            for key in ('base', 'normal', 'mr'):
                img = entry.get(key)
                if not img or img in tex_assets:
                    continue
                png = png_dir / (img + '.png')
                tex = _load_or_import_texture(png, tex_dir, is_normal=('ormal' in img), is_color=(key != 'mr'))
                if tex is not None:
                    tex_assets[img] = tex

        mats = {}
        for mname, entry in plan.items():
            san = mname.replace(':', '_')
            rpt = []
            mat = _wire_material(san, entry, tex_dir, tex_assets, rpt)
            if mat is not None:
                mats[mname] = mat
                mats[san] = mat
            report.setdefault(name, []).extend(rpt)

        # assign to mesh slots by name
        mesh_path = '/Game/Actor/%s/SkeletalMeshes/%s.%s' % (name, name, name)
        mesh = u.load_asset(mesh_path)
        if mesh is None:
            report.setdefault(name, []).append('mesh-missing')
            continue
        slots = mesh.get_editor_property('materials')
        assigned = []
        for i, sm in enumerate(slots):
            slot = str(sm.get_editor_property('material_slot_name'))
            mat = mats.get(slot) or mats.get(slot.casefold()) or mats.get(slot.replace(':', '_'))
            if mat is None:
                for k in mats:
                    if slot and (slot in k or k in slot):
                        mat = mats[k]
                        break
            if mat is not None:
                try:
                    sm.set_editor_property('material_interface', mat)
                    slots[i] = sm
                    assigned.append('%s->%s' % (slot, mat.get_name()))
                except Exception as e:
                    assigned.append('%s:ERR %s' % (slot, e))
            else:
                assigned.append('%s:NO_MAT' % slot)
        mesh.set_editor_property('materials', slots)
        LIB.save_loaded_asset(mesh, only_if_is_dirty=False)
        report.setdefault(name, []).append('slots=%s' % assigned)

    Path('V:/BORN 2 FLAP/_wire_avatar_report.json').write_text(json.dumps(report, indent=2))
    print('WIRE_AVATAR_TEXTURES_DONE')


if __name__ == '__main__':
    try:
        main()
    finally:
        u.SystemLibrary.execute_console_command(None, 'quit')
