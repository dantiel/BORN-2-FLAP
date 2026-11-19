"""Extract embedded textures from the Mixamo character GLBs to PNG files.

The character_refined_glb exports carry the real diffuse/glossiness/normal
maps (the FBX files reference them by broken absolute Linux paths, so UE's
FBX import produces textureless meshes). Normal maps are OpenGL (+Y G);
flip G so UE (DirectX convention) shades correctly.
"""
import base64
import io
import json
import struct
import sys
from pathlib import Path

from PIL import Image

DIR = Path(r'V:/BORN 2 FLAP/Unreal/Born2Flap/SourceAssets/Mixamo/Characters')
NAMES = ['SK_Amy', 'SK_Michelle', 'SK_Sophie', 'SK_Kate', 'SK_James', 'SK_Adam', 'SK_Brian', 'SK_Alex',
         'SK_Heraklios', 'SK_Alien', 'SK_Ninja', 'SK_Knight',
         'SK_Pumpkinhulk', 'SK_Whiteclown', 'SK_Goblin',
         'SK_Demon', 'SK_GasMask', 'SK_XBot']


def read_glb(path):
    b = path.read_bytes()
    total = struct.unpack_from('<I', b, 8)[0]
    off = 12
    json_bytes = bin_data = None
    while off + 8 <= total:
        clen, ctype = struct.unpack_from('<II', b, off)
        if ctype == 0x4E4F534A:
            json_bytes = b[off + 8:off + 8 + clen]
        elif ctype == 0x004E4942:
            bin_data = b[off + 8:off + 8 + clen]
        off += 8 + clen
    return json.loads(json_bytes), bin_data


def image_bytes(j, bin_data, img):
    if 'uri' in img and img['uri'].startswith('data:'):
        return base64.b64decode(img['uri'].split(',', 1)[1])
    if 'bufferView' in img:
        bv = j['bufferViews'][img['bufferView']]
        start = bv.get('byteOffset', 0)
        return bin_data[start:start + bv['byteLength']]
    return None


for name in NAMES:
    src = DIR / (name + '.glb')
    if not src.exists():
        print(name, 'MISSING GLB')
        continue
    j, bin_data = read_glb(src)
    outdir = DIR / ('Textures_' + name)
    outdir.mkdir(exist_ok=True)
    saved = []
    for idx, img in enumerate(j.get('images', [])):
        raw = image_bytes(j, bin_data, img)
        if raw is None:
            print(name, 'img%d no data' % idx)
            continue
        im = Image.open(io.BytesIO(raw))
        im = im.convert('RGBA')
        imname = img.get('name') or ('tex_%02d' % idx)
        if 'ormal' in imname:  # Normal -> flip G (OpenGL +Y to DirectX -Y)
            r, g, b, a = im.split()
            g = g.point(lambda v: 255 - v)
            im = Image.merge('RGBA', (r, g, b, a))
        out = outdir / (imname + '.png')
        im.save(out)
        saved.append(out.name)
    print(name, saved)
print('TEXTURE_EXTRACT_DONE')