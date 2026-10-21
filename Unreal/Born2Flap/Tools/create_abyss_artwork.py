"""Generate the Neon Abyss menu artwork (Abyss.png).

Paints the level-select card for the void world: a deep aether gradient,
soft nebulae, a verdigris-and-gold bronze ring, and scattered festival
lights (the neon jellyfish read). Mirrors the hand-authored Art/UI/Elements
cards (Coast/Valley/Islands) so the new UI shows a bespoke preview.

Run (offline, no Unreal):
    python Unreal/Born2Flap/Tools/create_abyss_artwork.py
Then import:
    UnrealEditor-Cmd.exe ... -run=pythonscript -script=Tools/import_abyss_artwork.py
"""
from __future__ import annotations

import math
import random
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

W, H = 512, 256
random.seed(2026)


def _lerp(a: float, b: float, t: float) -> float:
    return a + (b - a) * t


def _rgba(rgb: tuple, a: int) -> tuple:
    return rgb[0], rgb[1], rgb[2], a


def build() -> Image.Image:
    # 1. Aether gradient: deep indigo -> violet.
    img = Image.new("RGBA", (W, H))
    draw = ImageDraw.Draw(img)
    top = (4, 3, 18)
    bot = (28, 13, 58)
    for y in range(H):
        t = y / (H - 1)
        col = tuple(int(_lerp(top[i], bot[i], t)) for i in range(3))
        draw.line([(0, y), (W, y)], fill=_rgba(col, 255))

    # 2. Soft nebulae.
    nebulae = [
        (128, 78, 170, 62, (122, 47, 158)),
        (392, 96, 190, 66, (42, 157, 214)),
        (270, 44, 150, 60, (31, 127, 122)),
        (452, 168, 128, 48, (96, 40, 150)),
    ]
    glow = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    gd = ImageDraw.Draw(glow)
    for cx, cy, rx, ry, col in nebulae:
        gd.ellipse([cx - rx, cy - ry, cx + rx, cy + ry], fill=_rgba(col, 78))
    glow = glow.filter(ImageFilter.GaussianBlur(34))
    img = Image.alpha_composite(img, glow)
    draw = ImageDraw.Draw(img)

    # 3. Star / dust field.
    for _ in range(110):
        x = random.randrange(W)
        y = random.randrange(H)
        s = random.choice([(255, 255, 255), (130, 235, 255), (255, 212, 128)])
        a = random.randint(50, 210)
        r = 1 if random.random() < 0.8 else 2
        draw.ellipse([x - r, y - r, x + r, y + r], fill=_rgba(s, a))

    # 4. Bronze ring (verdigris band + gold seams) — the circle of ancient bronzes.
    cx, cy = int(W * 0.60), int(H * 0.50)
    rx, ry = 158, 96
    ring = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    rd = ImageDraw.Draw(ring)
    rd.ellipse([cx - rx, cy - ry, cx + rx, cy + ry], outline=_rgba((28, 82, 70), 255), width=16)
    rd.ellipse([cx - rx, cy - ry, cx + rx, cy + ry], outline=_rgba((244, 188, 60), 255), width=2)
    rd.ellipse([cx - rx + 16, cy - ry + 16, cx + rx - 16, cy + ry - 16],
               outline=_rgba((244, 188, 60), 190), width=1)
    ring = ring.filter(ImageFilter.GaussianBlur(0.6))
    img = Image.alpha_composite(img, ring)
    draw = ImageDraw.Draw(img)

    # 5. Festival lights (jellyfish glow) with a soft halo.
    lights = [
        (92, 150, (62, 226, 255)),
        (150, 186, (255, 90, 208)),
        (222, 140, (255, 216, 96)),
        (312, 192, (62, 226, 255)),
        (410, 152, (255, 90, 208)),
        (470, 200, (255, 216, 96)),
    ]
    halo = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    hd = ImageDraw.Draw(halo)
    for x, y, col in lights:
        hd.ellipse([x - 5, y - 5, x + 5, y + 5], fill=_rgba(col, 235))
    halo = halo.filter(ImageFilter.GaussianBlur(7))
    img = Image.alpha_composite(img, halo)
    draw = ImageDraw.Draw(img)
    for x, y, col in lights:
        draw.ellipse([x - 2, y - 2, x + 2, y + 2], fill=_rgba(col, 255))

    return img


def main() -> None:
    out = Path(__file__).resolve().parents[3] / "Art" / "UI" / "Elements" / "Abyss.png"
    out.parent.mkdir(parents=True, exist_ok=True)
    build().save(out)
    print(f"WROTE {out}")


if __name__ == "__main__":
    main()
