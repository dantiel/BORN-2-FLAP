import unreal as u
from pathlib import Path

mesh = u.load_asset('/Game/Actor/SK_Founder/SkeletalMeshes/SK_Founder.SK_Founder')
out = []
if not mesh:
    out.append('NO MESH')
else:
    bb = mesh.get_bounds()
    ext = bb.box_extent
    out.append('origin=%s' % bb.origin)
    out.append('extent=%.1f %.1f %.1f cm' % (ext.x, ext.y, ext.z))
    out.append('height=%.1f cm wingspan=%.1f cm' % (ext.z * 2.0, ext.x * 2.0))

    # topology: confirm the index buffer survived (indexed tris, not triangle soup)
    mats = mesh.get_editor_property('materials')
    render = u.MeshDescription()
    # total triangles via the LOD 0 render data
    import unreal
    rdata = mesh.get_resource_for_rendering() if hasattr(mesh, 'get_resource_for_rendering') else None
    tris = 0
    verts = 0
    try:
        rdata = mesh.get_render_data() if hasattr(mesh, 'get_render_data') else None
    except Exception:
        pass
    # Use FStaticMesh-style reporting not available; report via skinned asset stats instead
    for idx in range(mesh.get_lod_count() if hasattr(mesh, 'get_lod_count') else 1):
        pass
    out.append('num_materials=%d' % len(mats))
    # authoritative: count triangles from the import metadata if accessible
    try:
        import unreal
        skel = mesh.get_editor_property('skeleton')
        out.append('skeleton=%s' % (skel.get_name() if skel else 'None'))
    except Exception as e:
        out.append('skeleton_err=%s' % e)

Path('V:/BORN 2 FLAP/_founder_verify.txt').write_text('\n'.join(out))
