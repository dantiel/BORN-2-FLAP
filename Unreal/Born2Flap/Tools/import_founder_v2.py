"""Import the flattened Founder GLB (with baked Mixamo animations) and inspect
the created assets. Run via UnrealEditor-Cmd."""
from pathlib import Path
import unreal as u

TOOLS = u.AssetToolsHelpers.get_asset_tools()
LIB = u.EditorAssetLibrary
_HERE = Path(__file__).resolve()
_PROJECT = _HERE.parents[1]
SOURCE = _PROJECT / 'SourceAssets/Avatars/SK_Founder.glb'
DEST = '/Game/Actor/SK_Founder'


def _delete_folder(path):
    if LIB.does_directory_exist(path):
        for a in LIB.list_assets(path, recursive=True, include_folder=False):
            try:
                LIB.delete_asset(a)
            except Exception:
                pass
        try:
            LIB.delete_directory(path)
        except Exception:
            pass


def main():
    if not SOURCE.exists():
        raise RuntimeError('Missing %s' % SOURCE)

    _delete_folder(DEST)

    task = u.AssetImportTask()
    task.filename = str(SOURCE)
    task.destination_path = '/Game/Actor'
    task.destination_name = 'SK_Founder'
    task.automated = True
    task.replace_existing = True
    task.save = True
    TOOLS.import_asset_tasks([task])

    # inspect
    print('=== ASSETS under %s ===' % DEST)
    if LIB.does_directory_exist(DEST):
        for p in LIB.list_assets(DEST, recursive=True, include_folder=False):
            a = u.load_asset(p)
            if a is None:
                print('NULL', p)
                continue
            kind = type(a).__name__
            extra = ''
            if isinstance(a, u.AnimSequence):
                extra = ' frames=%s rate=%s' % (
                    a.get_editor_property('number_of_sampled_frames'),
                    a.get_editor_property('sampling_frame_rate'))
            print(f'{kind:24s} {p} {extra}')
    print('IMPORT_INSPECT_DONE')


if __name__ == '__main__':
    main()
