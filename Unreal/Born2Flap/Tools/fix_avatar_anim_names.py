"""Rename the `*_mixamo_com` animation assets back to their clean `AM_*` names.

Some Mixamo FBX clips carry a `_mixamo_com` take-name suffix; the FBX importer
uses that for the asset name. This strips it so the avatar code's `AM_*` paths
resolve uniformly across all characters.
"""
import unreal as u

LIB = u.EditorAssetLibrary

_FOLDERS = [
    '/Game/Actor/SK_Pirate/Animations',
    '/Game/Actor/SK_Soldier/Animations',
    '/Game/Actor/SK_Scout/Animations',
]


def rename():
    for folder in _FOLDERS:
        assets = LIB.list_assets(folder)
        for path in assets:
            base = path.rsplit('/', 1)[-1]
            if '_mixamo_com' in base:
                clean = base.replace('_mixamo_com', '')
                new_path = folder + '/' + clean
                if not LIB.does_asset_exist(new_path):
                    LIB.rename_asset(path, new_path)
                    u.log('rename: %s -> %s' % (path, new_path))
                else:
                    u.log_warning('rename: target exists, skip %s' % path)
    print('RENAME_DONE')


if __name__ == '__main__':
    rename()
