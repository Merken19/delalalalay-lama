# CLAUDE.md

Working notes for the Delay Lama Standalone fork. Keep the **Plan & progress**
section current as work lands.

## Goal

Split the Windows build into separate, freely placeable windows:

- **Flag window**: the playable Tibetan flag (X = pitch, Y = vowel), any size.
  Touchscreens already work through Windows' touch-to-mouse emulation; the flag
  only handles plain mouse messages (no WM_POINTER code, by the user's choice).
- **Monk window**: the animated singing figure, any size.
- **Settings window**: tabs for every synth parameter plus window layout options.
- **Classic display** (optional): one resizable window with the original 360x510
  panel (monk, flag, glide/voice knobs, echo slider, ? button) instead of the
  flag and monk windows.

## Repository layout

- `DSP/`: shared C vocal engine (MonkSynth FOF voice, delay, note stack). Also
  used by the macOS build, so only add functions here; never change behavior
  without porting the same fix from `reference/monksynth/dsp`.
- `Sources/`: macOS AppKit app + `standalone_bridge.h` (shared bridge API). Do not
  touch the macOS side.
- `Windows/`: the native Win32/GDI+ app. This is where the work happens.
  - `standalone_bridge_win32.c/.h`: WinMM MIDI + locking bridge; the `.h` adds
    Windows-only setters (envelope, unison, echo, flag pitch range).
  - `audio_engine.h`: WinMM waveOut player (unchanged logic, moved out of the
    old single-file app).
  - `app.h`: settings structs, helpers, window classes, `App`.
  - `delay_lama_win32.cpp`: `App` + `wWinMain`.
  - `flag_window.cpp`, `monk_window.cpp`, `classic_window.cpp`, `settings_window.cpp`,
    `settings_store.cpp` (INI persistence), `ui_common.cpp` (DPI/GDI+ helpers, flag
    grid/crosshair drawing).
- `Tests/`: DSP and bridge smoke tests (CTest). `dsp_hardening.c` holds the
  robustness tests ported from `reference/monksynth/cpp/tests` plus flag-glide
  checks; it reads engine internals via `synth_internal.h`. Test checks must not
  use `assert()` (CTest runs Release, where NDEBUG removes it).
- `reference/`: read-only clones, excluded via `.git/info/exclude`:
  - `reference/monksynth`: JonET/monksynth, the upstream MonkSynth (C DSP in
    `dsp/`, VSTGUI editor in `cpp/src/`). Source of truth for DSP and for the
    monk animation (`cpp/src/monk_view.*`) and XY pad (`cpp/src/xy_pad.cpp`,
    `cpp/src/processor.cpp`).
  - `reference/XYMonk`: xsyetopz/XYMonk, a Rust CLAP/VST3/AU rewrite. Useful for
    artwork geometry (`src/plugin/raw_editor/geometry.rs`), frame selection
    (`animation.rs`) and pad interaction tests.

## Branches

- `main`: upstream macOS-only source.
- `origin/codex/windows-build`: upstream Windows port (what the v1.0 Windows
  release exe was built from).
- `windows-split` (local, tracks `codex/windows-build`): this work.

## Build & test (Windows)

VS 2022 Community and CMake 3.29 are installed. From the repo root:

```powershell
cmake -S .\Windows -B .\Windows\build -G "Visual Studio 17 2022" -A x64
cmake --build .\Windows\build --config Release --parallel
ctest --test-dir .\Windows\build -C Release --output-on-failure
.\Windows\build\Release\DelayLamaStandalone.exe
```

The `'pwsh.exe' is not recognized` lines during the build come from a VS post-build
hook and are harmless. `Windows\package-release.ps1 -Clean` makes the release zip.

Settings persist to `%APPDATA%\DelayLamaStandalone\settings.ini`, or to
`settings.ini` next to the exe if that file exists (portable mode).

## Design decisions

- One process, one synth, one audio engine; windows are unowned top-level windows
  (each has a taskbar button). Closing a window hides it; closing the last visible
  window exits and remembers that window as visible.
- Window geometry (X/Y/W/H) is the **inner client area** in physical screen
  pixels, so toggling the title bar never moves the artwork.
- Per-monitor DPI aware (v2). Flag/monk keep their physical size across monitors;
  the settings window rescales.
- Flag input is the original single-drag mouse model. Do not filter out
  touch-synthesized mouse messages, that is how the touchscreen plays.
- The flag window shows the whole flag including its yellow border
  (background.png 98,362 163x79); the original only reacted to 98,365-262,426.
- Borderless flag window cannot be moved or resized with the mouse on purpose (so
  edge touches still play). Arrange it with the title bar on or with the X/Y/W/H
  fields. Borderless monk window drags to move and resizes at its edges.
- Default flag pitch range stays 48..72 (C3..C5) to match the released Windows
  exe; it is adjustable in settings.
- Display mode (`Settings::classic`, INI `[Display] Classic`): `App::in_layout`
  says which windows belong to it (flag + monk, or classic; settings always).
  Classic is the default (user's choice, 2026-11-03), and so is nearest-neighbour
  scaling (`smooth_scaling = false`). Switching to separate windows always shows
  flag, monk and settings; switching to classic shows the classic window; windows
  outside the layout stay hidden. Launches restore each window's saved `visible`
  flag. Reset all settings returns to the classic display (settings window stays
  open); Reset window layout keeps the mode.
- Classic panel geometry (canvas px, measured on `background.png`): monk frame
  at 22,5 (matches the panel pixels on every edge; the old exe used 23,0),
  flag hit area = the flag window's 98,362 163x79, glide/voice knob caps centred
  at 44.5,471 and 316.5,470 (r 21.5, hit r 27), echo groove y 486, handle
  centre x 121..241, ? button 284,298-327,335. The knob caps are baked into the
  background at their minimum; the window turns that piece of the background
  (280 degree sweep) and redraws each rim glint unturned so lighting stays put.
  No knob/fader bitmaps are copied from monksynth or XYMonk.

## Testing rules

- Never inject or simulate mouse/touch/keyboard input on the user's desktop.
  Test with code (CTest smoke tests, headless checks) or ask the user to try it
  with their own devices and report back.
- A launched test instance locks the exe; close it before rebuilding.

## Known differences vs. the references (candidate bug fixes)

1. **DSP hardening** (ported 2026-11-03): `DSP/` now matches
   `reference/monksynth/dsp` exactly except our `monk_delay_set_feedback` /
   `monk_synth_set_delay_feedback` (NaN-safe clamp, capped at 0.95) and two
   additions for the flag: `monk_synth_note_on_hz` (`monk_synth_note_on`
   now delegates to it, same behavior) and `monk_synth_top_note`. Ported:
   per-sample one-pole output and unison gain smoothing (~5 ms), chunked
   processing for any block length, NaN-safe clamps, pitch 8..20000 Hz, grain
   period >= 1 sample, `fmodf` vibrato wrap, sample-rate sanitizing, grain cap
   above 192 kHz, clamped volume/level/glide, and the delay read-index wrap fix
   (a tiny negative read position could round to one past the line end).
   Left as in monksynth: the output-gain *target* is still computed once per
   process call, so a glide rendered in different block sizes differs slightly
   (max ~1.5% of peak on a 100 ms two-octave jump; see `dsp_hardening.c`).
   The macOS build uses the same files but was not built here.
2. **Monk frame width**: each 314-px column holds a 311-px frame plus a 3-px
   border (monksynth `kFrameWidth = 311`, XYMonk "crop the three-pixel right
   border"). The released exe draws all 314 px.
3. **Idle animation**: monksynth holds frame 5, blinks on frame 2 at ticks 14-17
   and 31-34 (100 ms ticks), then after 4.8 s loops a 24-step "shuffle" sequence
   (`5,3,4,3,2,1,0,1, 5,3,4,3,5,1,0,1, 2,3,4,3,5,1,0,1`, 200 ms/step) until a
   note starts. This repo blinks on frame 4 and never shuffles.
4. **Flag pitch range**: original/monksynth is one octave C3..C4
   (`130.81 * 2^x`); `dl_get_pitch` also assumes C3..C4. This repo uses two
   octaves.
5. **Singing frame mapping**: this repo and monksynth use `6 + vowel * 23`;
   XYMonk uses `(0.2 + 0.8 * vowel) * 29` across the whole sheet.
6. **Flag "laser" on press** (fixed 2026-11-03): the bridge holds placeholder
   note 127 while the flag is pressed. `dl_xy_begin` sounded it with
   `monk_synth_note_on`, so an idle voice started at 12.5 kHz and glided
   down to the flag (100 ms at glide 0.5, ~1 s at glide 1). Releasing a MIDI
   key while the flag was held also fell back to 127 and slid up. Now the
   press uses `monk_synth_note_on_hz` at the flag pitch, and a key release
   that uncovers 127 re-asserts the flag pitch. monksynth has no placeholder
   (it starts XY notes with `monk_synth_set_pitch_hz`). The released v1.0
   Windows exe and the macOS bridge (`Sources/standalone_bridge.c`, not
   touched) still have the bug.

## Plan & progress

- [x] Clone DLamaStandalone, check out the Windows branch, build the original
- [x] Clone XYMonk and monksynth into `reference/` and survey differences
- [x] Bridge/DSP setters: echo time + feedback, breath, vibrato rate, envelope,
      unison, flag pitch range + semitone snap
- [x] Split UI into flag / monk / settings windows
- [x] Settings window: Voice, Echo/Envelope, Flag, Windows tabs; INI persistence
- [x] Touchscreen: works through mouse emulation; extra WM_POINTER code removed
- [x] Update CMake and bridge smoke test (new setters covered)
- [x] Build clean (/W4, no warnings), CTest 3/3, screenshot-check each window
- [x] Known difference 2 (311-px frame crop) and 3 (idle shuffle animation)
- [x] User check on their touchscreen: flag window, borderless mode, settings
      (confirmed working 2026-11-03)
- [x] Known difference 1: port monksynth DSP hardening (output gain smoothing etc.);
      new CTest `delay_lama_dsp_hardening` (16 checks), CTest 4/4, /W4 clean,
      `Tests/run_windows_validation.ps1` passes (it now builds every test target)
- [x] Known difference 6: flag press "laser" sweep from note 127; regression
      checks in `windows_bridge_smoke.c` and `dsp_hardening.c` (17 checks);
      user confirmed the press sounds clean (2026-11-03)
- [ ] User ear check of flag glides after the DSP port (slow/fast slides, max glide)
- [ ] Optional: add `delay_lama_dsp_hardening` to the CI release-gate regex in
      `.github/workflows/windows.yml` (it runs only the two Windows smoke tests)
- [x] Update Windows README for the new windows
- [x] Reset all settings button (bottom row of every settings tab, with
      confirmation) and classic single-window display; /W4 clean, CTest 4/4,
      screenshot-checked both modes, both Windows-tab layouts and the knobs at
      several values (2026-11-03)
- [ ] User check of the classic window with mouse and touchscreen: flag, knob
      drags, echo handle, ? button, borderless drag/resize, mode switching both
      ways, Reset all settings
- [x] Defaults: classic display, smooth scaling off; switching to separate
      windows shows flag, monk and settings (user request 2026-11-03). Fresh-ini
      launch checked: classic + settings visible, flag + monk hidden
- [x] Commit on `windows-split` (user asked, 2026-11-03)
