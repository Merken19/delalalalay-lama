# Windows validation

`run_windows_validation.ps1` validates the portable C DSP boundary and the
Windows bridge on Windows. It compiles `DSP/delay.c`, `DSP/synth.c`,
`DSP/voice.c`, and `Tests/windows_smoke.c` as native Windows objects, links
them into `windows_smoke.exe`, and runs that executable. It then compiles and
links `Windows/standalone_bridge_win32.c` with `Tests/bridge_smoke.c` (the
shared checks in `Tests/bridge_checks.h`) and `winmm.lib` into
`windows_bridge_smoke.exe`, and runs that executable too.
Finally, it configures and builds the `Windows/CMakeLists.txt` project for
x64 Release, including the `DelayLamaStandalone` application target and every
test executable, and runs all registered CTest tests.

Run from the repository root in PowerShell:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File .\Tests\run_windows_validation.ps1
```

The script selects `cl.exe` first. If it is not already on `PATH`, it uses
`vswhere.exe` and `VsDevCmd.bat` to load the Visual Studio C++ x64 environment.
It can also use an installed alternative explicitly:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File .\Tests\run_windows_validation.ps1 -Compiler ClangCl
pwsh -NoProfile -ExecutionPolicy Bypass -File .\Tests\run_windows_validation.ps1 -Compiler GCC
```

`windows-latest` provides Visual Studio, CMake, and the C++ toolchain used by
the default path. A local Windows machine needs Visual Studio/Build Tools with
the Desktop C++ workload plus CMake, or an installed `clang-cl`/MinGW
toolchain when using the corresponding option. The output directory is
temporary by default; use `-OutputDirectory` to retain the objects and
executables at a chosen path.

The checks cover MIDI pitch conversion, the delay's dry path, finite and
non-silent synthesized audio, parameter clamping, normalized pitch endpoints,
last-note-priority fallback, note-off state, bridge construction, bridge audio
processing, XY state access, all-notes-off, the flag's press and semitone snap,
and the settings-window setters. `dsp_hardening.c` (CTest
`delay_lama_dsp_hardening`) covers the robustness fixes ported from monksynth:
NaN, infinite and zero sample rates, pitches and parameters, block lengths of
any size, and smooth output gain during flag glides. The CMake build verifies the
Windows GUI target compiles and links; the GUI is not launched in CI because
that would require an interactive desktop and audio device. The process exit
code is non-zero on any failure.

CTest also runs `delay_lama_macos_bridge_logic`: the macOS bridge
(`Sources/standalone_bridge.c`) compiled against the stub CoreMIDI and
`os_unfair_lock` headers in `Tests/apple_stubs`. It runs the same bridge
checks plus the CoreMIDI byte parser (running status, SysEx, realtime bytes,
packet lists). The Swift app in `Sources/*.swift` is not built here; on macOS
use `build.sh` and `Tests/run_macos_tests.sh`, which GitHub Actions runs in
`.github/workflows/macos.yml`.
