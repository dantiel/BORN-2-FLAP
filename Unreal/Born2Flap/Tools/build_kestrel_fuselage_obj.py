"""Smooth and orient the kestrel loft for UE; numpy required.

Input: metres, +X nose to tail, rounded belly +Z (upside down).
Output: centimetres, +X aft, +Z dorsal, smooth normals and seam-safe UVs.
The runtime yaw of 180 points the nose forward. Paint has nose at image
bottom, back at U=0/1, belly at U=.5; UE inverts OBJ V during import.
"""
from pathlib import Path
import argparse
import numpy as np

DEFAULT_SOURCE = Path('C:/Users/d/Downloads/kestrel_fuselage_unreal_final/kestrel_fuselage_textured.obj')
DEFAULT_OUTPUT = Path(__file__).with_name('out') / 'kestrel_fuselage_cm.obj'


def smooth(values, sigma):
    radius = int(4 * sigma)
    kernel = np.exp(-0.5 * (np.arange(-radius, radius + 1) / sigma) ** 2)
    kernel /= kernel.sum()
    return np.convolve(np.pad(values, radius, mode='edge'), kernel, mode='valid')


def build(source=DEFAULT_SOURCE, output=DEFAULT_OUTPUT):
    vertices, texcoords, faces = [], [], []
    for line in source.read_text(encoding='utf-8').splitlines():
        fields = line.split()
        if not fields:
            continue
        if fields[0] == 'v':
            vertices.append([float(x) for x in fields[1:4]])
        elif fields[0] == 'vt':
            texcoords.append([float(x) for x in fields[1:3]])
        elif fields[0] == 'f':
            assert len(fields) == 4, 'Expected triangulated source'
            faces.append([(int(x.split('/')[0])-1, int(x.split('/')[1])-1) for x in fields[1:]])
    vertices = np.array(vertices)
    texcoords = np.array(texcoords)
    stations = np.unique(vertices[:, 0])
    assert len(stations) > 100 and np.isclose(stations[-1], 1.0)
    rings = [np.flatnonzero(vertices[:, 0] == x) for x in stations]
    top = np.array([vertices[r, 2].max() for r in rings])
    bottom = np.array([vertices[r, 2].min() for r in rings])
    old_width = np.array([np.abs(vertices[r, 1]).max() for r in rings])
    # Continuous shoulder-to-tail taper instead of a flat barrel with steps.
    anchors = [0, .04, .10, .18, .28, .40, .52, .65, .78, .90, .97, 1]
    widths = [.008, .034, .062, .069, .071, .077, .075, .064, .047, .026, .012, .002]
    width = smooth(np.interp(stations, anchors, widths), 3.0)
    width[0], width[-1] = widths[0], widths[-1]
    new_top, new_bottom = smooth(top, 1.5), smooth(bottom, 1.5)
    new_top[:4], new_bottom[:4] = top[:4], bottom[:4]
    new_top[-1], new_bottom[-1] = top[-1], bottom[-1]
    tail_center = (top[-1] + bottom[-1]) * .5
    for i, ring in enumerate(rings):
        center, radius = (top[i] + bottom[i]) * .5, (top[i] - bottom[i]) * .5
        new_center = (new_top[i] + new_bottom[i]) * .5
        new_radius = (new_top[i] - new_bottom[i]) * .5
        vertices[ring, 1] *= width[i] / max(old_width[i], 1e-8)
        z = new_center + (vertices[ring, 2] - center) * new_radius / max(radius, 1e-8)
        # Belly down; blend the tail center to the existing Z=0 fan.
        vertices[ring, 2] = -z + tail_center * stations[i] ** 3
    vertices *= 100.0
    # Reflection changes handedness. Share normals even at the UV seam.
    faces = [list(reversed(face)) for face in faces]
    triangles = np.array([[v for v, uv in f] for f in faces])
    a, b, c = (vertices[triangles[:, i]] for i in range(3))
    face_normals = np.cross(b-a, c-a)
    normals = np.zeros_like(vertices)
    for i in range(3):
        np.add.at(normals, triangles[:, i], face_normals)
    lengths = np.linalg.norm(normals, axis=1)
    assert np.all(lengths > 1e-10), 'Degenerate vertex normal'
    normals /= lengths[:, None]
    # Back uppermost and head at the nose after Unreal flips OBJ V.
    texcoords[:, 0] = (texcoords[:, 0] + .25) % 1.0
    texcoords[:, 1] = 1.0 - texcoords[:, 1]
    lines = ['# Kestrel: centimetres, +X aft, +Z dorsal; explicit smooth normals']
    lines += ['v %.8f %.8f %.8f' % tuple(v) for v in vertices]
    lines += ['vn %.8f %.8f %.8f' % tuple(n) for n in normals]
    uv_lines, face_lines = [], []
    for face in faces:
        uv = texcoords[[t for v, t in face]].copy()
        if np.ptp(uv[:, 0]) > .5:
            uv[uv[:, 0] < .5, 0] += 1.0
        base = len(uv_lines) + 1
        uv_lines += ['vt %.8f %.8f' % tuple(p) for p in uv]
        face_lines.append('f ' + ' '.join(f'{v+1}/{base+i}/{v+1}' for i, (v, t) in enumerate(face)))
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text('\n'.join(lines + uv_lines + face_lines) + '\n', encoding='utf-8')
    assert np.all(np.isfinite(vertices)) and np.all(np.isfinite(normals))
    assert abs(vertices[rings[-1], 2].mean()) < .01, 'Tail attachment is off center'
    mid = vertices[rings[len(rings)//2], 2]
    assert abs(mid.min()) > mid.max(), 'Body is upside down'
    print(f'FALCON_MESH_PASS {len(vertices)} vertices / {len(faces)} triangles: {output}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=DEFAULT_SOURCE)
    parser.add_argument('--output', type=Path, default=DEFAULT_OUTPUT)
    args = parser.parse_args()
    build(args.source, args.output)
