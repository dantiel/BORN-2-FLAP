"""Build the kestrel wing paint texture from the Inkscape SVG.

Source of truth:  C:/Users/d/Desktop/kestrelwing.svg
  * "plumage"           — the membrane raster (embedded PNG)
  * "wingmembrane shape" — the vector wing outline (the whole membrane surface)
  * three carbon spars  — drawn for reference; the diagonal spar feeds Structure.hs

The mesh (Born2FlapWingMesh, Design 2) now uses the "wingmembrane shape" outline
directly as its geometry, and maps its UVs straight into the raw "plumage" raster
via the raster's own placement in the SVG (no whitespace trimming, no warping), so
the artwork lands exactly where Inkscape put it — 1:1 on the membrane surface.

Run:  python Tools/build_kestrel_wing_textures.py
Output: Unreal/Born2Flap/Tools/out/kestrel-wing-top.png  (raw raster, RGBA)
"""
from __future__ import annotations
import base64
import io
import os
import re
import xml.etree.ElementTree as ET

from PIL import Image

SVG = r"C:\Users\d\Desktop\kestrelwing.svg"
OUT_DIR = os.path.join(os.path.dirname(__file__), "out")


def parse_svg(path: str):
    data = open(path, "rb").read()
    text = data.decode("utf-8")
    root = ET.fromstring(text)
    INK = "{http://www.inkscape.org/namespaces/inkscape}"
    XLINK = "{http://www.w3.org/1999/xlink}"

    img = None
    imx = imy = imw = imh = 0.0
    outline = None
    for e in root.iter():
        if e.tag.endswith("image"):
            href = e.get(XLINK + "href")
            png = base64.b64decode(href.split(",", 1)[1])
            img = Image.open(io.BytesIO(png)).convert("RGBA")
            imx, imy = float(e.get("x")), float(e.get("y"))
            imw, imh = float(e.get("width")), float(e.get("height"))
        if e.get(INK + "label") == "wingmembrane shape":
            d = e.get("d")
            toks = re.findall(r"[A-Za-z]|-?[0-9.]+", d)
            pts, cx, cy, subx, suby, cmd = [], 0.0, 0.0, 0.0, 0.0, None
            i = 0
            while i < len(toks):
                t = toks[i]
                if t.isalpha():
                    cmd = t
                    i += 1
                    if cmd in "Mm":
                        x, i = float(toks[i]), i + 1
                        y, i = float(toks[i]), i + 1
                        cx = x if cmd == "M" else cx + x
                        cy = y if cmd == "M" else cy + y
                        subx, suby = cx, cy
                        pts.append((cx, cy))
                        cmd = "l" if cmd == "m" else "L"
                    continue
                x, i = float(toks[i]), i + 1
                y, i = float(toks[i]), i + 1
                if cmd in ("L", "l"):
                    cx = x if cmd == "L" else cx + x
                    cy = y if cmd == "L" else cy + y
                    pts.append((cx, cy))
                elif cmd in ("Z", "z"):
                    cx, cy = subx, suby
                    pts.append((cx, cy))
            outline = pts
    return img, (imx, imy, imw, imh), outline


def build(out_path: str):
    img, (imx, imy, imw, imh), outline = parse_svg(SVG)
    print(f"membrane raster: {img.size}, placement x,y,w,h = "
          f"{imx:.2f},{imy:.2f},{imw:.2f},{imh:.2f}")
    print(f"membrane outline: {len(outline)} points, "
          f"x[{min(p[0] for p in outline):.2f}..{max(p[0] for p in outline):.2f}] "
          f"y[{min(p[1] for p in outline):.2f}..{max(p[1] for p in outline):.2f}]")

    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    img.save(out_path)
    print(f"WROTE {out_path}  ({img.size})")


if __name__ == "__main__":
    build(os.path.join(OUT_DIR, "kestrel-wing-top.png"))
