# Delay Lama Standalone

A native, standalone preservation build of AudioNerdz' 2002 **Delay Lama**
singing-monk instrument for current Windows and macOS. Drag across the Tibetan
flag to sing: left/right is the pitch, up/down is the vowel, and the monk sings
along.

> [!IMPORTANT]
> **Platform status**
>
> - **Windows** (x64, Windows 10 and 11; tested on Windows 11): built and
>   tested, including touchscreens. This is the maintained version.
> - **macOS** (13 or later, Apple silicon and Intel): updated to match the
>   Windows version, but **not built, run or tested by the maintainers**. It
>   may fail to compile or misbehave. The shared C code it uses is tested on
>   Windows, and GitHub Actions compiles the app on macOS, but nobody has
>   played it on a Mac yet. Reports and fixes from Mac users are welcome.

This project is a fork of
[SunnySkye/DLamaStandalone](https://github.com/SunnySkye/DLamaStandalone).
The original Delay Lama VST was a PowerPC/Carbon (Mac) and Windows plug-in that
no longer runs on modern systems; this build recreates it as a standalone app
with a clean-room vocal engine (see [Provenance and licensing](#provenance-and-licensing)).

## Features

- **Classic display** (default): the original 360×510 panel in one resizable
  window: the monk, the playable flag, the glide and voice knobs, the echo
  slider and the ? button.
- **Separate windows**: the flag, the monk and the settings in their own
  windows, each moved and resized freely (for example a large flag on a
  touchscreen and the monk on another display).
- **Settings window** with tabs for:
  - Voice: volume, glide, voice (deep to high), vibrato depth and speed,
    breath, unison voices, detune and spread.
  - Echo / Envelope: echo amount, time and feedback; attack, decay, sustain
    and release.
  - Flag: pitch range (default C3 to C5), semitone snap, note grid, crosshair.
  - Windows: display mode, title bar, always on top, keep aspect ratio and the
    exact position and size of each window; pixel-sharp or smooth scaling.
- Idle animation from MonkSynth: the monk blinks and then shuffles while
  waiting for the next note.
- Computer keyboard: `A W S E D F T G Y H U J K O L` play a chromatic octave
  and a bit from C3.
- MIDI input from every connected device: notes, pitch wheel to vowel, CC1
  vibrato, CC5 glide, CC7 volume, CC12 echo and CC13 voice.
- Right-click any window for quick options. The settings window always opens
  with the app and closing it quits; closing the flag, monk or classic window
  only hides it (tick **Show** on the Windows tab to bring it back). Every
  setting and window position is remembered.
- No installer, runtime, network access or third-party libraries.

## Download

Windows release ZIPs (`DelayLamaStandalone-<version>-windows-x64.zip`, with a
SHA-256 checksum) go on the repository's Releases page, and every run of the
[Windows workflow](.github/workflows/windows.yml) uploads one as an artifact.
Unzip it anywhere and run `DelayLamaStandalone.exe` (keep the `Resources`
folder next to it).

There is no macOS release yet. Each run of the
[macOS workflow](.github/workflows/macos.yml) uploads an untested
`delay-lama-macos-untested` app artifact for testers. The app is ad-hoc signed,
so macOS will ask for confirmation the first time (Control-click the app and
choose Open, or allow it under System Settings > Privacy & Security).

## Where settings are stored

- **Windows**: `%APPDATA%\DelayLamaStandalone\settings.ini`. Put an empty
  `settings.ini` next to the exe to keep the settings there instead (portable
  mode).
- **macOS**: the user defaults of `com.local.delaylama.standalone`. Remove them
  with `defaults delete com.local.delaylama.standalone`.

**Reset all settings...** at the bottom of the settings window restores every
default, including the window layout.

## Building

### Windows

Install Visual Studio 2019 or newer with the Desktop C++ workload (it includes
the Windows SDK) and CMake 3.20 or newer. From PowerShell at the repository
root:

```powershell
.\Windows\build.ps1
```

This configures, builds and runs every test. The app is
`Windows\build\Release\DelayLamaStandalone.exe`. To make the release ZIP:

```powershell
.\Windows\package-release.ps1 -Clean
```

See [Windows/README.md](Windows/README.md) for the CMake commands and details.

### macOS (untested)

With the Xcode Command Line Tools (Xcode 15 or newer) installed:

```bash
./build.sh
```

The universal app is written to `build/Delay Lama Standalone.app`. The C tests
(shared DSP and the CoreMIDI bridge) run with:

```bash
./Tests/run_macos_tests.sh
```

## Tests

| Test | Covers | Runs on |
| --- | --- | --- |
| `delay_lama_dsp_smoke`, `delay_lama_windows_smoke` | Shared DSP basics | Windows (CTest), macOS script |
| `delay_lama_dsp_hardening` | Robustness tests ported from MonkSynth, flag glides | Windows (CTest), macOS script |
| `delay_lama_windows_bridge_smoke` | Windows bridge (`Tests/bridge_checks.h`) | Windows (CTest) |
| `delay_lama_macos_bridge_logic` | macOS bridge logic and CoreMIDI parser, built against stub Apple headers | Windows (CTest) |
| `bridge_smoke` | macOS bridge with the real CoreMIDI | macOS script |

GitHub Actions runs the Windows suite on every change
([windows.yml](.github/workflows/windows.yml)) and builds the macOS app and
runs the macOS script ([macos.yml](.github/workflows/macos.yml)). The user
interface itself is tested by hand.

## Repository layout

| Path | Contents |
| --- | --- |
| `DSP/` | Shared C vocal engine (MonkSynth FOF voice, stereo delay, note stack) |
| `Sources/` | macOS app (Swift/AppKit), the macOS bridge and the shared bridge header `standalone_bridge.h` |
| `Windows/` | Windows app (Win32/GDI+, WinMM audio and MIDI) and its CMake project |
| `Tests/` | C tests and the Windows validation script |
| `Resources/` | Original artwork and manual (see below) |

[CONTRIBUTING.md](CONTRIBUTING.md) describes how the pieces fit together and
how to test changes; [CHANGELOG.md](CHANGELOG.md) lists what changed.

## Provenance and licensing

The code is MIT-licensed; see [LICENSE](LICENSE). The vocal synthesis code is
derived from Jonathan Taylor's MIT-licensed
[MonkSynth](https://github.com/JonET/monksynth) clean-room reconstruction; see
[MONKSYNTH-LICENSE.txt](MONKSYNTH-LICENSE.txt). How the original plug-in was
examined is described in [REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md).

The classic artwork and the original manual in `Resources/` were recovered
from the 2004 Delay Lama freeware package. AudioNerdz' original manual states
that Delay Lama is freeware and may be distributed freely when all original
files accompany the plug-in; it may not be sold or used as part of a commercial
promotion. These assets are not relicensed by this project.

This preservation build is not affiliated with or endorsed by AudioNerdz.
