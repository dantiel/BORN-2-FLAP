"""Verify the imported avatar characters: meshes, skeletons, and animations."""
import json
import unreal as u

OUT = "V:/BORN 2 FLAP/_avatar_verify.json"
out = {}

chars = {
    'SK_Pirate':  ['Section2', 'Section3', 'Section4', 'Section5'],
    'SK_Soldier': ['Section0', 'Section1', 'Section2', 'Section3', 'Section4', 'Section5', 'Section6'],
    'SK_Scout':   ['Section0', 'Section1'],
}
anims = ['AM_Idle', 'AM_Standing', 'AM_Walk', 'AM_Run', 'AM_Throw', 'AM_Catch']

for name, sections in chars.items():
    c = {'sections': {}, 'animations': {}}
    for s in sections:
        path = '/Game/Actor/%s/SkeletalMeshes/%s.%s' % (name, s, s)
        m = u.load_asset(path)
        if not m:
            c['sections'][s] = 'MISSING'
            continue
        skel = None
        try:
            skel = m.get_editor_property('skeleton')
        except Exception:
            pass
        bones = []
        if skel:
            try:
                bones = [skel.get_bone_name(i).__str__() for i in range(min(skel.get_bone_count(), 8))]
            except Exception:
                pass
        b = m.get_bounds()
        c['sections'][s] = {
            'type': m.get_class().get_name(),
            'skeleton': skel.get_path_name() if skel else None,
            'first_bones': bones,
            'bounds_extent': [round(v, 2) for v in (b.box_extent.x, b.box_extent.y, b.box_extent.z)],
        }
    for a in anims:
        p = '/Game/Actor/%s/Animations/%s' % (name, a)
        seq = u.load_asset(p)
        if seq and isinstance(seq, u.AnimSequence):
            skel = None
            try:
                skel = seq.get_editor_property('skeleton')
            except Exception:
                pass
            c['animations'][a] = 'OK' + (' (%s)' % skel.get_name() if skel else '')
        else:
            c['animations'][a] = 'MISSING'
    out[name] = c

with open(OUT, 'w') as f:
    json.dump(out, f, indent=2)
print('AVATAR_VERIFY ' + json.dumps(out))
u.SystemLibrary.execute_console_command(None, 'quit')
