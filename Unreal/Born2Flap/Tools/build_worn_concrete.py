"""Generate a seamless "simple fine but worn concrete" PBR set for Shiomori's
tidewalk stairs.

The existing concrete_floor_02 (Poly Haven) reads busy and repeats obviously
over the 900 m run. This synthesizes a cleaner, finer, low-contrast concrete
with soft large-scale wear mottling, and tiles perfectly so the world-space
triplanar projection (see concrete_material in create_shiomori_map.py) wraps
both horizontal treads and vertical risers without visible repetition.

Outputs Diffuse.jpg / nor_dx.jpg / Rough.jpg into
Saved/ShiomoriSource/concrete_worn/.
"""
import numpy as np
from PIL import Image
from pathlib import Path

SIZE = 2048
OUT = Path(__file__).resolve().parents[1] / 'Saved' / 'ShiomoriSource' / 'concrete_worn'


def tileable_noise(size, base, rng, octaves=4, persistence=0.55):
    """Tileable value noise (0..1), built from a wrapped lattice at `base`
    resolution, octave-upsampled with bilinear interpolation. Integer wrap on
    the lattice makes every octave perfectly periodic."""
    out = np.zeros((size, size), dtype=np.float32)
    amp = 1.0
    total = 0.0
    freq = base
    for _ in range(octaves):
        lattice = rng.random((freq, freq)).astype(np.float32)
        x = np.linspace(0, freq, size, endpoint=False)
        y = np.linspace(0, freq, size, endpoint=False)
        xi = np.floor(x).astype(np.int64)
        yi = np.floor(y).astype(np.int64)
        xf = (x - xi).astype(np.float32)
        yf = (y - yi).astype(np.float32)
        xi1 = (xi + 1) % freq
        yi1 = (yi + 1) % freq
        g00 = lattice[np.ix_(yi, xi)]
        g10 = lattice[np.ix_(yi, xi1)]
        g01 = lattice[np.ix_(yi1, xi)]
        g11 = lattice[np.ix_(yi1, xi1)]
        top = g00 * (1.0 - xf)[None, :] + g10 * xf[None, :]
        bot = g01 * (1.0 - xf)[None, :] + g11 * xf[None, :]
        val = top * (1.0 - yf)[:, None] + bot * yf[:, None]
        out += amp * val
        total += amp
        amp *= persistence
        freq *= 2
    return out / total


def height_field(rng):
    """A concrete relief: fine aggregate grain over soft, worn undulation."""
    fine = tileable_noise(SIZE, 96, rng, octaves=3, persistence=0.6)
    mid = tileable_noise(SIZE, 24, rng, octaves=4, persistence=0.55)
    low = tileable_noise(SIZE, 5, rng, octaves=5, persistence=0.5)
    h = 0.55 * fine + 0.30 * mid + 0.15 * low
    h = (h - h.mean()) / (h.std() + 1e-6)
    return h


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    rng = np.random.default_rng(1337)

    h = height_field(rng)

    # --- Albedo: light neutral-warm concrete, fine grain + soft wear mottling ---
    grain = tileable_noise(SIZE, 96, rng, octaves=3, persistence=0.6)
    wear = tileable_noise(SIZE, 6, rng, octaves=4, persistence=0.55)
    stain = tileable_noise(SIZE, 4, rng, octaves=5, persistence=0.5)

    # Subtle tonal drift around a warm light grey (R slightly > B, concrete-like).
    mottle = 0.88 + 0.16 * wear          # 0.88 .. 1.04 large patches
    fine = 1.0 + 0.10 * (grain - 0.5)    # +/-5% fine grain
    worn = 1.0 - 0.06 * (stain - 0.5)    # +/-3% large wear shading

    r = np.clip(0.800 * mottle * fine * worn, 0, 1)
    g = np.clip(0.795 * mottle * fine * worn, 0, 1)
    b = np.clip(0.785 * mottle * fine * worn, 0, 1)
    albedo = np.dstack([r, g, b])
    Image.fromarray((albedo * 255.0 + 0.5).astype(np.uint8), 'RGB').save(OUT / 'Diffuse.jpg', quality=95)

    # --- Normal (OpenGL convention, G = up): gentle relief gradient ---
    strength = 0.45  # world-space; the material blends it further (fine concrete)
    hx = (np.roll(h, -1, axis=1) - np.roll(h, 1, axis=1)) / 2.0
    hy = (np.roll(h, -1, axis=0) - np.roll(h, 1, axis=0)) / 2.0
    nx = -hx * strength
    ny = -hy * strength
    nz = np.ones_like(nx)
    inv = 1.0 / np.sqrt(nx * nx + ny * ny + 1.0)
    nx, ny, nz = nx * inv, ny * inv, nz * inv
    normal = np.dstack([nx * 0.5 + 0.5, ny * 0.5 + 0.5, nz * 0.5 + 0.5])
    Image.fromarray((normal * 255.0 + 0.5).astype(np.uint8), 'RGB').save(OUT / 'nor_dx.jpg', quality=95)

    # --- Roughness: slightly rough worn concrete, smoother where worn ---
    rough_noise = tileable_noise(SIZE, 48, rng, octaves=4, persistence=0.55)
    roughness = np.clip(0.58 + 0.22 * (rough_noise - 0.5) - 0.10 * (wear - 0.5), 0.25, 0.95)
    Image.fromarray((roughness * 255.0 + 0.5).astype(np.uint8), 'L').save(OUT / 'Rough.jpg', quality=95)

    print('B2F_WORN_CONCRETE_READY', OUT)


if __name__ == '__main__':
    main()
