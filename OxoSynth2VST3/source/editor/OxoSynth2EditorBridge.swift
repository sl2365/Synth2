import AppKit
import SwiftUI

/// The first click in an unfocused plugin window grabs the knob / presses
/// the button right away instead of only activating the window (standard
/// plugin behaviour, same as Oxo Simple).
private final class Synth2HostingView: NSHostingView<SynthPanelView> {
    weak var model: SynthModel?
    var keyMonitor: Any?

    override func acceptsFirstMouse(for event: NSEvent?) -> Bool { true }

    // Keys reach the editor by TWO routes, hosts differ (Oxo Drums): an
    // app-local NSEvent monitor (whatever AppKit dispatches) and this view's
    // own keyDown (hosts routing keys down their key window's responder
    // chain). Only keys the panel uses are consumed; the rest stay the host's.
    override func keyDown(with event: NSEvent) {
        if !handleKey(event) { super.keyDown(with: event) }
    }

    override func performKeyEquivalent(with event: NSEvent) -> Bool {
        handleKey(event) || super.performKeyEquivalent(with: event)
    }

    /// A click on the panel takes the keyboard, unless a text field inside
    /// already has it (deferred: SwiftUI's own focus work lands first).
    override func mouseDown(with event: NSEvent) {
        super.mouseDown(with: event)
        DispatchQueue.main.async { [weak self] in
            guard let self, let window else { return }
            if let focused = window.firstResponder as? NSView, focused === self || focused.isDescendant(of: self) { return }
            window.makeFirstResponder(self)
        }
    }

    func handleKey(_ event: NSEvent) -> Bool {
        guard let model, let window, event.type == .keyDown else { return false }
        // Logic never makes the plugin window key: accept the key when this
        // editor is the front window.
        if event.window !== window {
            guard window.isVisible, NSApp.orderedWindows.first(where: { $0.isVisible }) === window else { return false }
        }
        guard event.modifierFlags.intersection([.command, .option, .control]).isEmpty else { return false }
        let key: SynthModel.BrowserKey
        switch event.keyCode {
        case 126: key = .up
        case 125: key = .down
        case 123: key = .left
        case 124: key = .right
        case 36, 76: key = .confirm // Return, keypad Enter
        case 53: key = .escape
        default: return false
        }
        return model.browserKey(key)
    }
}

private var kModelKey: UInt8 = 0

@_cdecl("OxoSynth2EditorCreate")
public func OxoSynth2EditorCreate(_ hooks: UnsafePointer<OxoSynth2Hooks>, _ width: Double, _ height: Double,
                                  _ resizeCtx: UnsafeMutableRawPointer?, _ requestResize: OxoSynth2ResizeRequest?)
    -> UnsafeMutableRawPointer?
{
    let model = SynthModel(host: HooksSynthHost(hooks: hooks.pointee))
    let view = Synth2HostingView(rootView: SynthPanelView(model: model))
    view.frame = NSRect(x: 0, y: 0, width: width, height: height)
    view.model = model
    view.keyMonitor = NSEvent.addLocalMonitorForEvents(matching: .keyDown) { [weak view] event in
        view?.handleKey(event) == true ? nil : event
    }
    if let requestResize {
        // Clamped to the screen so 200 % on a laptop cannot ask for an
        // off-screen window.
        model.requestEditorWidth = { [weak view] width in
            let design = SynthPanelView.designSize
            var w = width
            if let screen = (view?.window?.screen ?? NSScreen.main)?.visibleFrame.size {
                w = min(w, screen.width, screen.height * design.width / design.height)
            }
            requestResize(resizeCtx, w)
        }
    }
    objc_setAssociatedObject(view, &kModelKey, model, .OBJC_ASSOCIATION_RETAIN)
    return Unmanaged.passRetained(view as NSView).toOpaque()
}

@_cdecl("OxoSynth2EditorRelease")
public func OxoSynth2EditorRelease(_ ptr: UnsafeMutableRawPointer?) {
    guard let ptr else { return }
    let view = Unmanaged<NSView>.fromOpaque(ptr).takeRetainedValue()
    if let hosting = view as? Synth2HostingView, let monitor = hosting.keyMonitor {
        NSEvent.removeMonitor(monitor)
        hosting.keyMonitor = nil
    }
    // invalidate() also leaves MIDI learn: it never survives the editor.
    if let model = objc_getAssociatedObject(view, &kModelKey) as? SynthModel {
        model.requestEditorWidth = nil // its ctx = the dying IPlugView
        model.invalidate()
    }
    objc_setAssociatedObject(view, &kModelKey, nil, .OBJC_ASSOCIATION_RETAIN)
    view.removeFromSuperview()
}

@_cdecl("OxoSynth2EditorAspect")
public func OxoSynth2EditorAspect() -> Double {
    Double(SynthPanelView.designSize.height / SynthPanelView.designSize.width)
}
