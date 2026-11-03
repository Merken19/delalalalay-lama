## What and why

<!-- What does this change, and why? Link the issue if there is one. -->

## Testing

- [ ] Windows: the build has no `/W4` warnings and `ctest --test-dir .\Windows\build -C Release` passes
- [ ] Tried it in the app (say how: mouse, touchscreen, keyboard, MIDI, display mode)
- [ ] macOS, if you have a Mac: `./build.sh` and `./Tests/run_macos_tests.sh` pass

## Checklist

- [ ] Bridge changes are in both `Sources/standalone_bridge.c` and `Windows/standalone_bridge_win32.c`, with a check in `Tests/bridge_checks.h`
- [ ] User interface changes are ported to the other platform, or noted here as platform-specific
- [ ] `CHANGELOG.md` updated for user-visible changes
