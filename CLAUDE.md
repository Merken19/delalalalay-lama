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

Windows is the maintained, tested platform. The macOS app (`Sources/*.swift`)
mirrors it but has never been compiled or run here (no Mac, no Swift
toolchain); the README says so. GitHub Actions (`macos.yml`) is the only
compile check. When a Windows UI behavior changes, port it to the Swift files
and keep the README/CHANGELOG disclaimers.

## Repository layout

- `DSP/`: shared C vocal engine (MonkSynth FOF voice, delay, note stack). Also
  used by the macOS build, so only add functions here; never change behavior
  without porting the same fix from `reference/monksynth/dsp`.
- `Sources/`: the macOS app and the shared bridge API.
  - `standalone_bridge.h`: the bridge API both platforms implement, including
    the settings setters (envelope, unison, echo, flag pitch range + snap).
  - `standalone_bridge.c`: CoreMIDI + `os_unfair_lock` bridge, same behavior as
    the Windows one (keep them in step; checks go in `Tests/bridge_checks.h`).
  - Swift (AppKit, `@main` in `AppController.swift`, built by `build.sh` with
    `-parse-as-library -swift-version 5`): `AppController.swift` (= `App`),
    `ArtworkViews.swift` (flag/monk/classic views, borderless-capable window),
    `SettingsWindow.swift`, `Settings.swift` (UserDefaults, keys named after the
    INI keys), `Artwork.swift` (pixel crops, knob cap/glint split, drawing
    helpers), `AudioController.swift` (AVAudioEngine). macOS geometry is the
    content rect in points from the top-left of the main display.
- `Windows/`: the native Win32/GDI+ app (the reference implementation).
  - `standalone_bridge_win32.c`: WinMM MIDI + locking bridge.
  - `audio_engine.h`: WinMM waveOut player (unchanged logic, moved out of the
    old single-file app).
  - `app.h`: settings structs, helpers, window classes, `App`.
  - `delay_lama_win32.cpp`: `App` + `wWinMain`.
  - `flag_window.cpp`, `monk_window.cpp`, `classic_window.cpp`, `settings_window.cpp`,
    `settings_store.cpp` (INI persistence), `ui_common.cpp` (DPI/GDI+ helpers, flag
    grid/crosshair drawing).
  - `monk_animation.h` (`MonkAnimator`: singing frame and idle blink/shuffle, one
    clock per monk, optional idle offset) and `monk_art.h/.cpp` (`FaceFrames`
    loads the 30 frames, `FaceCache` draws them pre-scaled). Shared by the
    standalone (`App`, `MonkWindow`) and `DelayLamaVoice`.
  - `DelayLamaVoice.exe`, the console helper for Delay Lama Everything (a
    midis2jam2 fork, parent folder of this clone): sings one MIDI channel read as
    text commands from stdin. `voice_bridge.h` (Windows-only bridge additions:
    `dl_create_without_midi`, `dl_set_pitch_bend`, `dl_set_level`,
    `dl_set_vowel`, implemented in `standalone_bridge_win32.c`),
    `voice_protocol.*` (parser), `voice_allocator.*` (choir voice allocation),
    `voice_choir.*` (K synths, channel state, `set` keys, mix; plain or
    profile mode), `voice_articulator.*` (profile mode, Delay Lama Everything
    phase 4b: per-note curves at 1 ms ticks, its own allocation, pan, choir
    echo; keys in `voice_profile_keys.inc`), `voice_render.*`
    (offline `--render` and WAV writer); these five form the
    `delay_lama_voice_core` library the tests link. `voice_window.*` (monks side
    by side) and `voice_main.cpp` (options, stdin reader thread, audio).
    `AudioEngine` takes a render callback for the choir mix.
  - `delay_lama.ico` + `delay_lama.rc` + `resource.h`: exe/window icon (singing
    monk, frame 20 of `faces.png`; head close-up at <= 32 px). Regenerate with
    `make-icon.ps1` (PowerShell + System.Drawing, no extra tools); the `.ico` is
    checked in.
- `Tests/`: DSP and bridge smoke tests (CTest). `dsp_hardening.c` holds the
  robustness tests ported from `reference/monksynth/cpp/tests` plus flag-glide
  checks; it reads engine internals via `synth_internal.h`. `bridge_checks.h`
  holds the bridge checks both bridges run (`bridge_smoke.c` links either
  bridge). `macos_bridge_logic.c` builds the macOS bridge on Windows against
  the stub headers in `apple_stubs/` and tests its CoreMIDI parser.
  `run_macos_tests.sh` runs the C tests on a Mac. Test checks must not use
  `assert()` (CTest runs Release, where NDEBUG removes it).
  `voice_protocol_checks.cpp`, `voice_allocator_checks.cpp` (also the choir
  idle offsets) and `voice_render_checks.cpp` (determinism, levels, real pitch
  bend measured from the audio) test the helper core; `delay_lama_voice_render_cli`
  runs the exe's `--render` on `voice_scripts/chord.txt`. `voice_scripts/` holds
  command scripts for renders and ear checks.
- `reference/`: read-only clones, excluded via `.git/info/exclude`:
  - `reference/monksynth`: JonET/monksynth, the upstream MonkSynth (C DSP in
    `dsp/`, VSTGUI editor in `cpp/src/`). Source of truth for DSP and for the
    monk animation (`cpp/src/monk_view.*`) and XY pad (`cpp/src/xy_pad.cpp`,
    `cpp/src/processor.cpp`).
  - `reference/XYMonk`: xsyetopz/XYMonk, a Rust CLAP/VST3/AU rewrite. Useful for
    artwork geometry (`src/plugin/raw_editor/geometry.rs`), frame selection
    (`animation.rs`) and pad interaction tests.

## Branches

- `main`: this work (formerly `windows-split`), tracks `origin/main`.
- `upstream/main`: upstream macOS-only source.
- `upstream/codex/windows-build`: upstream Windows port (what the v1.0 Windows
  release exe was built from).
- Remotes: `origin` is the user's repo, Merken19/delalalalay-lama (first pushed
  2026-11-03); `upstream` is SunnySkye/DLamaStandalone. Do not push without
  being asked.

## Build & test (Windows)

VS 2022 Community and CMake 3.29 are installed. From the repo root:

```powershell
cmake -S .\Windows -B .\Windows\build -G "Visual Studio 17 2022" -A x64
cmake --build .\Windows\build --config Release --parallel
ctest --test-dir .\Windows\build -C Release --output-on-failure
.\Windows\build\Release\DelayLamaStandalone.exe
.\Windows\build\Release\DelayLamaVoice.exe --help
```

The `'pwsh.exe' is not recognized` lines during the build come from a VS post-build
hook and are harmless. `Windows\package-release.ps1 -Clean` makes the release zip.

Settings persist to `%APPDATA%\DelayLamaStandalone\settings.ini`, or to
`settings.ini` next to the exe if that file exists (portable mode).

## Design decisions

- One process, one synth, one audio engine; windows are unowned top-level windows
  (each has a taskbar button). The settings window is the main window: it always
  opens at launch (its saved `visible` is ignored) and closing it exits (user
  request 2026-11-03). Closing a play window (flag, monk, classic) only hides it;
  hiding the last one brings the settings window forward.
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
  outside the layout stay hidden. Launches restore each play window's saved
  `visible` flag, or show all of the layout's play windows when none was left
  open. Reset all settings returns to the classic display (settings window stays
  open); Reset window layout keeps the mode.
- Classic panel geometry (canvas px, measured on `background.png`): monk frame
  at 22,5 (matches the panel pixels on every edge; the old exe used 23,0),
  flag hit area = the flag window's 98,362 163x79, glide/voice knob caps centred
  at 44.5,471 and 316.5,470 (r 21.5, hit r 27), echo groove y 486, handle
  centre x 121..241, ? button 284,298-327,335. The knob caps are baked into the
  background at their minimum; the window turns that piece of the background
  (280 degree sweep) and redraws each rim glint unturned so lighting stays put.
  No knob/fader bitmaps are copied from monksynth or XYMonk.

## DelayLamaVoice decisions

The project-wide decisions live in Delay Lama Everything's `docs/DECISIONS.md`
(D1-D7, O3, O4); these are the helper's own:

- The standalone's behaviour must not change: the helper only adds functions
  (bridge, `AudioEngine` constructor) or moves code unchanged (`MonkAnimator`,
  `FaceFrames`, `FaceCache`, `load_bitmap`, `enable_per_monitor_dpi_awareness`).
- Console subsystem, so it can be typed into and piped to. The reader thread
  uses `ReadFile` (not CRT stdio, which would deadlock exit) and is never
  joined; a `closing` flag under the session lock keeps it off the choir.
- One bridge handle per monk; each monk sings one note at a time. A stolen monk
  gets the new note before the old one's note-off, so it glides (mono legato).
- Levels: velocity, CC7 and CC11 each on the GM curve (squared), times `gain`,
  into `dl_set_level`. Default volume 0.09, about 16 dB below the standalone,
  for choir headroom (the standalone already peaks near full scale with one
  monk). The user ear-checked it (2026-11-03).
- A monk without a note keeps the level it had at its note-off and may only
  get quieter until its next note-on (`Choir::update_levels`). After a
  note-off the DSP still plays the last grains (up to 20 ms, longer at low
  pitches); a gain or CC11 raised before the next note used to replay them at
  full level, a blip of the old note before the new one (the "laser" bass
  onsets in the phase 4a listening test, 2026-11-04). Side effect: a note
  after a rest now ramps in through the DSP's 5 ms gain smoothing (first 5 ms
  about 7 dB softer in the c01 bass script).
- Only CC7, 11, 64, 120, 121 and 123 act; CC1/5/12/13 are not routed to the
  DSP (in MIDI files they mean other things). Every CC value and the program
  are stored for the imitation profiles later.
- Choir idle offsets (`MonkAnimator::kChoirIdleOffsetsMs`) were searched so no
  two monks ever show the blink frame together; a test checks it.
- Protocol 1 (phase 4b): `set articulator 1` switches to profile mode, where
  the articulator owns vowel, level, breath, bend, vibrato and voice size
  every 1 ms (the choir renders 1 ms sub-blocks), the DSP envelope and the
  synths' echo are off, and notes sound at their written pitch (+12 to the
  DSP note). Plain mode is bit-for-bit phase 1. Choir state is behind one
  recursive mutex (apply, render, window queries).
- DSP addition `monk_synth_set_vibrato_seed` (new function only, never called
  by the standalone); bridge additions `dl_set_vibrato_seed`,
  `dl_voice_note_on`/`off` (one note per handle, no stack fallback).
- `onset.mask`: the attack lasts at least mask x the onset vowel move.
  Default 0 (off) since the user's ear check: it removed none of the
  "laser" onsets. Profiles start steady instead (Delay Lama Everything D33).
- Round 3 (Delay Lama Everything D33): DSP addition
  `monk_synth_set_formant_bandwidth` (rebuilds each voice's `exp_decay`
  table; scale 1 is the exact default), bridge `dl_set_formant_bandwidth`,
  articulator keys `onset.bandwidth` (formants wider while the onset vowel
  moves, level times bandwidth^0.8 to make up) and `onset.after_rest` (the
  move only after the channel rested). Profile mode raises the DSP volume
  up to 4x and lowers the articulator's levels by the same factor
  (`Choir::level_headroom`, D35), so levels above 1 work; plain mode is
  unchanged.

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
   The macOS build uses the same files; its C tests run in `macos.yml`.
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
   Windows exe still has the bug; the macOS bridge was fixed the same way.
7. **macOS SysEx hang** (fixed 2026-11-03): the original macOS MIDI parser
   never advanced past a data byte with no running status, so any SysEx
   message spun the CoreMIDI thread forever (reproduced with the stub build).
   Realtime bytes (>= 0xF8) now keep the running status, as the MIDI spec says.

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
- [x] Windows CI runs every CTest test (was only the two Windows smoke tests)
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
- [x] Monk icon for the exe and every window (user request 2026-11-03): 9 sizes
      16..256, checked by extracting it from the built exe at 16/32/256
- [ ] User check: monk icon in Explorer, taskbar, title bars and Alt+Tab
- [x] Settings window always opens at launch and closing it exits the program;
      closing a play window only hides it (user request 2026-11-03)
- [ ] User check: closing the settings window (X, Alt+F4) exits; closing the
      classic/flag/monk window hides it and brings settings forward
- [x] GitHub prep (user request 2026-11-03): shared bridge header; macOS bridge
      fixes (laser, SysEx hang, note range); Swift port of every Windows window
      and setting (UNTESTED, never compiled); `delay_lama_macos_bridge_logic`
      CTest (CTest 5/5, /W4 clean); `run_macos_tests.sh` (dry-run here with a
      clang shim); `macos.yml`; README with untested-macOS disclaimer,
      CHANGELOG, CONTRIBUTING, `.gitattributes`, issue/PR templates
- [ ] First `macos.yml` run after the push: fix whatever the Swift compiler reports
- [ ] Mac user check of the macOS port
- [ ] Optional: macOS app icon (`.icns` from the same monk frame)
- [x] `DelayLamaVoice.exe` helper on branch `voice-helper` (Delay Lama Everything
      phase 1, 2026-11-03): no-MIDI bridge variant with bend/level/vowel setters,
      monk frame and idle logic split out of `App`, stdin protocol 0, choir of
      1-8 monks with voice allocator, one mixed waveOut stream, monks side by
      side, window options, `--render`. /W4 clean, CTest 9/9; live run
      screenshot-checked (3 monks singing a chord, 1 idle)
- [x] User ear check of the helper (typed commands and the WAVs in Delay Lama
      Everything's `renders/phase1/`), and of its default loudness: "sounds
      right" (2026-11-03)
- [x] Onset glitches from the phase 4a listening test (2026-11-04): a released
      monk's tail no longer gets louder after its note-off; regression check
      in `voice_render_checks.cpp` (tail before the next note at -44 dB, was
      -10 dB). CTest 9/9. The other glitches come from the phase 4a scripts and
      profiles, not the helper (Delay Lama Everything `renders/phase1-fix/`)
- [x] User A/B of `renders/phase1-fix/` (2026-11-04): the remaining
      glitches came from onset vowel moves, diagnosed in phase 4b
- [x] Articulator, protocol 1, vibrato seed (Delay Lama Everything phase
      4b, 2026-11-04): /W4 clean, CTest 9/9 with `articulator_*` checks in
      `voice_render_checks.cpp`
- [x] User ear checks, rounds 1 and 2 (Delay Lama Everything
      `renders/phase4b/`): steady onsets; `onset.mask` off by default
- [x] Round 3 keys (2026-11-04): formant bandwidth DSP addition,
      `onset.bandwidth`, `onset.after_rest`, profile-mode level headroom;
      CTest 9/9 with `articulator_bandwidth_and_rest`
- [x] Commit the phase 1 fix, the articulator and the round 3 keys on
      `voice-helper` as `5fde0c1` (user asked, 2026-11-04; not pushed)
- [ ] User ear check of Delay Lama Everything `renders/phase4b/round3/`;
      then settle the `onset.bandwidth` and `onset.after_rest` defaults
