// Draws the app icon: the expires rail, red to yellow to blue, on dark ink.
// Usage: swift scripts/make_icon.swift out.png
import AppKit

let size = 1024
let rep = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: size, pixelsHigh: size, bitsPerSample: 8,
                           samplesPerPixel: 4, hasAlpha: true, isPlanar: false,
                           colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0)!
NSGraphicsContext.saveGraphicsState()
NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: rep)
let context = NSGraphicsContext.current!.cgContext

func rgb(_ hex: Int) -> CGColor {
    CGColor(srgbRed: CGFloat((hex >> 16) & 255) / 255, green: CGFloat((hex >> 8) & 255) / 255,
            blue: CGFloat(hex & 255) / 255, alpha: 1)
}

// The standard macOS icon shape: 824 points inside a 1024 canvas.
let plate = CGRect(x: 100, y: 100, width: 824, height: 824)
context.addPath(CGPath(roundedRect: plate, cornerWidth: 185, cornerHeight: 185, transform: nil))
context.setFillColor(rgb(0x14171C))
context.fillPath()

// The expires rail: red, yellow and blue stops, then the square "gone" stop.
let y: CGFloat = 512
let radius: CGFloat = 60
let stops: [(CGFloat, Int)] = [(216, 0xEE5A47), (410, 0xECC94B), (604, 0x4F8BF0)]
let end: CGFloat = 806
context.setStrokeColor(rgb(0x6B7482))
context.setLineWidth(26)
context.setLineCap(.butt)
context.move(to: CGPoint(x: stops[0].0, y: y))
context.addLine(to: CGPoint(x: end, y: y))
context.strokePath()
for (x, color) in stops {
    context.setFillColor(rgb(0x14171C))
    context.fillEllipse(in: CGRect(x: x - radius - 8, y: y - radius - 8, width: (radius + 8) * 2, height: (radius + 8) * 2))
    context.setFillColor(rgb(color))
    context.fillEllipse(in: CGRect(x: x - radius, y: y - radius, width: radius * 2, height: radius * 2))
}
let box = CGRect(x: end - 54, y: y - 54, width: 108, height: 108)
context.addPath(CGPath(roundedRect: box, cornerWidth: 20, cornerHeight: 20, transform: nil))
context.setFillColor(rgb(0x14171C))
context.setStrokeColor(rgb(0xE7E9ED))
context.setLineWidth(24)
context.drawPath(using: .fillStroke)
NSGraphicsContext.restoreGraphicsState()
try rep.representation(using: .png, properties: [:])!.write(to: URL(fileURLWithPath: CommandLine.arguments[1]))
