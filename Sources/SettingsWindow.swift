import AppKit

private let flagHelp = """
    Drag anywhere on the flag: left/right sets the pitch, up/down sets the vowel.

    Computer keyboard (flag, monk or classic window in front): A W S E D F T G Y H U J K O L. \
    MIDI keyboards are picked up automatically at start-up.
    """

private let classicHelp = """
    The classic window is the original Delay Lama panel in one resizable window: play the flag, \
    turn the left knob for glide and the right knob for the voice, slide the handle under the \
    flag for echo, and press ? for help.
    """

private let windowsHelp = """
    X / Y / W / H set each window's inner area in points, measured from the top-left corner of \
    the main display (Return or Apply). Without a title bar the flag cannot be moved, so every \
    click plays; the borderless monk and classic windows move by dragging the picture and resize \
    at their edges. Right-click (or Control-click) any window for quick options.
    """

private func percentText(_ value: Int) -> String { "\(value)%" }
private func millisecondsText(_ value: Int) -> String { "\(value) ms" }

private struct SliderBinding {
    let slider: NSSlider
    let value: NSTextField
    let get: () -> Int
    let set: (Int) -> Void
    let format: (Int) -> String
}

private struct CheckBinding {
    let button: NSButton
    let get: () -> Bool
    let set: (Bool) -> Void
}

private struct GeometryGroup {
    let id: WindowID
    let fields: [NSTextField]
}

/// Tabs for every synth parameter plus the window layout, like the Windows
/// settings window (Windows/settings_window.cpp).
@MainActor
final class SettingsWindowController: NSObject {
    static let contentSize = NSSize(width: 500, height: 540)
    private static let top: CGFloat = 14
    private static let rowHeight: CGFloat = 32

    let window: NSWindow
    private unowned let app: AppController
    private let tabView: NSTabView
    private var sliders: [SliderBinding] = []
    private var checks: [CheckBinding] = []
    private var groups: [GeometryGroup] = []
    // Window groups that belong to one display (false: separate windows,
    // true: classic) and the display they were last shown for.
    private var layoutViews: [(view: NSView, classic: Bool)] = []
    private var addingLayout: Bool?
    private var shownClassic: Bool?

    init(app: AppController) {
        self.app = app
        let size = SettingsWindowController.contentSize
        window = NSWindow(contentRect: NSRect(origin: .zero, size: size),
                          styleMask: [.titled, .closable, .miniaturizable], backing: .buffered, defer: false)
        tabView = NSTabView(frame: NSRect(x: 10, y: 10, width: size.width - 20, height: 486))
        super.init()

        window.title = "Delay Lama Settings"
        window.isReleasedWhenClosed = false
        window.tabbingMode = .disallowed
        let content = FlippedView(frame: NSRect(origin: .zero, size: size))
        window.contentView = content
        content.addSubview(tabView)

        buildVoicePage(addPage("Voice"))
        buildEchoPage(addPage("Echo / Envelope"))
        buildFlagPage(addPage("Flag"))
        buildWindowsPage(addPage("Windows"))

        addButton(content, "About / Manual…", NSRect(x: 10, y: 500, width: 150, height: 32),
                  #selector(aboutClicked(_:)))
        addButton(content, "Reset All Settings…", NSRect(x: 175, y: 500, width: 150, height: 32),
                  #selector(resetAllClicked(_:)))
        addButton(content, "Quit Delay Lama", NSRect(x: 340, y: 500, width: 150, height: 32),
                  #selector(quitClicked(_:)))
        window.autorecalculatesKeyViewLoop = true
    }

    // MARK: Building

    private func addPage(_ title: String) -> NSView {
        let item = NSTabViewItem(identifier: title)
        item.label = title
        let page = FlippedView(frame: NSRect(x: 0, y: 0, width: 466, height: 440))
        item.view = page
        tabView.addTabViewItem(item)
        return page
    }

    private func add(_ view: NSView, to page: NSView) {
        page.addSubview(view)
        if let classic = addingLayout {
            layoutViews.append((view: view, classic: classic))
        }
    }

    @discardableResult
    private func addLabel(_ page: NSView, _ text: String, _ frame: NSRect, heading: Bool = false) -> NSTextField {
        let label = NSTextField(labelWithString: text)
        if heading {
            label.font = NSFont.boldSystemFont(ofSize: NSFont.systemFontSize)
        }
        label.frame = frame
        add(label, to: page)
        return label
    }

    private func addHelp(_ page: NSView, _ text: String, _ frame: NSRect) {
        let label = NSTextField(wrappingLabelWithString: text)
        label.font = NSFont.systemFont(ofSize: NSFont.smallSystemFontSize)
        label.textColor = .secondaryLabelColor
        label.frame = frame
        add(label, to: page)
    }

    @discardableResult
    private func addButton(_ page: NSView, _ title: String, _ frame: NSRect, _ action: Selector) -> NSButton {
        let button = NSButton(title: title, target: self, action: action)
        button.frame = frame
        add(button, to: page)
        return button
    }

    private func addSlider(_ page: NSView, row: Int, _ label: String, _ range: ClosedRange<Int>,
                           get: @escaping () -> Int, set: @escaping (Int) -> Void,
                           format: @escaping (Int) -> String) {
        let y = SettingsWindowController.top + CGFloat(row) * SettingsWindowController.rowHeight
        addLabel(page, label, NSRect(x: 16, y: y + 3, width: 150, height: 18))
        let slider = NSSlider(value: Double(range.lowerBound), minValue: Double(range.lowerBound),
                              maxValue: Double(range.upperBound), target: self,
                              action: #selector(sliderMoved(_:)))
        slider.frame = NSRect(x: 170, y: y, width: 210, height: 24)
        slider.isContinuous = true
        slider.tag = sliders.count
        add(slider, to: page)
        let value = addLabel(page, "", NSRect(x: 388, y: y + 3, width: 72, height: 18))
        sliders.append(SliderBinding(slider: slider, value: value, get: get, set: set, format: format))
    }

    /// A slider for a 0..1 (or seconds, or cents) sound parameter shown as
    /// whole steps of 1/scale.
    private func addSoundSlider(_ page: NSView, row: Int, _ label: String,
                                _ key: WritableKeyPath<SoundSettings, Float>,
                                _ range: ClosedRange<Int> = 0...100, scale: Float = 100,
                                format: @escaping (Int) -> String = percentText) {
        let app = self.app
        addSlider(page, row: row, label, range,
                  get: { [unowned app] in Int((app.settings.sound[keyPath: key] * scale).rounded()) },
                  set: { [unowned app] value in
                      app.settings.sound[keyPath: key] = Float(value) / scale
                      app.applySound()
                  },
                  format: format)
    }

    private func addCheck(_ page: NSView, _ title: String, _ frame: NSRect, radio: Bool = false,
                          get: @escaping () -> Bool, set: @escaping (Bool) -> Void) {
        let action = #selector(checkToggled(_:))
        let button = radio ? NSButton(radioButtonWithTitle: title, target: self, action: action)
                           : NSButton(checkboxWithTitle: title, target: self, action: action)
        button.frame = frame
        button.tag = checks.count
        add(button, to: page)
        checks.append(CheckBinding(button: button, get: get, set: set))
    }

    private func buildVoicePage(_ page: NSView) {
        let app = self.app
        addSoundSlider(page, row: 0, "Volume", \.volume)
        addSoundSlider(page, row: 1, "Glide", \.glide)
        addSoundSlider(page, row: 2, "Voice (deep – high)", \.voice)
        addSoundSlider(page, row: 3, "Vibrato depth", \.vibrato)
        addSoundSlider(page, row: 4, "Vibrato speed", \.vibratoRate,
                       format: { String(format: "×%.2f", pow(4.0, Double($0) / 50 - 1)) })
        addSoundSlider(page, row: 5, "Breath", \.breath)
        addSlider(page, row: 6, "Unison voices", 1...10,
                  get: { [unowned app] in app.settings.sound.unison },
                  set: { [unowned app] value in
                      app.settings.sound.unison = value
                      app.applySound()
                  },
                  format: { "\($0)" })
        addSoundSlider(page, row: 7, "Unison detune", \.unisonDetune, 0...50, scale: 1,
                       format: { "\($0) cents" })
        addSoundSlider(page, row: 8, "Unison spread", \.unisonSpread)
        let y = SettingsWindowController.top + 9 * SettingsWindowController.rowHeight + 8
        addButton(page, "Reset Sound to Defaults", NSRect(x: 16, y: y, width: 200, height: 32),
                  #selector(resetSoundClicked(_:)))
    }

    private func buildEchoPage(_ page: NSView) {
        addSoundSlider(page, row: 0, "Echo amount", \.delayMix)
        addSoundSlider(page, row: 1, "Echo time", \.delayTime, format: { value in
            // Left tap: 13653 samples at 44.1 kHz scaled by 0.1 + 1.9 * time.
            let seconds = 13653.0 / 44100.0 * (0.1 + 1.9 * Double(value) / 100)
            return millisecondsText(Int((seconds * 1000).rounded()))
        })
        addSoundSlider(page, row: 2, "Echo feedback", \.delayFeedback, 0...95)
        addSoundSlider(page, row: 3, "Attack", \.attack, 0...200, format: { millisecondsText($0 * 10) })
        addSoundSlider(page, row: 4, "Decay", \.decay, 0...200, format: { millisecondsText($0 * 10) })
        addSoundSlider(page, row: 5, "Sustain", \.sustain)
        addSoundSlider(page, row: 6, "Release", \.release, 0...200, format: { millisecondsText($0 * 10) })
        let y = SettingsWindowController.top + 7 * SettingsWindowController.rowHeight + 8
        addButton(page, "Reset Sound to Defaults", NSRect(x: 16, y: y, width: 200, height: 32),
                  #selector(resetSoundClicked(_:)))
    }

    private func buildFlagPage(_ page: NSView) {
        let app = self.app
        let noteLabel: (Int) -> String = { "\(noteName($0)) (\($0))" }
        addSlider(page, row: 0, "Lowest note (left)", 24...107,
                  get: { [unowned app] in app.settings.flag.lowNote },
                  set: { [unowned app, unowned self] value in
                      app.settings.flag.lowNote = value
                      if app.settings.flag.highNote <= value {
                          app.settings.flag.highNote = value + 1
                      }
                      app.applyFlag()
                      self.refresh()
                  },
                  format: noteLabel)
        addSlider(page, row: 1, "Highest note (right)", 25...108,
                  get: { [unowned app] in app.settings.flag.highNote },
                  set: { [unowned app, unowned self] value in
                      app.settings.flag.highNote = value
                      if app.settings.flag.lowNote >= value {
                          app.settings.flag.lowNote = value - 1
                      }
                      app.applyFlag()
                      self.refresh()
                  },
                  format: noteLabel)

        let checkTop = SettingsWindowController.top + 2 * SettingsWindowController.rowHeight + 8
        addCheck(page, "Snap pitch to whole semitones", NSRect(x: 16, y: checkTop, width: 420, height: 20),
                 get: { [unowned app] in app.settings.flag.snap },
                 set: { [unowned app] on in
                     app.settings.flag.snap = on
                     app.applyFlag()
                 })
        addCheck(page, "Show note grid on the flag", NSRect(x: 16, y: checkTop + 28, width: 420, height: 20),
                 get: { [unowned app] in app.settings.flag.showGrid },
                 set: { [unowned app] on in
                     app.settings.flag.showGrid = on
                     app.applyFlag()
                 })
        addCheck(page, "Show crosshair while playing", NSRect(x: 16, y: checkTop + 56, width: 420, height: 20),
                 get: { [unowned app] in app.settings.flag.showCrosshair },
                 set: { [unowned app] on in
                     app.settings.flag.showCrosshair = on
                     app.applyFlag()
                 })
        addHelp(page, flagHelp, NSRect(x: 16, y: checkTop + 96, width: 434, height: 110))
    }

    private func buildWindowsPage(_ page: NSView) {
        let app = self.app
        addLabel(page, "Display", NSRect(x: 16, y: 12, width: 300, height: 18), heading: true)
        addCheck(page, "Separate flag and monk windows", NSRect(x: 16, y: 34, width: 230, height: 20), radio: true,
                 get: { [unowned app] in !app.settings.classic },
                 set: { [unowned app] on in
                     if on {
                         app.setClassic(false)
                     }
                 })
        addCheck(page, "Classic single window", NSRect(x: 250, y: 34, width: 200, height: 20), radio: true,
                 get: { [unowned app] in app.settings.classic },
                 set: { [unowned app] on in
                     if on {
                         app.setClassic(true)
                     }
                 })

        // The window groups below the display choice follow the display.
        let groupTop: CGFloat = 68
        addingLayout = false
        addWindowGroup(page, y: groupTop, "Flag window", .flag)
        addWindowGroup(page, y: groupTop + 92, "Monk window", .monk)
        addingLayout = true
        addWindowGroup(page, y: groupTop, "Classic window", .classic)
        addHelp(page, classicHelp, NSRect(x: 16, y: groupTop + 92, width: 434, height: 70))
        addingLayout = nil

        let generalTop = groupTop + 186
        addLabel(page, "General", NSRect(x: 16, y: generalTop, width: 300, height: 18), heading: true)
        addCheck(page, "Settings window always on top", NSRect(x: 16, y: generalTop + 22, width: 230, height: 20),
                 get: { [unowned app] in app.settings[.settings].topmost },
                 set: { [unowned app] on in
                     app.settings[.settings].topmost = on
                     app.applyWindow(.settings)
                 })
        addCheck(page, "Smooth image scaling", NSRect(x: 250, y: generalTop + 22, width: 200, height: 20),
                 get: { [unowned app] in app.settings.smoothScaling },
                 set: { [unowned app] on in
                     app.settings.smoothScaling = on
                     app.applyScaling()
                 })
        addButton(page, "Reset Window Layout", NSRect(x: 16, y: generalTop + 50, width: 180, height: 32),
                  #selector(resetLayoutClicked(_:)))
        addHelp(page, windowsHelp, NSRect(x: 16, y: generalTop + 90, width: 434, height: 80))
    }

    private func addWindowGroup(_ page: NSView, y: CGFloat, _ heading: String, _ id: WindowID) {
        let app = self.app
        addLabel(page, heading, NSRect(x: 16, y: y, width: 300, height: 18), heading: true)
        addCheck(page, "Show", NSRect(x: 16, y: y + 22, width: 64, height: 20),
                 get: { [unowned app] in app.settings[id].visible },
                 set: { [unowned app] on in app.showWindow(id, on, activate: false) })
        addCheck(page, "Title bar", NSRect(x: 84, y: y + 22, width: 84, height: 20),
                 get: { [unowned app] in app.settings[id].titleBar },
                 set: { [unowned app] on in
                     app.settings[id].titleBar = on
                     app.applyWindow(id)
                 })
        addCheck(page, "Always on top", NSRect(x: 172, y: y + 22, width: 122, height: 20),
                 get: { [unowned app] in app.settings[id].topmost },
                 set: { [unowned app] on in
                     app.settings[id].topmost = on
                     app.applyWindow(id)
                 })
        addCheck(page, "Keep aspect ratio", NSRect(x: 298, y: y + 22, width: 150, height: 20),
                 get: { [unowned app] in app.settings[id].keepAspect },
                 set: { [unowned app] on in
                     app.settings[id].keepAspect = on
                     app.applyWindow(id)
                 })

        var fields: [NSTextField] = []
        for (index, caption) in ["X", "Y", "W", "H"].enumerated() {
            let x = 16 + CGFloat(index) * 84
            addLabel(page, caption, NSRect(x: x, y: y + 53, width: 14, height: 18))
            let field = NSTextField(string: "")
            field.frame = NSRect(x: x + 16, y: y + 50, width: 60, height: 22)
            field.target = self
            field.action = #selector(applyGeometry(_:))
            field.tag = groups.count
            add(field, to: page)
            fields.append(field)
        }
        let apply = addButton(page, "Apply", NSRect(x: 360, y: y + 46, width: 90, height: 32),
                              #selector(applyGeometry(_:)))
        apply.tag = groups.count
        groups.append(GeometryGroup(id: id, fields: fields))
    }

    // MARK: Updating

    /// Re-reads every control from the current settings.
    func refresh() {
        let classic = app.settings.classic
        if shownClassic != classic {
            shownClassic = classic
            for entry in layoutViews {
                entry.view.isHidden = entry.classic != classic
            }
        }
        for binding in sliders {
            let value = binding.get()
            binding.slider.integerValue = value
            binding.value.stringValue = binding.format(value)
        }
        for binding in checks {
            binding.button.state = binding.get() ? .on : .off
        }
        for group in groups {
            refreshGeometry(group.id)
        }
    }

    /// Leaves the fields alone while the user is typing in them.
    func refreshGeometry(_ id: WindowID) {
        let editing = (window.firstResponder as? NSText)?.delegate as? NSTextField
        for group in groups where group.id == id {
            if let editing = editing, group.fields.contains(where: { $0 === editing }) {
                continue
            }
            let geometry = app.settings[id]
            let values = [geometry.x, geometry.y, geometry.width, geometry.height]
            for (field, value) in zip(group.fields, values) {
                field.stringValue = String(Int(value.rounded()))
            }
        }
    }

    // MARK: Actions

    @objc private func sliderMoved(_ sender: NSSlider) {
        let binding = sliders[sender.tag]
        let value = Int(sender.doubleValue.rounded())
        binding.set(value)
        binding.value.stringValue = binding.format(value)
    }

    @objc private func checkToggled(_ sender: NSButton) {
        checks[sender.tag].set(sender.state == .on)
    }

    @objc private func applyGeometry(_ sender: NSControl) {
        guard groups.indices.contains(sender.tag) else { return }
        let group = groups[sender.tag]
        var values: [CGFloat] = []
        for field in group.fields {
            guard let value = Int(field.stringValue.trimmingCharacters(in: .whitespaces)) else {
                NSSound.beep()
                window.makeFirstResponder(field)
                return
            }
            values.append(CGFloat(max(-32000, min(32000, value))))
        }
        // Leave the fields so they show the geometry that was applied.
        window.makeFirstResponder(nil)
        app.setWindowGeometry(group.id, x: values[0], y: values[1], width: values[2], height: values[3])
    }

    @objc private func aboutClicked(_ sender: Any?) { app.presentAbout() }
    @objc private func resetAllClicked(_ sender: Any?) { app.resetAll() }
    @objc private func resetSoundClicked(_ sender: Any?) { app.resetSound() }
    @objc private func resetLayoutClicked(_ sender: Any?) { app.resetLayout() }
    @objc private func quitClicked(_ sender: Any?) { NSApp.terminate(sender) }
}
