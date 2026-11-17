"""Build PBR materials from the GLB texture sets and assign them to the
FBX-imported avatar meshes.

Textures were pre-extracted by Tools/extract_glb_textures.py (normal maps
already flipped to DirectX convention). This script imports them as
Texture2D, creates one Material per GLB material (BaseColor/Normal/
Roughness/Metallic wiring) and assigns materials to the mesh slots by name.

Run (editor closed, headless commandlet):
    UnrealEditor-Cmd Born2Flap.uproject -ExecutePythonScript="V:/BORN 2 FLAP/Unreal/Born2Flap/Tools/import_avatar_materials.py" -unattended -nop4 -nosplash -stdout
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
    """Map GLB material name -> texture image names per channel."""
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


def _import_texture(png, dest_dir, is_normal, is_color):
    if not png.exists():
        u.log_warning('import_avatar_materials: missing %s' % png)
        return None
    name = png.stem
    expected = dest_dir + '/' + name
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
    task.destination_path = dest_dir
    task.destination_name = name
    task.automated = True
    task.replace_existing = True
    task.save = True
    task.factory = factory
    TOOLS.import_asset_tasks([task])
    tex = u.load_asset(expected)
    return tex


def _add_tex_sample(mat, tex, x, y, sampler_type):
    expr = MEL.create_material_expression(mat, u.MaterialExpressionTextureSample, x, y)
    expr.set_editor_property('texture', tex)
    try:
        expr.set_editor_property('sampler_type', sampler_type)
    except Exception:
        pass
    return expr


def _build_material(mat_name, entry, tex_dir, tex_assets):
    folder = tex_dir.rsplit('/', 1)[0] + '/Materials'
    mat_path = folder + '/' + mat_name
    if LIB.does_asset_exist(mat_path):
        existing = u.load_asset(mat_path)
        if existing is not None and isinstance(existing, u.Material):
            return existing
    LIB.make_directory(folder)
    factory = u.MaterialFactoryNew()
    mat = TOOLS.create_asset(mat_name, folder, u.Material, factory)
    if mat is None:
        u.log_error('import_avatar_materials: create_asset failed for %s' % mat_name)
        return None
    two_sided = any(k in mat_name.lower() for k in ('hair', 'eyelash', 'beard', 'lash'))
    try:
        mat.set_editor_property('two_sided', two_sided)
    except Exception:
        pass

    if 'base' in entry and entry['base'] in tex_assets:
        s = _add_tex_sample(mat, tex_assets[entry['base']], -600, 0, u.MaterialSamplerType.SAMPLERTYPE_COLOR)
        MEL.connect_material_property(s, 'RGB', u.MaterialProperty.MP_BASE_COLOR)
    if 'normal' in entry and entry['normal'] in tex_assets:
        s = _add_tex_sample(mat, tex_assets[entry['normal']], -600, -260, u.MaterialSamplerType.SAMPLERTYPE_NORMAL)
        MEL.connect_material_property(s, 'RGB', u.MaterialProperty.MP_NORMAL)
    if 'mr' in entry and entry['mr'] in tex_assets:
        s = _add_tex_sample(mat, tex_assets[entry['mr']], -600, -520, u.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
        rough = MEL.create_material_expression(mat, u.MaterialExpressionComponentMask, -260, -560)
        rough.set_editor_property('g', True)
        try:
            MEL.connect_material_expressions(s, 'G', rough, '')
        except Exception:
            pass
        MEL.connect_material_property(rough, '', u.MaterialProperty.MP_ROUGHNESS)
        metal = MEL.create_material_expression(mat, u.MaterialExpressionComponentMask, -260, -760)
        metal.set_editor_property('b', True)
        try:
            MEL.connect_material_expressions(s, 'B', metal, '')
        except Exception:
            pass
        MEL.connect_material_property(metal, '', u.MaterialProperty.MP_METALLIC)
    else:
        # no MR texture: matte constants (skin/cloth look)
        c = MEL.create_material_expression(mat, u.MaterialExpressionConstant, -600, -780)
        c.set_editor_property('r', 0.0)
        MEL.connect_material_property(c, '', u.MaterialProperty.MP_METALLIC)
        c2 = MEL.create_material_expression(mat, u.MaterialExpressionConstant, -600, -900)
        c2.set_editor_property('r', 0.7)
        MEL.connect_material_property(c2, '', u.MaterialProperty.MP_ROUGHNESS)

    LIB.save_asset(mat_path)
    return mat


def main():
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
                tex = _import_texture(png, tex_dir, is_normal=('ormal' in img), is_color=(key != 'mr'))
                if tex is not None:
                    tex_assets[img] = tex
                    print('TEX_%s_%s=%s' % (name, img, tex.get_path_name()))

        # build materials
        materials = {}
        for mname, entry in plan.items():
            mat = _build_material(mname, entry, tex_dir, tex_assets)
            if mat is not None:
                materials[mname] = mat
                print('MAT_%s_%s=%s' % (name, mname, mat.get_path_name()))

        # assign to mesh slots by name
        mesh_path = '/Game/Actor/%s/SkeletalMeshes/%s.%s' % (name, name, name)
        mesh = u.load_asset(mesh_path)
        if mesh is None:
            u.log_error('import_avatar_materials: mesh missing %s' % mesh_path)
            continue
        mats = mesh.get_editor_property('materials')
        assigned = []
        for i, sm in enumerate(mats):
            slot = str(sm.get_editor_property('material_slot_name'))
            mat = materials.get(slot) or materials.get(slot.casefold())
            if mat is None and i < len(materials):
                mat = list(materials.values())[i]  # order fallback
            if mat is not None:
                try:
                    sm.set_editor_property('material_interface', mat)
                    assigned.append('%s->%s' % (slot, mat.get_name()))
                except Exception as e:
                    assigned.append('%s:ERR %s' % (slot, e))
            else:
                assigned.append('%s:NO_MAT' % slot)
        mesh.set_editor_property('materials', mats)
        LIB.save_asset(mesh_path)
        print('SLOTS_%s=%s' % (name, assigned))
    print('MATERIALS_DONE')


if __name__ == '__main__':
    main()