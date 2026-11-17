import unreal as u

lib = u.EditorAssetLibrary
for p, junk in {
    'SK_Sophie': ['Ch02_Body'],
    'SK_Kate': ['Ch21_Body'],
    'SK_Adam': ['Ch08_Beard'],
    'SK_Brian': ['Ch01_Body'],
}.items():
    for name in junk:
        for suffix in ('', '_Skeleton'):
            path = '/Game/Actor/%s/SkeletalMeshes/%s%s' % (p, name, suffix)
            if lib.does_asset_exist(path):
                lib.delete_asset(path)
                print('DEL', path)
    d = '/Game/Actor/%s/StaticMeshes' % p
    if lib.does_directory_exist(d):
        for a in lib.list_assets(d, recursive=True, include_folder=False):
            lib.delete_asset(a)
        lib.delete_directory(d)
        print('DEL_DIR', d)
u.SystemLibrary.collect_garbage()
print('CLEANUP_DONE')
