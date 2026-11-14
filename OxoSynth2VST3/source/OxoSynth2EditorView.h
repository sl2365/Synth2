#pragma once

#include "public.sdk/source/common/pluginview.h"

namespace oxo {

class OxoSynth2Controller;

// IPlugView hosting the SwiftUI editor (NSHostingView made by the Swift
// bridge) in the host's NSView. Resizable, aspect-locked to the design
// canvas (the SwiftUI root scales itself to the width).
class OxoSynth2EditorView : public Steinberg::CPluginView {
public:
    explicit OxoSynth2EditorView(OxoSynth2Controller *controller);
    ~OxoSynth2EditorView() SMTG_OVERRIDE;

    Steinberg::tresult PLUGIN_API isPlatformTypeSupported(Steinberg::FIDString type) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API canResize() SMTG_OVERRIDE { return Steinberg::kResultTrue; }
    Steinberg::tresult PLUGIN_API checkSizeConstraint(Steinberg::ViewRect *rect) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API onSize(Steinberg::ViewRect *newSize) SMTG_OVERRIDE;
    void attachedToParent() SMTG_OVERRIDE;
    void removedFromParent() SMTG_OVERRIDE;

private:
    // The logo's UI size menu: the host owns the resize (plugFrame->resizeView
    // then onSize), clamped through checkSizeConstraint.
    void requestHostResize(double width);

    OxoSynth2Controller *controller_ = nullptr;
    void *nsView_ = nullptr; // retained NSView* from the Swift bridge
};

} // namespace oxo
