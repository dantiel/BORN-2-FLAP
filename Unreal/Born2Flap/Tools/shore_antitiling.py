"""Shared stochastic sampling for shore colour, normals and roughness."""

# Explicit gradients ignore discontinuities in the cell hash, keeping mip
# selection stable at patch boundaries. All channels use the same transforms.
HLSL = r'''
struct ShoreSampling {
 float2 hash(float2 p) {
  float3 q=frac(float3(p.xyx)*float3(.1031,.1030,.0973));
  q+=dot(q,q.yzx+33.33);
  return frac((q.xx+q.yz)*q.zy);
 }
 float3 sampleSurface(Texture2D t, SamplerState s, float2 uv, bool normalMap) {
  float2 grid=uv*.8, cell=floor(grid), f=frac(grid);
  float2 blend=f*f*(3-2*f);
  float2 dx=ddx(uv), dy=ddy(uv);
  float3 result=0;
  [unroll] for(int y=0;y<2;y++) {
   [unroll] for(int x=0;x<2;x++) {
    float2 h=hash(cell+float2(x,y));
    float angle=h.x*6.2831853;
    float c=cos(angle), a=sin(angle);
    float2x2 rotation=float2x2(c,-a,a,c);
    float2 coord=mul(rotation,uv-cell-float2(x,y))+h*17.0;
    float3 v=Texture2DSampleGrad(t,s,coord,mul(rotation,dx),mul(rotation,dy)).rgb;
    if(normalMap) {
     v=v*2-1;
     v.xy=mul(transpose(rotation),v.xy);
    }
    float weight=(x ? blend.x : 1-blend.x)*(y ? blend.y : 1-blend.y);
    result+=v*weight;
   }
  }
  return result;
 }
};
ShoreSampling shore;
'''
