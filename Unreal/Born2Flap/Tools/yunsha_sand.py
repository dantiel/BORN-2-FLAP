"""Clean sand colour and simulated 5?20cm relief driven by wind/shelter UV1.
The offline solver lives in YunshaRippleSolver.h. Fine ripples are surface
normal relief; the native GroundHeight supplies resolved mesh undulation.
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
    // `rel` is passed in (not captured) because DXC/SM6 forbids a struct method
    // from referencing a variable declared in the enclosing CustomExpression.
    float height(float2 p, SandRelief rel) {
        float2 w = wind(p);
        float along = dot(p, w);
        float across = dot(p, float2(-w.y, w.x));
        float warp = 2.3 * rel.gnoise(p * 0.0009);
        float amp = 0.62 + 0.38 * rel.gnoise(float2(across * 0.0007, along * 0.0007));
        return sin(along * 0.021 + warp * 6.2831853) * amp * 0.6
             + rel.fbm(p * 0.000045) * 0.85;
    }
};
DesertRelief desert;
'''

# Albedo: stochastically-antitiled sand at two scales + per-dune-face variation.
DRY_COLOUR = r'''
// Clean quartz sand: low contrast mineral variation, no organic source textures.
float mineral=relief.gnoise(P.xy/95.0);
float sandBroad=relief.gnoise(P.xy/16000.0);
float3 dry=float3(0.76,0.59,0.36)*(1.0+0.025*mineral+0.025*sandBroad);
'''


def normal_code():
    return RELIEF + r'''
float2 q=float2(Flow.x*100.0,dot(P.xy,float2(-0.5735764,0.8191520)));
float2 uv0=q/Widths.x,uv1=q/Widths.y,uv2=q/Widths.z;
float2 g0=(Texture2DSample(R0,R0Sampler,uv0).rg*2.0-1.0)*2.0;
float2 g1=(Texture2DSample(R1,R1Sampler,uv1).rg*2.0-1.0)*2.0;
float2 g2=(Texture2DSample(R2,R2Sampler,uv2).rg*2.0-1.0)*2.0;
float region=saturate(0.5+1.4*relief.gnoise(P.xy/1300.0));
float2 grad=lerp(lerp(g0,g1,smoothstep(0.0,0.5,region)),g2,smoothstep(0.5,1.0,region));
// Texture mips filter the simulated slopes at distance, without an abrupt
// arbitrary fade that erased visible ripples in open ground.
float transport=saturate(Flow.y);
grad*=transport;
float3 px=ddx(P),py=ddy(P);
float3 rx=cross(py,N),ry=cross(N,px);
float det=dot(px,rx);
float safeDet=(det<0?-1.0:1.0)*max(abs(det),1e-8);
float hx=dot(grad,ddx(q)),hy=dot(grad,ddy(q));
return normalize(N-(rx*hx+ry*hy)/safeDet);
'''


_RIPPLE_ASSETS=None

def normal_inputs(mat,expr):
    global _RIPPLE_ASSETS
    import unreal as u
    from pathlib import Path
    import json, hashlib
    import yunsha_aeolian
    if _RIPPLE_ASSETS is None:
        folder=Path(__file__).resolve().parents[1]/'Saved/Aeolian'
        signature=hashlib.sha256(Path(yunsha_aeolian.__file__).read_bytes()).hexdigest()
        cache=folder/'source.sha256'
        if not cache.exists() or cache.read_text()!=signature or not all((folder/('Aeolian%d.png'%i)).exists() for i in range(3)):
            report=yunsha_aeolian.generate(folder)
            cache.write_text(signature)
        else:
            report=json.loads((folder/'report.json').read_text())
        textures=[]
        for i in range(3):
            task=u.AssetImportTask();task.filename=(folder/('Aeolian%d.png'%i)).as_posix()
            task.destination_path='/Game/Yunsha/Materials';task.destination_name='T_Aeolian%d'%i
            task.automated=True;task.replace_existing=True;task.save=True
            u.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
            texture=u.load_asset('/Game/Yunsha/Materials/T_Aeolian%d'%i);assert texture
            texture.set_editor_property('srgb',False)
            texture.set_editor_property('compression_settings',u.TextureCompressionSettings.TC_VECTOR_DISPLACEMENTMAP)
            u.EditorAssetLibrary.save_loaded_asset(texture)
            textures.append(texture)
        _RIPPLE_ASSETS=(textures,[v['width_cm'] for v in report])
        u.log('YUNSHA_AEOLIAN_PATCHES '+json.dumps(report))
    textures,widths=_RIPPLE_ASSETS
    return {'P':expr(mat,'WorldPosition'),'N':expr(mat,'VertexNormalWS'),
            'Flow':expr(mat,'TextureCoordinate',coordinate_index=1),
            'Widths':expr(mat,'Constant3Vector',constant=u.LinearColor(*widths,1.0)),
            **{'R%d'%i:expr(mat,'TextureObject',texture=t) for i,t in enumerate(textures)}}
