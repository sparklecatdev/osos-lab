#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Convert FLAC files to Apple Lossless for syncing to an iPod Classic."""

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


def number_tag(tags, name, *total_names):
    number = tags.get(name, "")
    if not number or "/" in number:
        return number
    for total_name in total_names:
        if tags.get(total_name):
            return f"{number}/{tags[total_name]}"
    return number


def convert(ffmpeg, ffprobe, source, destination):
    if os.path.lexists(destination):
        raise FileExistsError(f"output already exists: {destination}")

    probe = subprocess.run(
        [ffprobe, "-v", "error", "-show_streams", "-show_format", "-of", "json", str(source)],
        check=True,
        capture_output=True,
        text=True,
        encoding="utf-8",
    )
    info = json.loads(probe.stdout)
    streams = info.get("streams", [])
    audio = next((stream for stream in streams if stream.get("codec_type") == "audio"), {})
    if audio.get("codec_name") != "flac":
        raise ValueError("file does not contain FLAC audio")
    if (
        audio.get("bits_per_raw_sample") != "16"
        or audio.get("sample_rate") not in {"44100", "48000"}
        or audio.get("channels") not in {1, 2}
    ):
        raise ValueError("expected 16-bit, 44.1 or 48 kHz, mono or stereo FLAC")

    args = [
        ffmpeg, "-nostdin", "-hide_banner", "-loglevel", "error", "-xerror",
        "-i", str(source), "-map", f"0:{audio['index']}", "-map_metadata", "0",
        "-c:a", "alac", "-sample_fmt", "s16p", "-movflags", "+faststart",
    ]
    tags = {key.lower(): value for key, value in info.get("format", {}).get("tags", {}).items()}
    track = number_tag(tags, "track", "tracktotal", "totaltracks")
    disc = number_tag(tags, "disc", "disctotal", "totaldiscs")
    if track:
        args.extend(["-metadata", f"track={track}"])
    if disc:
        args.extend(["-metadata", f"disc={disc}"])

    artwork = next(
        (stream for stream in streams if stream.get("disposition", {}).get("attached_pic")),
        None,
    )
    if artwork:
        codec = "copy" if artwork.get("codec_name") in {"mjpeg", "png"} else "png"
        args.extend(["-map", f"0:{artwork['index']}", "-c:v", codec, "-disposition:v", "attached_pic"])

    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=destination.parent) as staging:
        staged = Path(staging) / destination.name
        subprocess.run([*args, str(staged)], check=True, capture_output=True, text=True)
        # Reserve the destination before replacing it so simultaneous imports cannot overwrite it.
        with destination.open("xb"):
            pass
        try:
            os.replace(staged, destination)
        except OSError:
            destination.unlink()
            raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="FLAC file or directory to scan recursively")
    parser.add_argument("--out", required=True, type=Path, help="Directory for converted .m4a files")
    args = parser.parse_args()

    ffmpeg, ffprobe = shutil.which("ffmpeg"), shutil.which("ffprobe")
    if not ffmpeg or not ffprobe:
        parser.error("ffmpeg and ffprobe must be available on PATH")

    try:
        source = args.source.absolute()
        output = args.out.resolve()
        if source.is_dir():
            root = source
            files = sorted(
                path for path in root.rglob("*") if path.suffix.lower() == ".flac" and path.is_file()
            )
        else:
            root = source.parent
            files = [source] if source.suffix.lower() == ".flac" and source.is_file() else []
    except OSError as error:
        parser.error(str(error))
    if not files:
        parser.error(f"no FLAC files found at {source}")

    failed = False
    for path in files:
        destination = output / path.relative_to(root).with_suffix(".m4a")
        try:
            convert(ffmpeg, ffprobe, path, destination)
        except (OSError, ValueError, subprocess.CalledProcessError) as error:
            detail = getattr(error, "stderr", None)
            print(f"{path}: {detail.strip() if detail else error}", file=sys.stderr)
            failed = True
        else:
            print(destination)
    return int(failed)


if __name__ == "__main__":
    sys.exit(main())
