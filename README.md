# osos-lab

RepriseOS adds features to Apple's original iPod firmware while retaining its
familiar interface and playback system. This repository contains the patches,
reverse engineering, and tools used to build and package it.

Builds use Apple **2.0.5** for all supported Classic hardware, with each device's
own identity in its companion.

## Compatibility

- iPod Classic 6G, 6.5G and 7G.
- FAT32 storage required.

## Repository layout

- `payload/`: OSOS features, C patch declarations, and UI definitions.
- `game-sdk/`: deferred, incomplete homebrew runtime for the stock Games loader; it does not support sound.
- `doom/`: deferred, incomplete Doom port.
- `nes/`: deferred, incomplete InfoNES port built on the game SDK.
- `patching/`: Python recipe builder, native UI generation, and framework tests.
- `firmware/`: pinned Apple inputs, hardware eligibility and native UI metadata.
- `loader/` and `patches/`: Apple companion loader and Rockbox bootloader patches.
- `usb-helper/`: FAT32 transfer, readback verification, and DFU return.
- `host/`: Rust device library, IPSW/AUPD preparation, bundle assembly, and CLI.
- `tools/`: firmware builds, release bundle export, and Ghidra import/export.
- `ghidra/`: tracked firmware analysis.

Boot selection: **no buttons → RepriseOS**, **Menu → Apple OS**,
**Play/Pause → Rockbox**. RepriseOS loads `/osos-cfw.bin` and `/cfw-loader.bin`.
See [firmware references](docs/README.md), [boot sequence](docs/boot.md)
and [host tools](host/README.md).

## Install

1. Back up the iPod's music, library database and playlists. Confirm it boots
   Apple OS and plays a track. Use a supported FAT32 iPod Classic, a charged
   battery, and a reliable USB cable plugged directly into the computer.
2. Download [RepriseOS Installer v0.2.8](https://github.com/lgreitis/repriseos-installer/releases/tag/v0.2.8)
   and [Apple's `iPod_38.2.0.5.ipsw`](https://secure-appldnld.apple.com/iPod/SBML/osx/bundles/041-8552.20121203.Bile3/iPod_38.2.0.5.ipsw).
   For experimental native FLAC, use `classic/repriseos.zip` inside the **firmware** artifact
   from a successful [native-flac build](https://github.com/sparklecatdev/osos-lab/actions/workflows/firmware.yml?query=branch%3Anative-flac).
3. In **Install/Update RepriseOS**, choose the Apple IPSW, open **Use a local
   package**, and choose the intact FLAC ZIP. Follow the app's DFU guide and
   continue only after its device checks pass. Stop on a failed check or an
   **Install anyway (experimental)** prompt.
4. Keep the iPod connected and the computer awake until the iPod reboots.
   Check Apple OS and RepriseOS, then test regular audio and a few FLAC tracks
   before syncing the library.

The installer updates disk files only when the installed bootloader matches
the package. A first install or a different bootloader requires a bootloader
write. The installer verifies a fresh NOR and SysCfg backup before that write.
If you need a disk-only update and the installed bootloader is uncertain,
stop before starting the install.

If installation stops with an error, use **Save debug ZIP** and keep the
installer's backup folder. If RepriseOS fails to boot, hold **Menu** at startup
to try Apple OS. Keep a known-good RepriseOS ZIP available to reinstall through
**Use a local package**. An Apple restore erases the iPod. See the
[recovery FAQ](https://repriseos.com/faq/).

## Features

- Five-band custom EQ with a graphical editor and adjustable filter types,
  frequency, gain, Q and precut.
- Clipping-free EQ: built-in presets and the custom EQ lower their gain by
  their peak boost and keep headroom between filter stages.
- Crossfeed for headphones: Jan Meier's crossfeed from Rockbox blends a
  filtered part of each channel into the other, so hard-panned stereo
  recordings sound less separated. It applies after the EQ, to all audio.
- Play Next and Play Last from song, album and artist context menus.
- Apple's EU Volume Limit setting on every European iPod, with full volume
  and no volume warning when it is Off.
- File Info from song, podcast, iTunes U, audiobook, movie, TV episode, video playlist and Now Playing context menus.
- Album Artists browsing with Artist fallback and configurable menu visibility.
- Clicker output options (Off, Speaker, Headphones, Both) in Settings.
- Dark Mode, with an On/Off toggle in Settings.
- Night Shift, with an On/Off toggle below Brightness in Settings.

Open **Settings → Custom EQ** to edit all five bands. Use the wheel to select a
band, then Center to select and edit its Type, Gain, Frequency or Q, or the
Precut shared by all bands. Switch Hold on to view the combined EQ response and
off to return to editing. Precut defaults to **Auto**, which attenuates by the
response's peak between 20 Hz and 20 kHz; the response view shows the applied
value. Choose a fixed precut from 0.0 to -10.0 dB instead to set it by hand.

Every supported Classic runs Apple 2.0.5, so European iPods, older models
included, get Apple's **Settings → Volume Limit** screen with **EU Volume Limit**.
On limits headphone volume to the EU level. Off allows full volume without Apple's
high-volume warning, the reduction to the EU level at startup or the automatic
reduction after long listening.

**Album Artists** groups tracks by their Album Artist tag, using Artist when that
field is empty. Show or hide the entry through Settings → Main Menu and Settings →
Music Menu.

**Playback Log** adds an On/Off switch in Settings. It exports a standard
`.scrobbler.log` by default, with optional detailed TSV reports and selectable
columns configured in a small text file. Listening time excludes pauses and
seek jumps. See the [configuration and column reference](docs/playback-log.md).

**Night Shift** warms the whole screen through the display's gamma tables. It is
off by default and the setting persists across reboots.

The game SDK and [Doom port](doom/README.md) are unfinished.

## Native FLAC

Music can open original `.flac` files synced with the
[iOpenPod patch](tools/iopenpod-native-flac.patch). The experimental decoder
supports mono and stereo FLAC from 8 to 48 kHz, 4 to 24 bits per sample,
and blocks up to 4,608 samples. It sends 16-bit PCM to the iPod's audio pipeline.
The decoder and sync preparation have been tested on a computer. Playback on
an iPod Classic still needs verification.

With a RepriseOS build from this branch, launch the patched iOpenPod with native
FLAC enabled:

```sh
git clone https://github.com/TheRealSavi/iOpenPod.git
cd iOpenPod
git switch --detach 5e48bd7b6ea0903a1ffc5f7a44c02bacac5496d0
git apply /path/to/osos-lab/tools/iopenpod-native-flac.patch
uv sync --locked
REPRISEOS_NATIVE_FLAC=1 uv run -m iOpenPod
```

Sync Music with default stream selection and Rockbox metadata disabled.
iOpenPod copies supported FLAC files byte for byte and uses its normal
conversion settings for other FLAC files.

Music uses embedded FLAC seek tables when available and otherwise seeks by
decoding frames. iOpenPod records the exact FLAC sample count with zero
encoder delay and padding for gapless transitions. Enable Sound Check on the
iPod to use ReplayGain from synced files. iOpenPod uses the track gain when
present and falls back to the album gain. Reimport previously synced FLAC
files to refresh their playback metadata.

## Build

Requires Python 3.11+, Git, Make, Perl, a host C compiler, and Rockbox's
`arm-elf-eabi-` GCC 9.5.0 toolchain. Rust tools require Cargo, libusb, and
pkg-config. Use Xcode Command Line Tools on macOS or libusb development headers
on Linux.

```sh
python3.11 tools/setup.py
cargo build --manifest-path host/Cargo.toml --release -j 8
python3.11 tools/import_inputs.py --ipsw /path/to/iPod_38.2.0.5.ipsw \
  --osos /path/to/osos.bin --aupd /path/to/aupd-body.bin \
  --out /path/to/new-inputs
python3.11 tools/build.py --inputs /path/to/new-inputs --nor /path/to/device-nor.bin \
  --cross-prefix /path/to/bin/arm-elf-eabi-
```

Firmware builds use the inputs in [the firmware manifest](firmware/apple.json).
Outputs go to `build/`, intermediates to `.build/`. Individual targets:
`osos`, `osos-recipe`, `loader`, `loader-recipe`, `bootloader`. `tools/setup.py`
prepares pinned dependencies under `vendor/`; all project tooling lives in this repository.

`osos-recipe` and `loader-recipe` produce JSON recipes and compiled data without
Apple inputs. `osos` and `loader` also assemble images with local inputs through
the shared Rust assembler and require Cargo.

## Distribution

```sh
python3.11 tools/release.py --cross-prefix /path/to/bin/arm-elf-eabi- --jobs 8
```

This builds `build/releases/classic/repriseos.zip` without Apple firmware inputs.
Bundles contain compiled patches, assembly recipes, the NOR installer, and the
USB helper. Assembly combines them with the 2.0.5 IPSW-derived inputs and the
device's NOR identity. Schema 2 bundles require installer **0.2.0 or newer**;
installer 0.1.x does not support this format. Local ZIPs use compatibility
and hash checks; downloaded releases use signed manifests. Never copy a
personalized companion between devices. Resources currently come from the
installed Apple resource partition; dedicated resources are deferred.

## Ghidra

Ghidra tooling requires 12.1.4 and JDK 21+;
set `GHIDRA_INSTALL_DIR` and `JAVA_HOME`, then build `reprise-cli` as above.

```sh
python3.11 tools/ghidra.py import --target firmware-2.0.5 \
  --inputs /path/to/decrypted-inputs --project /path/to/new-project
python3.11 tools/ghidra.py export --project /path/to/new-project
```

Import and export default to the 41 programs for 2.0.5, the only tracked analysis.
Import restores saved annotations by default; `--fresh` creates new analysis.
Alternatively supply `--ipsw FILE --osos DECRYPTED_FILE --aupd DECRYPTED_BODY`
for 2.0.5. Historical 2.0.1/2.0.4 programs require an explicit `--target`;
restore their analysis from Git history or use `--fresh` with preserved plaintext
inputs. Local historical exports are ignored by Git.
Preparation is offline and requires supported plaintext firmware. Import verifies
a temporary project before publishing it and requires a new destination directory.
Save and close Ghidra before exporting. Use `--analysis DIR` for a separate
annotation directory. Logs are in `.build/ghidra-*.log`; all tools expose `--help`.

## Credits

- [Rockbox](https://github.com/Rockbox/rockbox) contributors: the bootloader,
  dual-boot installer, and hardware drivers used by the USB helper, and the
  Meier crossfeed (Thom Johansen, Bertrik Sikken and Michael Sevakis), ported
  to `payload/crossfeed.c` under GPL-2.0-or-later.
- [wInd3x](https://github.com/freemyipod/wInd3x) by Serge “q3k” Bazanski:
  the BootROM exploit, DFU protocol, and payload assembler code ported from Go
  to Rust in `reprise-device`, including `payload.rs` and `dfu.rs`.

## Support

♥ [Support on Ko-fi](https://ko-fi.com/lgreitis). Help fund my questionable iPod purchases.
