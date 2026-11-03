import AppKit

// Geometry of the original artwork, in background (canvas) pixels. Same
// numbers as Windows/app.h and Windows/classic_window.cpp.
enum Canvas {
    static let width: CGFloat = 360
    static let height: CGFloat = 510
    // Monk frames sit in 314x311 cells of a 5x6 sheet (column-major); each
    // cell is a 311-px frame plus a 3-px border, and its bottom 20 rows show
    // the top of the nameplate, so both are cropped away. On the background
    // the frames line up with the panel pixels at faceX, faceY.
    static let faceX: CGFloat = 22
    static let faceY: CGFloat = 5
    static let faceCellWidth = 314
    static let faceCellHeight = 311
    static let faceWidth = 311
    static let faceHeight = 291
    static let faceFrames = 30
    static let faceRowsPerColumn = 6
    // The whole flag, yellow border included.
    static let flag = CGRect(x: 98, y: 362, width: 163, height: 79)
}

struct KnobGeometry {
    let x: CGFloat
    let y: CGFloat
    let sourceX: Int
    let sourceY: Int
    let glintX: Range<Int>
    let glintY: Range<Int>
}

// Classic panel controls. The knob caps, the dark notched part, are baked
// into the background pointing at their minimum; drawing that part turned
// shows the value. Each rim has a light glint that must not turn with it.
enum ClassicLayout {
    static let knobs = [
        KnobGeometry(x: 44.5, y: 471, sourceX: 22, sourceY: 449, glintX: 58..<64, glintY: 459..<465),
        KnobGeometry(x: 316.5, y: 470, sourceX: 294, sourceY: 448, glintX: 324..<331, glintY: 453..<461),
    ]
    static let knobSourceSize = 45
    // Glint pixels are brighter than the silver ring around the cap
    // (178..197); on the turning cap they take the grey of the cap face.
    static let glintLuminance = 205
    static let capLuminance = 100
    static let knobCapRadius: CGFloat = 21.5
    static let knobHitRadius: CGFloat = 27
    static let knobSweepDegrees: CGFloat = 280
    // The whole range per 100 canvas pixels of vertical drag, as in the original.
    static let knobDragRange: CGFloat = 100
    // The echo handle slides along the groove under the flag.
    static let echoLeft: CGFloat = 121
    static let echoRight: CGFloat = 241
    static let echoY: CGFloat = 486
    static let echoHit = CGRect(x: 94, y: 469, width: 172, height: 37)
    static let helpHit = CGRect(x: 284, y: 298, width: 43, height: 37)
}

let artworkColorSpace = CGColorSpace(name: CGColorSpace.sRGB) ?? CGColorSpaceCreateDeviceRGB()

/// The original PNGs cut into the pieces the windows draw. Images are read
/// by pixel (not by the PNG's point size) so the crops match the Windows build.
final class Artwork {
    let background: CGImage
    let faces: [CGImage]
    let flag: CGImage
    let knobCaps: [CGImage]
    let knobGlints: [CGImage]
    let resourceURL: URL

    init?(resourceURL: URL) {
        self.resourceURL = resourceURL
        guard let sheet = Artwork.loadImage(resourceURL.appendingPathComponent("background.png")),
              let background = sheet.cropping(to: CGRect(x: 0, y: 0, width: Canvas.width, height: Canvas.height)),
              let faceSheet = Artwork.loadImage(resourceURL.appendingPathComponent("faces.png")) else {
            return nil
        }
        self.background = background

        var faces: [CGImage] = []
        for frame in 0..<Canvas.faceFrames {
            let column = frame / Canvas.faceRowsPerColumn
            let row = frame % Canvas.faceRowsPerColumn
            let cell = CGRect(x: column * Canvas.faceCellWidth, y: row * Canvas.faceCellHeight,
                              width: Canvas.faceWidth, height: Canvas.faceHeight)
            guard let face = faceSheet.cropping(to: cell) else { return nil }
            faces.append(face)
        }
        self.faces = faces

        // A higher-resolution Resources/flag.png replaces the original flag.
        if let custom = Artwork.loadImage(resourceURL.appendingPathComponent("flag.png")) {
            flag = custom
        } else if let original = background.cropping(to: Canvas.flag) {
            flag = original
        } else {
            return nil
        }

        var caps: [CGImage] = []
        var glints: [CGImage] = []
        for knob in ClassicLayout.knobs {
            guard let parts = Artwork.splitKnob(background, knob) else { return nil }
            caps.append(parts.cap)
            glints.append(parts.glint)
        }
        knobCaps = caps
        knobGlints = glints
    }

    static func loadImage(_ url: URL) -> CGImage? {
        guard let data = try? Data(contentsOf: url),
              let bitmap = NSBitmapImageRep(data: data) else {
            return nil
        }
        return bitmap.cgImage
    }

    /// Splits a knob's piece of the background into the cap, with the glint
    /// dimmed to the cap's grey, and a glint-only overlay. Drawn unturned, the
    /// two give back the original pixels.
    private static func splitKnob(_ background: CGImage, _ knob: KnobGeometry) -> (cap: CGImage, glint: CGImage)? {
        let size = ClassicLayout.knobSourceSize
        let bounds = CGRect(x: 0, y: 0, width: size, height: size)
        let bitmapInfo = CGImageAlphaInfo.premultipliedLast.rawValue
        guard let piece = background.cropping(to: CGRect(x: knob.sourceX, y: knob.sourceY, width: size, height: size)),
              let capContext = CGContext(data: nil, width: size, height: size, bitsPerComponent: 8,
                                         bytesPerRow: size * 4, space: artworkColorSpace, bitmapInfo: bitmapInfo),
              let glintContext = CGContext(data: nil, width: size, height: size, bitsPerComponent: 8,
                                           bytesPerRow: size * 4, space: artworkColorSpace, bitmapInfo: bitmapInfo) else {
            return nil
        }
        capContext.draw(piece, in: bounds)
        glintContext.clear(bounds)
        guard let cap = capContext.data?.assumingMemoryBound(to: UInt8.self),
              let glint = glintContext.data?.assumingMemoryBound(to: UInt8.self) else {
            return nil
        }

        // Bitmap rows run top to bottom, like the artwork's own pixel rows.
        for y in knob.glintY {
            for x in knob.glintX {
                let offset = (y - knob.sourceY) * size * 4 + (x - knob.sourceX) * 4
                let red = Int(cap[offset])
                let green = Int(cap[offset + 1])
                let blue = Int(cap[offset + 2])
                let luminance = (red + green + blue) / 3
                if luminance < ClassicLayout.glintLuminance {
                    continue
                }
                for channel in 0..<4 {
                    glint[offset + channel] = cap[offset + channel]
                }
                cap[offset] = UInt8(red * ClassicLayout.capLuminance / luminance)
                cap[offset + 1] = UInt8(green * ClassicLayout.capLuminance / luminance)
                cap[offset + 2] = UInt8(blue * ClassicLayout.capLuminance / luminance)
                cap[offset + 3] = 255
            }
        }
        guard let capImage = capContext.makeImage(), let glintImage = glintContext.makeImage() else {
            return nil
        }
        return (capImage, glintImage)
    }
}

// MARK: - Drawing helpers (views are flipped: y grows downwards)

func clamp01(_ value: Float) -> Float {
    value > 0 ? (value < 1 ? value : 1) : 0
}

func clamp01(_ value: CGFloat) -> CGFloat {
    value > 0 ? (value < 1 ? value : 1) : 0
}

/// Letterboxes content of the given aspect inside bounds when keepAspect is on.
func fitRect(_ bounds: CGRect, contentWidth: CGFloat, contentHeight: CGFloat, keepAspect: Bool) -> CGRect {
    guard keepAspect, contentWidth > 0, contentHeight > 0, bounds.width > 0, bounds.height > 0 else {
        return bounds
    }
    var width = bounds.width
    var height = bounds.width * contentHeight / contentWidth
    if height > bounds.height {
        height = bounds.height
        width = bounds.height * contentWidth / contentHeight
    }
    return CGRect(x: bounds.minX + (bounds.width - width) / 2, y: bounds.minY + (bounds.height - height) / 2,
                  width: width, height: height)
}

/// Draws an image upright into a rectangle of a flipped context.
func drawImage(_ image: CGImage, in rect: CGRect, context: CGContext, smooth: Bool) {
    context.saveGState()
    context.interpolationQuality = smooth ? .high : .none
    context.translateBy(x: rect.minX, y: rect.maxY)
    context.scaleBy(x: 1, y: -1)
    context.draw(image, in: CGRect(origin: .zero, size: rect.size))
    context.restoreGState()
}

func noteName(_ note: Int) -> String {
    let names = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
    let clamped = max(0, min(127, note))
    return "\(names[clamped % 12])\(clamped / 12 - 1)"
}

/// One line per semitone at the exact pitch of that note (the centre of its
/// zone when snapping), brighter on naturals and brightest with a label on C.
func drawNoteGrid(in area: CGRect, flag: FlagSettings, context: CGContext) {
    let span = CGFloat(flag.highNote - flag.lowNote)
    guard span > 0 else { return }
    let labelSize = max(9, min(28, area.height * 0.12))
    let font = NSFont.boldSystemFont(ofSize: labelSize)
    let sharps: Set<Int> = [1, 3, 6, 8, 10]

    for note in flag.lowNote...flag.highNote {
        let x = area.minX + CGFloat(note - flag.lowNote) / span * area.width
        let pitchClass = note % 12
        let alpha: CGFloat = pitchClass == 0 ? 190 : (sharps.contains(pitchClass) ? 40 : 90)
        context.setStrokeColor(CGColor(gray: 1, alpha: alpha / 255))
        context.setLineWidth(pitchClass == 0 ? 2 : 1)
        context.move(to: CGPoint(x: x, y: area.minY))
        context.addLine(to: CGPoint(x: x, y: area.maxY))
        context.strokePath()
        if pitchClass == 0 {
            let label = noteName(note) as NSString
            let labelY = area.maxY - labelSize * 1.45
            label.draw(at: NSPoint(x: x + 4, y: labelY + 1),
                       withAttributes: [.font: font, .foregroundColor: NSColor(white: 0, alpha: 170 / 255)])
            label.draw(at: NSPoint(x: x + 3, y: labelY),
                       withAttributes: [.font: font, .foregroundColor: NSColor(white: 1, alpha: 235 / 255)])
        }
    }
}

/// x and y are the drag position normalized to the area.
func drawCrosshair(in area: CGRect, x: CGFloat, y: CGFloat, context: CGContext) {
    let markerX = area.minX + x * area.width
    let markerY = area.minY + y * area.height
    let radius = max(4, min(area.width, area.height) * 0.035)
    context.setStrokeColor(CGColor(gray: 1, alpha: 185 / 255))
    context.setLineWidth(1)
    context.move(to: CGPoint(x: area.minX, y: markerY))
    context.addLine(to: CGPoint(x: area.maxX, y: markerY))
    context.move(to: CGPoint(x: markerX, y: area.minY))
    context.addLine(to: CGPoint(x: markerX, y: area.maxY))
    context.strokePath()
    context.setFillColor(CGColor(gray: 1, alpha: 230 / 255))
    context.fillEllipse(in: CGRect(x: markerX - radius, y: markerY - radius, width: radius * 2, height: radius * 2))
}
