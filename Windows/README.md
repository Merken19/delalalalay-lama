# Delay Lama Standalone for Windows

This directory holds the native Win32 build. It shares the C vocal DSP
(`DSP/`) and the bridge API (`Sources/standalone_bridge.h`) with the macOS app
and uses only Windows-provided APIs:

- Win32/GDI+ for separate, freely sized flag, monk and settings windows, or a
  classic single resizable window, drawn from the original PNG artwork;
- WinMM `waveOut` with four 512-frame buffers for 48 kHz stereo audio;
- WinMM `midiIn` for all available short-message MIDI input devices.

No third-party runtime or audio/MIDI library is required. GDI+ and WinMM are
part of Windows. The audio output is converted from the DSP's float samples to
16-bit PCM for compatibility with the system wave mapper.

## Build and run

Install Visual Studio 2019 or newer (the Desktop C++ workload), CMake 3.20 or
newer, and the Windows SDK. From PowerShell at the repository root:

```powershell
.\Windows\build.ps1
```

For a clean Debug build:

```powershell
.\Windows\build.ps1 -Configuration Debug -Clean
```

The script configures `Windows/CMakeLists.txt`, builds the GUI and headless
Windows smoke tests, and runs those tests through CTest (including
`delay_lama_dsp_hardening`, the DSP robustness tests ported from monksynth, and
`delay_lama_macos_bridge_logic`, the macOS bridge built against stub Apple
headers). The executable is in
the CMake configuration's output directory under `Windows/build`; its
`Resources` folder is populated automatically from the repository assets. Run
`DelayLamaStandalone.exe` from that output directory so it can find the copied
artwork and original manual.

The same path can be driven directly by CMake, for example in a Visual Studio
developer shell:

```powershell
cmake -S .\Windows -B .\Windows\build -A x64
cmake --build .\Windows\build --config Release --parallel
ctest --test-dir .\Windows\build -C Release --output-on-failure
```

To create the reproducible x64 release bundle, run:

```powershell
.\Windows\package-release.ps1 -Clean
```

This builds Release, stages `DelayLamaStandalone.exe`, the `Resources` folder,
both license files, and this Windows README, then writes these files under
`Windows\build\package`:

- `DelayLamaStandalone-v1.0-windows-x64.zip`
- `DelayLamaStandalone-v1.0-windows-x64.zip.sha256`

The exe and window icon is the singing monk, `Windows\delay_lama.ico`, which is
compiled in through `delay_lama.rc`. It is cut from `Resources\faces.png` by
`.\Windows\make-icon.ps1` (pass `-Frame 0..29` to choose a different pose); the
generated file is checked in, so the build does not run the script.

The release gate intentionally uses the two focused CTest executables
`delay_lama_windows_smoke` and `delay_lama_windows_bridge_smoke` rather than a
GUI `--self-test` mode. They exercise the shared DSP and Win32 bridge without
requiring a desktop session, audio device, or physical MIDI device.

GitHub Actions exercises this configuration in
`.github/workflows/windows.yml` on `windows-latest`.

## Windows and settings

With separate windows selected, the app shows three top-level windows that can
be moved, resized and placed on any monitor independently:

- **Flag**: drag (or touch) across the flag to sing; left/right is pitch,
  up/down is the vowel. Optional note grid, semitone snap and a configurable
  pitch range (default C3 to C5).
- **Monk**: the animated singer, including the idle blink and mouth-shuffle
  animation from MonkSynth.
- **Settings**: tabs for Voice (volume, glide, voice, vibrato depth and speed,
  breath, unison), Echo / Envelope (echo amount, time, feedback, attack, decay,
  sustain, release), Flag, and Windows (display mode, then show, title bar,
  always on top, keep aspect ratio, and exact X/Y/W/H for each window).
  **Reset all settings...** at the bottom of every tab asks for confirmation,
  then restores every option and window position to its default, with the
  classic display shown.

The app starts in the **classic display** (Windows tab, or "Classic display
(one window)" in the right-click menu): one window instead of the flag and monk
windows, showing the original 360x510 panel scaled to any size: the monk, the
playable flag, the left knob for glide, the right knob for the voice, the handle
under the flag for the echo amount, and the ? button for help. The knobs and
handle stay in sync with the settings sliders. Choosing separate windows shows
the flag, monk and settings windows where they were last placed.

Artwork is scaled pixel for pixel by default; turn on **Smooth image scaling**
on the Windows tab for filtered scaling.

The settings window always opens with the app, and closing it exits Delay
Lama. Closing the flag, monk or classic window only hides it (tick **Show** on
the Windows tab to bring it back); if none of them was left open, the next
launch shows them again. Right-click any window for quick options. Settings and
window positions are saved to
`%APPDATA%\DelayLamaStandalone\settings.ini`, or to `settings.ini` next to the
exe when that file exists (portable mode). A higher-resolution
`Resources\flag.png` replaces the original flag artwork in the flag window if
present.

## Controls and known boundaries

The flag, classic panel, settings sliders, computer keyboard, and the About/manual action are
implemented with native Win32 messages. MIDI follows the existing
mapping: notes, pitch wheel to vowel, CC1 vibrato, CC5 glide, CC7 volume, CC12
delay, and CC13 voice. The default Windows playback device is used; there is
no ASIO-specific path or device picker. WinMM short MIDI messages are handled,
while SysEx is intentionally ignored.
