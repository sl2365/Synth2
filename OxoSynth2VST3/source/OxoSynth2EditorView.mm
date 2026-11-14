#include "OxoSynth2EditorView.h"

#include "OxoSynth2Controller.h"
#include "EditorCore/OxoSynth2Bridge.h"
#include "pluginterfaces/gui/iplugview.h"

#import <AppKit/AppKit.h>

#include <algorithm>
#include <cmath>

using namespace Steinberg;

namespace oxo {

namespace {
constexpr int kDefaultWidth = 700; // 100 % = SynthPanelView.unitWidth (Synth1 at 150 %)
constexpr int kMinWidth = 525;     // 75 %, the smallest UI size step
NSString *const kWidthKey = @"oxo.synth2.editorWidth";

int HeightFor(int width) { return (int)std::lround(width * OxoSynth2EditorAspect()); }
} // namespace

OxoSynth2EditorView::OxoSynth2EditorView(OxoSynth2Controller *controller)
    : CPluginView(nullptr), controller_(controller) {
    const NSInteger saved = [[NSUserDefaults standardUserDefaults] integerForKey:kWidthKey];
    const int width = saved >= kMinWidth ? (int)saved : kDefaultWidth;
    rect = ViewRect(0, 0, width, HeightFor(width));
}

OxoSynth2EditorView::~OxoSynth2EditorView() { removedFromParent(); }

tresult PLUGIN_API OxoSynth2EditorView::isPlatformTypeSupported(FIDString type) {
    return type && strcmp(type, kPlatformTypeNSView) == 0 ? kResultTrue : kResultFalse;
}

tresult PLUGIN_API OxoSynth2EditorView::checkSizeConstraint(ViewRect *r) {
    if (!r) return kInvalidArgument;
    const int width = std::max(r->getWidth(), kMinWidth);
    r->right = r->left + width;
    r->bottom = r->top + HeightFor(width);
    return kResultTrue;
}

tresult PLUGIN_API OxoSynth2EditorView::onSize(ViewRect *newSize) {
    if (!newSize) return kInvalidArgument;
    rect = *newSize;
    [[NSUserDefaults standardUserDefaults] setInteger:rect.getWidth() forKey:kWidthKey];
    if (nsView_) {
        NSView *view = (__bridge NSView *)nsView_;
        [view setFrame:NSMakeRect(0, 0, rect.getWidth(), rect.getHeight())];
    }
    return kResultTrue;
}

void OxoSynth2EditorView::attachedToParent() {
    NSView *parent = (__bridge NSView *)systemWindow;
    if (!parent || !controller_ || nsView_) return;
    OxoSynth2Hooks hooks = controller_->MakeHooks();
    void *created = OxoSynth2EditorCreate(&hooks, rect.getWidth(), rect.getHeight(), this, [](void *ctx, double width) {
        static_cast<OxoSynth2EditorView *>(ctx)->requestHostResize(width);
    });
    if (!created) return;
    NSView *view = (__bridge NSView *)created;
    view.frame = parent.bounds;
    view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    [parent addSubview:view];
    nsView_ = created;
}

void OxoSynth2EditorView::removedFromParent() {
    if (nsView_) {
        OxoSynth2EditorRelease(nsView_);
        nsView_ = nullptr;
    }
}

void OxoSynth2EditorView::requestHostResize(double width) {
    if (!plugFrame) return;
    ViewRect r(0, 0, (int32)std::lround(width), 0);
    checkSizeConstraint(&r);
    plugFrame->resizeView(this, &r);
}

} // namespace oxo
