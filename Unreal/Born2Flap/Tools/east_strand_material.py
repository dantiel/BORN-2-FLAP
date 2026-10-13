"""One continuous coarse-sand strand across the beach and eastern headland."""
import unreal as u
from shore_antitiling import HLSL


def blend(m, pos, colour, normal, roughness, node, custom):
    # The headland's elevation/slope weights subsequently fade this strand into
    # vegetation. Keeping this sampling identical on both meshes avoids a seam.
    weight = custom(m, HLSL + '''
float2 q=P.xy/1800, cell=floor(q), f=frac(q); f=f*f*(3-2*f);
float noise=lerp(lerp(shore.hash(cell).x,shore.hash(cell+float2(1,0)).x,f.x),
 lerp(shore.hash(cell+float2(0,1)).x,shore.hash(cell+1).x,f.x),f.y);
return smoothstep(36000+(noise-.5)*1800,44800,P.x);
''', {'P': pos}, 1)
    result = []
    for channel, base, size in [('Diffuse', colour, 3), ('Normal', normal, 3), ('Rough', roughness, 1)]:
        texture = u.load_asset('/Game/Shiomori/Textures/T_Basalt_' + channel)
        assert texture
        args = {'P': pos, 'T': node(m, 'TextureObject', texture=texture), 'A': base, 'W': weight}
        if channel == 'Normal':
            code = '''float3 t=shore.sampleSurface(T,TSampler,P.xy/95,true);
return normalize(lerp(A,normalize(float3(t.xy*.65,max(.25,t.z))),W));'''
        elif channel == 'Diffuse':
            code = '''float3 c=shore.sampleSurface(T,TSampler,P.xy/95,false)*float3(.55,.55,.56);
return lerp(A,c,W);'''
        else:
            code = '''float r=shore.sampleSurface(T,TSampler,P.xy/95,false).r;
return lerp(A,clamp(r,.55,.95),W);'''
        result.append(custom(m, HLSL+code, args, size))
    return tuple(result)
