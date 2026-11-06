"""Verify the flattened avatar imports as a proper ~180cm human skeletal mesh."""
import unreal as u

def verify():
    for path in [
        '/Game/Actor/SK_Soldier/SkeletalMeshes/SK_Soldier.SK_Soldier',
        '/Game/Actor/SK_Scout/SkeletalMeshes/SK_Scout.SK_Scout',
    ]:
        m = u.load_asset(path)
        if not m or not isinstance(m, u.SkeletalMesh):
            print('MISSING', path)
            continue
        skel = m.get_editor_property('skeleton')
        b = m.get_bounds()
        print('MESH %s' % path)
        print('  skeleton = %s' % (skel.get_name() if skel else 'NONE'))
        print('  bounds origin = %s' % str(b.origin))
        print('  bounds extent = %s' % str(b.box_extent))

verify()
print('VERIFY_DONE')