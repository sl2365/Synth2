import CoreAudioKit
import SwiftUI

/// AUv3 factory + editor: the same SwiftUI panel as the mac VST3
/// (Shared/SynthPanelView) over the instance's C hooks.
public final class AudioUnitViewController: AUViewController, AUAudioUnitFactory {
    private var audioUnit: OxoSynth2AudioUnit?
    private var hosting: UIHostingController<SynthPanelView>?
    private var model: SynthModel?

    public func createAudioUnit(with componentDescription: AudioComponentDescription) throws -> AUAudioUnit {
        let au = try OxoSynth2AudioUnit(componentDescription: componentDescription, options: [])
        audioUnit = au
        DispatchQueue.main.async { self.connectUI() }
        return au
    }

    public override func viewDidLoad() {
        super.viewDidLoad()
        view.backgroundColor = .black
        preferredContentSize = CGSize(width: 924, height: 672) // 1.2 x the 770 x 560 design
        connectUI()
    }

    private func connectUI() {
        guard isViewLoaded, hosting == nil, let au = audioUnit else { return }
        let model = SynthModel(host: HooksSynthHost(hooks: au.hooks))
        model.uiWidthModeStorage = (get: { [weak au] in au?.uiWidthMode ?? false },
                                    set: { [weak au] in au?.chooseUIWidthMode($0) })
        let host = UIHostingController(rootView: SynthPanelView(model: model))
        host.view.translatesAutoresizingMaskIntoConstraints = false
        host.view.backgroundColor = .clear
        // No safe-area handling: when the keyboard shows (a text field in a
        // dialog), keyboard avoidance shrank the view and the panel scaled
        // down to fit, nearly unreadable. The panel keeps its size; the text
        // fields sit at the top of their dialogs, above the keyboard.
        host.safeAreaRegions = []
        addChild(host)
        view.addSubview(host.view)
        NSLayoutConstraint.activate([
            host.view.leadingAnchor.constraint(equalTo: view.leadingAnchor),
            host.view.trailingAnchor.constraint(equalTo: view.trailingAnchor),
            host.view.topAnchor.constraint(equalTo: view.topAnchor),
            host.view.bottomAnchor.constraint(equalTo: view.bottomAnchor),
        ])
        host.didMove(toParent: self)
        hosting = host
        self.model = model
    }

    // Learn mode never survives the editor (an armed control left behind
    // would re-bind the next CC).
    public override func viewWillDisappear(_ animated: Bool) {
        super.viewWillDisappear(animated)
        model?.exitLearn()
    }

    deinit { model?.invalidate() }
}
