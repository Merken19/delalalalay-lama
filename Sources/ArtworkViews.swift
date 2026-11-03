import AppKit

/// Flag, monk and classic windows. Borderless windows still take keyboard
/// focus so the computer keyboard keeps playing notes.
final class ArtworkWindow: NSWindow {
    override var canBecomeKey: Bool { true }
    override var canBecomeMain: Bool { true }
}

final class FlippedView: NSView {
    override var isFlipped: Bool { true }
}

/// Shared behavior of the flag, monk and classic views: keyboard notes and
/// the right-click menu.
class ArtworkView: NSView {
    unowned let app: AppController
    let id: WindowID

    init(app: AppController, id: WindowID) {
        self.app = app
        self.id = id
        super.init(frame: NSRect(x: 0, y: 0, width: 320, height: 240))
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

    override var isFlipped: Bool { true }
    override var isOpaque: Bool { true }
    override var acceptsFirstResponder: Bool { true }

    // The first click into an inactive window already plays.
    override func acceptsFirstMouse(for event: NSEvent?) -> Bool { true }

    override func keyDown(with event: NSEvent) {
        if !app.keyDown(event) {
            super.keyDown(with: event)
        }
    }

    override func keyUp(with event: NSEvent) {
        if !app.keyUp(event) {
            super.keyUp(with: event)
        }
    }

    override func menu(for event: NSEvent) -> NSMenu? {
        app.contextMenu(for: id)
    }

    func fillBlack(_ context: CGContext) {
        context.setFillColor(CGColor(gray: 0, alpha: 1))
        context.fill(bounds)
    }

    /// Content of the given size placed in the view, pixel-aligned.
    func contentArea(width: CGFloat, height: CGFloat) -> CGRect {
        let area = fitRect(bounds, contentWidth: width, contentHeight: height,
                           keepAspect: app.settings[id].keepAspect)
        return backingAlignedRect(area, options: .alignAllEdgesNearest)
    }
}

// MARK: - Flag

/// The playable flag: left/right is pitch, up/down the vowel. Trackpad and
/// mouse play through the same single-drag model as the Windows flag.
final class FlagView: ArtworkView {
    private var dragging = false
    private var dragX: CGFloat = 0.5
    private var dragY: CGFloat = 0.5

    private var flagArea: CGRect {
        contentArea(width: CGFloat(app.artwork.flag.width), height: CGFloat(app.artwork.flag.height))
    }

    override func draw(_ dirtyRect: NSRect) {
        guard let context = NSGraphicsContext.current?.cgContext else { return }
        fillBlack(context)
        let area = flagArea
        guard area.width > 0, area.height > 0 else { return }
        drawImage(app.artwork.flag, in: area, context: context, smooth: app.settings.smoothScaling)
        if app.settings.flag.showGrid {
            drawNoteGrid(in: area, flag: app.settings.flag, context: context)
        }
        if dragging && app.settings.flag.showCrosshair {
            drawCrosshair(in: area, x: dragX, y: dragY, context: context)
        }
    }

    override func mouseDown(with event: NSEvent) {
        window?.makeFirstResponder(self)
        let point = convert(event.locationInWindow, from: nil)
        let area = flagArea
        guard area.contains(point) else { return }
        dragging = true
        updatePosition(point, area: area)
        dl_xy_begin(app.synth, Float(dragX), Float(1 - dragY))
        needsDisplay = true
    }

    override func mouseDragged(with event: NSEvent) {
        guard dragging else { return }
        updatePosition(convert(event.locationInWindow, from: nil), area: flagArea)
        dl_xy_move(app.synth, Float(dragX), Float(1 - dragY))
        if app.settings.flag.showCrosshair {
            needsDisplay = true
        }
    }

    override func mouseUp(with event: NSEvent) {
        finishDrag()
    }

    func finishDrag() {
        guard dragging else { return }
        dragging = false
        dl_xy_end(app.synth)
        needsDisplay = true
    }

    private func updatePosition(_ point: NSPoint, area: CGRect) {
        dragX = clamp01((point.x - area.minX) / max(1, area.width))
        dragY = clamp01((point.y - area.minY) / max(1, area.height))
    }
}

// MARK: - Monk

/// The animated singer. Without a title bar the whole picture moves the
/// window; the monk is not interactive, so nothing is lost.
final class MonkView: ArtworkView {
    private(set) var shownFrame = -1

    override var mouseDownCanMoveWindow: Bool { true }

    override func draw(_ dirtyRect: NSRect) {
        guard let context = NSGraphicsContext.current?.cgContext else { return }
        fillBlack(context)
        let frame = app.activeFrame()
        shownFrame = frame
        let area = contentArea(width: CGFloat(Canvas.faceWidth), height: CGFloat(Canvas.faceHeight))
        guard area.width > 0, area.height > 0 else { return }
        drawImage(app.artwork.faces[frame], in: area, context: context, smooth: app.settings.smoothScaling)
    }
}

// MARK: - Classic panel

/// The original single-window panel (monk, flag, glide and voice knobs, echo
/// slider, help button) scaled to any window size.
final class ClassicView: ArtworkView {
    private enum Control {
        case none, flag, glide, voice, echo, help
    }

    private var drag = Control.none
    private var dragStartY: CGFloat = 0
    private var dragStartValue: Float = 0
    // Flag drag position normalized to the flag image.
    private var flagX: CGFloat = 0.5
    private var flagY: CGFloat = 0.5
    // Background with each monk frame pasted in at canvas resolution, so the
    // frame lines up with the panel at every window size.
    private var composites: [Int: CGImage] = [:]
    private(set) var shownFrame = -1

    private var canvasArea: CGRect {
        contentArea(width: Canvas.width, height: Canvas.height)
    }

    private func toCanvas(_ point: NSPoint) -> CGPoint {
        let area = canvasArea
        return CGPoint(x: (point.x - area.minX) * Canvas.width / max(1, area.width),
                       y: (point.y - area.minY) * Canvas.height / max(1, area.height))
    }

    /// A canvas rectangle in view coordinates.
    private func toView(_ rect: CGRect, area: CGRect) -> CGRect {
        let scaleX = area.width / Canvas.width
        let scaleY = area.height / Canvas.height
        return CGRect(x: area.minX + rect.minX * scaleX, y: area.minY + rect.minY * scaleY,
                      width: rect.width * scaleX, height: rect.height * scaleY)
    }

    private func control(at point: CGPoint) -> Control {
        if Canvas.flag.contains(point) {
            return .flag
        }
        for (index, knob) in ClassicLayout.knobs.enumerated() {
            let dx = point.x - knob.x
            let dy = point.y - knob.y
            if (dx * dx + dy * dy).squareRoot() <= ClassicLayout.knobHitRadius {
                return index == 0 ? .glide : .voice
            }
        }
        if ClassicLayout.echoHit.contains(point) {
            return .echo
        }
        if ClassicLayout.helpHit.contains(point) {
            return .help
        }
        return .none
    }

    override func mouseDown(with event: NSEvent) {
        window?.makeFirstResponder(self)
        let point = toCanvas(convert(event.locationInWindow, from: nil))
        let hit = control(at: point)
        switch hit {
        case .none:
            // Without a title bar the rest of the picture moves the window.
            if !app.settings[id].titleBar {
                window?.performDrag(with: event)
            }
            return
        case .help:
            app.presentAbout()
            return
        case .flag:
            updateFlag(point)
            dl_xy_begin(app.synth, Float(flagX), Float(1 - flagY))
        case .glide:
            dragStartValue = app.settings.sound.glide
        case .voice:
            dragStartValue = app.settings.sound.voice
        case .echo:
            break
        }
        drag = hit
        dragStartY = point.y
        if hit == .echo {
            dragged(to: point)
        }
        needsDisplay = true
    }

    override func mouseDragged(with event: NSEvent) {
        dragged(to: toCanvas(convert(event.locationInWindow, from: nil)))
    }

    override func mouseUp(with event: NSEvent) {
        finishDrag()
    }

    // Knobs follow vertical drags from where they were grabbed; the echo
    // handle jumps to the pointer.
    private func dragged(to point: CGPoint) {
        switch drag {
        case .flag:
            updateFlag(point)
            dl_xy_move(app.synth, Float(flagX), Float(1 - flagY))
            if app.settings.flag.showCrosshair {
                needsDisplay = true
            }
        case .glide, .voice:
            let change = Float((dragStartY - point.y) / ClassicLayout.knobDragRange)
            setValue(drag, clamp01(dragStartValue + change))
        case .echo:
            let position = (point.x - ClassicLayout.echoLeft) / (ClassicLayout.echoRight - ClassicLayout.echoLeft)
            setValue(.echo, Float(clamp01(position)))
        case .none, .help:
            break
        }
    }

    private func updateFlag(_ point: CGPoint) {
        flagX = clamp01((point.x - Canvas.flag.minX) / Canvas.flag.width)
        flagY = clamp01((point.y - Canvas.flag.minY) / Canvas.flag.height)
    }

    private func setValue(_ control: Control, _ value: Float) {
        switch control {
        case .glide:
            app.settings.sound.glide = value
            dl_set_glide(app.synth, value)
        case .voice:
            app.settings.sound.voice = value
            dl_set_voice(app.synth, value)
        case .echo:
            app.settings.sound.delayMix = value
            dl_set_delay(app.synth, value)
        case .none, .flag, .help:
            return
        }
        app.refreshSettingsWindow()
        needsDisplay = true
    }

    func finishDrag() {
        guard drag != .none else { return }
        let wasFlag = drag == .flag
        drag = .none
        if wasFlag {
            dl_xy_end(app.synth)
        }
        needsDisplay = true
    }

    override func draw(_ dirtyRect: NSRect) {
        guard let context = NSGraphicsContext.current?.cgContext else { return }
        fillBlack(context)
        let frame = app.activeFrame()
        shownFrame = frame
        let area = canvasArea
        guard area.width > 0, area.height > 0, let panel = composite(frame) else { return }
        let smooth = app.settings.smoothScaling
        drawImage(panel, in: area, context: context, smooth: smooth)
        drawKnob(context, area: area, index: 0, value: app.settings.sound.glide, smooth: smooth)
        drawKnob(context, area: area, index: 1, value: app.settings.sound.voice, smooth: smooth)

        let flag = toView(Canvas.flag, area: area)
        if app.settings.flag.showGrid {
            drawNoteGrid(in: flag, flag: app.settings.flag, context: context)
        }
        if drag == .flag && app.settings.flag.showCrosshair {
            drawCrosshair(in: flag, x: flagX, y: flagY, context: context)
        }
        drawEchoHandle(context, area: area, value: app.settings.sound.delayMix)
    }

    private func composite(_ frame: Int) -> CGImage? {
        if let cached = composites[frame] {
            return cached
        }
        let width = Int(Canvas.width)
        let height = Int(Canvas.height)
        guard let context = CGContext(data: nil, width: width, height: height, bitsPerComponent: 8,
                                      bytesPerRow: 0, space: artworkColorSpace,
                                      bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else {
            return nil
        }
        context.translateBy(x: 0, y: Canvas.height)
        context.scaleBy(x: 1, y: -1)
        drawImage(app.artwork.background, in: CGRect(x: 0, y: 0, width: Canvas.width, height: Canvas.height),
                  context: context, smooth: false)
        let face = CGRect(x: Canvas.faceX, y: Canvas.faceY,
                          width: CGFloat(Canvas.faceWidth), height: CGFloat(Canvas.faceHeight))
        drawImage(app.artwork.faces[frame], in: face, context: context, smooth: false)
        let image = context.makeImage()
        composites[frame] = image
        return image
    }

    /// Paints the knob's own cap turned clockwise by the value, then its glint.
    private func drawKnob(_ context: CGContext, area: CGRect, index: Int, value: Float, smooth: Bool) {
        let knob = ClassicLayout.knobs[index]
        let scaleX = area.width / Canvas.width
        let scaleY = area.height / Canvas.height
        let centerX = area.minX + knob.x * scaleX
        let centerY = area.minY + knob.y * scaleY
        let radius = ClassicLayout.knobCapRadius
        let size = CGFloat(ClassicLayout.knobSourceSize)

        context.saveGState()
        context.addEllipse(in: CGRect(x: centerX - radius * scaleX, y: centerY - radius * scaleY,
                                      width: radius * 2 * scaleX, height: radius * 2 * scaleY))
        context.clip()
        // Cap pixels -> around the knob centre -> turned -> scaled -> placed.
        context.translateBy(x: centerX, y: centerY)
        context.scaleBy(x: scaleX, y: scaleY)
        context.rotate(by: CGFloat(clamp01(value)) * ClassicLayout.knobSweepDegrees * .pi / 180)
        let cap = CGRect(x: CGFloat(knob.sourceX) - knob.x, y: CGFloat(knob.sourceY) - knob.y,
                         width: size, height: size)
        drawImage(app.artwork.knobCaps[index], in: cap, context: context, smooth: smooth)
        context.restoreGState()

        let glint = CGRect(x: area.minX + CGFloat(knob.sourceX) * scaleX, y: area.minY + CGFloat(knob.sourceY) * scaleY,
                           width: size * scaleX, height: size * scaleY)
        drawImage(app.artwork.knobGlints[index], in: glint, context: context, smooth: smooth)
    }

    /// A small brass fader cap sitting in the groove, drawn in canvas pixels.
    private func drawEchoHandle(_ context: CGContext, area: CGRect, value: Float) {
        context.saveGState()
        context.translateBy(x: area.minX, y: area.minY)
        context.scaleBy(x: area.width / Canvas.width, y: area.height / Canvas.height)

        let x = ClassicLayout.echoLeft + CGFloat(clamp01(value)) * (ClassicLayout.echoRight - ClassicLayout.echoLeft)
        let body = CGRect(x: x - 6, y: ClassicLayout.echoY - 11, width: 12, height: 22)
        let path = CGPath(roundedRect: body, cornerWidth: 3, cornerHeight: 3, transform: nil)
        let outline = CGColor(srgbRed: 86 / 255, green: 50 / 255, blue: 18 / 255, alpha: 1)

        context.saveGState()
        context.translateBy(x: 1.5, y: 1.5)
        context.addPath(path)
        context.setFillColor(CGColor(srgbRed: 40 / 255, green: 20 / 255, blue: 0, alpha: 120 / 255))
        context.fillPath()
        context.restoreGState()

        context.saveGState()
        context.addPath(path)
        context.clip()
        let colors = [CGColor(srgbRed: 250 / 255, green: 222 / 255, blue: 140 / 255, alpha: 1),
                      CGColor(srgbRed: 150 / 255, green: 92 / 255, blue: 32 / 255, alpha: 1)] as CFArray
        let locations: [CGFloat] = [0, 1]
        if let gradient = CGGradient(colorsSpace: artworkColorSpace, colors: colors, locations: locations) {
            context.drawLinearGradient(gradient, start: CGPoint(x: body.minX, y: body.midY),
                                       end: CGPoint(x: body.maxX, y: body.midY), options: [])
        }
        context.restoreGState()

        context.addPath(path)
        context.setStrokeColor(outline)
        context.setLineWidth(1)
        context.strokePath()
        context.setStrokeColor(outline.copy(alpha: 220 / 255) ?? outline)
        context.setLineWidth(1.4)
        context.move(to: CGPoint(x: x, y: body.minY + 4))
        context.addLine(to: CGPoint(x: x, y: body.maxY - 4))
        context.strokePath()
        context.restoreGState()
    }
}
