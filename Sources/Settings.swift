import AppKit

// Mirrors Windows/app.h and Windows/settings_store.cpp. Settings persist in the
// app's user defaults (com.local.delaylama.standalone) under keys named after
// the Windows INI sections and keys, for example "Sound.Volume".

enum WindowID: Int, CaseIterable {
    case flag = 0, monk, settings, classic

    var key: String {
        switch self {
        case .flag: return "FlagWindow"
        case .monk: return "MonkWindow"
        case .settings: return "SettingsWindow"
        case .classic: return "ClassicWindow"
        }
    }
}

struct WindowSettings {
    var visible = true
    var titleBar = true
    var topmost = false
    var keepAspect = false
    // Inner (content) area in points, measured from the top-left corner of
    // the main display with y growing downwards; width 0 means unplaced.
    var x: CGFloat = 0
    var y: CGFloat = 0
    var width: CGFloat = 0
    var height: CGFloat = 0
}

struct SoundSettings {
    var volume: Float = 0.28
    var glide: Float = 0.5
    var voice: Float = 0.5
    var vibrato: Float = 0.15
    var vibratoRate: Float = 0.5
    var breath: Float = 0.5
    var unison = 1
    var unisonDetune: Float = 0 // cents
    var unisonSpread: Float = 0
    var delayMix: Float = 0.42
    var delayTime: Float = 0.5
    var delayFeedback: Float = 0.5
    var attack: Float = 0 // seconds
    var decay: Float = 0 // seconds
    var sustain: Float = 1
    var release: Float = 0 // seconds
}

struct FlagSettings {
    var lowNote = 48 // C3
    var highNote = 72 // C5
    var snap = false
    var showGrid = false
    var showCrosshair = true
}

struct Settings {
    var sound = SoundSettings()
    var flag = FlagSettings()
    // Classic: one window with the original panel instead of flag + monk.
    var classic = true
    var smoothScaling = false
    var windows = [WindowSettings](repeating: WindowSettings(), count: WindowID.allCases.count)

    subscript(id: WindowID) -> WindowSettings {
        get { windows[id.rawValue] }
        set { windows[id.rawValue] = newValue }
    }

    static func defaults() -> Settings {
        var settings = Settings()
        settings[.monk].keepAspect = true
        settings[.classic].keepAspect = true
        return settings
    }

    static func load(from store: UserDefaults = .standard) -> Settings {
        var settings = Settings.defaults()
        let reader = DefaultsReader(store: store)

        var sound = settings.sound
        sound.volume = reader.float("Sound.Volume", sound.volume, 0...1)
        sound.glide = reader.float("Sound.Glide", sound.glide, 0...1)
        sound.voice = reader.float("Sound.Voice", sound.voice, 0...1)
        sound.vibrato = reader.float("Sound.Vibrato", sound.vibrato, 0...1)
        sound.vibratoRate = reader.float("Sound.VibratoRate", sound.vibratoRate, 0...1)
        sound.breath = reader.float("Sound.Breath", sound.breath, 0...1)
        sound.unison = reader.int("Sound.UnisonVoices", sound.unison, 1...10)
        sound.unisonDetune = reader.float("Sound.UnisonDetune", sound.unisonDetune, 0...50)
        sound.unisonSpread = reader.float("Sound.UnisonSpread", sound.unisonSpread, 0...1)
        sound.delayMix = reader.float("Sound.EchoAmount", sound.delayMix, 0...1)
        sound.delayTime = reader.float("Sound.EchoTime", sound.delayTime, 0...1)
        sound.delayFeedback = reader.float("Sound.EchoFeedback", sound.delayFeedback, 0...0.95)
        sound.attack = reader.float("Sound.Attack", sound.attack, 0...2)
        sound.decay = reader.float("Sound.Decay", sound.decay, 0...2)
        sound.sustain = reader.float("Sound.Sustain", sound.sustain, 0...1)
        sound.release = reader.float("Sound.Release", sound.release, 0...2)
        settings.sound = sound

        var flag = settings.flag
        flag.lowNote = reader.int("Flag.LowNote", flag.lowNote, 24...107)
        flag.highNote = reader.int("Flag.HighNote", flag.highNote, 25...108)
        if flag.highNote <= flag.lowNote {
            flag.highNote = flag.lowNote + 1
        }
        flag.snap = reader.bool("Flag.SnapToSemitones", flag.snap)
        flag.showGrid = reader.bool("Flag.ShowNoteGrid", flag.showGrid)
        flag.showCrosshair = reader.bool("Flag.ShowCrosshair", flag.showCrosshair)
        settings.flag = flag

        settings.classic = reader.bool("Display.Classic", settings.classic)
        settings.smoothScaling = reader.bool("Display.SmoothScaling", settings.smoothScaling)

        for id in WindowID.allCases {
            let prefix = id.key + "."
            var window = settings[id]
            window.visible = reader.bool(prefix + "Visible", window.visible)
            window.titleBar = reader.bool(prefix + "TitleBar", window.titleBar)
            window.topmost = reader.bool(prefix + "AlwaysOnTop", window.topmost)
            window.keepAspect = reader.bool(prefix + "KeepAspectRatio", window.keepAspect)
            window.x = reader.cgFloat(prefix + "X", window.x, -32000...32000)
            window.y = reader.cgFloat(prefix + "Y", window.y, -32000...32000)
            window.width = reader.cgFloat(prefix + "Width", window.width, 0...32000)
            window.height = reader.cgFloat(prefix + "Height", window.height, 0...32000)
            settings[id] = window
        }
        // The settings window always keeps its frame.
        settings[.settings].titleBar = true
        return settings
    }

    func save(to store: UserDefaults = .standard) {
        store.set(sound.volume, forKey: "Sound.Volume")
        store.set(sound.glide, forKey: "Sound.Glide")
        store.set(sound.voice, forKey: "Sound.Voice")
        store.set(sound.vibrato, forKey: "Sound.Vibrato")
        store.set(sound.vibratoRate, forKey: "Sound.VibratoRate")
        store.set(sound.breath, forKey: "Sound.Breath")
        store.set(sound.unison, forKey: "Sound.UnisonVoices")
        store.set(sound.unisonDetune, forKey: "Sound.UnisonDetune")
        store.set(sound.unisonSpread, forKey: "Sound.UnisonSpread")
        store.set(sound.delayMix, forKey: "Sound.EchoAmount")
        store.set(sound.delayTime, forKey: "Sound.EchoTime")
        store.set(sound.delayFeedback, forKey: "Sound.EchoFeedback")
        store.set(sound.attack, forKey: "Sound.Attack")
        store.set(sound.decay, forKey: "Sound.Decay")
        store.set(sound.sustain, forKey: "Sound.Sustain")
        store.set(sound.release, forKey: "Sound.Release")

        store.set(flag.lowNote, forKey: "Flag.LowNote")
        store.set(flag.highNote, forKey: "Flag.HighNote")
        store.set(flag.snap, forKey: "Flag.SnapToSemitones")
        store.set(flag.showGrid, forKey: "Flag.ShowNoteGrid")
        store.set(flag.showCrosshair, forKey: "Flag.ShowCrosshair")

        store.set(classic, forKey: "Display.Classic")
        store.set(smoothScaling, forKey: "Display.SmoothScaling")

        for id in WindowID.allCases {
            let prefix = id.key + "."
            let window = self[id]
            store.set(window.visible, forKey: prefix + "Visible")
            store.set(window.titleBar, forKey: prefix + "TitleBar")
            store.set(window.topmost, forKey: prefix + "AlwaysOnTop")
            store.set(window.keepAspect, forKey: prefix + "KeepAspectRatio")
            store.set(Double(window.x), forKey: prefix + "X")
            store.set(Double(window.y), forKey: prefix + "Y")
            store.set(Double(window.width), forKey: prefix + "Width")
            store.set(Double(window.height), forKey: prefix + "Height")
        }
    }
}

/// Typed, range-checked reads that fall back when a key is missing or bad.
private struct DefaultsReader {
    let store: UserDefaults

    private func number(_ key: String) -> NSNumber? {
        store.object(forKey: key) as? NSNumber
    }

    func float(_ key: String, _ fallback: Float, _ range: ClosedRange<Float>) -> Float {
        guard let value = number(key)?.floatValue, value.isFinite else { return fallback }
        return min(max(value, range.lowerBound), range.upperBound)
    }

    func cgFloat(_ key: String, _ fallback: CGFloat, _ range: ClosedRange<CGFloat>) -> CGFloat {
        guard let value = number(key)?.doubleValue, value.isFinite else { return fallback }
        return min(max(CGFloat(value), range.lowerBound), range.upperBound)
    }

    func int(_ key: String, _ fallback: Int, _ range: ClosedRange<Int>) -> Int {
        guard let value = number(key)?.intValue else { return fallback }
        return min(max(value, range.lowerBound), range.upperBound)
    }

    func bool(_ key: String, _ fallback: Bool) -> Bool {
        number(key)?.boolValue ?? fallback
    }
}
