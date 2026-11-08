import AppKit
import SwiftUI

/// The same palette as the website. Blue, yellow and red are the grades and
/// carry state everywhere; everything else stays neutral.
enum Theme {
    static let paper = Color(light: 0xF3F4F6, dark: 0x0E1014)
    static let sheet = Color(light: 0xFFFFFF, dark: 0x161A20)
    static let ink = Color(light: 0x14171C, dark: 0xE7E9ED)
    static let ink2 = Color(light: 0x48505B, dark: 0xB2B8C2)
    static let ink3 = Color(light: 0x6F7783, dark: 0x8B93A0)
    static let line = Color(light: 0xDCE0E5, dark: 0x2B313A)
    static let line2 = Color(light: 0xECEEF1, dark: 0x1F242B)
    static let untried = Color(light: 0xC3C9D1, dark: 0x4B535E)

    static let blue = Color(light: 0x1F5FD1, dark: 0x7BA8FF)
    static let yellow = Color(light: 0xE6AE0B, dark: 0xECC94B)
    static let red = Color(light: 0xBF3121, dark: 0xEE5A47)

    static let blueText = Color(light: 0x1F5FD1, dark: 0x7BA8FF)
    static let redText = Color(light: 0xBF3121, dark: 0xFF826F)

    static let blueSoft = Color(light: 0xE5EDFB, dark: 0x16264A)
    static let yellowSoft = Color(light: 0xFBF1D1, dark: 0x352B0F)
    static let redSoft = Color(light: 0xFAE3DF, dark: 0x401A13)

    static func color(track: String) -> Color {
        switch track {
        case "red": return red
        case "yellow": return yellow
        case "blue", "retired": return blue
        default: return untried
        }
    }

    static func color(grade: String) -> Color {
        switch grade {
        case "b": return blue
        case "y": return yellow
        default: return red
        }
    }

    static func softColor(grade: String) -> Color {
        switch grade {
        case "b": return blueSoft
        case "y": return yellowSoft
        default: return redSoft
        }
    }

    static let mono = Font.system(size: 12, weight: .medium, design: .monospaced)
    static let label = Font.system(size: 11, weight: .medium, design: .monospaced)
}

extension Color {
    init(light: UInt32, dark: UInt32) {
        func make(_ hex: UInt32) -> NSColor {
            NSColor(srgbRed: CGFloat((hex >> 16) & 0xFF) / 255,
                    green: CGFloat((hex >> 8) & 0xFF) / 255,
                    blue: CGFloat(hex & 0xFF) / 255,
                    alpha: 1)
        }
        self.init(nsColor: NSColor(name: nil) { appearance in
            appearance.bestMatch(from: [.aqua, .darkAqua]) == .darkAqua ? make(dark) : make(light)
        })
    }
}

extension View {
    /// The bordered white panel used across the app.
    func sheetStyle(padding: CGFloat = 16) -> some View {
        self.padding(padding)
            .background(Theme.sheet)
            .clipShape(RoundedRectangle(cornerRadius: 10))
            .overlay(RoundedRectangle(cornerRadius: 10).strokeBorder(Theme.line))
    }
}

/// Card text with `backticks` rendered as code, at `codeSize` points.
func styledText(_ text: String, codeSize: CGFloat = 13.5) -> AttributedString {
    var result = AttributedString()
    for (index, part) in text.components(separatedBy: "`").enumerated() {
        var piece = AttributedString(part)
        if index % 2 == 1 {
            piece.font = .system(size: codeSize, weight: .medium, design: .monospaced)
        }
        result += piece
    }
    return result
}

/// Lays out chips left to right, wrapping at the edge.
struct FlowLayout: Layout {
    var spacing: CGFloat = 6

    func sizeThatFits(proposal: ProposedViewSize, subviews: Subviews, cache: inout ()) -> CGSize {
        let rows = arrange(width: proposal.width ?? .infinity, subviews: subviews)
        let height = rows.map(\.height).reduce(0, +) + spacing * CGFloat(max(0, rows.count - 1))
        let width = rows.map(\.width).max() ?? 0
        return CGSize(width: width, height: height)
    }

    func placeSubviews(in bounds: CGRect, proposal: ProposedViewSize, subviews: Subviews, cache: inout ()) {
        var y = bounds.minY
        for row in arrange(width: bounds.width, subviews: subviews) {
            var x = bounds.minX
            for index in row.items {
                let size = subviews[index].sizeThatFits(.unspecified)
                subviews[index].place(at: CGPoint(x: x, y: y), proposal: ProposedViewSize(size))
                x += size.width + spacing
            }
            y += row.height + spacing
        }
    }

    private struct Row {
        var items: [Int] = []
        var width: CGFloat = 0
        var height: CGFloat = 0
    }

    private func arrange(width: CGFloat, subviews: Subviews) -> [Row] {
        var rows = [Row()]
        for (index, subview) in subviews.enumerated() {
            let size = subview.sizeThatFits(.unspecified)
            if !rows[rows.count - 1].items.isEmpty, rows[rows.count - 1].width + spacing + size.width > width {
                rows.append(Row())
            }
            var row = rows[rows.count - 1]
            row.width += (row.items.isEmpty ? 0 : spacing) + size.width
            row.height = max(row.height, size.height)
            row.items.append(index)
            rows[rows.count - 1] = row
        }
        return rows
    }
}
