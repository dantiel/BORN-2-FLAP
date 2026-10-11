"""Generate the tiny procedural insect meshes + tintable materials for the swarm.

Poly Haven (the sole CC0 source for foliage) carries no insect models, so these
are authored here as minimal flat OBJ geometry and tintable materials, then
imported like the nature assets. Everything is tintable at runtime through a
single VectorParameter ("Color") so the C++ swarm needs no per-type material.

Two visual tiers, matching the C++ layering:
  • M_InsectSpeck  — plain unlit translucent tint (no texture). Used by the
    hundreds of instanced swarm silhouettes; stays cheap and clean.
  • M_InsectWing / M_InsectBody — the close-up "hero" insects. These sample
    tiny procedural PNGs (T_InsectWing = veined membrane, T_InsectBody =
    segmented lozenge) so the flapping close-ups read as real insects, and the
    wings carry a cheap fresnel+time iridescence shimmer.

Run after (or without) fetch_nature_assets.py, with UnrealEditor-Cmd:
  -run=pythonscript -script=.../create_insect_assets.py -unattended -nullrhi
"""
from pathlib import Path
import unreal as u

ROOT = Path(__file__).resolve().parents[1] / 'Saved/InsectSource'
TOOLS = u.AssetToolsHelpers.get_asset_tools()
EDIT = u.MaterialEditingLibrary
LIB = u.EditorAssetLibrary
DEST = '/Game/Nature/Insects'


# --------------------------------------------------------------------------- #
#  PNG authoring (stdlib zlib + struct, no PIL dependency)                    #
# --------------------------------------------------------------------------- #
def _write_png(path, W, H, rows):
    import zlib
    import struct

    def chunk(tag, data):
        return (struct.pack('>I', len(data)) + tag + data
                + struct.pack('>I', zlib.crc32(tag + data) & 0xffffffff))

    raw = bytearray()
    for row in rows:
        raw.append(0)  # filter type 0 (None)
        for r, g, b, a in row:
            raw += bytes((r, g, b, a))
    ihdr = struct.pack('>IIBBBBB', W, H, 8, 6, 0, 0, 0)
    data = (b'\x89PNG\r\n\x1a\n'
            + chunk(b'IHDR', ihdr)
            + chunk(b'IDAT', zlib.compress(bytes(raw), 9))
            + chunk(b'IEND', b''))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def _dist_seg(px, py, ax, ay, bx, by):
    dx = bx - ax
    dy = by - ay
    l2 = dx * dx + dy * dy
    t = 0.0 if l2 <= 0.0 else max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / l2))
    qx = ax + t * dx
    qy = ay + t * dy
    return ((px - qx) ** 2 + (py - qy) ** 2) ** 0.5


def build_wing_png():
    # A single wing: root at the left (flap pivot), tip at the right. Greyscale
    # membrane with darker radiating veins; alpha fades at the membrane edge.
    W, H = 256, 128
    cx, cy = 96.0, 64.0
    rx, ry = 118.0, 62.0
    root = (12.0, 64.0)
    tips = [(252, 14), (254, 36), (255, 64), (254, 92), (252, 114)]
    veins = [(root, t) for t in tips]
    mids = [((root[0] + t[0]) * 0.5, (root[1] + t[1]) * 0.5) for t in tips]
    cross = [(mids[i], mids[i + 1]) for i in range(len(mids) - 1)]
    segs = veins + cross
    rows = []
    for py in range(H):
        row = []
        for px in range(W):
            d = ((px - cx) / rx) ** 2 + ((py - cy) / ry) ** 2
            alpha = 0
            shade = 235
            if d < 1.0:
                a = min(1.0, (1.0 - d) / 0.09)
                alpha = int(205 * a)
                vd = min(_dist_seg(px, py, *s[0], *s[1]) for s in segs)
                if vd < 1.3:
                    shade = 95
                    alpha = max(alpha, 225)
                elif vd < 2.6:
                    shade = int(235 - (235 - 95) * ((2.6 - vd) / 1.3) * 0.55)
            row.append((shade, shade, shade, alpha))
        rows.append(row)
    _write_png(ROOT / 'T_InsectWing.png', W, H, rows)


def build_body_png():
    # A horizontal lozenge strip: dark head, mid thorax, banded abdomen. The
    # body mesh maps U along its length, so this reads head->tail left->right.
    W, H = 256, 64
    rows = []
    for py in range(H):
        vy = py / (H - 1)
        edge = abs(vy - 0.5) * 2.0
        row = []
        for px in range(W):
            uu = px / (W - 1)
            if uu < 0.16:
                v = 80          # head
            elif uu < 0.34:
                v = 140         # thorax
            else:
                v = 210         # abdomen
                if (uu - 0.34) % 0.062 < 0.012:
                    v = 120
            if edge > 0.92:
                v = min(v, 90)
            row.append((v, v, v, 255))
        rows.append(row)
    _write_png(ROOT / 'T_InsectBody.png', W, H, rows)


# --------------------------------------------------------------------------- #
#  OBJ + import helpers                                                       #
# --------------------------------------------------------------------------- #
def write_obj(name, verts, faces, uvs=None):
    lines = ['# B2F procedural insect part', 'o ' + name]
    for x, y, z in verts:
        lines.append('v %.4f %.4f %.4f' % (x, y, z))
    if uvs is None:
        uvs = [(x, y) for x, y, _ in verts]
    for uu, vv in uvs:
        lines.append('vt %.4f %.4f' % (uu, vv))
    for f in faces:
        lines.append('f ' + ' '.join('%d/%d' % (i, i) for i in f))
    path = ROOT / (name + '.obj')
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text('\n'.join(lines) + '\n', encoding='utf-8')
    return path


def quad(a, b, c, d):
    return [(a, b, c), (a, c, d)]  # two triangles (1-based indices)


def import_mesh(source, name):
    if not LIB.does_directory_exist(DEST):
        LIB.make_directory(DEST)
    task = u.AssetImportTask()
    task.filename = str(source)
    task.destination_path = DEST
    task.destination_name = name
    task.automated = True
    task.replace_existing = True
    task.save = True
    TOOLS.import_asset_tasks([task])
    for p in task.imported_object_paths:
        asset = u.load_asset(p)
        if isinstance(asset, u.StaticMesh):
            return asset
    raise RuntimeError('No StaticMesh imported for ' + name)


def import_texture(source, name):
    if not LIB.does_directory_exist(DEST):
        LIB.make_directory(DEST)
    task = u.AssetImportTask()
    task.filename = str(source)
    task.destination_path = DEST
    task.destination_name = name
    task.automated = True
    task.replace_existing = True
    task.save = True
    TOOLS.import_asset_tasks([task])
    for p in task.imported_object_paths:
        asset = u.load_asset(p)
        if isinstance(asset, u.Texture2D):
            return asset
    raise RuntimeError('No Texture2D imported for ' + name)


def node(material, kind, **props):
    result = EDIT.create_material_expression(material, getattr(u, 'MaterialExpression' + kind))
    for key, value in props.items():
        result.set_editor_property(key, value)
    return result


def connect(a, a_pin, b, b_pin):
    return EDIT.connect_material_expressions(a, a_pin, b, b_pin)


def output(n, prop, pin=''):
    EDIT.connect_material_property(n, pin, getattr(u.MaterialProperty, 'MP_' + prop))


def newmat(name):
    path = DEST + '/' + name
    if LIB.does_asset_exist(path):
        material = u.load_asset(path)
    else:
        material = TOOLS.create_asset(name, DEST, u.Material, u.MaterialFactoryNew())
    EDIT.delete_all_material_expressions(material)
    return material


def save(material):
    material.set_editor_property('used_with_instanced_static_meshes', True)
    EDIT.recompile_material(material)
    if not LIB.save_loaded_asset(material):
        raise RuntimeError('Could not save ' + material.get_path_name() + '; close the game before importing.')


def build_meshes():
    # SM_Insect_Wing — a single flat wing. Inboard edge at x=0 (flap pivot), tip
    # at x=+5, chord along Y (±1). UVs normalized 0..1 so the veined texture maps
    # root->tip cleanly. One-sided quad, rendered two-sided.
    write_obj('SM_Insect_Wing',
              [(0, -1, 0), (5, -1, 0), (5, 1, 0), (0, 1, 0)],
              [(1, 2, 3), (1, 3, 4)],
              uvs=[(0, 0), (1, 0), (1, 1), (0, 1)])
    # SM_Insect_Body — elongated lozenge along +X (forward), 4 cm long. U maps the
    # long axis (head->tail) for the segmented texture.
    body_verts = [(-2, -0.6, -0.6), (2, -0.6, -0.6), (2, 0.6, -0.6), (-2, 0.6, -0.6),
                  (-2, -0.6, 0.6), (2, -0.6, 0.6), (2, 0.6, 0.6), (-2, 0.6, 0.6)]
    body_uvs = [((x + 2.0) / 4.0, (y + 0.6) / 1.2) for x, y, z in body_verts]
    write_obj('SM_Insect_Body', body_verts,
              [(1, 3, 2), (1, 4, 3), (5, 6, 7), (5, 7, 8),   # front/back
               (1, 2, 6), (1, 6, 5), (2, 3, 7), (2, 7, 6),   # top/bottom
               (3, 4, 8), (3, 8, 7), (4, 1, 5), (4, 5, 8)],  # left/right
              uvs=body_uvs)
    # SM_Insect_Speck — 4-winged cross silhouette (dragonfly/butterfly star) for
    # the instanced swarm: four thin wings radiating from a tiny body bar.
    write_obj('SM_Insect_Speck',
              # body bar along X
              [(-1.5, -0.2, 0), (1.5, -0.2, 0), (1.5, 0.2, 0), (-1.5, 0.2, 0),
               # east wing
               (1.0, -0.7, 0), (5.5, -0.7, 0), (5.5, 0.7, 0), (1.0, 0.7, 0),
               # west wing
               (-1.0, -0.7, 0), (-5.5, -0.7, 0), (-5.5, 0.7, 0), (-1.0, 0.7, 0),
               # north wing
               (-0.7, 1.0, 0), (-0.7, 5.0, 0), (0.7, 5.0, 0), (0.7, 1.0, 0),
               # south wing
               (-0.7, -1.0, 0), (-0.7, -5.0, 0), (0.7, -5.0, 0), (0.7, -1.0, 0)],
              [(1, 2, 3), (1, 3, 4),                      # body
               (5, 6, 7), (5, 7, 8),                      # east
               (9, 10, 11), (9, 11, 12),                  # west
               (13, 14, 15), (13, 15, 16),                # north
               (17, 18, 19), (17, 19, 20)])               # south
    for name in ('SM_Insect_Wing', 'SM_Insect_Body', 'SM_Insect_Speck'):
        mesh = import_mesh(ROOT / (name + '.obj'), name)
        u.log('B2F_INSECT_MESH ' + mesh.get_path_name())


def build_textures():
    build_wing_png()
    build_body_png()
    wing_tex = import_texture(ROOT / 'T_InsectWing.png', 'T_InsectWing')
    body_tex = import_texture(ROOT / 'T_InsectBody.png', 'T_InsectBody')
    u.log('B2F_INSECT_TEXTURES T_InsectWing T_InsectBody')
    return wing_tex, body_tex


def build_materials(wing_tex, body_tex):
    # ---- M_InsectSpeck — plain swarm silhouette, no texture fetch. ----
    speck = newmat('M_InsectSpeck')
    speck.set_editor_property('blend_mode', u.BlendMode.BLEND_TRANSLUCENT)
    speck.set_editor_property('two_sided', True)
    speck.set_editor_property('shading_model', u.MaterialShadingModel.MSM_UNLIT)
    scolor = node(speck, 'VectorParameter', parameter_name='Color',
                  default_value=u.LinearColor(1.0, 1.0, 1.0, 1.0))
    sopacity = node(speck, 'ScalarParameter', parameter_name='Opacity', default_value=0.72)
    output(scolor, 'EMISSIVE_COLOR')
    output(sopacity, 'OPACITY')
    save(speck)

    # ---- M_InsectWing — veined membrane + fresnel/time iridescence. ----
    wing = newmat('M_InsectWing')
    wing.set_editor_property('blend_mode', u.BlendMode.BLEND_TRANSLUCENT)
    wing.set_editor_property('two_sided', True)
    wing.set_editor_property('shading_model', u.MaterialShadingModel.MSM_UNLIT)
    tex = node(wing, 'TextureSampleParameter2D', parameter_name='WingTexture', texture=wing_tex)
    color = node(wing, 'VectorParameter', parameter_name='Color',
                 default_value=u.LinearColor(1.0, 1.0, 1.0, 1.0))
    opacity = node(wing, 'ScalarParameter', parameter_name='Opacity', default_value=0.7)

    base = node(wing, 'Multiply')                 # tinted membrane (RGB * Color)
    connect(tex, 'RGB', base, 'A')
    connect(color, '', base, 'B')

    time = node(wing, 'Time')
    tspeed = node(wing, 'Constant', r=1.6)
    tmul = node(wing, 'Multiply')
    connect(time, '', tmul, 'A')
    connect(tspeed, '', tmul, 'B')
    osc = node(wing, 'Sine')
    connect(tmul, '', osc, '')
    osc_amp = node(wing, 'Constant', r=0.4)
    osc_scaled = node(wing, 'Multiply')
    connect(osc, '', osc_scaled, 'A')
    connect(osc_amp, '', osc_scaled, 'B')
    one = node(wing, 'Constant', r=1.0)
    shimmer = node(wing, 'Add')                   # 1 + 0.4*sin(t)  -> 0.6..1.4
    connect(one, '', shimmer, 'A')
    connect(osc_scaled, '', shimmer, 'B')
    shimmered = node(wing, 'Multiply')
    connect(base, '', shimmered, 'A')
    connect(shimmer, '', shimmered, 'B')

    fresnel = node(wing, 'Fresnel')
    sheen = node(wing, 'Constant3Vector', constant=u.LinearColor(0.55, 0.42, 1.0, 1.0))
    sheen_scale = node(wing, 'Constant', r=0.6)
    sheen_amt = node(wing, 'Multiply')
    connect(fresnel, '', sheen_amt, 'A')
    connect(sheen_scale, '', sheen_amt, 'B')
    sheen_add = node(wing, 'Multiply')            # violet sheen at grazing angles
    connect(sheen, '', sheen_add, 'A')
    connect(sheen_amt, '', sheen_add, 'B')

    emissive = node(wing, 'Add')
    connect(shimmered, '', emissive, 'A')
    connect(sheen_add, '', emissive, 'B')
    output(emissive, 'EMISSIVE_COLOR')

    opmul = node(wing, 'Multiply')                # Opacity * membrane alpha
    connect(opacity, '', opmul, 'A')
    connect(tex, 'A', opmul, 'B')
    output(opmul, 'OPACITY')
    save(wing)

    # ---- M_InsectBody — segmented lozenge, tinted. ----
    body = newmat('M_InsectBody')
    body.set_editor_property('blend_mode', u.BlendMode.BLEND_OPAQUE)
    body.set_editor_property('two_sided', False)
    body.set_editor_property('shading_model', u.MaterialShadingModel.MSM_UNLIT)
    btex = node(body, 'TextureSampleParameter2D', parameter_name='BodyTexture', texture=body_tex)
    bcolor = node(body, 'VectorParameter', parameter_name='Color',
                  default_value=u.LinearColor(1.0, 1.0, 1.0, 1.0))
    bbase = node(body, 'Multiply')
    connect(btex, 'RGB', bbase, 'A')
    connect(bcolor, '', bbase, 'B')
    output(bbase, 'EMISSIVE_COLOR')
    save(body)

    u.log('B2F_INSECT_MATERIALS M_InsectSpeck M_InsectWing M_InsectBody')


def main():
    build_meshes()
    wing_tex, body_tex = build_textures()
    build_materials(wing_tex, body_tex)
    print('B2F_INSECT_ASSETS_READY', flush=True)


if __name__ == '__main__':
    main()
