# Contributing

Thanks for helping keep the monk singing. Bug reports, fixes and Mac testing
are all welcome.

## Platform status

Windows is the maintained, tested version. The macOS app was ported from it
but has not been built or played by the maintainers, so reports from Mac users
("it builds", "it crashes when...", "the knob turns the wrong way") are
especially useful. Please say which platform you used in every issue.

## How the code fits together

- `DSP/` is the vocal engine shared by both apps. It mirrors
  [MonkSynth](https://github.com/JonET/monksynth)'s `dsp/` folder. Only add
  functions here; do not change existing behavior unless the same fix exists
  upstream (port it from there and say so).
- `Sources/standalone_bridge.h` is the thread-safe API the apps use. It has
  two implementations with the same behavior:
  `Windows/standalone_bridge_win32.c` (WinMM MIDI, critical section) and
  `Sources/standalone_bridge.c` (CoreMIDI, `os_unfair_lock`). A change to one
  needs the same change in the other, plus a check in `Tests/bridge_checks.h`,
  which both bridges run.
- `Windows/` is the Win32/GDI+ app; `Sources/*.swift` is the AppKit app. The
  Windows app is the reference: the Swift files mirror its windows, settings
  and geometry numbers (see the comments that name the matching Windows file).
  Port UI changes to the other platform, or note in the pull request that a
  change is platform-specific.

## Building and testing on Windows

Requirements: Visual Studio 2019 or newer with the Desktop C++ workload, and
CMake 3.20 or newer. From the repository root:

```powershell
cmake -S .\Windows -B .\Windows\build -A x64
cmake --build .\Windows\build --config Release --parallel
ctest --test-dir .\Windows\build -C Release --output-on-failure
```

The build must stay free of warnings at `/W4`. CTest covers the DSP, the
Windows bridge, and the macOS bridge compiled against the stub Apple headers
in `Tests/apple_stubs`, so bridge changes for macOS can be tested on Windows
too. `Tests/run_windows_validation.ps1` additionally builds everything from
scratch with a chosen compiler.

Close any running `DelayLamaStandalone.exe` before rebuilding; it locks the
file.

## Building and testing on macOS

Requirements: the Xcode Command Line Tools (Xcode 15 or newer).

```bash
./build.sh                  # app in build/Delay Lama Standalone.app
./Tests/run_macos_tests.sh  # DSP tests and the CoreMIDI bridge
```

If you can, play the app for a while and try the settings window, both display
modes and window options, and a MIDI keyboard, and report what you find.

## Test rules

- Test code must not use `assert()`: CTest runs Release builds, where `NDEBUG`
  removes it. Print the failure and return non-zero instead.
- New behavior in the bridges or the DSP gets a test.
- User interface changes are checked by hand; describe what you tried
  (mouse, touchscreen, MIDI, display scaling) in the pull request.

## Style and files

- Match the surrounding code: naming, comment density and formatting.
- Text files use LF line endings in the repository (see `.gitattributes`).
- Do not add third-party artwork or code without a compatible license. The
  original Delay Lama artwork in `Resources/` is AudioNerdz freeware and is not
  relicensed by this project.
- Add a line to `CHANGELOG.md` under **Unreleased** for user-visible changes.
