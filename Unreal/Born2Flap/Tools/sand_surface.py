"""Authored sand with the proven pre-rework look restored, plus the missing
"Plastik" (3D relief).

The pre-rework strand (commit f919457) was almost white with a direct
gradient-noise footprint/wind relief on the normal -- clean, light, and
organic. The value-noise intermediate (hash + quintic lerp) read as blocky,
axis-aligned "compression artefacts" with a coarse, repetitive grain; gradient
(Perlin) noise interpolates random gradients instead of scalar cell values, so
it is continuous, lattice-free and yields fine high-quality detail. This module
keeps the clean albedo + direct footprint relief, adds a domain warp to kill
tiling, and adds the missing depth: a single coherent height field (broad dune
undulation + fine trodden grain) that drives parallax, normal slope and a subtle
cavity shadow -- so the grain is raised, not just painted on.
"""

from shore_antitiling import HLSL

_RELIEF = r'''
struct SandRelief {
    float2 hash22(float2 p) {
        float3 p3 = frac(float3(p.xyx) * float3(0.1031, 0.1030, 0.0973));
        p3 += dot(p3, p3.yzx + 33.33);
        return frac((p3.xx + p3.yz) * p3.zy);
    }
    // Gradient (Perlin) noise. Interpolates random gradients, not random cell
    // values, so it has no axis-aligned lattice, no blocky cell look and a
    // continuous derivative -- the clean fine grain value noise lacks.
    float gnoise(float2 p) {
        float2 i = floor(p);
        float2 f = frac(p);
        float2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
        float a00 = dot(hash22(i) * 2.0 - 1.0, f);
        float a10 = dot(hash22(i + float2(1.0, 0.0)) * 2.0 - 1.0, f - float2(1.0, 0.0));
        float a01 = dot(hash22(i + float2(0.0, 1.0)) * 2.0 - 1.0, f - float2(0.0, 1.0));
        float a11 = dot(hash22(i + float2(1.0, 1.0)) * 2.0 - 1.0, f - float2(1.0, 1.0));
        return lerp(lerp(a00, a10, u.x), lerp(a01, a11, u.x), u.y) * 1.71;
    }
    // Fractal gradient noise with a domain warp. The warp folds the lattice
    // over itself so the tiling pattern is destroyed (no visible repetition);
    // the higher octaves add progressively finer grain.
    float fbm(float2 p) {
        float2 w = float2(gnoise(p), gnoise(p + 5.2)) * 1.5;
        return gnoise(p + w) * 0.52
             + gnoise(p * 2.2 + w * 1.3) * 0.32
             + gnoise(p * 4.6 + w * 1.7 + 2.7) * 0.16;
    }
    // Coherent height field: broad dune undulation + the fine trodden grain.
    // Parallax, normal slope and (subtle) cavity shadow all derive from this
    // ONE field, so every bump that shades as raised also displaces -- real
    // depth, not painted-on relief.
    float height(float2 p) {
        float broad = 0.62 * gnoise(p / 48.0) + 0.38 * gnoise(p / 115.0 + 17.0);
        float fine  = fbm(p * 0.06);
        return broad + fine * 0.6;
    }
    // Concavity of the height field: negative in troughs, positive on crests.
    // Cheap 4-tap probe feeding the subtle cavity shadow so troughs recede
    // while crests stay light (real depth without a global darkening).
    float cavity(float2 p) {
        float c = height(p);
        float n = height(p + float2(1.5, 0.0)) + height(p - float2(1.5, 0.0))
                + height(p + float2(0.0, 1.5)) + height(p - float2(0.0, 1.5));
        return c - n * 0.25;
    }
    // Footprint relief: finer fractal gradient noise, decorrelated in X/Y,
    // plus a shore-elongated wind streak. Applied straight to the normal so
    // it reads as trodden sand, never lattice lines.
    float2 footprint(float2 p, float strength) {
        float nx = fbm(p * 0.06);
        float ny = fbm(p * 0.10 + 3.7);
        float w  = fbm(float2(p.x * 0.002, p.y * 0.013));
        return float2(nx * strength + w * 0.2, ny * strength + w * 0.55);
    }
    float2 displaced(float2 p, float3 view, float amplitude) {
        float2 ray = view.xy / max(abs(view.z), .25);
        float2 q = p;
        [unroll] for (int i = 0; i < 4; i++) {
            q = p + clamp(-height(q) * amplitude * ray, float2(-10, -10), float2(10, 10));
        }
        return q;
    }
};
SandRelief relief;
'''


def surface(m, diffuse, normal, roughness, node, custom, wet=False,
            macro_cm=300.0, parallax_cm=6.5, ao_strength=0.35,
            relief_strength=0.60, normal_strength=0.65,
            grain_cm=55.0, grain_strength=0.0, brightness=1.0):
    """Emit colour / normal / roughness expressions for a seamless sand strand.

    `macro_cm` is the authored texture scale (kept at 300 cm so the approved
    "beautiful" light sand look is retained); `parallax_cm` is the peak height
    in cm driving the parallax offset (broad undulation + fine grain, so the
    trodden grain displaces with real depth); `ao_strength` scales a subtle
    cavity shadow in the troughs. Wet sand dampens relief and tightens the
    roughness band.
    """
    p = node(m, 'WorldPosition')
    n = node(m, 'VertexNormalWS')
    cam = node(m, 'CameraVectorWS')

    def tex(t):
        return node(m, 'TextureObject', texture=t)

    nstr = normal_strength * (0.55 if wet else 1.0)
    rstr = relief_strength * (0.60 if wet else 1.0)

    # --- Colour: authored albedo + subtle cavity shadow ---------------------- #
    # Parallax-offset sample, then a gentle height-based cavity shadow so the
    # troughs recede while crests stay light. No global AO and no brightness
    # lift, so the strand keeps its light sunlit tone -- depth without dirt.
    colour = custom(m, HLSL + _RELIEF + '''
float2 q = relief.displaced(P.xy, C, RELAMP);
float3 col = shore.sampleSurface(T, TSampler, q / MACROCM, false);
col *= lerp(1.0, 0.82, smoothstep(0.0, 0.55, -relief.cavity(q) * AOSTR));
return col;
'''.replace('RELAMP', repr(parallax_cm))
    .replace('MACROCM', repr(macro_cm))
    .replace('AOSTR', repr(ao_strength)),
                    {'P': p, 'C': cam, 'T': tex(diffuse)}, 3)

    # --- Normal: authored sand + direct footprint/wind relief ---------------- #
    # The proven pre-rework look: noise perturbs the normal directly (organic
    # trodden bumps), plus the slope of the height field (broad + fine grain)
    # so the shading matches the displaced depth.
    normal_code = (HLSL + _RELIEF + '''
float2 q = relief.displaced(P.xy, C, RELAMP);
float3 tn = shore.sampleSurface(T, TSampler, q / MACROCM, true);
float tz = sqrt(saturate(1.0 - dot(tn.xy, tn.xy)));
float2 base = tn.xy / max(tz, 0.45);
float2 fp = relief.footprint(q, RELSTR);
// Gentle height-field slope (cm/cm); small so it never bands.
float e = 1.0;
float2 undu = float2(relief.height(q + float2(e, 0.0)) - relief.height(q - float2(e, 0.0)),
                     relief.height(q + float2(0.0, e)) - relief.height(q - float2(0.0, e)));
undu = -undu / (2.0 * e) * RELAMP * 0.35;
float2 slope = base + fp + undu;
float3 detail = float3(slope, 0.0);
return normalize(N + (detail - N * dot(detail, N)) * NORMSTR);
'''.replace('RELAMP', repr(parallax_cm))
    .replace('MACROCM', repr(macro_cm))
    .replace('RELSTR', repr(rstr))
    .replace('NORMSTR', repr(nstr)))
    detail = custom(m, normal_code, {'P': p, 'N': n, 'C': cam, 'T': tex(normal)}, 3)

    # --- Roughness: sampled band; crests loosened a touch -------------------- #
    if roughness:
        rough = custom(m, HLSL + _RELIEF + '''
float2 q = relief.displaced(P.xy, C, RELAMP);
float h = relief.height(q);
float r = shore.sampleSurface(T, TSampler, q / MACROCM, false).r;
r += ROUGHREL * h;
return clamp(r, RMIN, RMAX);
'''.replace('RELAMP', repr(parallax_cm))
            .replace('MACROCM', repr(macro_cm))
            .replace('ROUGHREL', repr(0.06 if not wet else 0.0))
            .replace('RMIN', repr(0.28 if wet else 0.72))
            .replace('RMAX', repr(0.50 if wet else 0.92)),
                       {'P': p, 'C': cam, 'T': tex(roughness)}, 1)
    else:
        rough = node(m, 'Constant', r=0.35 if wet else 0.82)

    return colour, detail, rough