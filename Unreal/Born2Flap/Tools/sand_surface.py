"""Bright authored sand with chaotic height-derived shading and bounded parallax.

Retains the original 300 cm colour texture scale and adds irregular scuffs,
shallow hollows, and close grain. This is shader relief, not mesh displacement.
"""

from shore_antitiling import HLSL

# Aperiodic world-space undulation field. `height` returns ~[-1, 1]; callers
# scale to centimetres. Shared verbatim by colour, normal and roughness so all
# depth cues stay locked to the same surface.
_RELIEF = r'''
struct SandRelief {
    float hash(float2 p) {
        float3 q = frac(float3(p.xyx) * float3(0.1031, 0.1030, 0.0973));
        q += dot(q, q.yzx + 33.33);
        return frac((q.x + q.y) * q.z);
    }
    float noise(float2 p) {
        float2 i = floor(p), f = frac(p);
        f = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
        return lerp(lerp(hash(i), hash(i + float2(1.0, 0.0)), f.x),
                    lerp(hash(i + float2(0.0, 1.0)), hash(i + float2(1.0, 1.0)), f.x), f.y)
               * 2.0 - 1.0;
    }
    // Warped, differently rotated scales: scuffs and hollows without a
    // preferred ridge direction. The field stays static in world space.
    float height(float2 p) {
        float2 warp=float2(noise(p/105),noise(p/93+17))*24;
        float2 q=p+warp;
        float2 r=float2(q.x*.798-q.y*.602,q.x*.602+q.y*.798);
        return (.55*noise(q/11)+.85*noise(r/32)+.65*noise(p/145)
               +.35*noise(p/280+31))/2.4;
    }
    float2 displaced(float2 p, float3 view, float amplitude) {
        float2 ray=view.xy/max(abs(view.z),.25);
        float2 q=p;
        // Bounded fixed-point parallax; colour, normals and roughness share it.
        [unroll] for(int i=0;i<3;i++) {
            float2 offset=-height(q)*amplitude*ray;
            q=p+clamp(offset,float2(-8,-8),float2(8,8));
        }
        return q;
    }
};
SandRelief relief;
'''


def surface(m, diffuse, normal, roughness, node, custom, wet=False,
            macro_cm=300.0, parallax_cm=6.0, ao_strength=0.035,
            relief_strength=0.6, normal_strength=0.65,
            grain_cm=65.0, grain_strength=0.18, brightness=1.12):
    """Emit colour / normal / roughness expressions for a seamless sand strand.

    `macro_cm` is the authored texture scale (kept at 300 cm so the approved
    "beautiful" sand look is retained); `parallax_cm` is the peak undulation
    height in cm. Wet sand dampens relief, grain, AO and roughness.
    """
    p = node(m, 'WorldPosition')
    n = node(m, 'VertexNormalWS')
    cam = node(m, 'CameraVectorWS')

    def tex(t):
        return node(m, 'TextureObject', texture=t)

    # Wet sand is smoother: less grain/relief/AO, and a tighter roughness band.
    nstr = normal_strength * (0.55 if wet else 1.0)
    rstr = relief_strength * (0.60 if wet else 1.0)
    gstr = grain_strength * (0.50 if wet else 1.0)
    astr = ao_strength * (0.50 if wet else 1.0)
    bstr = 1.0 if wet else brightness

    # --- Colour: macro albedo + parallax + symmetric relief shading ---------- #
    colour = custom(m, HLSL + _RELIEF + '''
float2 q=relief.displaced(P.xy,C,RELAMP*.2);
float h=relief.height(q);
float3 c = shore.sampleSurface(T, TSampler, q / MACROCM, false);
// Symmetric relief shading: crests brighten and troughs shade by the same
// amount, so the strand keeps its light sunlit tone instead of reading dark.
// BRIGHT is a small overall lift restoring the bright pre-rework look.
float shade = 1.0 + AOSTR * h;
return c * BRIGHT * shade;
'''.replace('RELAMP', repr(parallax_cm))
    .replace('MACROCM', repr(macro_cm))
    .replace('AOSTR', repr(astr))
    .replace('BRIGHT', repr(bstr)),
                    {'P': p, 'C': cam, 'T': tex(diffuse)}, 3)

    # --- Normal: authored sand + undulation gradient + fine grain ------------ #
    normal_code = (HLSL + _RELIEF + '''
float footprint = max(length(ddx(P)), length(ddy(P)));
float grainFade = 1.0 - smoothstep(GRAIN_F0, GRAIN_F1, footprint);
float2 q=relief.displaced(P.xy,C,RELAMP*.2);
// Authored sand normal (macro scale) — its XY/tilt is the primary ripple detail.
float3 tn = shore.sampleSurface(T, TSampler, q / MACROCM, true);
float tz=sqrt(saturate(1-dot(tn.xy,tn.xy)));
float2 texSlope=tn.xy/max(tz,.45)*.8;
// Broad undulation slope from the height-field gradient (true cm/cm). A rising
// surface tilts the normal back, hence the minus sign.
float e = 0.3;
float2 grad = float2(relief.height(q + float2(e, 0.0)) - relief.height(q - float2(e, 0.0)),
                     relief.height(q + float2(0.0, e)) - relief.height(q - float2(0.0, e)));
float2 undu = -grad/(2*e) * RELSTR * RELAMP;
// Fine isotropic grain (crisp close-up detail), faded with distance to stop aliasing.
float2 grain=shore.sampleSurface(T,TSampler,q/GRAINCM,true).xy*GRAINSTR;
float2 slope = texSlope + undu + grain * grainFade;
float3 detail = float3(slope, 0.0);
return normalize(N + (detail - N * dot(detail, N)) * NORMSTR);
'''.replace('RELAMP', repr(parallax_cm))
    .replace('MACROCM', repr(macro_cm))
    .replace('RELSTR', repr(rstr))
    .replace('NORMSTR', repr(nstr))
    .replace('GRAINCM', repr(grain_cm))
    .replace('GRAINSTR', repr(gstr))
    .replace('GRAIN_F0', repr(.3))
    .replace('GRAIN_F1', repr(2.0)))
    detail = custom(m, normal_code, {'P': p, 'N': n, 'C': cam, 'T': tex(normal)}, 3)

    # --- Roughness: sampled band, crests slightly looser --------------------- #
    if roughness:
        rough = custom(m, HLSL + _RELIEF + '''
float2 q=relief.displaced(P.xy,C,RELAMP*.2);
float h=relief.height(q);
float r = shore.sampleSurface(T, TSampler, q / MACROCM, false).r;
r += ROUGHREL * h;
return clamp(r, RMIN, RMAX);
'''.replace('RELAMP', repr(parallax_cm))
            .replace('MACROCM', repr(macro_cm))
        .replace('ROUGHREL', repr(0.10 if not wet else 0.0))
        .replace('RMIN', repr(0.28 if wet else 0.72))
        .replace('RMAX', repr(0.50 if wet else 0.92)),
                       {'P': p, 'C': cam, 'T': tex(roughness)}, 1)
    else:
        rough = node(m, 'Constant', r=0.35 if wet else 0.82)

    return colour, detail, rough