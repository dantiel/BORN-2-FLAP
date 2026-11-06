"""Single source of truth for the Shiomori Gerstner wave spectrum.

The 16-component deterministic spectrum must stay byte-identical in every place
it is evaluated, otherwise the rendered water surface drifts out of lock-step
with the gameplay water height (Born2FlapWater.cpp SampleWaveHeight) and the
sand wetness / foam all disagree with the visible waves.

Import the fragments below instead of re-typing the arrays:

  from gerstner_hlsl import HEIGHT_INLINE, HEIGHT_GRAD, SHORE_EDGE

UE Material Custom nodes paste their HLSL INSIDE a generated function body, so
top-level helper-function definitions are forbidden on mobile GLSL ES3.1
("function definition is not allowed here" -> "use of undeclared identifier").
The Gerstner height is therefore emitted as INLINE fragments (local arrays + a
for loop only — no function wrapper):

HEIGHT_INLINE -- declares lenF/ampF/dirOff/phase + Wl/Cs/ampScale/d2r, then
                 runs the 16-component loop accumulating a single raw height
                 into `float H` at the current `P`. No shore falloff.
HEIGHT_GRAD    -- same declarations, but accumulates THREE raw heights into
                 h0/hx/hy at P, P+(e,0) and P+(0,e). Requires the caller to
                 have declared `float e` BEFORE this fragment.
SHORE_EDGE     -- a single HLSL statement `float edge=10000+520*sin(...)...;`
                  matching Born2FlapSurf::Shore(X). Callers multiply their wave
                  height by `smoothstep(100.0, 3000.0, P.y - edge)` to apply the
                  shore falloff exactly as SampleWaveHeight does.
"""

_DECLS = '''\
float lenF[16]={1.0,0.5,0.33,0.22,0.151,0.107,0.079,0.059,0.045,0.034,0.0265,0.0205,0.016,0.0125,0.0098,0.0076};
float ampF[16]={1.00,0.42,0.30,0.21,0.15,0.105,0.075,0.054,0.038,0.027,0.019,0.0135,0.0095,0.0067,0.0047,0.0033};
float dirOff[16]={0.0,-8.0,12.0,-20.0,28.0,-35.0,45.0,-55.0,65.0,-50.0,40.0,-30.0,50.0,-60.0,22.0,-15.0};
float phase[16]={0.0,1.7,3.1,4.4,5.7,1.1,2.4,3.7,5.0,0.6,1.9,3.2,4.5,5.8,0.9,2.2};
float Wl=max(1.0,WL);
float Cs=max(0.0,CS);
float ampScale=lerp(0.35,1.6,Sea/6.0);
float d2r=0.01745329251;
'''

HEIGHT_INLINE = _DECLS + '''\
float H=0.0;
for(int i=0;i<16;i++)
{
    float L=Wl*lenF[i];
    float A=Amp*ampF[i]*ampScale;
    float ang=(Dir+dirOff[i])*d2r;
    float K=6.28318530718/L;
    float Om=6.28318530718*Cs/(Wl*sqrt(lenF[i]));
    H+=A*cos(K*(P.x*cos(ang)+P.y*sin(ang))-Om*WT+phase[i]);
}
'''

HEIGHT_GRAD = _DECLS + '''\
float h0=0.0;
float hx=0.0;
float hy=0.0;
for(int i=0;i<16;i++)
{
    float L=Wl*lenF[i];
    float A=Amp*ampF[i]*ampScale;
    float ang=(Dir+dirOff[i])*d2r;
    float K=6.28318530718/L;
    float Om=6.28318530718*Cs/(Wl*sqrt(lenF[i]));
    h0+=A*cos(K*(P.x*cos(ang)+P.y*sin(ang))-Om*WT+phase[i]);
    hx+=A*cos(K*((P.x+e)*cos(ang)+P.y*sin(ang))-Om*WT+phase[i]);
    hy+=A*cos(K*(P.x*cos(ang)+(P.y+e)*sin(ang))-Om*WT+phase[i]);
}
'''

# Rugged shoreline (waterline Y as a function of X), identical to
# Born2FlapSurf::Shore() and Python waterline(x). Emitted as a statement; the
# surrounding custom node must still declare `float edge;`.
SHORE_EDGE = 'float edge=10000.0+520.0*sin(P.x*0.00030+1.7)+300.0*sin(P.x*0.00105+4.2)+160.0*sin(P.x*0.0024+0.6)+85.0*sin(P.x*0.0056+2.3)+45.0*sin(P.x*0.013+5.1);'
