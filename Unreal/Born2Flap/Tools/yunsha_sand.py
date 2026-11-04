"""Dry aeolian sand for Yunsha: wind-aligned ripples + local dune variation.

The previous material sampled the beach normal map at fine scale (reading as
organic "leaves/branches") and evaluated a domain-warped fbm several times per
pixel, which is why the desert both looked wrong and cost a lot of shader time.
This rewrite:

  * keeps the stochastic antitiling sampler (it kills tiling well) but uses it
    only for albedo, and at two well-separated scales;
  * derives the ripple + broad undulation from ONE cheap coherent height field
    whose wind direction follows the exact same seasonal wind-mix as
    ABorn2FlapYunsha::GroundHeight, so ripples lie along the dune slip faces;
  * drops the 26cm ripple (which moired into twigs at flight distance) for a
    clean ~3m ripple and a gentle 30cm procedural grain on the normal;
  * adds per-dune-face brightness/warmth variation so the floor is never a
    uniform wash.

Custom-node contract: `HLSL` (ShoreSampling + `shore`), `RELIEF` (SandRelief +
`relief`, DesertRelief + `desert`), `DRY_COLOUR` (defines `float3 dry`), and
`normal_code()` (world-space normal). All are consumed by yunsha_oasis and
yunsha_excavation.
"""
from shore_antitiling import HLSL

# Lighter relief than the Shiomori beach module: gradient noise + a 3-octave
# fbm without the expensive per-pixel domain warp, plus the desert wind field.
RELIEF = r'''
struct SandRelief {
    float2 hash22(float2 p) {
        float3 p3 = frac(float3(p.xyx) * float3(0.1031, 0.1030, 0.0973));
        p3 += dot(p3, p3.yzx + 33.33);
        return frac((p3.xx + p3.yz) * p3.zy);
    }
    float gnoise(float2 p) {
        float2 i = floor(p);
        float2 f = frac(p);
        float2 u = f * f * (3.0 - 2.0 * f);
        float a = dot(hash22(i) * 2.0 - 1.0, f);
        float b = dot(hash22(i + float2(1.0, 0.0)) * 2.0 - 1.0, f - float2(1.0, 0.0));
        float c = dot(hash22(i + float2(0.0, 1.0)) * 2.0 - 1.0, f - float2(0.0, 1.0));
        float d = dot(hash22(i + float2(1.0, 1.0)) * 2.0 - 1.0, f - float2(1.0, 1.0));
        return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y) * 1.71;
    }
    float fbm(float2 p) {
        return gnoise(p) * 0.55 + gnoise(p * 2.2 + 5.2) * 0.30 + gnoise(p * 4.6 + 2.7) * 0.15;
    }
};
SandRelief relief;

struct DesertRelief {
    // The same seasonal wind-mix as GroundHeight: two transverse-wind regimes
    // blended by a slow sine, so ripple crests rotate with the dune field.
    float2 wind(float2 p) {
        float m = smoothstep(-0.6, 0.6, sin(p.x * 0.000013 - p.y * 0.000009));
        return normalize(lerp(float2(0.85, 0.53), float2(-0.46, 0.89), m));
    }
    // ONE coherent height field: wind-aligned ripple + broad regional
    // undulation. Ripples are domain-warped only once (aperiodic, no moire).
    float height(float2 p) {
        float2 w = wind(p);
        float along = dot(p, w);
        float across = dot(p, float2(-w.y, w.x));
        float warp = 2.3 * relief.gnoise(p * 0.0009);
        float amp = 0.62 + 0.38 * relief.gnoise(float2(across * 0.0007, along * 0.0007));
        return sin(along * 0.021 + warp * 6.2831853) * amp * 0.6
             + relief.fbm(p * 0.000045) * 0.85;
    }
};
DesertRelief desert;
'''

# Albedo: stochastically-antitiled sand at two scales + per-dune-face variation.
DRY_COLOUR = r'''
float2 ray = C.xy / max(abs(C.z), 0.3);
float2 sandUV = P.xy - clamp(desert.height(P.xy) * ray, float2(-5, -5), float2(5, 5));
float3 sdFine  = shore.sampleSurface(S, SSampler, sandUV / 360.0, false);
float3 sdBroad = shore.sampleSurface(S, SSampler, sandUV / 2200.0, false);
float3 sdTex = lerp(sdFine, sdBroad, 0.32);
float sdVar = relief.fbm(sandUV * 0.00009) + 0.5;
float3 dry = sdTex * lerp(float3(0.90, 0.72, 0.52), float3(1.06, 0.86, 0.62), sdVar);
dry *= 0.93 + 0.07 * saturate(desert.height(sandUV) + 0.6);
'''


def normal_code():
    return HLSL + RELIEF + r'''
// Analytic ripple slope (no 4x height sampling) + a gentle procedural grain.
float2 w = desert.wind(P.xy);
float along = dot(P.xy, w);
float across = dot(P.xy, float2(-w.y, w.x));
float warp = 2.3 * relief.gnoise(P.xy * 0.0009);
float amp = 0.62 + 0.38 * relief.gnoise(float2(across * 0.0007, along * 0.0007));
float slope = 0.021 * cos(along * 0.021 + warp * 6.2831853) * amp;
float2 grain = float2(relief.gnoise(P.xy / 45.0), relief.gnoise(P.xy / 45.0 + 7.3));
float3 d = float3(-w * slope * 2.0 + grain * 0.10, 0.0);
return normalize(N + d - N * dot(N, d));
'''