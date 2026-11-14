// App icon: the panel's knob (Shared/SynthControls.swift KnobView recipe) over
// "Synth2", "Synth1" factory theme. Chosen 2026-10-30 out of the /tmp rounds.
//   swiftc -O tools/app_icon/render.swift -o build/app_icon && ./build/app_icon build/AppIcon.png
// Then strip the color metadata like the other Oxo icons (opaque RGB, no
// profile: only IHDR / IDAT / IEND) and copy to both AppIcon.appiconset
// folders (+ oxo_music's oxo_synth2_icon_1024.png):
//   python3 -c "import struct,sys;p=sys.argv[1];d=open(p,'rb').read();o=[d[:8]];i=8
//   exec('while i<len(d):\n n,=struct.unpack(\">I\",d[i:i+4])\n if d[i+4:i+8] in (b\"IHDR\",b\"IDAT\",b\"IEND\"): o.append(d[i:i+12+n])\n i+=12+n')
//   open(p,'wb').write(b''.join(o))" build/AppIcon.png
import AppKit
import SwiftUI

func hex(_ s: String) -> Color {
    let v = UInt32(s.dropFirst(), radix: 16)!
    return Color(red: Double(v >> 16 & 255) / 255, green: Double(v >> 8 & 255) / 255, blue: Double(v & 255) / 255)
}

// Synth1 theme (ThemePalette.factory "Synth1"); rim = panel x 0.5.
let panel = hex("#D5D097"), ink = hex("#1A1A0F")
let knobBody = hex("#171717"), knobGradient = hex("#212121"), dial = hex("#FF2D2D")
let rim = Color(red: 0xD5 / 255.0 * 0.5, green: 0xD0 / 255.0 * 0.5, blue: 0x97 / 255.0 * 0.5)

struct Knob: View {
    var size: CGFloat
    var angle: Double = -40
    var body: some View {
        ZStack {
            ForEach(0..<11, id: \.self) { i in
                Circle().fill(ink.opacity(0.3)).frame(width: size * 0.067, height: size * 0.067)
                    .offset(y: -size * 0.65 + size * 0.06)
                    .rotationEffect(.degrees(-140 + 28 * Double(i)))
            }
            ZStack {
                Circle().fill(RadialGradient(colors: [knobGradient, knobBody], center: .init(x: 0.35, y: 0.3),
                                             startRadius: 0, endRadius: size * 0.6))
                Circle().strokeBorder(rim.opacity(0.45), lineWidth: size * 0.067).blur(radius: size * 0.03)
                Capsule().fill(dial).frame(width: size * 0.085, height: size * 0.36).offset(y: -size * 0.24)
                    .rotationEffect(.degrees(angle))
            }
            .shadow(color: .black.opacity(0.3), radius: size * 0.04, x: size * 0.04, y: size * 0.04)
        }
        .frame(width: size, height: size)
    }
}

let icon = ZStack {
    panel
    VStack(spacing: 30) {
        Knob(size: 430)
        Text("Synth2").font(.system(size: 200, weight: .medium)).foregroundColor(ink).fixedSize()
    }.offset(y: 10)
}.frame(width: 1024, height: 1024)

MainActor.assumeIsolated {
    let r = ImageRenderer(content: icon)
    r.scale = 1
    r.isOpaque = true
    let out = CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : "AppIcon.png"
    try! NSBitmapImageRep(cgImage: r.cgImage!).representation(using: .png, properties: [:])!.write(to: URL(fileURLWithPath: out))
}
