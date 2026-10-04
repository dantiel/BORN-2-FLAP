"""Falcon texture templates (SVG) — exact UV-space outlines to paint in.

Source of truth: the procedural mesh generators in
  Source/Born2Flap/Flight/Born2FlapWingMesh.cpp   (membrane wing, Design 2)
  Source/Born2Flap/Flight/Born2FlapRavenCrow.cpp  (body + tail shard mesh)

Each surface owns its own texture. The SVGs reproduce the *exact* UV
coordinates the mesh samples, so anything painted inside an outline lands
on that surface. Body uses a side-view planar projection (X->U, Z->V);
tail uses a top-view projection (X->U, Y->V); wing uses its existing
aeroelastic mapping. Left/right halves mirror automatically (wing/tail)
or overlap (body sides).

Run:  python Tools/create_falcon_texture_templates.py
"""
from __future__ import annotations
import math
import os

OUT_DIR = os.path.join(os.path.dirname(__file__), "..", "Content", "Birds", "Templates")
SIZE = 1024  # UV 0..1 -> 0..SIZE px; texture can be any power-of-two.
TAN12 = math.tan(math.radians(12.0))


def _p(u: float, v: float) -> tuple[float, float]:
    """UV (0..1, bottom-left origin) -> SVG px (top-left origin)."""
    return (u * SIZE, (1.0 - v) * SIZE)


def _poly(pts: list[tuple[float, float]]) -> str:
    return " ".join(f"{x:.1f},{y:.1f}" for x, y in pts)


def _tri(t: tuple, fill: str, stroke: str, op: float = 1.0, width: float = 1.0) -> str:
    return (f'<polygon points="{_poly([_p(u, v) for u, v in t])}" fill="{fill}" '
            f'fill-opacity="{op}" stroke="{stroke}" stroke-width="{width}"/>')


def _anchor(label: str, u: float, v: float, dx: float, dy: float) -> str:
    x, y = _p(u, v)
    return (f'<line x1="{x:.1f}" y1="{y:.1f}" x2="{x+dx:.1f}" y2="{y+dy:.1f}" stroke="#8899aa" stroke-width="1.2"/>'
            f'<text x="{x+dx:.1f}" y="{y+dy:.1f}" dx="{4 if dx >= 0 else -4}" dy="{-4 if dy <= 0 else 4}" '
            f'font-family="monospace" font-size="14" fill="#ccd6e0" text-anchor="{"start" if dx >= 0 else "end"}">{label}</text>')


def _doc(title: str, subtitle: str, inner: str) -> str:
    return "\n".join([
        f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {SIZE} {SIZE}" width="{SIZE}" height="{SIZE}">',
        '<rect width="100%" height="100%" fill="#10161d"/>',
        f'<rect x="1" y="1" width="{SIZE-2}" height="{SIZE-2}" fill="none" stroke="#445" stroke-width="1.5" stroke-dasharray="8 6"/>',
        f'<text x="12" y="{SIZE-12}" font-family="monospace" font-size="13" fill="#667">texture UV (0,0) bottom-left / (1,1) top-right</text>',
        f'<text x="12" y="24" font-family="monospace" font-size="15" fill="#8899aa">{title}</text>',
        f'<text x="12" y="43" font-family="monospace" font-size="12" fill="#667">{subtitle}</text>',
        inner,
        '</svg>',
    ])


# ---------------------------------------------------------------------------
# Wing (membrane, Design 2) — existing aeroelastic UV, both wings share it.
# ---------------------------------------------------------------------------
def wing_outline(n: int = 96) -> list[tuple[float, float]]:
    pts: list[tuple[float, float]] = []
    for i in range(n + 1):
        v = i / n
        x_le = 17.0 - 14.0 * v - 10.0 * v ** 3
        pts.append(((30.0 - x_le) / 95.0, (100.0 * v) / 105.0))
    for i in range(n, -1, -1):
        v = i / n
        x_te = 17.0 - 14.0 * v - 10.0 * v ** 3 - (47.0 - 36.0 * v ** 3)
        pts.append(((30.0 - x_te) / 95.0, (100.0 * v) / 105.0))
    return pts


def _path(pts: list[tuple[float, float]]) -> str:
    d = [f"M {pts[0][0]:.2f} {pts[0][1]:.2f}"]
    d += [f"L {u:.2f} {v:.2f}" for u, v in pts[1:]]
    d.append("Z")
    return " ".join(d)


def write_wing_svg() -> str:
    pts = wing_outline()
    path = _path([_p(u, v) for u, v in pts])
    us = [p[0] for p in pts]; vs = [p[1] for p in pts]
    inner = "\n".join([
        f'<path d="{path}" fill="#0e3a40" fill-opacity="0.55" stroke="#38e0c8" stroke-width="2.5"/>',
        _anchor("LEADING EDGE", *wing_outline(2)[0], -150, 0),
        _anchor("TRAILING EDGE", *wing_outline(2)[-1], 150, 0),
        _anchor("ROOT", (13.0/95.0 + 60.0/95.0)/2, 0.0, 0, 60),
        _anchor("TIP", (37.0/95.0 + 48.0/95.0)/2, 100.0/105.0, 0, -60),
    ])
    return _doc("COMMON KESTREL — membrane wing (Design 2)",
                f"UV bounds: U [{min(us):.3f}..{max(us):.3f}]  V [{min(vs):.3f}..{max(vs):.3f}]  (both wings share this UV)",
                inner)


# ---------------------------------------------------------------------------
# Body — folded shard fuselage, side-view planar projection (X->U, Z->V).
# ---------------------------------------------------------------------------
def _fold(A, B, D, E, h):
    r = ((A[0] + B[0] + D[0] + E[0]) * .25, (A[1] + B[1] + D[1] + E[1]) * .25,
         (A[2] + B[2] + D[2] + E[2]) * .25 + h)
    return [(A, B, r), (B, D, r), (D, E, r), (E, A, r)]


def _body_uv(x, z):
    return ((x + 63.0) / 133.0, (z + 12.0) / 33.0)


def body_triangles() -> list[tuple]:
    tris: list[tuple] = []
    tris += _fold((55, 0, 7), (12, 14, 1), (-63, 0, -3), (12, -14, 1), 8)
    tris += _fold((40, 0, 5), (8, -10, -2), (-50, 0, -12), (8, 10, -2), -8)
    tris += _fold((54, 0, 15), (34, 11, 13), (17, 0, 18), (34, -11, 13), 6)
    tris += [((70, 0, 9), (47, 7, 12), (49, 0, 17)),
             ((70, 0, 9), (49, 0, 17), (47, -7, 12)),
             ((70, 0, 9), (47, -7, 12), (47, 7, 12))]
    for side in (-1, 1):
        for i in range(5):
            x = 16 - i * 10
            y = side * (5 + i * .65)
            tris += _fold((x + 12, y * .65, 13 - i * 2), (x + 1, y + side * 4, 7 - i),
                          (x - 29, y * .7, 2 - i), (x - 2, y * .25, 10 - i), 2)
        tris.append(((43, side * 8.8, 16), (37, side * 10.4, 17), (40, side * 10.0, 14)))
    return [tuple(_body_uv(x, z) for x, y, z in t) for t in tris]


def write_body_svg() -> str:
    tris = body_triangles()
    us = [p[0] for t in tris for p in t]
    vs = [p[1] for t in tris for p in t]
    fill = "".join(_tri(t, "#233d4a", "#3f6f7d", 0.28, 0.6) for t in tris)
    edge = "".join(_tri(t, "none", "#6fc0cf", 0, 1.1) for t in tris)
    inner = "\n".join([
        fill, edge,
        _anchor("NOSE / BEAK", 1.0, 0.5, -120, 0),
        _anchor("TAIL ROOT", 0.0, 0.5, 120, 0),
        _anchor("BACK", 0.5, 1.0, 0, -60),
        _anchor("BELLY", 0.5, 0.0, 0, 60),
    ])
    return _doc("COMMON KESTREL — body (side view, X->U / Z->V)",
                f"UV bounds: U [{min(us):.3f}..{max(us):.3f}]  V [{min(vs):.3f}..{max(vs):.3f}]  (left/right sides overlap)",
                inner)


# ---------------------------------------------------------------------------
# Tail — fanned delta, top-view projection (X->U, Y->V), left + right stacked.
# ---------------------------------------------------------------------------
def _tail_uv(x, y):
    return ((x + 112.0) / 51.0, (y + 22.0) / 44.0)


def tail_triangles() -> list[tuple]:
    tris: list[tuple] = []
    for side in (-1, 1):
        def tp(x, y, side=side):
            return (x, side * y, 1 - y * TAN12)
        tris.append((tp(-61, 0), tp(-112, 22), tp(-112, 0)))
        tris.append((tp(-64, 0), tp(-107, 17), tp(-107, 0)))
    return [tuple(_tail_uv(x, y) for x, y, z in t) for t in tris]


def write_tail_svg() -> str:
    tris = tail_triangles()
    us = [p[0] for t in tris for p in t]
    vs = [p[1] for t in tris for p in t]
    fill = "".join(_tri(t, "#3a2f1f", "#8a6a3a", 0.5, 0.6) for t in tris)
    edge = "".join(_tri(t, "none", "#d0a060", 0, 1.2) for t in tris)
    inner = "\n".join([
        fill, edge,
        _anchor("TIP", 0.0, 0.5, 120, 0),
        _anchor("ROOT (body)", 1.0, 0.5, -120, 0),
        _anchor("RIGHT FAN", 0.25, 1.0, 0, -50),
        _anchor("LEFT FAN", 0.25, 0.0, 0, 50),
    ])
    return _doc("COMMON KESTREL — tail (top view, X->U / Y->V)",
                f"UV bounds: U [{min(us):.3f}..{max(us):.3f}]  V [{min(vs):.3f}..{max(vs):.3f}]  (left fan bottom half, right fan top half)",
                inner)


def main() -> None:
    os.makedirs(OUT_DIR, exist_ok=True)
    for name, fn in (("FalconWing", write_wing_svg), ("FalconBody", write_body_svg), ("FalconTail", write_tail_svg)):
        path = os.path.join(OUT_DIR, f"{name}.svg")
        with open(path, "w", encoding="utf-8") as f:
            f.write(fn())
        print(f"WROTE {path}")


if __name__ == "__main__":
    main()