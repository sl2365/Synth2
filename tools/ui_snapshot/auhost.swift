// Opens the installed "Oxo: Oxo Synth2" AUv2 (VST3 via the SDK wrapper),
// loads a patch through the real editor hooks path is not possible from
// here, so it just snapshots the editor view to PNG.
//   swiftc auhost.swift -o auhost && ./auhost out.png
import AppKit
import AVFoundation
import CoreAudioKit

let out = CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : "/tmp/synth2_au.png"
let desc = AudioComponentDescription(componentType: kAudioUnitType_MusicDevice, componentSubType: 0x4F58_5332, // OXS2
                                     componentManufacturer: 0x4F58_4F4D, componentFlags: 0, componentFlagsMask: 0)
let app = NSApplication.shared
app.setActivationPolicy(.prohibited)
var done = false
AVAudioUnit.instantiate(with: desc, options: []) { unit, error in
    guard let unit else {
        print("instantiate failed", error as Any)
        done = true
        return
    }
    unit.auAudioUnit.requestViewController { vc in
        guard let vc else {
            print("no view controller")
            done = true
            return
        }
        DispatchQueue.main.async {
            let view = vc.view
            let size = view.frame.size.width > 10 ? view.frame.size : NSSize(width: 1200, height: 660)
            let window = NSWindow(contentRect: NSRect(origin: .zero, size: size), styleMask: [.titled], backing: .buffered, defer: false)
            window.contentView = view
            window.orderFront(nil)
            RunLoop.main.run(until: Date().addingTimeInterval(1.0))
            view.layoutSubtreeIfNeeded()
            if let rep = view.bitmapImageRepForCachingDisplay(in: view.bounds) {
                view.cacheDisplay(in: view.bounds, to: rep)
                try? rep.representation(using: .png, properties: [:])?.write(to: URL(fileURLWithPath: out))
                print("ok \(Int(size.width))x\(Int(size.height))")
            }
            done = true
        }
    }
}
while !done { RunLoop.main.run(until: Date().addingTimeInterval(0.05)) }
