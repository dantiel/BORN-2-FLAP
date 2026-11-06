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

A single world-height wetness (1 below the water surface, 0 above it) is shared
verbatim by the dry beach, the wet seabed and the headland, so the dry->wet
hand-off has no hard material seam: wet sand darkens with a warm tint, glosses
and shimmers with aperiodic caustics, all fading in smoothly across the swash
zone.
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


def surface(m, diffuse, normal, roughness, node, custom, mpc=None,
            macro_cm=300.0, parallax_cm=6.5, ao_strength=0.35,
            relief_strength=0.60, normal_strength=0.65,
            wet_tint=(0.74, 0.68, 0.56), wet_rough_scale=0.50,
            wet_dry=-20.0, wet_wet=5.0, wet_warp=12.0,
            caustic_cm=16.0, caustic_strength=0.10, foam_strength=0.18,
            fine_cm=85.0, fine_blend=0.30,
            deep_tint=(0.28, 0.38, 0.36), absorb_cm=170.0,
            macro_scale=0.00032, macro_strength=0.12,
            ripple_scale=0.032, ripple_strength=0.35):
    """Emit colour / normal / roughness expressions for a seamless sand strand.

    `macro_cm` is the authored texture scale (kept at 300 cm so the approved
    "beautiful" light sand look is retained); `parallax_cm` is the peak height
    in cm driving the parallax offset (broad undulation + fine grain, so the
    trodden grain displaces with real depth); `ao_strength` scales a subtle
    cavity shadow in the troughs.

    A single world-height wetness (1 below the water surface, 0 above it) is
    shared verbatim by the dry beach, the wet seabed and the headland, so the
    dry->wet hand-off has no hard material seam: wet sand darkens with
    `wet_tint`, glosses by `wet_rough_scale` and shimmers with aperiodic
    caustics, all fading in smoothly across the swash zone. The grain itself is
    NOT dampened underwater, so the submerged sand keeps the same fine quality
    as the dry strand.
    """
    p = node(m, 'WorldPosition')
    n = node(m, 'VertexNormalWS')
    cam = node(m, 'CameraVectorWS')

    def tex(t):
        return node(m, 'TextureObject', texture=t)

    # Wetness is the displaced water surface height minus the sand height, so
    # the wet/dry hand-off follows the Gerstner waves (advancing/receding with
    # the surf) instead of a fixed world-Z band. A domain-warped swash edge
    # makes the receding water leave irregular wet tongues, not a ruler line.
    if mpc is not None:
        from gerstner_hlsl import HEIGHT_INLINE, SHORE_EDGE
        def _mpc(name):
            return node(m, 'CollectionParameter', collection=mpc, parameter_name=name)
        wetness = custom(m, _RELIEF + SHORE_EDGE + '''
    float shore=smoothstep(100.0,3000.0,P.y-edge);
''' + HEIGHT_INLINE + '''
    float waveZ=H*shore;
    float depth=(OceanLevel+waveZ)-P.z;
    float2 wq=P.xy*0.02;
    float warp=(relief.gnoise(wq+float2(3.1,1.7)) + relief.gnoise(wq*2.3-float2(1.1,4.3))*0.5) * WETWARP;
    return float2(smoothstep(WETDRY, WETWET, depth+warp), depth);'''
            .replace('WETWARP', repr(wet_warp))
            .replace('WETDRY', repr(wet_dry))
            .replace('WETWET', repr(wet_wet)),
            {'P': p, 'Amp': _mpc('SwellAmplitude'), 'WL': _mpc('SwellLength'),
             'CS': _mpc('SwellSpeed'), 'Sea': _mpc('SeaState'),
             'Dir': _mpc('PrimaryWaveDirection'), 'WT': _mpc('WaveTime'),
             'OceanLevel': _mpc('OceanLevel')}, 2)
    else:
        wetness = custom(m, HLSL + 'return float2(1.0 - smoothstep(-70.0, -5.0, P.z), 0.0);', {'P': p}, 2)

    # --- Colour: authored albedo + cavity shadow + wet tint + caustics ------- #
    colour = custom(m, HLSL + _RELIEF + '''
float2 q = relief.displaced(P.xy, C, RELAMP);
float3 col = shore.sampleSurface(T, TSampler, q / MACROCM, false);
// Second octave: the same authored sand at a finer scale, blended in so the
// grain has two frequencies instead of one repeating tile.
float3 fine = shore.sampleSurface(T, TSampler, q / FINECM, false);
col = lerp(col, fine, FINEBLEND);
col *= lerp(1.0, 0.82, smoothstep(0.0, 0.55, -relief.cavity(q) * AOSTR));
// Wet sand tint at the swash (warm), then underwater depth absorption cools and
// darkens toward the deep seabed — a smooth dry -> wet -> submerged gradient.
col = lerp(col, col * WETTINT, W.x);
float absorb = 1.0 - exp(-max(W.y, 0.0) / ABSORBCM);
col = lerp(col, col * DEEPTINT, absorb);
// Macro tonal variation: large-scale domain-warped gradient noise breaks the
// flat repeating tile across the wide seabed (soft light/dark patches).
float2 mq = P.xy * MACROSC;
float2 mw = float2(relief.gnoise(mq + 3.7), relief.gnoise(mq * 1.7 + 9.2)) * 2.0;
float mac = relief.gnoise(mq * 2.1 + mw) + relief.gnoise(mq * 4.3 - mw * 1.3 + 5.5);
col *= 1.0 + MACROSTR * mac;
// Aperiodic caustics: domain-warped gradient noise (not a sine lattice) so the
// submerged sand shimmers without repeating stripes. Fades out when dry (W.x=0).
float2 cq = P.xy / CACCM;
float t = TIME * 0.30;
float2 cw = float2(relief.gnoise(cq * 0.55 + t), relief.gnoise(cq * 0.55 - t * 0.85 + 7.0));
float c = relief.gnoise(cq * 1.3 + cw * 2.2 + t * 0.5)
        + relief.gnoise(cq * 2.9 - cw * 1.4 - t * 0.35);
col *= 1.0 + W.x * CACSTR * c;
// Receding foam line: a bright film where the wave has just retreated, at the
// mid-wetness inflection of the swash edge. Since W.x is wave-height driven, the
// line advances/recedes with the surf instead of sitting at a fixed height.
col = lerp(col, col + float3(0.10, 0.12, 0.13), FOAMSTR * W.x * (1.0 - W.x) * 4.0);
return col;
'''.replace('RELAMP', repr(parallax_cm))
    .replace('MACROCM', repr(macro_cm))
    .replace('FINECM', repr(fine_cm))
    .replace('FINEBLEND', repr(fine_blend))
    .replace('AOSTR', repr(ao_strength))
    .replace('WETTINT', 'float3(%g, %g, %g)' % wet_tint)
    .replace('DEEPTINT', 'float3(%g, %g, %g)' % deep_tint)
    .replace('ABSORBCM', repr(absorb_cm))
    .replace('MACROSC', repr(macro_scale))
    .replace('MACROSTR', repr(macro_strength))
    .replace('CACCM', repr(caustic_cm))
    .replace('CACSTR', repr(caustic_strength))
    .replace('FOAMSTR', repr(foam_strength)),
                    {'P': p, 'C': cam, 'T': tex(diffuse), 'W': wetness,
                     'TIME': node(m, 'Time')}, 3)

    # --- Normal: authored sand + footprint relief + height slope ------------ #
    normal_code = (HLSL + _RELIEF + '''
float2 q = relief.displaced(P.xy, C, RELAMP);
float3 tn = shore.sampleSurface(T, TSampler, q / MACROCM, true);
float tz = sqrt(saturate(1.0 - dot(tn.xy, tn.xy)));
float2 base = tn.xy / max(tz, 0.45);
float2 fp = relief.footprint(q, RELSTR);
float e = 1.0;
float2 undu = float2(relief.height(q + float2(e, 0.0)) - relief.height(q - float2(e, 0.0)),
                     relief.height(q + float2(0.0, e)) - relief.height(q - float2(0.0, e)));
undu = -undu / (2.0 * e) * RELAMP * 0.35;
// Underwater sand ripples: shore-parallel ridges that grow with submersion,
// giving the sea floor the structured, trodden look of the dry strand.
float sub = smoothstep(0.0, 60.0, W.y);
float2 rq = P.xy * RIPPLESC;
float rw = relief.gnoise(rq * 0.22 + 4.0) * 1.4;
float2 rgrad = float2(relief.gnoise(rq * 0.22) * 0.15, cos(rq.y + rw)) * RIPPLESTR * sub;
float2 slope = base + fp + undu + rgrad;
float3 detail = float3(slope, 0.0);
return normalize(N + (detail - N * dot(detail, N)) * NORMSTR);
'''.replace('RELAMP', repr(parallax_cm))
    .replace('MACROCM', repr(macro_cm))
    .replace('RELSTR', repr(relief_strength))
    .replace('NORMSTR', repr(normal_strength))
    .replace('RIPPLESC', repr(ripple_scale))
    .replace('RIPPLESTR', repr(ripple_strength)))
    detail = custom(m, normal_code, {'P': p, 'N': n, 'C': cam, 'T': tex(normal), 'W': wetness}, 3)

    # --- Roughness: sampled band; wet sand glosses --------------------------- #
    if roughness:
        rough = custom(m, HLSL + _RELIEF + '''
float2 q = relief.displaced(P.xy, C, RELAMP);
float h = relief.height(q);
float r = shore.sampleSurface(T, TSampler, q / MACROCM, false).r;
r += ROUGHREL * h;
r = clamp(r, RMIN, RMAX);
return lerp(r, r * WETRSCALE, W.x);
'''.replace('RELAMP', repr(parallax_cm))
            .replace('MACROCM', repr(macro_cm))
            .replace('ROUGHREL', repr(0.06))
            .replace('RMIN', repr(0.72))
            .replace('RMAX', repr(0.92))
            .replace('WETRSCALE', repr(wet_rough_scale)),
                       {'P': p, 'C': cam, 'T': tex(roughness), 'W': wetness}, 1)
    else:
        rough = custom(m, 'return lerp(DRY, WET, W);',
                       {'DRY': node(m, 'Constant', r=0.82),
                        'WET': node(m, 'Constant', r=0.38),
                        'W': wetness}, 1)

    return colour, detail, rough