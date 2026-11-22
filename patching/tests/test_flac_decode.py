# SPDX-License-Identifier: GPL-3.0-only

import json
import math
import random
import shutil
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path


@unittest.skipUnless(
    shutil.which("cc") and shutil.which("ffmpeg"), "cc and ffmpeg required"
)
class FlacDecodeTests(unittest.TestCase):
    def setUp(self):
        root = Path(__file__).resolve().parents[2]
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.decoder = Path(directory.name) / "decode"
        subprocess.run(
            [
                "cc",
                "-std=c99",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-I",
                str(root / "payload/audio"),
                str(root / "payload/audio/flac.c"),
                str(root / "payload/audio/flac_wave.c"),
                str(root / "patching/tests/fixtures/flac_decode.c"),
                "-o",
                str(self.decoder),
            ],
            check=True,
        )

    @staticmethod
    def encode(samples, rate, channels, width, block=1024):
        shift = 32 - width
        source = struct.pack(
            f"<{len(samples)}i", *(sample << shift for sample in samples)
        )
        format_args = ["-sample_fmt", "s16"] if width == 16 else ["-sample_fmt", "s32"]
        return subprocess.run(
            [
                "ffmpeg",
                "-v",
                "error",
                "-f",
                "s32le",
                "-ar",
                str(rate),
                "-ac",
                str(channels),
                "-i",
                "pipe:0",
                "-c:a",
                "flac",
                *format_args,
                "-frame_size",
                str(block),
                "-compression_level",
                "8",
                "-f",
                "flac",
                "pipe:1",
            ],
            input=source,
            capture_output=True,
            check=True,
        ).stdout

    def decode(self, stream):
        return subprocess.run([self.decoder], input=stream, capture_output=True)

    def test_pcm_matches_source(self):
        random_source = random.Random(444)
        for rate, channels, width, block, pattern in (
            (8000, 1, 16, 192, "silence"),
            (44100, 2, 16, 4096, "correlated"),
            (48000, 2, 24, 4608, "correlated"),
            (44100, 2, 16, 1024, "noise"),
            (12345, 1, 16, 173, "ramp"),
        ):
            with self.subTest(
                rate=rate, channels=channels, width=width, pattern=pattern
            ):
                samples = []
                scale = 1 << (width - 1)
                for index in range(9751):
                    for channel in range(channels):
                        if pattern == "correlated":
                            value = int(
                                scale * 0.79 * math.sin((index + channel) * 0.033)
                            )
                        elif pattern == "noise":
                            value = random_source.randrange(-scale, scale)
                        elif pattern == "ramp":
                            value = ((index * 7 - 1024) % 4096 - 2048) * 8
                        else:
                            value = 0
                        samples.append(value)
                encoded = self.encode(samples, rate, channels, width, block)
                decoded = self.decode(encoded)
                expected = struct.pack(
                    f"={len(samples)}h", *(sample >> (width - 16) for sample in samples)
                )
                self.assertEqual(decoded.returncode, 0)
                self.assertEqual(decoded.stdout, expected)

    def test_rejects_bad_streams(self):
        encoded = self.encode([1234] * 2000, 44100, 1, 16)
        corrupted = encoded[:-1] + bytes([encoded[-1] ^ 1])
        for stream in (b"", b"RIFF", encoded[:31], encoded[:-1], corrupted):
            self.assertEqual(self.decode(stream).returncode, 1)
        stereo = self.encode([0] * 4000, 96000, 2, 16)
        self.assertEqual(self.decode(stereo).returncode, 1)

    def test_wave_read_and_seek(self):
        samples = [(i % 31 - 15) * 512 for i in range(7400)]
        encoded = self.encode(samples, 44100, 2, 16)
        pcm = struct.pack(f"<{len(samples)}h", *samples)
        header = struct.pack(
            "<4sI8sIHHIIHH4sI",
            b"RIFF",
            len(pcm) + 36,
            b"WAVEfmt ",
            16,
            1,
            2,
            44100,
            176400,
            4,
            16,
            b"data",
            len(pcm),
        )
        expected = header + pcm
        with tempfile.TemporaryFile() as source:
            source.write(encoded)
            source.seek(0)
            result = subprocess.run(
                [self.decoder, "wave"], stdin=source, capture_output=True
            )
            self.assertEqual(result.returncode, 1)

            # FFmpeg cannot backfill STREAMINFO when writing to a pipe.
            source.seek(21)
            count = ((encoded[21] & 0xF0) << 32) | (len(samples) // 2)
            source.write(count.to_bytes(5, "big"))
            source.seek(0)
            result = subprocess.run(
                [self.decoder, "wave"], stdin=source, capture_output=True
            )
            self.assertEqual(result.returncode, 0)
            self.assertEqual(
                result.stdout,
                expected + expected[43:46] + expected[9046:9057] + expected[-7:],
            )

    @unittest.skipUnless(shutil.which("ffprobe"), "ffprobe required")
    def test_indexed_seeks_and_fallback(self):
        samples = [(i * 733 % 65536) - 32768 for i in range(12791)]
        encoded = bytearray(self.encode(samples, 44100, 1, 16))
        count = ((encoded[21] & 0xF0) << 32) | len(samples)
        encoded[21:26] = count.to_bytes(5, "big")
        probe = subprocess.run(
            [
                "ffprobe",
                "-v",
                "error",
                "-show_entries",
                "packet=pts,duration,pos",
                "-of",
                "json",
                "-i",
                "pipe:0",
            ],
            input=encoded,
            capture_output=True,
            check=True,
        )
        packets = json.loads(probe.stdout)["packets"]
        first_frame = int(packets[0]["pos"])
        pcm = struct.pack(f"<{len(samples)}h", *samples)
        end = len(pcm) + 44
        positions = (end - 471, end // 3 + 7, end - 471)
        expected = b"".join(pcm[pos - 44 : pos - 44 + 97] for pos in positions)

        for offset_error in (0, 1):
            with self.subTest(offset_error=offset_error):
                table = b"".join(
                    struct.pack(
                        ">QQH",
                        int(packet["pts"]),
                        int(packet["pos"]) - first_frame + offset_error,
                        int(packet["duration"]),
                    )
                    for packet in packets[::2]
                ) + struct.pack(">QQH", (1 << 64) - 1, 0, 0)
                block = b"\x03" + len(table).to_bytes(3, "big") + table
                with tempfile.TemporaryFile() as source:
                    source.write(encoded[:42] + block + encoded[42:])
                    source.seek(0)
                    result = subprocess.run(
                        [self.decoder, "indexed"], stdin=source, capture_output=True
                    )
                self.assertEqual(result.returncode, 0)
                self.assertEqual(result.stdout, expected)
                audio_start = first_frame + len(block)
                audio_seeks = [
                    offset
                    for offset in map(int, result.stderr.splitlines())
                    if offset >= audio_start
                ]
                last = int(packets[12]["pos"]) + len(block) + offset_error
                earlier = int(packets[4]["pos"]) + len(block)
                self.assertEqual(
                    audio_seeks,
                    [last, audio_start, audio_start]
                    if offset_error
                    else [last, earlier, last],
                )


if __name__ == "__main__":
    unittest.main()
