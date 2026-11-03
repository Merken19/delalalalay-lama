# Changelog

## Unreleased

### Windows

- New `DelayLamaVoice.exe` helper (for Delay Lama Everything): a console
  program that sings one MIDI channel read as text commands from stdin, with a
  choir of up to 8 monks mixed into one audio stream and shown side by side in
  one window. Real pitch bend, velocity, volume, expression and sustain pedal;
  `--render` writes a command script to a WAV file. It opens no MIDI input.
  The standalone is unchanged; its monk drawing and idle animation moved to
  shared code (`monk_art.*`, `monk_animation.h`) without changing behaviour.
- The flag, the monk and the settings are now separate windows that can be
  moved and resized freely, with optional title bars, always on top and keep
  aspect ratio. A **classic display** shows the original panel (monk, flag,
  glide and voice knobs, echo slider, ? button) in one resizable window and is
  the default.
- New settings window with Voice, Echo / Envelope, Flag and Windows tabs,
  covering every synth parameter, the flag's pitch range (default C3 to C5),
  semitone snap, note grid and crosshair, and the exact position and size of
  each window. **Reset all settings...** restores every default.
- The settings window is the main window: it always opens at launch and
  closing it exits. Closing the flag, monk or classic window only hides it.
- Settings are saved to `%APPDATA%\DelayLamaStandalone\settings.ini`, or next
  to the exe in portable mode.
- The exe and its windows use a singing-monk icon (`Windows/delay_lama.ico`,
  made from the artwork by `Windows/make-icon.ps1`).
- Per-monitor DPI awareness; touchscreens play the flag through Windows'
  touch-to-mouse handling.
- Pixel-sharp scaling by default, smooth scaling optional.
- Fixed: pressing the flag no longer starts with a high "laser" sweep down to
  the flag's pitch, and releasing a MIDI key while holding the flag returns to
  the flag instead of sliding up.
- Fixed: the monk frame no longer shows the 3-pixel border of the next frame,
  and the idle animation now matches MonkSynth (blinks, then a mouth shuffle).
- Fixed: note numbers above 127 passed to the bridge no longer write past its
  held-note table.

### macOS (not built or tested)

- Ported the Windows features above: separate flag, monk and settings windows,
  the classic display with turning knobs and echo handle, the settings window
  as the main window, the right-click menu, and settings saved in user
  defaults. The monk app icon is not ported yet.
- Fixed: a MIDI SysEx message, or any data byte without a status byte, made
  the CoreMIDI thread spin forever, which stopped MIDI input and used a full
  CPU core. MIDI clock bytes no longer cancel running status.
- Fixed: the flag "laser" sweep on press and the slide up after releasing a
  MIDI key over the held flag (same fixes as Windows).
- The artwork is cut out by pixel, the monk frame is cropped to its 311 pixels
  and the idle animation matches MonkSynth.

### Shared

- DSP hardening ported from MonkSynth: smoothed output and unison gain, any
  block length, NaN-safe parameter clamps, safe sample rates and pitches, and
  a fix for the delay line's read position.
- Both platform bridges implement the same API (`Sources/standalone_bridge.h`)
  with the settings-window setters, and run the same checks
  (`Tests/bridge_checks.h`).
- New tests: the macOS bridge and its MIDI parser are built and tested on
  Windows against stub Apple headers; `Tests/run_macos_tests.sh` runs the C
  tests on a Mac; GitHub Actions builds the macOS app, and the Windows
  workflow now runs every test.
- Repository files: README, CONTRIBUTING, CHANGELOG, issue and pull request
  templates, `.gitattributes`.

## 1.0

- Standalone macOS app (AppKit, universal) and native Windows port
  (Win32/GDI+, WinMM) of the classic single-window panel, from
  [SunnySkye/DLamaStandalone](https://github.com/SunnySkye/DLamaStandalone).
