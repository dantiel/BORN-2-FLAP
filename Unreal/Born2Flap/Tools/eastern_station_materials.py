"""Local mineral and service-structure surfaces; no external asset dependency."""
import unreal as u

def granite(scope, m):
    node, custom = scope['node'], scope['custom']
    p = node(m, 'WorldPosition')
    n = node(m, 'VertexNormalWS')
    # World-space quartz/feldspar/mica grains, filtered out below pixel size.
    colour = custom(m, '''float3 cell=floor(P/1.8);
float grain=frac(sin(dot(cell,float3(127.1,311.7,74.7)))*43758.5453);
float macro=.5+.5*sin(P.x*.017+sin(P.y*.023)+sin(P.z*.019));
float3 minerals=grain<.15?float3(.09,.105,.11):grain>.68?float3(.57,.55,.51):float3(.34,.33,.31);
float footprint=max(length(ddx(P)),length(ddy(P)));
return lerp(minerals,float3(.36,.355,.335),smoothstep(1,7,footprint))*(.82+.18*macro);''', {'P':p}, 3)
    normal = custom(m, '''float3 ripple=float3(cos(P.x*.22),sin(P.y*.27),cos(P.z*.21));
return normalize(N+(ripple-N*dot(ripple,N))*.13);''', {'P':p,'N':n}, 3)
    return colour, normal

def build(scope):
    lib, ela = u.MaterialEditingLibrary, u.EditorAssetLibrary
    for name, tint, rough in [('EastGranite',(.36,.355,.335),.88),
                              ('EastBasalt',(.065,.07,.068),.94),
                              ('EastConcrete',(.28,.29,.27),.92),
                              ('EastMetal',(.105,.13,.12),.71),
                              ('EastDish',(.43,.45,.42),.78)]:
        m=scope['pbr_material'](name,color=tint,rough=rough)
        m.set_editor_property('two_sided',True)
        if name=='EastGranite':
            c,n=granite(scope,m)
            m.set_editor_property('tangent_space_normal',False)
            scope['output'](c,'BASE_COLOR');scope['output'](n,'NORMAL')
        else:
            p=scope['node'](m,'WorldPosition')
            c=scope['custom'](m,'''float wear=.86+.09*sin(P.z*.023+sin(P.x*.033))+.05*sin(P.y*.071+P.x*.027);
return C*wear;''',{'P':p,'C':scope['node'](m,'Constant3Vector',constant=u.LinearColor(*tint))},3)
            scope['output'](c,'BASE_COLOR')
        lib.recompile_material(m);assert ela.save_loaded_asset(m)
