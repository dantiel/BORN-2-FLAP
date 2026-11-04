"""Import the two user-supplied Yúnshā radio tracks as SoundWave assets.

The tracks live in the user's Downloads folder (redirected to G:\\Downloads on
this machine) under the Unicode names:

    Yúnshā · 玄铁.mp3      ->  /Game/Yunsha/Audio/YUNSHA_XUANTIE_I
    Yúnshā · 玄铁 2.mp3    ->  /Game/Yunsha/Audio/YUNSHA_XUANTIE_II

Run using UnrealEditor-Cmd (sound system must stay ON for audio import):

    UnrealEditor-Cmd.exe "Unreal/Born2Flap/Born2Flap.uproject" \
        -run=pythonscript -script="Unreal/Born2Flap/Tools/import_yunsha_radio.py" \
        -unattended -nop4 -nosplash -nullrhi

No config change is required beyond Config/DefaultGame.ini's +LevelRadios=
entry, which already points at these two asset paths.
"""
import sys
from pathlib import Path

import unreal as u

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

from project_guard import require_project  # noqa: E402
import mp3_cover  # noqa: E402

require_project()

# "Yúnshā · 玄铁" written as explicit escapes so the file is encoding-proof:
#   Y + ú(U+00FA) + nsh + ā(U+0101) + space + ·(U+00B7) + space + 玄铁(U+7384 U+94C1)
_BASE = "Y\u00fansh\u0101 \u00b7 \u7384\u94c1"
TRACKS = [
    (_BASE + ".mp3", "YUNSHA_XUANTIE_I"),
    (_BASE + " 2.mp3", "YUNSHA_XUANTIE_II"),
]

DOWNLOAD_ROOTS = [Path("G:/Downloads"), Path.home() / "Downloads"]
DEST = "/Game/Yunsha/Audio"


def _find(filename):
    for root in DOWNLOAD_ROOTS:
        candidate = root / filename
        if candidate.exists():
            return candidate
    return None


def main():
    ela = u.EditorAssetLibrary
    assets = u.AssetToolsHelpers.get_asset_tools()
    ela.make_directory(DEST)

    for filename, asset_name in TRACKS:
        source = _find(filename)
        path = DEST + "/" + asset_name
        if not ela.does_asset_exist(path):
            if source is None:
                raise RuntimeError(
                    "Missing Yúnshā radio track: %s (looked in %s)."
                    % (filename, ", ".join(str(r) for r in DOWNLOAD_ROOTS))
                )
            task = u.AssetImportTask()
            task.filename = str(source)
            task.destination_path = DEST
            task.destination_name = asset_name
            task.automated = True
            task.replace_existing = True
            task.save = True
            assets.import_asset_tasks([task])

        wave = u.load_asset(path)
        if not isinstance(wave, u.SoundWave):
            u.log_warning("Yunsha radio track not a SoundWave: " + asset_name)
            continue
        wave.set_editor_property("looping", False)
        ela.save_asset(path)
        u.log("YUNSHA_RADIO track=" + asset_name + " duration=" + str(wave.duration))

        if source is not None:
            mp3_cover.extract_mp3_cover(source, DEST, asset_name + "_Cover")


if __name__ == "__main__":
    main()
