import AVFoundation
import AudioToolbox

/// Oxo Synth2 as an iPad AUv3 instrument (aumu OXS2 / OXOM): the C++
/// Synth2Instance does the work; this class is the host-facing shell —
/// stereo output bus, the 99-parameter tree (native Synth1 values, ids =
/// Synth1 indices like the VST3), fullState = the same 'OXS2' chunk as the
/// VST3 state (+ the UI width mode), render block -> Synth2Render.
public final class OxoSynth2AudioUnit: AUAudioUnit {
    static let stateKey = "OXS2"
    /// The editor's UI width mode: per instance, in fullState beside the
    /// chunk (iPad only, the VST3 has no such mode); the last choice seeds
    /// new instances.
    static let uiWidthModeKey = "OxoUIWidthMode"
    private static let uiWidthModeDefault = "oxo.synth2.uiWidthMode"
    private(set) var uiWidthMode = UserDefaults.standard.bool(forKey: uiWidthModeDefault)

    func chooseUIWidthMode(_ on: Bool) {
        uiWidthMode = on
        UserDefaults.standard.set(on, forKey: Self.uiWidthModeDefault)
    }

    let instance: UnsafeMutableRawPointer
    private var outputBus: AUAudioUnitBus!
    private var busArray: AUAudioUnitBusArray!
    private var tree: AUParameterTree!
    private let transport = TransportBox()

    /// Host clock blocks, refreshed when render resources are allocated
    /// (read by the render block without touching `self`).
    private final class TransportBox {
        var musicalContext: AUHostMusicalContextBlock?
        var transportState: AUHostTransportStateBlock?
    }

    public override init(componentDescription: AudioComponentDescription,
                         options: AudioComponentInstantiationOptions = []) throws {
        instance = Synth2Create(SynthLibrary.appGroupLibraryRoot.path, SynthLibrary.factoryRoot?.path,
                                Bundle.main.infoDictionary?["CFBundleVersion"] as? String)
        try super.init(componentDescription: componentDescription, options: options)
        let format = AVAudioFormat(standardFormatWithSampleRate: 44100, channels: 2)!
        outputBus = try AUAudioUnitBus(format: format)
        outputBus.maximumChannelCount = 2
        busArray = AUAudioUnitBusArray(audioUnit: self, busType: .output, busses: [outputBus])
        tree = Self.makeTree(instance)
        Synth2SetEditCallback(instance, Unmanaged.passUnretained(self).toOpaque()) { ctx, id, value, phase in
            guard let ctx else { return }
            let au = Unmanaged<OxoSynth2AudioUnit>.fromOpaque(ctx).takeUnretainedValue()
            guard let param = au.tree.parameter(withAddress: AUParameterAddress(id)) else { return }
            let type: AUParameterAutomationEventType = phase == 0 ? .touch : phase == 2 ? .release : .value
            param.setValue(phase == 1 ? AUValue(value) : param.value, originator: nil, atHostTime: 0, eventType: type)
        }
    }

    deinit { Synth2Destroy(instance) }

    private static func makeTree(_ instance: UnsafeMutableRawPointer) -> AUParameterTree {
        var params: [AUParameter] = []
        for id in 0..<Int(Synth2ParamCount()) {
            var lo: Int32 = 0, hi: Int32 = 0, def: Int32 = 0
            Synth2ParamRange(Int32(id), &lo, &hi, &def)
            var flags: AudioUnitParameterOptions = [.flag_IsReadable, .flag_IsWritable]
            if Synth2ParamAutomatable(Int32(id)) == 0 { flags.insert(.flag_ExpertMode) }
            let p = AUParameterTree.createParameter(
                withIdentifier: String(cString: Synth2ParamKey(Int32(id))),
                name: String(cString: Synth2ParamName(Int32(id))),
                address: AUParameterAddress(id), min: AUValue(lo), max: AUValue(hi),
                unit: .generic, unitName: nil, flags: flags, valueStrings: nil, dependentParameters: nil)
            p.value = AUValue(Synth2GetParam(instance, Int32(id))) // the startup program, not `def`
            params.append(p)
        }
        let tree = AUParameterTree.createTree(withChildren: params)
        tree.implementorValueObserver = { param, value in
            Synth2SetParam(instance, Int32(param.address), Int32(value.rounded()))
        }
        tree.implementorValueProvider = { param in AUValue(Synth2GetParam(instance, Int32(param.address))) }
        tree.implementorStringFromValueCallback = { param, valuePtr in
            var buf = [CChar](repeating: 0, count: 64)
            Synth2FormatParam(instance, Int32(param.address), Int32((valuePtr?.pointee ?? param.value).rounded()), &buf, 64)
            return String(cString: buf)
        }
        return tree
    }

    var hooks: OxoSynth2Hooks { Synth2MakeHooks(instance) }

    public override var outputBusses: AUAudioUnitBusArray { busArray }
    public override var parameterTree: AUParameterTree? {
        get { tree }
        set {}
    }
    public override var channelCapabilities: [NSNumber]? { [0, 2] }
    public override var canProcessInPlace: Bool { true }

    public override func allocateRenderResources() throws {
        try super.allocateRenderResources()
        transport.musicalContext = musicalContextBlock
        transport.transportState = transportStateBlock
        Synth2Prepare(instance, outputBus.format.sampleRate, maximumFramesToRender)
    }

    public override var internalRenderBlock: AUInternalRenderBlock {
        let instance = self.instance
        let transport = self.transport
        return { _, timestamp, frameCount, _, outputData, events, _ in
            var tempo = 120.0, beat = 0.0
            var playing: Int32 = 0
            if let context = transport.musicalContext {
                var t = 0.0, b = 0.0
                if context(&t, nil, nil, &b, nil, nil) {
                    tempo = t
                    beat = b
                }
            }
            if let state = transport.transportState {
                var flags = AUHostTransportStateFlags()
                if state(&flags, nil, nil, nil) { playing = flags.contains(.moving) ? 1 : 0 }
            }
            Synth2Render(instance, frameCount, outputData, events, Int64(timestamp.pointee.mSampleTime), tempo, beat, playing)
            return noErr
        }
    }

    // Session state = the patch itself (same chunk as the VST3).
    public override var fullState: [String: Any]? {
        get {
            var state = super.fullState ?? [:]
            let size = Synth2EncodeState(instance, nil, 0)
            var bytes = [UInt8](repeating: 0, count: Int(size))
            _ = Synth2EncodeState(instance, &bytes, size)
            state[Self.stateKey] = Data(bytes)
            state[Self.uiWidthModeKey] = uiWidthMode
            return state
        }
        set {
            if let mode = newValue?[Self.uiWidthModeKey] as? Bool { uiWidthMode = mode }
            guard let data = newValue?[Self.stateKey] as? Data else { return }
            let ok = data.withUnsafeBytes { raw -> Bool in
                guard let base = raw.bindMemory(to: UInt8.self).baseAddress else { return false }
                return Synth2RestoreState(instance, base, Int32(data.count)) != 0
            }
            guard ok else { return }
            // keep the tree (what hosts read) in line with the restored patch
            for p in tree.allParameters { p.value = AUValue(Synth2GetParam(instance, Int32(p.address))) }
        }
    }
}
