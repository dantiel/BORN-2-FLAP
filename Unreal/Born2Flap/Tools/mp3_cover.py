"""Extract the embedded ID3v2 cover art (APIC frame) from an MP3.

Unreal's audio import keeps only the decoded sound and silently drops the
ID3v2 tags — including the APIC picture most songs embed as their cover art.
This module recovers that image and re-imports it as a UTexture2D next to the
track, using the "<Track>_Cover" name the radio's auto-detect already looks
for (Born2FlapRadio.cpp AutoDetectCover), so no config change is needed.

Usage (UnrealEditor-Cmd -run=pythonscript -script=<caller>):
    import mp3_cover
    mp3_cover.extract_mp3_cover(source_mp3, "/Game/Shiomori/Audio", "SHIOMORI_BAY_I_Cover")
"""
from pathlib import Path
import unreal as u


def _syncsafe(data, off):
    # ID3v2 "syncsafe" integers: 7 bits per byte, MSB is always 0.
    return ((data[off] & 0x7f) << 21) | ((data[off + 1] & 0x7f) << 14) | \
           ((data[off + 2] & 0x7f) << 7) | (data[off + 3] & 0x7f)


def read_cover(data):
    """Return (mime, picture_bytes) from the first APIC frame, else (None, None)."""
    if len(data) < 10 or data[:3] != b"ID3":
        return None, None

    version = data[3]          # 3 -> ID3v2.3, 4 -> ID3v2.4
    flags = data[5]
    end = min(10 + _syncsafe(data, 6), len(data))
    pos = 10

    # Optional extended header (bit 6 of the flags byte).
    if flags & 0x40:
        if pos + 4 > end:
            return None, None
        pos += 4 + _syncsafe(data, pos)

    # ID3v2.4 stores frame sizes syncsafe; ID3v2.3 stores plain 32-bit BE.
    syncsafe_frames = version >= 4
    best = None  # (priority, mime, picture_bytes); 0 = front cover, 1 = other

    while pos + 10 <= end:
        if data[pos:pos + 4] == b"\x00\x00\x00\x00":  # padding reached
            break
        frame_id = data[pos:pos + 4]
        if syncsafe_frames:
            frame_size = _syncsafe(data, pos + 4)
        else:
            frame_size = (data[pos + 4] << 24) | (data[pos + 5] << 16) | \
                         (data[pos + 6] << 8) | data[pos + 7]
        body = pos + 10
        body_end = body + frame_size
        if body_end > end:
            break

        if frame_id == b"APIC":
            q = body
            enc = data[q]      # text encoding of the description
            q += 1
            # MIME type is a null-terminated latin-1 string.
            mime_end = data.find(b"\x00", q, body_end)
            if mime_end < 0:
                mime_end = body_end
            mime = data[q:mime_end].decode("latin-1", "replace").lower()
            q = mime_end + 1
            if q >= body_end:
                break
            pictype = data[q]  # 3 = front cover
            q += 1

            # Skip the description: null-terminated for 0/3 (latin-1/utf-8),
            # double-null-terminated for 1/2 (utf-16).
            if enc in (1, 2):
                while q + 1 < body_end and not (data[q] == 0 and data[q + 1] == 0):
                    q += 1
                q += 2
            else:
                d_end = data.find(b"\x00", q, body_end)
                q = body_end if d_end < 0 else d_end + 1

            # Belt-and-braces: some taggers put stray bytes between the
            # description and the image, so clamp to the image magic bytes.
            starts = [x for x in (data.find(b"\xff\xd8\xff", q, body_end),
                                  data.find(b"\x89PNG", q, body_end)) if x != -1]
            if starts:
                q = min(starts)
            pic = data[q:body_end]
            priority = 0 if pictype == 3 else 1
            if best is None or priority < best[0]:
                best = (priority, mime, pic)

        pos = body_end

    if best is None:
        return None, None
    return best[1], best[2]


def extract_mp3_cover(source, destination_path, destination_name):
    """Extract the APIC cover art from `source` and import it as a UTexture2D.

    Returns the loaded asset on success, or None when the MP3 has no cover art.
    `destination_path` is a /Game/... folder; `destination_name` the asset name.
    """
    src = Path(source)
    if not src.exists():
        u.log_warning("mp3_cover: source missing " + str(src))
        return None

    mime, pic = read_cover(src.read_bytes())
    if not pic:
        u.log("mp3_cover: no cover art in " + src.name)
        return None

    editor = u.EditorAssetLibrary
    asset_path = destination_path + "/" + destination_name
    if editor.does_asset_exist(asset_path):
        return u.load_asset(asset_path)

    # Stage the recovered picture to a real file for the importer.
    ext = ".png" if "png" in mime else ".jpg"
    cache = Path(__file__).resolve().parents[1] / "Saved/RadioCoverCache"
    cache.mkdir(parents=True, exist_ok=True)
    tmp = cache / (destination_name + ext)
    tmp.write_bytes(pic)

    editor.make_directory(destination_path)
    task = u.AssetImportTask()
    task.filename = str(tmp)
    task.destination_path = destination_path
    task.destination_name = destination_name
    task.automated = True
    task.replace_existing = True
    task.save = True
    u.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

    tex = u.load_asset(asset_path)
    if tex:
        try:
            tex.set_editor_property("srgb", True)
        except Exception:
            pass
        editor.save_asset(asset_path)
        u.log("mp3_cover: imported " + asset_path)
    return tex
