import AppKit

/// The application: owns the synth-facing state shared by every window, like
/// App in Windows/delay_lama_win32.cpp. The settings window is the main window:
/// it always opens at launch and closing it quits. Closing a play window (flag,
/// monk, classic) only hides it.
@main
@MainActor
final class AppController: NSObject, NSApplicationDelegate, NSWindowDelegate, NSMenuItemValidation {
    static func main() {
        let application = NSApplication.shared
        let controller = AppController()
        application.delegate = controller
        application.setActivationPolicy(.regular)
        // NSApplication holds its delegate weakly.
        withExtendedLifetime(controller) {
            application.run()
        }
    }

    // Idle animation from monksynth's MonkView: hold frame 5 with two blinks
    // on frame 2, then loop a 24-step mouth shuffle until the next note.
    private static let idleTick: TimeInterval = 0.1
    private static let idleHoldTicks = 48
    private static let idleHoldFrame = 5
    private static let idleBlinkFrame = 2
    private static let idleShuffle = [5, 3, 4, 3, 2, 1, 0, 1, 5, 3, 4, 3, 5, 1, 0, 1, 2, 3, 4, 3, 5, 1, 0, 1]
    private static let idleShuffleTicksPerStep = 2
    private static let animationInterval: TimeInterval = 0.03
    private static let playWindows: [WindowID] = [.flag, .monk, .classic]

    private static let keyMap: [String: UInt8] = [
        "a": 48, "w": 49, "s": 50, "e": 51, "d": 52, "f": 53,
        "t": 54, "g": 55, "y": 56, "h": 57, "u": 58, "j": 59,
        "k": 60, "o": 61, "l": 62,
    ]

    var settings = Settings.defaults()
    private(set) var artwork: Artwork!
    private var audio: AudioController!
    private var windows: [WindowID: NSWindow] = [:]
    private var flagView: FlagView!
    private var monkView: MonkView!
    private var classicView: ClassicView!
    private var settingsController: SettingsWindowController!
    private var animationTimer: Timer?
    private var wasSinging = false
    private var idleStart: TimeInterval = 0
    private var ready = false
    private var applyingWindow = false
    private var quitting = false
    private var heldKeys: [String: UInt8] = [:]

    var synth: OpaquePointer { audio.handle }

    // MARK: Lifecycle

    func applicationDidFinishLaunching(_ notification: Notification) {
        buildMainMenu()
        guard let resources = Bundle.main.resourceURL, let artwork = Artwork(resourceURL: resources) else {
            fail("Delay Lama artwork is missing",
                 "background.png and faces.png were not found in the application bundle.")
            return
        }
        self.artwork = artwork
        do {
            audio = try AudioController()
        } catch {
            fail("Delay Lama could not start audio", error.localizedDescription)
            return
        }

        settings = Settings.load()
        ensureOnScreen()
        createWindows()
        applySound()
        applyFlag()
        for id in WindowID.allCases {
            applyWindow(id)
        }
        // The settings window always opens. The play windows of the display
        // keep their saved state unless none of them was left open.
        settings[.settings].visible = true
        if !anyPlayWindowVisible() {
            for id in AppController.playWindows where inLayout(id) {
                settings[id].visible = true
            }
        }

        ready = true
        idleStart = ProcessInfo.processInfo.systemUptime
        for id in WindowID.allCases where inLayout(id) && settings[id].visible {
            windows[id]?.orderFront(nil)
        }
        let focus = [WindowID.classic, .flag, .monk].first(where: { inLayout($0) && settings[$0].visible })
        windows[focus ?? .settings]?.makeKeyAndOrderFront(nil)
        settingsController.refresh()
        startAnimation()
        NSApp.activate(ignoringOtherApps: true)
    }

    func applicationWillTerminate(_ notification: Notification) {
        // Nothing to save when start-up failed before the settings were read.
        guard ready, !quitting else { return }
        save()
        quitting = true
        finishDrags()
        releaseKeys()
        dl_all_notes_off(synth)
        animationTimer?.invalidate()
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { false }

    func applicationSupportsSecureRestorableState(_ app: NSApplication) -> Bool { true }

    private func fail(_ message: String, _ detail: String) {
        let alert = NSAlert()
        alert.messageText = message
        alert.informativeText = detail
        alert.alertStyle = .critical
        alert.runModal()
        NSApp.terminate(nil)
    }

    func save() {
        settings.save()
    }

    // MARK: Windows

    private func createWindows() {
        flagView = FlagView(app: self, id: .flag)
        monkView = MonkView(app: self, id: .monk)
        classicView = ClassicView(app: self, id: .classic)
        windows[.flag] = makeArtworkWindow("Delay Lama Flag", flagView)
        windows[.monk] = makeArtworkWindow("Delay Lama Monk", monkView)
        windows[.classic] = makeArtworkWindow("Delay Lama Standalone", classicView)
        settingsController = SettingsWindowController(app: self)
        settingsController.window.delegate = self
        windows[.settings] = settingsController.window
    }

    private func makeArtworkWindow(_ title: String, _ view: NSView) -> NSWindow {
        let window = ArtworkWindow(contentRect: NSRect(x: 0, y: 0, width: 320, height: 240),
                                   styleMask: [.titled, .closable, .miniaturizable, .resizable],
                                   backing: .buffered, defer: false)
        window.title = title
        window.contentView = view
        window.isReleasedWhenClosed = false
        window.backgroundColor = .black
        window.tabbingMode = .disallowed
        window.collectionBehavior = [.fullScreenNone]
        window.delegate = self
        return window
    }

    private func windowID(of window: NSWindow) -> WindowID? {
        windows.first(where: { $0.value === window })?.key
    }

    private func view(for id: WindowID) -> ArtworkView? {
        switch id {
        case .flag: return flagView
        case .monk: return monkView
        case .classic: return classicView
        case .settings: return nil
        }
    }

    /// Whether a window belongs to the current display (flag + monk, or classic).
    func inLayout(_ id: WindowID) -> Bool {
        switch id {
        case .flag, .monk: return !settings.classic
        case .classic: return settings.classic
        case .settings: return true
        }
    }

    /// Top edge of the main display, the origin of the stored y coordinates.
    private var desktopTop: CGFloat {
        NSScreen.screens.first?.frame.maxY ?? 0
    }

    private func contentRect(for geometry: WindowSettings) -> NSRect {
        NSRect(x: geometry.x, y: desktopTop - geometry.y - geometry.height,
               width: geometry.width, height: geometry.height)
    }

    private func applyFrameStyle(_ window: NSWindow, _ id: WindowID, titleBar: Bool) {
        let style: NSWindow.StyleMask
        if titleBar {
            style = [.titled, .closable, .miniaturizable, .resizable]
        } else if id == .flag {
            // Not movable or resizable with the mouse, so clicks at the edges
            // still play. Arrange it with the title bar on or with X/Y/W/H.
            style = [.borderless]
        } else {
            // A hidden title bar keeps edge resizing; the picture moves the window.
            style = [.titled, .resizable, .fullSizeContentView]
        }
        if window.styleMask != style {
            window.styleMask = style
        }
        window.titlebarAppearsTransparent = !titleBar
        window.titleVisibility = titleBar ? .visible : .hidden
        for button in [NSWindow.ButtonType.closeButton, .miniaturizeButton, .zoomButton] {
            window.standardWindowButton(button)?.isHidden = !titleBar
        }
        window.isMovable = titleBar || id != .flag
        window.isMovableByWindowBackground = !titleBar && id == .monk
        window.hasShadow = true
    }

    /// Places a window so its content area lands exactly on the stored geometry.
    func applyWindow(_ id: WindowID) {
        guard let window = windows[id] else { return }
        var geometry = settings[id]
        if id == .settings {
            geometry.width = SettingsWindowController.contentSize.width
            geometry.height = SettingsWindowController.contentSize.height
        }

        applyingWindow = true
        if window.isMiniaturized {
            window.deminiaturize(nil)
        }
        if id != .settings {
            applyFrameStyle(window, id, titleBar: geometry.titleBar)
        }
        window.setFrame(window.frameRect(forContentRect: contentRect(for: geometry)), display: true)
        window.level = geometry.topmost ? .floating : .normal
        applyingWindow = false

        geometryChanged(id)
        view(for: id)?.needsDisplay = true
    }

    func setWindowGeometry(_ id: WindowID, x: CGFloat, y: CGFloat, width: CGFloat, height: CGFloat) {
        settings[id].x = x
        settings[id].y = y
        settings[id].width = max(16, width)
        settings[id].height = max(16, height)
        applyWindow(id)
    }

    func geometryChanged(_ id: WindowID) {
        guard ready, !applyingWindow, !quitting, let window = windows[id], !window.isMiniaturized else { return }
        let rect = window.contentRect(forFrameRect: window.frame)
        settings[id].x = rect.minX
        settings[id].y = desktopTop - rect.maxY
        settings[id].width = rect.width
        settings[id].height = rect.height
        settingsController.refreshGeometry(id)
    }

    /// A window outside the current display only remembers the choice.
    func showWindow(_ id: WindowID, _ show: Bool, activate: Bool) {
        guard let window = windows[id] else { return }
        settings[id].visible = show
        if show && inLayout(id) {
            if window.isMiniaturized {
                window.deminiaturize(nil)
            }
            if activate {
                window.makeKeyAndOrderFront(nil)
                NSApp.activate(ignoringOtherApps: true)
            } else {
                window.orderFront(nil)
            }
        } else if !show {
            if id == .flag {
                flagView.finishDrag()
            }
            if id == .classic {
                classicView.finishDrag()
            }
            window.orderOut(nil)
        }
        settingsController.refresh()
    }

    private func anyPlayWindowVisible() -> Bool {
        AppController.playWindows.contains(where: { inLayout($0) && settings[$0].visible })
    }

    /// Closing the settings window quits. Closing a play window only hides it;
    /// when the last one goes, the settings window comes forward to show them again.
    func closeWindow(_ id: WindowID) {
        if id == .settings {
            NSApp.terminate(nil)
            return
        }
        showWindow(id, false, activate: false)
        if !anyPlayWindowVisible() {
            showWindow(.settings, true, activate: true)
        }
    }

    private func finishDrags() {
        flagView?.finishDrag()
        classicView?.finishDrag()
    }

    /// Shows the windows of the current display that are meant to be visible
    /// and hides the rest; the settings window is left alone.
    private func updateLayoutVisibility() {
        let key = NSApp.keyWindow
        var lostKey = false
        for id in AppController.playWindows {
            guard let window = windows[id] else { continue }
            let show = inLayout(id) && settings[id].visible
            if show && !window.isVisible {
                window.orderFront(nil)
            } else if !show && window.isVisible {
                lostKey = lostKey || window === key
                window.orderOut(nil)
            }
        }
        // Hand the focus to the new display when the focused window disappeared.
        if lostKey {
            let focus: WindowID = settings.classic ? .classic : (settings[.flag].visible ? .flag : .monk)
            windows[focus]?.makeKeyAndOrderFront(nil)
        }
    }

    /// Switching to the classic display shows the classic window; switching to
    /// separate windows shows all of them (flag, monk and settings).
    func setClassic(_ classic: Bool) {
        guard settings.classic != classic else { return }
        finishDrags()
        settings.classic = classic
        if classic {
            settings[.classic].visible = true
        } else {
            settings[.flag].visible = true
            settings[.monk].visible = true
        }
        updateLayoutVisibility()
        if !classic {
            showWindow(.settings, true, activate: false)
        }
        settingsController.refresh()
    }

    private func setDefaultGeometry(_ target: inout Settings) {
        let visible = NSScreen.screens.first?.visibleFrame ?? NSRect(x: 0, y: 0, width: 1280, height: 800)
        let left = visible.minX + 48
        let top = desktopTop - visible.maxY + 56

        target[.monk].x = left
        target[.monk].y = top
        target[.monk].width = CGFloat(Canvas.faceWidth)
        target[.monk].height = CGFloat(Canvas.faceHeight)

        target[.flag].x = left
        target[.flag].y = top + CGFloat(Canvas.faceHeight) + 72
        target[.flag].width = Canvas.flag.width * 3
        target[.flag].height = Canvas.flag.height * 3

        // The classic panel opens at its original size where the monk would be.
        target[.classic].x = left
        target[.classic].y = top
        target[.classic].width = Canvas.width
        target[.classic].height = Canvas.height

        let widest = max(target[.monk].width, target[.flag].width, target[.classic].width)
        target[.settings].x = left + widest + 56
        target[.settings].y = top
        target[.settings].width = SettingsWindowController.contentSize.width
        target[.settings].height = SettingsWindowController.contentSize.height
    }

    /// Falls back to the default spot for any window that would open mostly
    /// off screen (for example after a display was unplugged).
    private func ensureOnScreen() {
        var fallback = Settings.defaults()
        setDefaultGeometry(&fallback)
        for id in WindowID.allCases {
            let geometry = settings[id]
            var usable = geometry.width > 0 && geometry.height > 0
            if usable {
                let rect = contentRect(for: geometry)
                usable = NSScreen.screens.contains(where: { screen in
                    let visible = rect.intersection(screen.visibleFrame)
                    return !visible.isNull && visible.width >= min(geometry.width, 64) &&
                        visible.height >= min(geometry.height, 32)
                })
            }
            if !usable {
                settings[id].x = fallback[id].x
                settings[id].y = fallback[id].y
                settings[id].width = fallback[id].width
                settings[id].height = fallback[id].height
            }
        }
    }

    // MARK: Window delegate

    func windowShouldClose(_ sender: NSWindow) -> Bool {
        if let id = windowID(of: sender) {
            closeWindow(id)
        }
        return false
    }

    func windowDidMove(_ notification: Notification) {
        trackGeometry(notification)
    }

    func windowDidResize(_ notification: Notification) {
        trackGeometry(notification)
    }

    func windowDidResignKey(_ notification: Notification) {
        // Key-up events go to the newly focused window, so release held keys.
        releaseKeys()
    }

    private func trackGeometry(_ notification: Notification) {
        if let window = notification.object as? NSWindow, let id = windowID(of: window) {
            geometryChanged(id)
        }
    }

    // MARK: Sound

    func applySound() {
        let sound = settings.sound
        dl_set_volume(synth, sound.volume)
        dl_set_glide(synth, sound.glide)
        dl_set_voice(synth, sound.voice)
        dl_set_vibrato(synth, sound.vibrato)
        dl_set_vibrato_rate(synth, sound.vibratoRate)
        dl_set_breath(synth, sound.breath)
        dl_set_unison(synth, Int32(sound.unison), sound.unisonDetune, sound.unisonSpread)
        dl_set_delay(synth, sound.delayMix)
        dl_set_delay_time(synth, sound.delayTime)
        dl_set_delay_feedback(synth, sound.delayFeedback)
        dl_set_envelope(synth, sound.attack, sound.decay, sound.sustain, sound.release)
        // The classic panel shows glide, voice and echo.
        classicView?.needsDisplay = true
    }

    func applyFlag() {
        let flag = settings.flag
        dl_set_xy_range(synth, Float(flag.lowNote), Float(flag.highNote), flag.snap ? 1 : 0)
        flagView?.needsDisplay = true
        classicView?.needsDisplay = true
    }

    func applyScaling() {
        flagView?.needsDisplay = true
        monkView?.needsDisplay = true
        classicView?.needsDisplay = true
    }

    func refreshSettingsWindow() {
        settingsController.refresh()
    }

    func resetSound() {
        settings.sound = SoundSettings()
        applySound()
        settingsController.refresh()
    }

    func resetLayout() {
        var defaults = Settings.defaults()
        setDefaultGeometry(&defaults)
        for id in WindowID.allCases {
            settings[id] = defaults[id]
            applyWindow(id)
            showWindow(id, true, activate: false)
        }
        settingsController.refresh()
    }

    /// Back to a fresh install: the classic display with every sound, flag and
    /// window option at its default. The settings window stays open.
    func resetAll() {
        let alert = NSAlert()
        alert.messageText = "Reset every setting to its default?"
        alert.informativeText = "This restores the sound, the flag options and the classic display, "
            + "with every window at its default place."
        alert.alertStyle = .warning
        alert.addButton(withTitle: "Cancel")
        alert.addButton(withTitle: "Reset")
        guard alert.runModal() == .alertSecondButtonReturn else { return }

        finishDrags()
        var defaults = Settings.defaults()
        setDefaultGeometry(&defaults)
        settings = defaults
        applySound()
        applyFlag()
        applyScaling()
        for id in WindowID.allCases {
            applyWindow(id)
        }
        updateLayoutVisibility()
        showWindow(.settings, true, activate: false)
        settingsController.refresh()
    }

    // MARK: Monk animation

    /// Singing maps the vowel across frames 6..29; otherwise the idle sequence
    /// runs from the moment the last note ended.
    func activeFrame() -> Int {
        let now = ProcessInfo.processInfo.systemUptime
        if dl_is_active(synth) != 0 {
            wasSinging = true
            let frame = 6 + Int((clamp01(dl_get_vowel(synth)) * 23).rounded())
            return min(29, max(6, frame))
        }
        if wasSinging {
            wasSinging = false
            idleStart = now
        }

        let ticks = Int(max(0, now - idleStart) / AppController.idleTick)
        if ticks < AppController.idleHoldTicks {
            let blink = (14..<17).contains(ticks) || (31..<34).contains(ticks)
            return blink ? AppController.idleBlinkFrame : AppController.idleHoldFrame
        }
        let step = (ticks - AppController.idleHoldTicks) / AppController.idleShuffleTicksPerStep
        return AppController.idleShuffle[step % AppController.idleShuffle.count]
    }

    private func startAnimation() {
        let timer = Timer(timeInterval: AppController.animationInterval, target: self,
                          selector: #selector(animationTick(_:)), userInfo: nil, repeats: true)
        RunLoop.main.add(timer, forMode: .common)
        animationTimer = timer
    }

    @objc private func animationTick(_ timer: Timer) {
        let frame = activeFrame()
        if monkView.window?.isVisible == true && frame != monkView.shownFrame {
            monkView.needsDisplay = true
        }
        if classicView.window?.isVisible == true && frame != classicView.shownFrame {
            classicView.needsDisplay = true
        }
    }

    // MARK: Keyboard

    /// Plays A W S E D F T G Y H U J K O L; returns false for other keys.
    func keyDown(_ event: NSEvent) -> Bool {
        guard event.modifierFlags.intersection([.command, .control, .option]).isEmpty,
              let key = event.charactersIgnoringModifiers?.lowercased(),
              let note = AppController.keyMap[key] else {
            return false
        }
        if !event.isARepeat && heldKeys[key] == nil {
            heldKeys[key] = note
            dl_note_on(synth, note, 1)
        }
        return true
    }

    func keyUp(_ event: NSEvent) -> Bool {
        guard let key = event.charactersIgnoringModifiers?.lowercased(),
              AppController.keyMap[key] != nil else {
            return false
        }
        if let note = heldKeys.removeValue(forKey: key) {
            dl_note_off(synth, note)
        }
        return true
    }

    func releaseKeys() {
        guard audio != nil else { return }
        for note in heldKeys.values {
            dl_note_off(synth, note)
        }
        heldKeys.removeAll()
    }

    // MARK: About

    func presentAbout() {
        releaseKeys()
        let alert = NSAlert()
        alert.messageText = "Delay Lama Standalone"
        alert.informativeText = """
            Drag across the Tibetan flag to sing: horizontal controls pitch and vertical controls the vowel.

            The flag, monk and settings are separate windows. Move and resize them freely, or set exact \
            positions on the Windows tab of the settings. The classic display puts the original panel, \
            with its glide and voice knobs and echo slider, in one resizable window instead. Right-click \
            any window for quick options.

            The settings window always opens with Delay Lama, and closing it quits. Closing any other \
            window only hides it; tick Show on the Windows tab to bring it back.

            Computer keyboard: A W S E D F T G Y H U J K O L

            MIDI: notes, pitch wheel → vowel, CC1 vibrato, CC5 glide, CC7 volume, CC12 delay and CC13 \
            voice. Connected MIDI sources: \(dl_midi_source_count(synth)).

            Vocal DSP derived from the MIT-licensed MonkSynth clean-room project. Classic artwork and \
            manual were recovered from the 2004 freeware release.
            """
        if let image = NSImage(contentsOf: artwork.resourceURL.appendingPathComponent("about.png")) {
            image.size = NSSize(width: 126.5, height: 137.5)
            alert.icon = image
        }
        alert.addButton(withTitle: "OK")
        alert.addButton(withTitle: "Open Original Manual")
        if alert.runModal() == .alertSecondButtonReturn {
            _ = NSWorkspace.shared.open(artwork.resourceURL.appendingPathComponent("DelayLama-Original-Manual.pdf"))
        }
    }

    // MARK: Menus

    @objc func showAbout(_ sender: Any?) {
        presentAbout()
    }

    @objc func showSettings(_ sender: Any?) {
        showWindow(.settings, true, activate: true)
    }

    @objc func closeKeyWindow(_ sender: Any?) {
        guard let window = NSApp.keyWindow else { return }
        if let id = windowID(of: window) {
            closeWindow(id)
        } else {
            window.performClose(sender)
        }
    }

    @objc private func toggleClassic(_ sender: Any?) {
        setClassic(!settings.classic)
    }

    @objc private func toggleFlagWindow(_ sender: Any?) {
        toggleVisible(.flag)
    }

    @objc private func toggleMonkWindow(_ sender: Any?) {
        toggleVisible(.monk)
    }

    private func toggleVisible(_ id: WindowID) {
        if settings[id].visible {
            closeWindow(id)
        } else {
            showWindow(id, true, activate: true)
        }
    }

    @objc private func toggleTitleBar(_ sender: NSMenuItem) {
        guard let id = WindowID(rawValue: sender.tag) else { return }
        settings[id].titleBar.toggle()
        applyWindow(id)
        settingsController.refresh()
    }

    @objc private func toggleTopmost(_ sender: NSMenuItem) {
        guard let id = WindowID(rawValue: sender.tag) else { return }
        settings[id].topmost.toggle()
        applyWindow(id)
        settingsController.refresh()
    }

    @objc private func toggleKeepAspect(_ sender: NSMenuItem) {
        guard let id = WindowID(rawValue: sender.tag) else { return }
        settings[id].keepAspect.toggle()
        applyWindow(id)
        settingsController.refresh()
    }

    func validateMenuItem(_ menuItem: NSMenuItem) -> Bool {
        switch menuItem.action {
        case #selector(toggleClassic(_:)):
            menuItem.state = settings.classic ? .on : .off
        case #selector(toggleFlagWindow(_:)):
            menuItem.state = settings[.flag].visible ? .on : .off
            return ready && !settings.classic
        case #selector(toggleMonkWindow(_:)):
            menuItem.state = settings[.monk].visible ? .on : .off
            return ready && !settings.classic
        default:
            break
        }
        return ready
    }

    /// The right-click menu of the flag, monk and classic windows.
    func contextMenu(for id: WindowID) -> NSMenu {
        let menu = NSMenu()
        menu.autoenablesItems = false
        addItem(menu, "Settings…", #selector(showSettings(_:)))
        addItem(menu, "About / Manual…", #selector(showAbout(_:)))
        menu.addItem(.separator())
        addItem(menu, "Classic Display (One Window)", #selector(toggleClassic(_:)), on: settings.classic)
        if !settings.classic {
            addItem(menu, "Show Flag Window", #selector(toggleFlagWindow(_:)), on: settings[.flag].visible)
            addItem(menu, "Show Monk Window", #selector(toggleMonkWindow(_:)), on: settings[.monk].visible)
        }
        menu.addItem(.separator())
        addItem(menu, "Title Bar", #selector(toggleTitleBar(_:)), on: settings[id].titleBar, tag: id.rawValue)
        addItem(menu, "Always on Top", #selector(toggleTopmost(_:)), on: settings[id].topmost, tag: id.rawValue)
        addItem(menu, "Keep Aspect Ratio", #selector(toggleKeepAspect(_:)), on: settings[id].keepAspect,
                tag: id.rawValue)
        menu.addItem(.separator())
        let quit = addItem(menu, "Quit Delay Lama", #selector(NSApplication.terminate(_:)))
        quit.target = NSApp
        return menu
    }

    @discardableResult
    private func addItem(_ menu: NSMenu, _ title: String, _ action: Selector, on: Bool = false,
                         tag: Int = 0) -> NSMenuItem {
        let item = NSMenuItem(title: title, action: action, keyEquivalent: "")
        item.target = self
        item.state = on ? .on : .off
        item.tag = tag
        menu.addItem(item)
        return item
    }

    private func buildMainMenu() {
        let mainMenu = NSMenu()

        let appMenu = NSMenu(title: "Delay Lama Standalone")
        appMenu.addItem(withTitle: "About Delay Lama Standalone", action: #selector(showAbout(_:)), keyEquivalent: "")
        appMenu.addItem(.separator())
        appMenu.addItem(withTitle: "Settings…", action: #selector(showSettings(_:)), keyEquivalent: ",")
        appMenu.addItem(.separator())
        appMenu.addItem(withTitle: "Hide Delay Lama Standalone", action: #selector(NSApplication.hide(_:)),
                        keyEquivalent: "h")
        appMenu.addItem(withTitle: "Quit Delay Lama Standalone", action: #selector(NSApplication.terminate(_:)),
                        keyEquivalent: "q")
        addSubmenu(appMenu, to: mainMenu)

        // Copy and paste for the X/Y/W/H fields of the settings window.
        let editMenu = NSMenu(title: "Edit")
        editMenu.addItem(withTitle: "Undo", action: Selector(("undo:")), keyEquivalent: "z")
        editMenu.addItem(withTitle: "Redo", action: Selector(("redo:")), keyEquivalent: "Z")
        editMenu.addItem(.separator())
        editMenu.addItem(withTitle: "Cut", action: #selector(NSText.cut(_:)), keyEquivalent: "x")
        editMenu.addItem(withTitle: "Copy", action: #selector(NSText.copy(_:)), keyEquivalent: "c")
        editMenu.addItem(withTitle: "Paste", action: #selector(NSText.paste(_:)), keyEquivalent: "v")
        editMenu.addItem(withTitle: "Select All", action: #selector(NSText.selectAll(_:)), keyEquivalent: "a")
        addSubmenu(editMenu, to: mainMenu)

        let viewMenu = NSMenu(title: "View")
        viewMenu.addItem(withTitle: "Classic Display (One Window)", action: #selector(toggleClassic(_:)),
                         keyEquivalent: "")
        viewMenu.addItem(withTitle: "Show Flag Window", action: #selector(toggleFlagWindow(_:)), keyEquivalent: "")
        viewMenu.addItem(withTitle: "Show Monk Window", action: #selector(toggleMonkWindow(_:)), keyEquivalent: "")
        addSubmenu(viewMenu, to: mainMenu)

        let windowMenu = NSMenu(title: "Window")
        windowMenu.addItem(withTitle: "Close Window", action: #selector(closeKeyWindow(_:)), keyEquivalent: "w")
        windowMenu.addItem(withTitle: "Minimize", action: #selector(NSWindow.performMiniaturize(_:)),
                           keyEquivalent: "m")
        addSubmenu(windowMenu, to: mainMenu)
        NSApp.windowsMenu = windowMenu

        let helpMenu = NSMenu(title: "Help")
        helpMenu.addItem(withTitle: "Delay Lama Help", action: #selector(showAbout(_:)), keyEquivalent: "?")
        addSubmenu(helpMenu, to: mainMenu)
        NSApp.helpMenu = helpMenu

        NSApp.mainMenu = mainMenu
    }

    private func addSubmenu(_ submenu: NSMenu, to menu: NSMenu) {
        let item = NSMenuItem(title: submenu.title, action: nil, keyEquivalent: "")
        item.submenu = submenu
        menu.addItem(item)
    }
}
