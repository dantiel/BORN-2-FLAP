"""Dry aeolian sand using Shiomori's shared stochastic and gradient sampling."""
from shore_antitiling import HLSL
from sand_surface import _RELIEF

# The same relief drives normal gradients, parallax and cavity shading. No
# shoreline wetness or underwater caustics are applied to the desert.
RELIEF = _RELIEF + r'''
struct DesertRelief {
 float height(float2 p) {
  SandRelief r;
  float blend=smoothstep(-.6,.6,sin(p.x*.000013-p.y*.000009));
  float2 wind=normalize(lerp(float2(.85,.53),float2(-.46,.89),blend));
  float warp=8*r.fbm(p/170);
  float phase=(dot(p,wind)+warp)/26;
  float resolved=saturate(1-2*fwidth(phase));
  return resolved*(.65*sin(phase*6.2831853)+.16*sin(phase*12.5663706))
       +.22*r.fbm(p*.10)*saturate(1-length(fwidth(p))*.12)+.35*r.fbm(p/250);
 }
};
DesertRelief desert;
'''

DRY_COLOUR = '''
float2 ray=C.xy/max(abs(C.z),.3);
float2 sandUV=P.xy-clamp(desert.height(P.xy)*ray,float2(-4,-4),float2(4,4));
float3 dry=lerp(shore.sampleSurface(S,SSampler,sandUV/360,false),
               shore.sampleSurface(S,SSampler,sandUV/1700,false),.32);
dry*=float3(1,.80,.54)*(.94+.06*relief.fbm(P.xy/18000));
dry*=.96+.04*saturate(desert.height(sandUV)+.6);
'''


def normal_code():
    return HLSL + RELIEF + '''
float e=1.5;
float2 slope=float2(desert.height(P.xy+float2(e,0))-desert.height(P.xy-float2(e,0)),
                    desert.height(P.xy+float2(0,e))-desert.height(P.xy-float2(0,e)))/(2*e);
float3 grain=shore.sampleSurface(T,TSampler,P.xy/180,true);
float3 d=float3(-slope+grain.xy*.20,0);
return normalize(N+d-N*dot(N,d));
'''
