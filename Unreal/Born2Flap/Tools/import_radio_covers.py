"""Import only the radio cover art (ID3 APIC) for every shipped level.

Run using UnrealEditor-Cmd <uproject> -run=pythonscript -script=<this file>.
Much faster than full map regeneration: it extracts + imports the three cover
textures next to their SoundWave tracks, under the "<Track>_Cover" name the
radio's AutoDetectCover already resolves, so no config change is required.
"""
import sys
from pathlib import Path
import unreal as u

sys.path.insert(0, str(Path(__file__).resolve().parent))
import mp3_cover

HOME = Path.home()

TRACKS = [
    (HOME / 'Downloads/Shiomori Bay I.mp3', '/Game/Shiomori/Audio', 'SHIOMORI_BAY_I_Cover'),
    (HOME / 'Downloads/Shiomori Bay II.mp3', '/Game/Shiomori/Audio', 'SHIOMORI_BAY_II_Cover'),
    (HOME / 'Downloads/TurboRaven.mp3', '/Game/Ravenstonefield/Audio', 'TURBORAVEN_Cover'),
]

for source, folder, name in TRACKS:
    tex = mp3_cover.extract_mp3_cover(source, folder, name)
    if tex:
        u.log('RADIO_COVER_IMPORTED ' + folder + '/' + name)
    else:
        u.log_warning('RADIO_COVER_MISSING ' + str(source))
