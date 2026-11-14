#include "WindowsEditor.hpp"
#include "OxoSynth2VST3/source/OxoSynth2Controller.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/base/keycodes.h"
#include "OxoSynth2Identity.h"
#include "EditorCore/Storage.hpp"
#include "EditorCore/Trash.hpp"
#include "EditorCore/UpdateCheck.hpp"
#include "WindowsZoomSettings.hpp"
#include "WindowsThemeEditor.hpp"
#include "WindowsPrompt.hpp"
#include <shellapi.h>
#include <shlobj.h>
#include <commdlg.h>
#include <cstring>
#include <windowsx.h>

#include <algorithm>
#include <numeric>
#include <unordered_map>
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <cwctype>
#include <sstream>
#include <fstream>
#include <string_view>
#include <vector>
#include <iomanip>
#include <limits>
#include <locale>
#include <iterator>

using namespace Steinberg;
using namespace Gdiplus;

namespace oxo {
namespace {
constexpr float kDesignWidth = 770.f, kDesignHeight = 560.f;
constexpr int kMinWidth = 578;  // 75% of the 770-pixel design width, rounded up
constexpr int kMaxWidth = 2310; // 300% of the design width
constexpr float kPi = 3.14159265358979323846f;
constexpr int kPanel=themes::kPanel, kHeader=themes::kHeader, kHeaderText=themes::kHeaderText,
              kLabel=themes::kLabel, kKnob=themes::kKnobBody, kKnobHighlight=themes::kKnobGradient,
              kDial=themes::kKnobDial, kButton=themes::kButton, kLed=themes::kLed, kRadio=themes::kRadio,
              kDisplayText=themes::kDisplayText, kDisplayTop=themes::kDisplayTop,
              kDisplayBottom=themes::kDisplayBottom;
constexpr int kThemeCommand=1000; // theme menu items: kThemeCommand + index into the picks
constexpr const wchar_t* kProjectURL=L"https://github.com/chab/oxo_synth2";
// A plain Win32 About dialog: no Common Controls v6 import. VST3 modules
// must be loadable by Steinberg's moduleinfotool and hosts without a v6
// Common Controls activation context.
constexpr const wchar_t* kForkURL=L"https://github.com/sl2365/Synth2/tree/master";
constexpr int kAboutOxoLink=901, kAboutForkLink=902;
constexpr int kAboutTitle=903, kAboutCredits=904;
constexpr int kAboutOxoLabel=905, kAboutForkLabel=906;
struct AboutDialogState {
    HWND editorWindow=nullptr; // actual VST3 panel, not the host's full window
    HFONT titleFont=nullptr;
    HFONT headingFont=nullptr;
    HFONT linkFont=nullptr;
};
INT_PTR CALLBACK AboutDialogProc(HWND dialog,UINT msg,WPARAM wp,LPARAM lp) {
    if(msg==WM_INITDIALOG) {
        auto* state=reinterpret_cast<AboutDialogState*>(lp);
        SetWindowLongPtrW(dialog,DWLP_USER,reinterpret_cast<LONG_PTR>(state));
        LOGFONTW base{};
        if(GetObjectW(GetStockObject(DEFAULT_GUI_FONT),sizeof(base),&base)) {
            HDC dc=GetDC(dialog);
            const int dpi=dc ? GetDeviceCaps(dc,LOGPIXELSY) : 96;
            if(dc)ReleaseDC(dialog,dc);
            LOGFONTW title=base;
            title.lfHeight=-MulDiv(12,dpi,72); // 12 pt, bold (dialog body: 9 pt)
            title.lfWeight=FW_BOLD;
            state->titleFont=CreateFontIndirectW(&title);
            LOGFONTW heading=base;
            heading.lfWeight=FW_BOLD;
            state->headingFont=CreateFontIndirectW(&heading);
            LOGFONTW link=base;
            link.lfUnderline=TRUE;
            state->linkFont=CreateFontIndirectW(&link);
            if(state->titleFont)
                SendDlgItemMessageW(dialog,kAboutTitle,WM_SETFONT,
                                    reinterpret_cast<WPARAM>(state->titleFont),TRUE);
            if(state->headingFont)
                SendDlgItemMessageW(dialog,kAboutCredits,WM_SETFONT,
                                    reinterpret_cast<WPARAM>(state->headingFont),TRUE);
            if(state->linkFont) {
                for(const int id:{kAboutOxoLink,kAboutForkLink})
                    SendDlgItemMessageW(dialog,id,WM_SETFONT,
                                        reinterpret_cast<WPARAM>(state->linkFont),TRUE);
            }
        }
        // Measure actual rendered glyph widths, not estimated dialog units.
        // SS_NOTIFY applies only to the tightly fitted link window: the blank
        // space after the neighbouring label must never open a browser.
        const auto fitInlineLink=[&](int labelId,int linkId) {
            const HWND label=GetDlgItem(dialog,labelId);
            const HWND link=GetDlgItem(dialog,linkId);
            if(!label || !link)return;
            HDC dc=GetDC(dialog);
            if(!dc)return;
            const auto measure=[&](HWND control,const wchar_t* value) {
                SIZE result{};
                const HFONT font=reinterpret_cast<HFONT>(SendMessageW(control,WM_GETFONT,0,0));
                HGDIOBJ oldFont=font?SelectObject(dc,font):nullptr;
                GetTextExtentPoint32W(dc,value,static_cast<int>(wcslen(value)),&result);
                if(oldFont)SelectObject(dc,oldFont);
                return result;
            };
            wchar_t labelText[128]{};
            wchar_t linkText[128]{};
            GetWindowTextW(label,labelText,128);
            GetWindowTextW(link,linkText,128);
            const SIZE labelSize=measure(label,labelText);
            const SIZE spaceSize=measure(label,L" ");
            const SIZE linkSize=measure(link,linkText);
            ReleaseDC(dialog,dc);
            RECT rc{};
            GetWindowRect(label,&rc);
            POINT origin{rc.left,rc.top};
            ScreenToClient(dialog,&origin);
            const int height=rc.bottom-rc.top;
            SetWindowPos(label,nullptr,origin.x,origin.y,labelSize.cx,height,
                         SWP_NOZORDER|SWP_NOACTIVATE);
            SetWindowPos(link,nullptr,origin.x+labelSize.cx+spaceSize.cx,origin.y,
                         linkSize.cx,height,SWP_NOZORDER|SWP_NOACTIVATE);
        };
        fitInlineLink(kAboutOxoLabel,kAboutOxoLink);
        fitInlineLink(kAboutForkLabel,kAboutForkLink);
        // Centre within Synth2's own editor, not the DAW or desktop.
        if(state && IsWindow(state->editorWindow)) {
            RECT panel{},box{};
            if(GetWindowRect(state->editorWindow,&panel) && GetWindowRect(dialog,&box)) {
                const int w=box.right-box.left,h=box.bottom-box.top;
                const int x=panel.left+((panel.right-panel.left)-w)/2;
                const int y=panel.top+((panel.bottom-panel.top)-h)/2;
                SetWindowPos(dialog,nullptr,x,y,0,0,
                             SWP_NOZORDER|SWP_NOSIZE|SWP_NOACTIVATE);
            }
        }
        return TRUE;
    }
    if(msg==WM_CTLCOLORSTATIC) {
        const HWND control=reinterpret_cast<HWND>(lp);
        if(control==GetDlgItem(dialog,kAboutOxoLink) ||
           control==GetDlgItem(dialog,kAboutForkLink)) {
            HDC dc=reinterpret_cast<HDC>(wp);
            SetTextColor(dc,RGB(0,102,204));
            SetBkMode(dc,TRANSPARENT);
            return reinterpret_cast<INT_PTR>(GetSysColorBrush(COLOR_BTNFACE));
        }
    }
    if(msg==WM_SETCURSOR) {
        const HWND control=reinterpret_cast<HWND>(wp);
        if(control==GetDlgItem(dialog,kAboutOxoLink) ||
           control==GetDlgItem(dialog,kAboutForkLink)) {
            SetCursor(LoadCursorW(nullptr,IDC_HAND));
            return TRUE;
        }
    }
    if(msg==WM_COMMAND) {
        const int id=LOWORD(wp);
        if(HIWORD(wp)==STN_CLICKED && (id==kAboutOxoLink || id==kAboutForkLink)) {
            ShellExecuteW(dialog,L"open",id==kAboutOxoLink?kProjectURL:kForkURL,
                          nullptr,nullptr,SW_SHOWNORMAL);
            return TRUE;
        }
        if(id==IDOK || id==IDCANCEL) {
            EndDialog(dialog,id);
            return TRUE;
        }
    }
    if(msg==WM_DESTROY) {
        auto* state=reinterpret_cast<AboutDialogState*>(GetWindowLongPtrW(dialog,DWLP_USER));
        if(state) {
            if(state->titleFont)DeleteObject(state->titleFont);
            if(state->headingFont)DeleteObject(state->headingFont);
            if(state->linkFont)DeleteObject(state->linkFont);
        }
    }
    return FALSE;
}
// Theme.lcdText (Shared/SynthControls.swift): fixed browser / LCD text.
constexpr int kLcdText=0xE0FFFF;
// Regions redrawn on their own (design coordinates).
const RectF kLcdRect(50,484,432,70), kGridRect(548,505,216,28), kBrowserRect(6,34,758,444);

Color rgb(int rgbValue) { return Color(255, (rgbValue>>16)&255, (rgbValue>>8)&255, rgbValue&255); }
Color muted(const Color& c, float factor) {
    return Color(255, BYTE(c.GetR()*factor), BYTE(c.GetG()*factor), BYTE(c.GetB()*factor));
}
void fill(Graphics& g, const Color& c, float x,float y,float w,float h) {
    SolidBrush b(c); g.FillRectangle(&b, x,y,w,h);
}
void stroke(Graphics& g, const Color& c, float x,float y,float w,float h,float width=1) {
    Pen p(c,width); g.DrawRectangle(&p,x,y,w,h);
}
// The drag cursor over a bank row: the arrow with a "+" box (the copy
// cursor macOS shows for the mac's .copy drop proposal; Windows has no
// stock one outside OLE drag and drop). 32x32 ARGB, hotspot = the tip.
HCURSOR makeCopyCursor() {
    constexpr int size=32;
    BITMAPV5HEADER header{};
    header.bV5Size=sizeof(header);
    header.bV5Width=size;header.bV5Height=-size; // top-down
    header.bV5Planes=1;header.bV5BitCount=32;header.bV5Compression=BI_BITFIELDS;
    header.bV5RedMask=0x00FF0000;header.bV5GreenMask=0x0000FF00;header.bV5BlueMask=0x000000FF;header.bV5AlphaMask=0xFF000000;
    void* bits=nullptr;
    HDC screen=GetDC(nullptr);
    HBITMAP color=CreateDIBSection(screen,reinterpret_cast<BITMAPINFO*>(&header),DIB_RGB_COLORS,&bits,nullptr,0);
    ReleaseDC(nullptr,screen);
    if(!color || !bits) {if(color) DeleteObject(color);return nullptr;}
    {
        Bitmap canvas(size,size,size*4,PixelFormat32bppPARGB,static_cast<BYTE*>(bits));
        Graphics g(&canvas);
        g.Clear(Color(0,0,0,0));
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        const PointF arrow[]={{1.5f,1.5f},{1.5f,17.5f},{5.5f,13.5f},{8.5f,20.f},{11.f,19.f},{8.f,12.5f},{13.5f,12.5f}};
        SolidBrush white(Color(255,255,255,255));
        Pen black(Color(255,0,0,0),1.f);
        g.FillPolygon(&white,arrow,7);
        g.DrawPolygon(&black,arrow,7);
        g.SetSmoothingMode(SmoothingModeNone);
        g.FillRectangle(&white,14,18,10,10);
        g.DrawRectangle(&black,14,18,10,10);
        SolidBrush ink(Color(255,0,0,0)); // the "+"
        g.FillRectangle(&ink,16,22,7,2);
        g.FillRectangle(&ink,18,20,2,7);
    }
    const std::vector<BYTE> zeros(size*size/8,0);
    HBITMAP mask=CreateBitmap(size,size,1,1,zeros.data());
    ICONINFO info{FALSE,1,1,mask,color};
    HCURSOR cursor=reinterpret_cast<HCURSOR>(CreateIconIndirect(&info));
    DeleteObject(mask);DeleteObject(color);
    return cursor;
}
// Bank list order (UI only: hook indices, slot numbers and host state are
// untouched): digits compare as numbers ("Bank 2" < "Bank 10"), letters
// case-insensitively.
bool NaturalLess(const std::wstring& a, const std::wstring& b) {
    size_t i=0,j=0;
    while(i<a.size() && j<b.size()) {
        if(a[i]>=L'0' && a[i]<=L'9' && b[j]>=L'0' && b[j]<=L'9') {
            const size_t ai=i,bj=j;
            while(i<a.size() && a[i]>=L'0' && a[i]<=L'9') ++i;
            while(j<b.size() && b[j]>=L'0' && b[j]<=L'9') ++j;
            size_t nzA=ai,nzB=bj;
            while(nzA<i && a[nzA]==L'0') ++nzA;
            while(nzB<j && b[nzB]==L'0') ++nzB;
            const size_t la=i-nzA,lb=j-nzB;
            if(la!=lb) return la<lb;
            for(size_t k=0;k<la;k++) if(a[nzA+k]!=b[nzB+k]) return a[nzA+k]<b[nzB+k];
            // Stable equal-value tie break: fewer leading zeroes first.
            if(i-ai!=j-bj) return i-ai<j-bj;
        } else {
            const wchar_t ac=wchar_t(std::towlower(a[i])),bc=wchar_t(std::towlower(b[j]));
            if(ac!=bc) return ac<bc;
            ++i;++j;
        }
    }
    return i==a.size() && j!=b.size();
}
} // namespace

// Text is drawn as antialiased outlines in design-space coordinates: unlike
// grid-fitted glyph bitmaps, the letter positions do not snap to different
// pixels while the VST3 view is resized, and the ink bounds centre short LCD
// values. Fixed-size, natural-proportion text: long labels are ellipsized
// instead of shrinking the font; a shared ascender / descender reference
// keeps every baseline stable. Building an outline (AddString) is the
// expensive part, so each one is built ONCE per (text, size, box width,
// font) and then only filled.
class TextCache {
public:
    struct Shape {
        std::unique_ptr<GraphicsPath> path; // at the origin; null = nothing fits
        RectF ink;                          // its ink bounds
        float refY = 0, refH = 0;           // the line's reference ink ("Agjpqy")
    };
    TextCache() : segoe_(L"Segoe UI"), consolas_(L"Consolas") {
        format_.SetFormatFlags(StringFormatFlagsNoWrap | StringFormatFlagsNoClip);
    }
    const Shape& get(const std::wstring& s, float fontSize, float maxInkWidth, bool mono) {
        std::wstring key=s;
        key+=L'\x1f';key+=std::to_wstring(std::lround(fontSize*100));
        key+=L'\x1f';key+=std::to_wstring(std::lround(maxInkWidth*100));
        key+=mono?L'm':L's';
        if(const auto it=shapes_.find(key); it!=shapes_.end()) return it->second;
        if(shapes_.size()>=kMaxShapes) shapes_.clear(); // LCD readouts / names come and go
        return shapes_.emplace(std::move(key),build(s,fontSize,maxInkWidth,mono)).first->second;
    }

private:
    static constexpr size_t kMaxShapes=4096;
    bool make(const std::wstring& s, float size, bool mono, GraphicsPath& path, RectF& bounds) const {
        return path.AddString(s.c_str(),-1,mono?&consolas_:&segoe_,FontStyleBold,size,PointF(0.f,0.f),&format_)==Ok &&
               path.GetBounds(&bounds)==Ok;
    }
    Shape build(const std::wstring& s, float size, float maxInkWidth, bool mono) const {
        Shape shape;
        auto path=std::make_unique<GraphicsPath>();
        if(!make(s,size,mono,*path,shape.ink)) return shape;
        if(shape.ink.Width>maxInkWidth) {
            // Reserve the full-size ellipsis and find the longest fitting prefix.
            const std::wstring ellipsis(1,wchar_t(0x2026));
            GraphicsPath dots;
            RectF dotBounds{};
            if(!make(ellipsis,size,mono,dots,dotBounds) || dotBounds.Width>maxInkWidth) return shape;
            size_t low=0,high=s.size();
            while(low<high) {
                const size_t mid=low+(high-low+1)/2;
                GraphicsPath probe;
                RectF probeBounds{};
                if(!make(s.substr(0,mid)+ellipsis,size,mono,probe,probeBounds)) return shape;
                if(probeBounds.Width<=maxInkWidth) low=mid; else high=mid-1;
            }
            std::wstring shortened=s.substr(0,low);
            // Windows wchar_t is UTF-16; don't cut a surrogate pair in half.
            if(!shortened.empty() && shortened.back()>=wchar_t(0xD800) && shortened.back()<=wchar_t(0xDBFF))
                shortened.pop_back();
            path->Reset();
            if(!make(shortened+ellipsis,size,mono,*path,shape.ink)) return shape;
        }
        GraphicsPath reference;
        RectF metrics{};
        if(!make(L"Agjpqy",size,mono,reference,metrics)) return shape;
        shape.refY=metrics.Y;
        shape.refH=metrics.Height;
        shape.path=std::move(path);
        return shape;
    }
    FontFamily segoe_, consolas_;
    StringFormat format_;
    std::unordered_map<std::wstring, Shape> shapes_;
};

namespace {
Color blend(const Color& background, const Color& foreground, float amount) {
    amount=std::clamp(amount,0.f,1.f);
    auto part=[&](BYTE back, BYTE front) {
        return BYTE(std::lround(back*(1.f-amount)+front*amount));
    };
    return Color(255,part(background.GetR(),foreground.GetR()),
                 part(background.GetG(),foreground.GetG()),
                 part(background.GetB(),foreground.GetB()));
}
void roundedPath(GraphicsPath& path,float x,float y,float w,float h,float radius) {
    const float d=std::min(2.f*radius,std::min(w,h));
    path.AddArc(x,y,d,d,180,90);
    path.AddArc(x+w-d,y,d,d,270,90);
    path.AddArc(x+w-d,y+h-d,d,d,0,90);
    path.AddArc(x,y+h-d,d,d,90,90);
    path.CloseFigure();
}
void roundedFill(Graphics& g, const Color& color,float x,float y,float w,float h,float radius) {
    GraphicsPath path;roundedPath(path,x,y,w,h,radius);
    SolidBrush brush(color);g.FillPath(&brush,&path);
}
void roundedOutline(Graphics& g, const Color& color,float x,float y,float w,float h,float radius,float width=1.f) {
    GraphicsPath path;roundedPath(path,x,y,w,h,radius);
    Pen pen(color,width);g.DrawPath(&pen,&path);
}
// Match the original SwiftUI LED shadow: bloom only when the LED is on.
// This is a small, transparent radial halo, not an enlarged solid LED.
void led(Graphics& g, const Color& c, float x,float y,float radius=3, bool glowing=false) {
    if(glowing) {
        // Two small translucent SolidBrush halos avoid the PathGradientBrush
        // used in v10, which is suspected of hanging some VST3 host paints.
        // SolidBrush/FillEllipse are also used by the working v9 editor.
        const float outerRadius=radius+3.f;
        const float innerRadius=radius+1.5f;
        SolidBrush outer(Color(22,c.GetR(),c.GetG(),c.GetB()));
        SolidBrush inner(Color(40,c.GetR(),c.GetG(),c.GetB()));
        g.FillEllipse(&outer,x-outerRadius,y-outerRadius,2.f*outerRadius,2.f*outerRadius);
        g.FillEllipse(&inner,x-innerRadius,y-innerRadius,2.f*innerRadius,2.f*innerRadius);
    }
    SolidBrush b(c);
    g.FillEllipse(&b,x-radius,y-radius,2*radius,2*radius);
    Pen edge(Color(130,0,0,0),.65f);
    g.DrawEllipse(&edge,x-radius,y-radius,2*radius,2*radius);
}
void darkButton(Graphics& g, Color face, float x,float y,float w,float h, bool hovered=false) {
    GraphicsPath path;roundedPath(path,x,y,w,h,3.f);
    // Highlight the button face without changing its size or text layout.
    // Hover is visual only; existing click/parameter behaviour is unchanged.
    if (hovered) face=blend(face,rgb(0xFFFFFF),.07f);
    LinearGradientBrush grad(PointF(x,y),PointF(x,y+h),blend(face,rgb(0xFFFFFF),.10f),face);
    g.FillPath(&grad,&path);
    Pen edge(Color(170,0,0,0),1.f); // Identical outline when hovered.
    g.DrawPath(&edge,&path);
}
// Named custom themes live only in Synth2_Data/Themes/*.json.
// The independent theme_current.json remembers unsaved live colours and
// preferences between instances. themes.json is never accessed by Windows.
struct SharedThemes {
    themes::Store store{storage::Root()/"theme_current.json"};
    ULONGLONG writeDue=0;
    SharedThemes() { store.Load(); }
    void remember(const themes::Palette& p,const winvoice::Palette* extras=nullptr) {
        bool changed=store.Remember(p);
        if(extras)changed=winvoice::Remember(store,*extras)||changed;
        if(changed)writeDue=GetTickCount64()+400;
    }
    void flush(bool now) {
        if(writeDue && (now || GetTickCount64()>=writeDue)) {writeDue=0;store.Write();}
    }
    std::vector<winthemefiles::Entry> userThemes() const {
        return winthemefiles::List(storage::Root()/"Themes");
    }
    themes::Palette startPalette() {
        if(writeDue) flush(true);
        else store.Load();
        return store.Current();
    }
};
SharedThemes& sharedThemes() {
    static SharedThemes shared;
    return shared;
}

} // namespace

std::wstring WindowsEditor::toWide(const char* str) {
    if(!str || !*str) return {};
    int n=MultiByteToWideChar(CP_UTF8,0,str,-1,nullptr,0);
    if(n<=0) return L"?";
    std::wstring out(size_t(n),L'\0');
    MultiByteToWideChar(CP_UTF8,0,str,-1,out.data(),n);
    out.pop_back();
    return out;
}
std::string WindowsEditor::toUtf8(const std::wstring& s) {
    if(s.empty()) return {};
    int n=WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),nullptr,0,nullptr,nullptr);
    if(n<=0) return {};
    std::string out(size_t(n),'\0');
    WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),out.data(),n,nullptr,nullptr);
    return out;
}

WindowsEditor::WindowsEditor(OxoSynth2Controller* controller)
    : CPluginView(nullptr), controller_(controller) {
    // Prefer last accepted user zoom in the active standard/portable data folder.
    // Keep the original 700px (~91%) first-run default for compatibility.
    const int savedWidth=winzoom::LoadWidth(storage::Root(),700,kMinWidth,kMaxWidth);
    rect=ViewRect(0,0,savedWidth,int(std::lround(savedWidth*560.0/770.0)));
    if(controller_) hooks_=controller_->MakeHooks();
    favouritesReadable_=winfav::Load(storage::Root(),favourites_);
    favouritesLookup_.RebuildFavourites(favourites_);
    GdiplusStartupInput gdiplusInput;
    if(GdiplusStartup(&gdiplusToken_,&gdiplusInput,nullptr)==Ok) textCache_=std::make_unique<TextCache>();
    constructLayout();
    resolveLayoutGeometry();
    defaultLayout_=snapshotLayout();
    loadSavedLayout(); // optional; malformed layout never changes the factory geometry
    loadedLayout_=snapshotLayout();
    buildLearnRects();
    // SynthModel.init: the session's palette, else the last one picked;
    // either way pinned to the session.
    const auto restored=hooks_.themeText ? themes::FromStateText(hooks_.themeText(hooks_.ctx)) : std::nullopt;
    theme_=restored ? *restored : sharedThemes().startPalette();
    voiceGrid_=winvoice::Load(storage::Root(),theme_);
    if(const auto it=sharedThemes().store.CurrentExtras().find("presetTextColours");
       it!=sharedThemes().store.CurrentExtras().end())
        presetTextColours_=(it->second!="false");
    for(size_t i=0;i<colors_.size();++i) colors_[i]=rgb(int(theme_[i]));
    if(hooks_.setThemeText) hooks_.setThemeText(hooks_.ctx,themes::StateText(theme_).c_str());
}
WindowsEditor::~WindowsEditor() {
    sharedThemes().flush(true);
    if(themeEditor_)themeEditor_->Close();
    removedFromParent();
    if(copyCursor_) DestroyCursor(copyCursor_);
    textCache_.reset(); // before GDI+ goes
    if(gdiplusToken_) GdiplusShutdown(gdiplusToken_);
}
tresult PLUGIN_API WindowsEditor::isPlatformTypeSupported(FIDString type) {
    return type && strcmp(type,kPlatformTypeHWND)==0 ? kResultTrue : kResultFalse;
}
tresult PLUGIN_API WindowsEditor::checkSizeConstraint(ViewRect* r) {
    if(!r) return kInvalidArgument;
    int width=std::clamp(int(r->getWidth()),kMinWidth,kMaxWidth);
    r->right=r->left+width;
    r->bottom=r->top+int(std::lround(width*560.0/770.0));
    return kResultTrue;
}
tresult PLUGIN_API WindowsEditor::onSize(ViewRect* r) {
    if(!r) return kInvalidArgument;
    rect=*r;
    layoutChild();
    // Only a zoom explicitly requested in this editor is stored. Some hosts
    // send an initial onSize at their own default; do not overwrite the user's
    // previous preference with that initial host notification.
    if(!resizeDragging_ && requestedZoomWidth_==rect.getWidth()) {
        requestedZoomWidth_=-1;
        winzoom::SaveWidth(storage::Root(),rect.getWidth(),kMinWidth,kMaxWidth);
    }
    return kResultTrue;
}
win::WindowClass& WindowsEditor::windowClass() {
    static win::WindowClass c(L"OxoSynth2WindowsEditorView",&WindowsEditor::wndProc,CS_DBLCLKS);
    return c;
}
void WindowsEditor::attachedToParent() {
    if(window_ || !systemWindow || !windowClass().Acquire()) return;
    window_=CreateWindowExW(0,windowClass().Name(),L"",WS_CHILD|WS_VISIBLE|WS_CLIPSIBLINGS,
                          0,0,rect.getWidth(),rect.getHeight(),
                          reinterpret_cast<HWND>(systemWindow),nullptr,win::ModuleInstance(),this);
    if(!window_) {windowClass().Release();return;}
    for(size_t id=0;id<knownValues_.size();++id) knownValues_[id]=value(int(id));
    SetTimer(window_,1,50,nullptr);
    // Critical: Json() and loadedProgram() call EnsureScanned(), which
    // scans all banks synchronously. Never call them before a background
    // scan has produced a usable revision (large libraries took ~1 min).
    const int revision=hooks_.libraryRevision ? hooks_.libraryRevision(hooks_.ctx) : 0;
    if(revision>0) { knownRevision_=revision; refreshBanks(); }
    // As the Mac editor: rescan in the background on every open, so banks
    // changed in Explorer (or zips dropped in) show up by themselves; the
    // saved index keeps the list usable meanwhile, the poll swaps it in.
    if(hooks_.startLibraryScan) hooks_.startLibraryScan(hooks_.ctx);
}
void WindowsEditor::removedFromParent() {
    updateCheck_.Cancel(); // no answer for a closed editor, no thread left behind
    flash({});
    if(editName_) {DestroyWindow(editName_);editName_=nullptr;}
    if(window_) {KillTimer(window_,1);DestroyWindow(window_);window_=nullptr;windowClass().Release();}
    releaseBuffers(); // the editor object may outlive its window: no bitmaps while closed
}
void WindowsEditor::layoutChild() {
    if(window_) MoveWindow(window_,0,0,rect.getWidth(),rect.getHeight(),TRUE);
    updateEditControl();
}
void WindowsEditor::repaint() const {
    if(window_) InvalidateRect(window_,nullptr,FALSE);
}
int WindowsEditor::value(int id) const {
    return hooks_.getValue ? int(hooks_.getValue(hooks_.ctx,id)) : 0;
}
std::wstring WindowsEditor::formatted(int id) const {
    if(id==86 || id==88) {
        const int v=value(id);
        if((v>>8)==0xE0) return L"pitch bend";
        if((v>>8)==0xD0) return L"ch pressure";
        if((v>>8)==0xB0) return L"#"+std::to_wstring(v&0xFF)+L":cc";
        return L"none";
    }
    if(id==87 || id==89) {
        const int v=value(id);
        if(v<0) return L"none";
        return toWide(hooks_.paramName ? hooks_.paramName(hooks_.ctx,v) : "");
    }
    if(!hooks_.formatValue) return std::to_wstring(value(id));
    // formatValue uses a shared scratch buffer; consume immediately.
    return toWide(hooks_.formatValue(hooks_.ctx,id,value(id)));
}
std::wstring WindowsEditor::patchName() const { return toWide(hooks_.patchName ? hooks_.patchName(hooks_.ctx) : "Init"); }
void WindowsEditor::flash(const std::wstring& text,unsigned milliseconds) {
    if(text.empty() && flashText_.empty()) return;
    flashText_=text;
    flashUntil_=text.empty() ? 0 : GetTickCount64()+milliseconds;
    invalidate(kLcdRect);
}
// SynthModel.show: an edit from the panel names the param and its value
// for 1.5 s (host automation shows nothing).
void WindowsEditor::show(int id) {
    flash(toWide(hooks_.displayName ? hooks_.displayName(hooks_.ctx,id) : "")+L":  "+formatted(id));
}

void WindowsEditor::useSection(SectionId section) {
    buildingSection_=section;
}
const WindowsEditor::SectionLayout& WindowsEditor::sectionLayout(SectionId section) const {
    return sectionLayouts_[static_cast<size_t>(section)];
}
void WindowsEditor::addControl(Control control) {
    control.section=buildingSection_;
    if(buildingSection_!=SectionId::Fixed) {
        const auto& section=sectionLayout(buildingSection_);
        control.localX=control.x-section.x;
        control.localY=control.y-section.y;
    } else {
        control.localX=control.x;
        control.localY=control.y;
    }
    controls_.push_back(std::move(control));
}
bool WindowsEditor::isPortamentoControl(const Control& c) {
    return c.section==SectionId::Voice &&
           ((c.kind==Kind::Knob && c.id==39) ||
            (c.kind==Kind::Toggle && c.id==74) ||
            (c.kind==Kind::Label && c.label==L"portamento"));
}
bool WindowsEditor::compatibleAlignmentKinds(Kind first,Kind second) {
    // A Toggle and a Cycle are visually the same family of parameter
    // buttons. They must align with one another (ON/type/range, dest/track,
    // Filter vel/type), without snapping to unrelated knobs or labels.
    if(first==second)return true;
    const auto button=[](Kind kind) {return kind==Kind::Toggle || kind==Kind::Cycle;};
    return button(first) && button(second);
}
Gdiplus::RectF WindowsEditor::layoutControlBounds(const Control& c) const {
    if(c.kind!=Kind::Label || c.label.empty() || !textCache_)
        return {c.x,c.y,c.w,c.h};
    // Labels are drawn centred inside a legacy 80x16 rectangle. Use their
    // actual cached glyph outlines for edit-mode hit testing and selection;
    // do not alter their stored geometry or the existing layout.json format.
    constexpr float fontSize=10.8f; // text(..., 9.5f) uses max(10.8, 9.5*1.08)
    const auto& shape=textCache_->get(c.label,fontSize,c.w-2.f,false);
    if(!shape.path)return {c.x,c.y,c.w,c.h};
    const float left=c.x+(c.w-shape.ink.Width)*.5f;
    const float top=c.y+(c.h-shape.refH)*.5f+(shape.ink.Y-shape.refY);
    constexpr float padding=2.f;
    const float x=std::max(c.x,left-padding);
    const float y=std::max(c.y,top-padding);
    const float right=std::min(c.x+c.w,left+shape.ink.Width+padding);
    const float bottom=std::min(c.y+c.h,top+shape.ink.Height+padding);
    return {x,y,std::max(1.f,right-x),std::max(1.f,bottom-y)};
}
WindowsEditor::AlignAnchors WindowsEditor::alignmentAnchors(const Control& c) const {
    if(c.kind==Kind::Knob) {
        // The knob control rectangle includes the label below the face.
        // Align the *visible circle*, not that larger rectangle.
        const float diameter=c.w/1.3f;
        const float cx=c.x+c.w*.5f,cy=c.y+c.w*.5f;
        const float r=diameter*.5f;
        return {{cx-r,cx,cx+r},{cy-r,cy,cy+r}};
    }
    const auto r=layoutControlBounds(c);
    return {{r.X,r.X+r.Width*.5f,r.X+r.Width},
            {r.Y,r.Y+r.Height*.5f,r.Y+r.Height}};
}
void WindowsEditor::resolveLayoutGeometry() {
    // Draw, click, hover and MIDI Learn continue using Control.x/y. Keeping
    // this resolution central makes a future section move update them all.
    for(auto& control:controls_) {
        if(control.section==SectionId::Fixed) {
            control.x=control.localX;
            control.y=control.localY;
        } else {
            const auto& section=sectionLayout(control.section);
            control.x=section.x+control.localX;
            control.y=section.y+control.localY;
        }
    }
}

// Layout mode uses 100%-zoom coordinates, saved only in the current data folder.
// The VST3 state, presets, MIDI map and DSP are not involved.
WindowsEditor::LayoutSnapshot WindowsEditor::snapshotLayout() const {
    LayoutSnapshot s;
    s.sections=sectionLayouts_;
    s.separators=separatorLayouts_;
    s.portamento=portamentoLayout_;
    s.positions.reserve(controls_.size());
    for(const auto& c:controls_) s.positions.push_back({c.localX,c.localY});
    return s;
}
void WindowsEditor::applyLayout(const LayoutSnapshot& s) {
    if(s.positions.size()!=controls_.size()) return;
    sectionLayouts_=s.sections;
    separatorLayouts_=s.separators;
    portamentoLayout_=s.portamento;
    for(size_t i=0;i<controls_.size();++i) {
        controls_[i].localX=s.positions[i][0];
        controls_[i].localY=s.positions[i][1];
    }
    layoutChanged();
}
bool WindowsEditor::equalLayout(const LayoutSnapshot& a,const LayoutSnapshot& b) {
    if(a.positions.size()!=b.positions.size()) return false;
    // JSON coordinates are written to 0.01px. Float construction uses
    // slightly different rounding; do not prompt to save an unchanged file.
    const auto same=[](float x,float y){return std::abs(x-y)<.005f;};
    for(size_t i=0;i<a.sections.size();++i) {
        const auto& x=a.sections[i];const auto& y=b.sections[i];
        if(!same(x.x,y.x) || !same(x.y,y.y) || !same(x.w,y.w) || !same(x.h,y.h)) return false;
    }
    for(size_t i=0;i<a.separators.size();++i) {
        const auto& x=a.separators[i];const auto& y=b.separators[i];
        if(!same(x.x1,y.x1) || !same(x.y1,y.y1) || !same(x.x2,y.x2) || !same(x.y2,y.y2)) return false;
    }
    const auto& ap=a.portamento;const auto& bp=b.portamento;
    if(!same(ap.x,bp.x) || !same(ap.y,bp.y) || !same(ap.w,bp.w) || !same(ap.h,bp.h))return false;
    for(size_t i=0;i<a.positions.size();++i)
        if(!same(a.positions[i][0],b.positions[i][0]) || !same(a.positions[i][1],b.positions[i][1])) return false;
    return true;
}
void WindowsEditor::layoutChanged() {
    resolveLayoutGeometry();
    buildLearnRects();
    hoverChanged(-1,0);
    staticDirty_=true;
    repaint();
}

// The parser is deliberately strict: an incompatible or damaged layout file
// is never partially applied. It can be replaced only after manual removal.
bool WindowsEditor::loadSavedLayout() {
    invalidLayoutFile_=false;
    const auto root=storage::Root();
    if(root.empty()) return false;
    const auto path=root/L"layout.json";
    std::error_code ec;
    if(!std::filesystem::exists(path,ec) && !ec) return false;
    if(ec || !std::filesystem::is_regular_file(path,ec) || ec ||
       std::filesystem::file_size(path,ec)>256*1024 || ec) {
        invalidLayoutFile_=true;
        return false;
    }
    std::ifstream stream(path,std::ios::binary);
    if(!stream) {invalidLayoutFile_=true;return false;}
    const std::string bytes((std::istreambuf_iterator<char>(stream)),{});
    json::Value parsed;
    if(!stream.bad() && json::Parse(bytes,parsed) && parsed.IsObject()) {
        const auto* version=parsed.Find("version");
        // The initial 24px EQ/Sens update retained layout.json v1, but older
        // saved top-left positions refer to 22px knob rectangles. Tag newly
        // saved layouts so those positions can be distinguished on reload.
        const auto* knobGeometry=parsed.Find("knobGeometry");
        const bool currentKnobGeometry=knobGeometry &&
            knobGeometry->type==json::Value::Type::Number && knobGeometry->number==24;
        const auto* sections=parsed.Find("sections");
        const auto* controls=parsed.Find("controls");
        const auto* separators=parsed.Find("separators");
        const auto goodNumber=[](const json::Value& v,float low,float high) {
            return v.type==json::Value::Type::Number && std::isfinite(v.number) &&
                   v.number>=low && v.number<=high;
        };
        if(version && goodNumber(*version,1.f,1.f) &&
           (!knobGeometry || currentKnobGeometry) && sections && controls && separators &&
           sections->type==json::Value::Type::Array &&
           controls->type==json::Value::Type::Array &&
           separators->type==json::Value::Type::Array &&
           sections->array.size()==sectionLayouts_.size() &&
           // The Arp "As Played" and Filter "Notch" LEDs were appended,
           // so older files with either/both controls missing can be migrated.
           (controls->array.size()==controls_.size() ||
            (controls->array.size()+1==controls_.size() &&
             controls_.back().id==14 && controls_.back().value==5) ||
            (controls->array.size()+2==controls_.size() &&
             controls_[controls_.size()-2].id==31 && controls_[controls_.size()-2].value==5 &&
             controls_.back().id==14 && controls_.back().value==5)) &&
           separators->array.size()==separatorLayouts_.size()) {
            LayoutSnapshot trial=snapshotLayout();
            bool valid=true;
            for(size_t i=0;i<trial.sections.size() && valid;++i) {
                const auto& row=sections->array[i];
                if(row.type!=json::Value::Type::Array || row.array.size()!=4) {valid=false;break;}
                for(size_t j=0;j<4;++j) if(!goodNumber(row.array[j],0.f,770.f)) valid=false;
                if(!valid) break;
                auto& s=trial.sections[i];
                s.x=float(row.array[0].number);s.y=float(row.array[1].number);
                s.w=float(row.array[2].number);s.h=float(row.array[3].number);
                if(s.x<6 || s.y<34 || s.w<45 || s.h<24 || s.x+s.w>764 || s.y+s.h>478) valid=false;
            }
            for(size_t i=0;i<controls->array.size() && valid;++i) {
                const auto& row=controls->array[i];
                if(row.type!=json::Value::Type::Array || row.array.size()!=3 ||
                   !goodNumber(row.array[0],-1.f,100.f) ||
                   row.array[0].number!=double(controls_[i].id) ||
                   !goodNumber(row.array[1],-770.f,770.f) ||
                   !goodNumber(row.array[2],-560.f,560.f)) {valid=false;break;}
                trial.positions[i]={float(row.array[1].number),float(row.array[2].number)};
                const auto& ctl=controls_[i];
                if(!currentKnobGeometry && ctl.kind==Kind::Knob &&
                   (ctl.id==50 || ctl.id==51 || ctl.id==61 || ctl.id==62 ||
                    ctl.id==63 || ctl.id==60 || ctl.id==90)) {
                    // These seven knobs changed from 22 to 24px. A v1 layout
                    // may still carry their old top-left coordinates, which
                    // shifts their centres +1.3px and breaks exact alignment.
                    // Detect the legacy coordinates on each axis. Integer
                    // nudges and 4px snapped moves preserve this signature;
                    // coordinates already saved at 24px are left untouched.
                    constexpr float halfBoundsGrowth=(24.f-22.f)*.65f;
                    for(int axis=0;axis<2;++axis) {
                        const float oldRelativeToNew=trial.positions[i][axis] -
                            defaultLayout_.positions[i][axis] - halfBoundsGrowth;
                        if(std::abs(oldRelativeToNew-std::round(oldRelativeToNew))<.06f)
                            trial.positions[i][axis]-=halfBoundsGrowth;
                    }
                }
                const auto& sec=trial.sections[static_cast<size_t>(ctl.section==SectionId::Fixed ? SectionId::Oscillators : ctl.section)];
                const float x=ctl.section==SectionId::Fixed ? trial.positions[i][0] : sec.x+trial.positions[i][0];
                const float y=ctl.section==SectionId::Fixed ? trial.positions[i][1] : sec.y+trial.positions[i][1];
                // Some built-in labels deliberately extend slightly beyond
                // the panel edge (Wheel/MIDI). Allow a small drawing margin.
                if(ctl.section==SectionId::Fixed) {
                    if(trial.positions[i]!=defaultLayout_.positions[i]) valid=false;
                } else if(x<-40.f || y<27.f || x+ctl.w>800.f || y+ctl.h>500.f) valid=false;
            }
            for(size_t i=0;i<trial.separators.size() && valid;++i) {
                const auto& row=separators->array[i];
                if(row.type!=json::Value::Type::Array || row.array.size()!=4) {valid=false;break;}
                for(size_t j=0;j<4;++j) if(!goodNumber(row.array[j],-770.f,770.f)) valid=false;
                if(!valid) break;
                auto& s=trial.separators[i];
                s.x1=float(row.array[0].number);s.y1=float(row.array[1].number);
                s.x2=float(row.array[2].number);s.y2=float(row.array[3].number);
                const auto& sec=trial.sections[static_cast<size_t>(s.owner)];
                // Section borders may be resized independently of the
                // separators. Preserve their endpoints until the user moves
                // them, provided they remain within the editable canvas.
                if(sec.x+s.x1<0 || sec.y+s.y1<30 || sec.x+s.x2<0 || sec.y+s.y2<30 ||
                   sec.x+s.x1>770 || sec.x+s.x2>770 || sec.y+s.y1>485 || sec.y+s.y2>485)
                    valid=false;
            }
            // Append missing controls without disturbing the user's saved
            // control order. In pre-As-Played files, position it below Random.
            if(valid && controls->array.size()+2==trial.positions.size()) {
                bool foundRandom=false;
                for(size_t i=0;i<controls->array.size();++i) {
                    const auto& ctl=controls_[i];
                    if(ctl.kind==Kind::Radio && ctl.id==31 && ctl.value==4) {
                        trial.positions[trial.positions.size()-2]={trial.positions[i][0],trial.positions[i][1]+14.f};
                        foundRandom=true;
                        break;
                    }
                }
                if(!foundRandom) valid=false;
            }
            // Existing five Filter LEDs can extend below a resized filter
            // panel. Fit six rows using the original 14px LED spacing,
            // preserving their shared X and all unrelated custom positions.
            // Save writes the expanded 165-control layout if requested.
            if(valid && controls->array.size()<trial.positions.size()) {
                size_t filterRows[5]{};
                bool found[5]{};
                for(size_t i=0;i<controls->array.size();++i) {
                    const auto& ctl=controls_[i];
                    if(ctl.kind==Kind::Radio && ctl.id==14 && ctl.value>=0 && ctl.value<5) {
                        filterRows[ctl.value]=i;
                        found[ctl.value]=true;
                    }
                }
                for(bool f:found) if(!f) valid=false;
                if(valid) {
                    const auto& filterSection=trial.sections[static_cast<size_t>(SectionId::Filter)];
                    const float lastY=std::min(trial.positions[filterRows[4]][1],filterSection.h-14.f);
                    const float firstY=lastY-5.f*14.f;
                    for(int i=0;i<5;++i) trial.positions[filterRows[i]][1]=firstY+14.f*i;
                    trial.positions.back()={trial.positions[filterRows[4]][0],lastY};
                }
            }
            // The Portamento box was added after layout.json v1 shipped.
            // Older layouts omit this key; infer a border from the already
            // saved positions instead of invalidating a user's arrangement.
            if(valid) {
                const auto* box=parsed.Find("portamento");
                if(box) {
                    if(box->type!=json::Value::Type::Array || box->array.size()!=4) valid=false;
                    else {
                        for(size_t i=0;i<4;++i)
                            if(!goodNumber(box->array[i],i<2?-770.f:0.f,770.f))valid=false;
                        if(valid) {
                            auto& p=trial.portamento;
                            p.x=float(box->array[0].number);p.y=float(box->array[1].number);
                            p.w=float(box->array[2].number);p.h=float(box->array[3].number);
                            const auto& voice=trial.sections[static_cast<size_t>(SectionId::Voice)];
                            // Match the existing layout's small off-panel
                            // margin: users may shrink Voice independently
                            // without losing a previously saved layout.
                            if(p.w<45.f || p.h<30.f || voice.x+p.x< -40.f || voice.y+p.y<27.f ||
                               voice.x+p.x+p.w>800.f || voice.y+p.y+p.h>500.f)valid=false;
                        }
                    }
                } else {
                    float left=770.f,top=560.f,right=0.f,bottom=0.f;
                    for(size_t i=0;i<controls_.size();++i) {
                        const auto& ctl=controls_[i];
                        if(!isPortamentoControl(ctl))continue;
                        left=std::min(left,trial.positions[i][0]);
                        top=std::min(top,trial.positions[i][1]);
                        right=std::max(right,trial.positions[i][0]+ctl.w);
                        bottom=std::max(bottom,trial.positions[i][1]+ctl.h);
                    }
                    if(left<right && top<bottom) {
                        trial.portamento={left-6.f,top-6.f,right-left+12.f,bottom-top+12.f};
                    }
                }
            }
            if(valid) {applyLayout(trial);return true;}
        }
    }
    invalidLayoutFile_=true; // a damaged/incompatible layout is never overwritten
    return false;
}

WindowsEditor::LayoutSaveResult WindowsEditor::saveLayout() {
    const auto root=storage::Root();
    if(root.empty() || invalidLayoutFile_) return LayoutSaveResult::Failed;
    const auto target=root/L"layout.json";
    std::error_code ec;
    if(std::filesystem::exists(target,ec)) {
        // Never overwrite a corrupt/incompatible file; the user may need it.
        if(ec || !std::filesystem::is_regular_file(target,ec) || ec) return LayoutSaveResult::Failed;
        std::ifstream current(target,std::ios::binary);
        if(!current) return LayoutSaveResult::Failed;
        const std::string original((std::istreambuf_iterator<char>(current)),{});
        json::Value probe;
        if(current.bad() || !json::Parse(original,probe) || !probe.IsObject() ||
           !probe.Find("version") || probe.Find("version")->type!=json::Value::Type::Number ||
           probe.Find("version")->number!=1) return LayoutSaveResult::Failed;
    }
    // Factory reset is a preview only. If a custom layout was previously
    // loaded, never replace its file after Reset without an explicit warning.
    // This also protects Ctrl+S and the Save prompt when leaving Edit Layout.
    if(factoryResetPending_ &&
       !equalLayout(loadedLayout_,defaultLayout_) &&
       !equalLayout(snapshotLayout(),loadedLayout_)) {
        std::error_code existsError;
        const bool previousFileExists=std::filesystem::exists(target,existsError);
        if(existsError) return LayoutSaveResult::Failed;
        if(previousFileExists &&
           MessageBoxW(window_,
               L"WARNING: Your existing custom layout will be overwritten.\n\n"
               L"You reset the layout to factory defaults. Saving now will replace "
               L"your saved layout.json with the current arrangement.\n\n"
               L"If you want to keep the old layout, choose No and use Undo "
               L"or leave Edit Layout without saving.\n\n"
               L"Overwrite your saved custom layout?",
               L"Replace saved custom layout?",
               MB_YESNO|MB_ICONWARNING|MB_DEFBUTTON2)!=IDYES)
            return LayoutSaveResult::Cancelled;
    }
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out<<std::fixed<<std::setprecision(2);
    out<<"{\n  \"version\": 1,\n  \"knobGeometry\": 24,\n  \"sections\": [\n";
    for(size_t i=0;i<sectionLayouts_.size();++i) {
        const auto& s=sectionLayouts_[i];
        out<<"    ["<<s.x<<", "<<s.y<<", "<<s.w<<", "<<s.h<<"]"
           <<(i+1<sectionLayouts_.size()?",":"")<<"\n";
    }
    out<<"  ],\n  \"controls\": [\n";
    for(size_t i=0;i<controls_.size();++i) {
        const auto& c=controls_[i];
        out<<"    ["<<c.id<<", "<<c.localX<<", "<<c.localY<<"]"
           <<(i+1<controls_.size()?",":"")<<"\n";
    }
    out<<"  ],\n  \"separators\": [\n";
    for(size_t i=0;i<separatorLayouts_.size();++i) {
        const auto& s=separatorLayouts_[i];
        out<<"    ["<<s.x1<<", "<<s.y1<<", "<<s.x2<<", "<<s.y2<<"]"
           <<(i+1<separatorLayouts_.size()?",":"")<<"\n";
    }
    const auto& port=portamentoLayout_;
    out<<"  ],\n  \"portamento\": ["<<port.x<<", "<<port.y<<", "<<port.w<<", "<<port.h<<"]\n}\n";
    std::filesystem::create_directories(root,ec);
    if(ec) return LayoutSaveResult::Failed;
    auto temp=target;
    temp+=L".tmp."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(GetCurrentThreadId());
    {
        std::ofstream f(temp,std::ios::binary|std::ios::trunc);
        if(!f)return LayoutSaveResult::Failed;
        const std::string contents=out.str();
        f.write(contents.data(),std::streamsize(contents.size()));
        f.flush();
        if(!f) {std::filesystem::remove(temp,ec);return LayoutSaveResult::Failed;}
    }
    if(!MoveFileExW(temp.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {
        std::filesystem::remove(temp,ec);
        return LayoutSaveResult::Failed;
    }
    loadedLayout_=snapshotLayout();
    factoryResetPending_=false;
    return LayoutSaveResult::Saved;
}

void WindowsEditor::commitLayoutChange(const LayoutSnapshot& before) {
    const auto after=snapshotLayout();
    if(equalLayout(before,after))return;
    layoutUndo_.push_back(before);
    if(layoutUndo_.size()>80)layoutUndo_.erase(layoutUndo_.begin());
    layoutRedo_.clear();
}
void WindowsEditor::undoLayout() {
    if(layoutUndo_.empty())return;
    layoutRedo_.push_back(snapshotLayout());
    const auto before=layoutUndo_.back();layoutUndo_.pop_back();
    applyLayout(before);
}
void WindowsEditor::redoLayout() {
    if(layoutRedo_.empty())return;
    layoutUndo_.push_back(snapshotLayout());
    const auto after=layoutRedo_.back();layoutRedo_.pop_back();
    applyLayout(after);
}
void WindowsEditor::resetLayout() {
    const LayoutSnapshot before=snapshotLayout();
    applyLayout(defaultLayout_);
    commitLayoutChange(before);
    factoryResetPending_=true; // even if already at factory positions, Reset needs overwrite protection
    // Reset is a live edit until Save: old saved file remains recoverable.
    flash(L"Factory layout restored (Save layout to keep it)",2500);
}
void WindowsEditor::toggleLayoutEditor() {
    if(layoutEditing_) {
        if(layoutDragging_)finishLayoutDrag(true);
        if(!equalLayout(loadedLayout_,snapshotLayout())) {
            const int choice=MessageBoxW(window_,
                L"Save layout changes?\n\nYes: save layout.json\nNo: discard unsaved changes\nCancel: keep editing",
                L"Synth2 Layout",MB_YESNOCANCEL|MB_ICONQUESTION);
            if(choice==IDCANCEL)return;
            if(choice==IDYES) {
                const auto result=saveLayout();
                if(result==LayoutSaveResult::Cancelled)return; // stay in edit mode
                if(result==LayoutSaveResult::Failed) {
                    MessageBoxW(window_,L"Could not save layout.json. Check permissions or remove an incompatible layout file, then reopen the plugin.",
                                L"Synth2 Layout",MB_OK|MB_ICONERROR);
                    return;
                }
            }
            if(choice==IDNO) {
                applyLayout(loadedLayout_);
                factoryResetPending_=false;
            }
        }
        layoutEditing_=false;
        layoutPart_=LayoutPart::None;
        layoutIndex_=-1;
    } else {
        if(browser_)closeBrowser();
        if(midiLearn_)setLearn(false);
        layoutEditing_=true;
        layoutPart_=LayoutPart::None;
        layoutIndex_=-1;
    }
    hoverChanged(-1,0);
    repaint();
}

bool WindowsEditor::layoutHit(float x,float y,LayoutPart& part,int& index) const {
    part=LayoutPart::None;index=-1;
    if(x<6 || x>=764 || y<34 || y>=478)return false;
    // Section edges first, so borders can be resized even beside controls.
    for(int i=int(sectionLayouts_.size())-1;i>=0;--i) {
        const auto& s=sectionLayouts_[size_t(i)];
        if((x>=s.x+s.w-6 && x<=s.x+s.w+3 && y>=s.y+16 && y<=s.y+s.h+3) ||
           (y>=s.y+s.h-6 && y<=s.y+s.h+3 && x>=s.x && x<=s.x+s.w+3)) {
            part=LayoutPart::ResizeSection;index=i;return true;
        }
    }
    // The Portamento border is a movable/resizable group inside Voice.
    // Its interior remains available for editing each control separately.
    {
        const auto& sec=sectionLayout(SectionId::Voice);
        const float left=sec.x+portamentoLayout_.x,top=sec.y+portamentoLayout_.y;
        const float right=left+portamentoLayout_.w,bottom=top+portamentoLayout_.h;
        if(x>=left-4.f && x<=right+4.f && y>=top-4.f && y<=bottom+4.f) {
            if(x>=right-7.f && y>=bottom-7.f) {
                part=LayoutPart::ResizePortamento;index=0;return true;
            }
            if(std::abs(x-left)<=4.f || std::abs(x-right)<=4.f ||
               std::abs(y-top)<=4.f || std::abs(y-bottom)<=4.f) {
                part=LayoutPart::Portamento;index=0;return true;
            }
        }
    }
    for(int i=int(separatorLayouts_.size())-1;i>=0;--i) {
        const auto& line=separatorLayouts_[size_t(i)];const auto& sec=sectionLayout(line.owner);
        const float ax=sec.x+line.x1,ay=sec.y+line.y1;
        const float bx=sec.x+line.x2,by=sec.y+line.y2;
        if(std::hypot(x-ax,y-ay)<7.f) {part=LayoutPart::SeparatorStart;index=i;return true;}
        if(std::hypot(x-bx,y-by)<7.f) {part=LayoutPart::SeparatorEnd;index=i;return true;}
        const float dx=bx-ax,dy=by-ay;
        const float length=dx*dx+dy*dy;
        if(length>0.01f) {
            const float t=std::clamp(((x-ax)*dx+(y-ay)*dy)/length,0.f,1.f);
            if(std::hypot(x-(ax+t*dx),y-(ay+t*dy))<4.f) {
                part=LayoutPart::Separator;index=i;return true;
            }
        }
    }
    // Give knobs, buttons, radio LEDs and displays precedence over broad labels.
    for(int pass=0;pass<2;++pass) for(int i=int(controls_.size())-1;i>=0;--i) {
        const auto& c=controls_[size_t(i)];
        if(c.section==SectionId::Fixed || (c.kind==Kind::Label && c.label.empty()) ||
           (pass==0)==(c.kind==Kind::Label))continue;
        const auto hit=layoutControlBounds(c);
        if(x>=hit.X && x<hit.X+hit.Width && y>=hit.Y && y<hit.Y+hit.Height) {
            part=LayoutPart::Control;index=i;return true;
        }
    }
    for(int i=int(sectionLayouts_.size())-1;i>=0;--i) {
        const auto& s=sectionLayouts_[size_t(i)];
        if(x>=s.x && x<=s.x+s.w && y>=s.y && y<s.y+16) {
            part=LayoutPart::Section;index=i;return true;
        }
    }
    return false;
}
void WindowsEditor::beginLayoutDrag(float x,float y) {
    LayoutPart part=LayoutPart::None;int index=-1;
    layoutHit(x,y,part,index);
    layoutPart_=part;layoutIndex_=index;
    if(part==LayoutPart::None) {repaint();return;}
    layoutStartX_=x;layoutStartY_=y;
    dragBefore_=snapshotLayout();
    layoutDragging_=true;
    SetCapture(window_);
    repaint();
}
void WindowsEditor::updateLayoutFromDrag(const LayoutSnapshot& original,float dx,float dy) {
    // Drag deltas are relative to mouse-down, not accumulated per event.
    // Grid snapping affects the displacement, preserving original half pixels.
    if(layoutSnap_) {dx=4.f*std::round(dx/4.f);dy=4.f*std::round(dy/4.f);}
    switch(layoutPart_) {
    case LayoutPart::Section: {
        auto& s=sectionLayouts_[size_t(layoutIndex_)];const auto& old=original.sections[size_t(layoutIndex_)];
        s.x=std::clamp(old.x+dx,6.f,764.f-old.w);
        s.y=std::clamp(old.y+dy,34.f,478.f-old.h);
        break;
    }
    case LayoutPart::ResizeSection: {
        auto& s=sectionLayouts_[size_t(layoutIndex_)];const auto& old=original.sections[size_t(layoutIndex_)];
        s.w=std::clamp(old.w+dx,45.f,764.f-old.x);
        s.h=std::clamp(old.h+dy,24.f,478.f-old.y);
        break;
    }
    case LayoutPart::Portamento: {
        const auto& sec=sectionLayout(SectionId::Voice);
        const auto& old=original.portamento;
        auto& box=portamentoLayout_;
        // The group can move anywhere in the editable canvas, just like an
        // individual control. Resizing Voice must not suddenly jump it.
        const float minX=6.f-sec.x,minY=34.f-sec.y;
        box.x=std::clamp(old.x+dx,minX,std::max(minX,764.f-sec.x-old.w));
        box.y=std::clamp(old.y+dy,minY,std::max(minY,478.f-sec.y-old.h));
        const float shiftedX=box.x-old.x,shiftedY=box.y-old.y;
        for(size_t i=0;i<controls_.size();++i) if(isPortamentoControl(controls_[i])) {
            controls_[i].localX=original.positions[i][0]+shiftedX;
            controls_[i].localY=original.positions[i][1]+shiftedY;
        }
        break;
    }
    case LayoutPart::ResizePortamento: {
        const auto& sec=sectionLayout(SectionId::Voice);
        const auto& old=original.portamento;
        portamentoLayout_.w=std::clamp(old.w+dx,45.f,std::max(45.f,764.f-sec.x-old.x));
        portamentoLayout_.h=std::clamp(old.h+dy,30.f,std::max(30.f,478.f-sec.y-old.y));
        break;
    }
    case LayoutPart::Control: {
        const size_t i=size_t(layoutIndex_);
        const auto& originalControl=controls_[i];
        const auto& sec=sectionLayout(originalControl.section);
        // Keep each control visible inside the editable central canvas.
        const float ox=sec.x+original.positions[i][0];
        const float oy=sec.y+original.positions[i][1];
        float nx=std::clamp(ox+dx,6.f,764.f-originalControl.w);
        float ny=std::clamp(oy+dy,34.f,478.f-originalControl.h);
        // Gentle magnetic alignment, only during mouse drags. Keep the
        // existing optional 4px grid and arrow-key nudges independent.
        // Compare like-for-like left/centre/right and top/mid/bottom anchors
        // so different-sized displays/knobs can line up precisely.
        if(layoutDragging_ && !layoutSnap_) {
            float offsetX=3.f,offsetY=3.f; // strictly within 3 design pixels
            for(size_t j=0;j<controls_.size();++j) {
                if(j==i)continue;
                const auto& peer=controls_[j];
                if(peer.section==SectionId::Fixed ||
                   !compatibleAlignmentKinds(peer.kind,originalControl.kind) ||
                   (peer.kind==Kind::Label && peer.label.empty()))continue;
                // Moving a waveform or radio group must not magnetise it to
                // the other members of that same group.
                if((originalControl.kind==Kind::Radio || originalControl.kind==Kind::Wave) &&
                   peer.id==originalControl.id && peer.section==originalControl.section)continue;
                Control proposed=originalControl;
                proposed.x=nx;proposed.y=ny;
                const auto moved=alignmentAnchors(proposed);
                const auto target=alignmentAnchors(peer);
                for(int a=0;a<3;++a) {
                    const float deltaX=target.x[a]-moved.x[a],deltaY=target.y[a]-moved.y[a];
                    if(std::abs(deltaX)<std::abs(offsetX) &&
                       nx+deltaX>=6.f && nx+deltaX+originalControl.w<=764.f)offsetX=deltaX;
                    if(std::abs(deltaY)<std::abs(offsetY) &&
                       ny+deltaY>=34.f && ny+deltaY+originalControl.h<=478.f)offsetY=deltaY;
                }
            }
            if(std::abs(offsetX)<3.f)nx+=offsetX;
            if(std::abs(offsetY)<3.f)ny+=offsetY;
        }
        controls_[i].localX=nx-sec.x;
        controls_[i].localY=ny-sec.y;
        // An LED/waveform grid is one logical control group, but buttons
        // with the same parameter ID stay separate (e.g. LFO destination).
        for(size_t j=0;j<controls_.size();++j) if(j!=i &&
           controls_[j].section==originalControl.section &&
           controls_[j].kind==originalControl.kind &&
           (originalControl.kind==Kind::Radio || originalControl.kind==Kind::Wave) &&
           controls_[j].id==originalControl.id) {
            controls_[j].localX=original.positions[j][0]+(nx-ox);
            controls_[j].localY=original.positions[j][1]+(ny-oy);
        }
        break;
    }
    case LayoutPart::Separator:
    case LayoutPart::SeparatorStart:
    case LayoutPart::SeparatorEnd: {
        auto& s=separatorLayouts_[size_t(layoutIndex_)];
        const auto& old=original.separators[size_t(layoutIndex_)];
        const auto& sec=sectionLayout(s.owner);
        if(layoutPart_!=LayoutPart::SeparatorEnd) {
            s.x1=std::clamp(old.x1+dx,0.f,sec.w);
            s.y1=std::clamp(old.y1+dy,0.f,sec.h);
        }
        if(layoutPart_!=LayoutPart::SeparatorStart) {
            s.x2=std::clamp(old.x2+dx,0.f,sec.w);
            s.y2=std::clamp(old.y2+dy,0.f,sec.h);
        }
        break;
    }
    default: return;
    }
    layoutChanged();
}
void WindowsEditor::moveLayoutDrag(float x,float y) {
    if(!layoutDragging_)return;
    updateLayoutFromDrag(dragBefore_,x-layoutStartX_,y-layoutStartY_);
}
void WindowsEditor::finishLayoutDrag(bool commit) {
    if(!layoutDragging_)return;
    layoutDragging_=false;
    if(!commit)applyLayout(dragBefore_);
    else commitLayoutChange(dragBefore_);
    if(window_ && GetCapture()==window_)ReleaseCapture();
    repaint();
}
void WindowsEditor::nudgeLayout(int dx,int dy) {
    if(layoutIndex_<0 || layoutPart_==LayoutPart::None)return;
    const auto before=snapshotLayout();
    const bool snapping=layoutSnap_;
    layoutSnap_=false; // arrow keys always move in exact 1/5 px steps
    updateLayoutFromDrag(before,float(dx),float(dy));
    layoutSnap_=snapping;
    commitLayoutChange(before);
}
void WindowsEditor::drawLayoutGuides(Graphics& g) {
    if(!layoutEditing_ || browser_)return;
    // Clearly distinguish design mode from MIDI Learn without obscuring the synth.
    fill(g,Color(215,20,26,36),244.f,6.f,172.f,21.f);
    stroke(g,rgb(0xE7B765),244.f,6.f,172.f,21.f);
    wchar_t status[96]=L"LAYOUT EDIT - Save via menu";
    if(layoutPart_==LayoutPart::Control && layoutIndex_>=0 && layoutIndex_<int(controls_.size())) {
        const auto& selected=controls_[size_t(layoutIndex_)];
        std::swprintf(status,96,L"EDIT  X %.1f   Y %.1f",selected.x,selected.y);
    } else if(layoutPart_==LayoutPart::Portamento || layoutPart_==LayoutPart::ResizePortamento) {
        const auto& sec=sectionLayout(SectionId::Voice);
        const auto& p=portamentoLayout_;
        std::swprintf(status,96,L"PORT  X %.0f Y %.0f W %.0f H %.0f",
                      sec.x+p.x,sec.y+p.y,p.w,p.h);
    } else if((layoutPart_==LayoutPart::Section || layoutPart_==LayoutPart::ResizeSection) &&
              layoutIndex_>=0 && layoutIndex_<int(sectionLayouts_.size())) {
        const auto& selected=sectionLayouts_[size_t(layoutIndex_)];
        std::swprintf(status,96,L"X %.0f Y %.0f  W %.0f H %.0f",selected.x,selected.y,selected.w,selected.h);
    }
    text(g,status,249.f,7.f,164.f,18.f,10.f,rgb(0xFFE2A0),true,true);
    for(size_t i=0;i<sectionLayouts_.size();++i) {
        const auto& s=sectionLayouts_[i];
        Pen pen(Color(115,255,201,86),1.f);
        pen.SetDashStyle(DashStyleDash);
        g.DrawRectangle(&pen,s.x,s.y,s.w,s.h);
        if((layoutPart_==LayoutPart::Section || layoutPart_==LayoutPart::ResizeSection) && layoutIndex_==int(i)) {
            Pen chosen(rgb(0xFFF0A0),2.f);
            g.DrawRectangle(&chosen,s.x,s.y,s.w,s.h);
        }
        fill(g,Color(170,253,200,91),s.x+s.w-5,s.y+s.h-5,5,5);
    }
    {
        const auto& sec=sectionLayout(SectionId::Voice);
        const auto& p=portamentoLayout_;
        if(layoutPart_==LayoutPart::Portamento || layoutPart_==LayoutPart::ResizePortamento) {
            Pen chosen(rgb(0xFFF0A0),2.f);
            g.DrawRectangle(&chosen,sec.x+p.x,sec.y+p.y,p.w,p.h);
        }
        fill(g,Color(170,253,200,91),sec.x+p.x+p.w-5.f,sec.y+p.y+p.h-5.f,5.f,5.f);
    }
    for(size_t i=0;i<separatorLayouts_.size();++i) {
        const auto& l=separatorLayouts_[i];const auto& sec=sectionLayout(l.owner);
        const bool selected=layoutIndex_==int(i) &&
            (layoutPart_==LayoutPart::Separator || layoutPart_==LayoutPart::SeparatorStart || layoutPart_==LayoutPart::SeparatorEnd);
        const Color c=selected?rgb(0xFFFFFF):Color(150,255,190,80);
        fill(g,c,sec.x+l.x1-3.f,sec.y+l.y1-3.f,6.f,6.f);
        fill(g,c,sec.x+l.x2-3.f,sec.y+l.y2-3.f,6.f,6.f);
    }
    if(layoutPart_==LayoutPart::Control && layoutIndex_>=0 && layoutIndex_<int(controls_.size())) {
        const auto& c=controls_[size_t(layoutIndex_)];
        Pen chosen(rgb(0xFFF0A0),1.5f);
        chosen.SetDashStyle(DashStyleDash);
        const auto selection=layoutControlBounds(c);
        g.DrawRectangle(&chosen,selection.X-2.f,selection.Y-2.f,
                        selection.Width+4.f,selection.Height+4.f);

        // Align like-for-like controls by matching left/centre/right or
        // top/middle/bottom anchors. All comparisons use unscaled 100%-zoom
        // coordinates, so the guides are exact even at another host zoom.
        // Ignore unlike objects (for example a label beside a knob), which
        // would otherwise produce distracting guides throughout the panel.
        constexpr float tolerance=.26f; // show only genuinely aligned geometry
        const auto selectedAnchors=alignmentAnchors(c);
        bool alignedX[3]{},alignedY[3]{};
        for(size_t i=0;i<controls_.size();++i) {
            if(int(i)==layoutIndex_)continue;
            const auto& peer=controls_[i];
            if(peer.section==SectionId::Fixed ||
               !compatibleAlignmentKinds(peer.kind,c.kind) ||
               (peer.kind==Kind::Label && peer.label.empty()))continue;
            const auto peerAnchors=alignmentAnchors(peer);
            bool matchX=false,matchY=false;
            for(int a=0;a<3;++a) {
                if(std::abs(selectedAnchors.x[a]-peerAnchors.x[a])<tolerance) {alignedX[a]=true;matchX=true;}
                if(std::abs(selectedAnchors.y[a]-peerAnchors.y[a])<tolerance) {alignedY[a]=true;matchY=true;}
            }
            if(matchX || matchY) {
                Pen peerOutline(Color(225,93,235,209),1.8f);
                const auto peerRect=layoutControlBounds(peer);
                g.DrawRectangle(&peerOutline,peerRect.X-1.f,peerRect.Y-1.f,
                                peerRect.Width+2.f,peerRect.Height+2.f);
            }
        }
        Pen guide(Color(225,93,235,209),1.f);
        guide.SetDashStyle(DashStyleDash);
        for(int a=0;a<3;++a) {
            if(alignedX[a])g.DrawLine(&guide,selectedAnchors.x[a],34.f,selectedAnchors.x[a],478.f);
            if(alignedY[a])g.DrawLine(&guide,6.f,selectedAnchors.y[a],764.f,selectedAnchors.y[a]);
        }
        // Small crosshair on the selected object's centre. Mouse cursor also
        // switches to crosshair over controls while Layout Edit is active.
        const float midX=selectedAnchors.x[1],midY=selectedAnchors.y[1];
        Pen cross(Color(240,255,229,157),1.1f);
        g.DrawLine(&cross,midX-7.f,midY,midX+7.f,midY);
        g.DrawLine(&cross,midX,midY-7.f,midX,midY+7.f);
    }
}

void WindowsEditor::knob(int id,const wchar_t* name,float sx,float sy,float x,float y,float size) {
    // KnobView's SwiftUI stack is centred at y+6.5, but its circular knob
    // centre is y; the label sits underneath. Avoid an extra 6.5 px offset.
    addControl({Kind::Knob,id,0,sx+x-size*.65f,sy+y-size*.65f,size*1.3f,size*1.65f,name});
}
void WindowsEditor::button(int id,const wchar_t* name,Kind kind,float sx,float sy,float x,float y,float width) {
    // All interactive parameter buttons share a 17px design-space height.
    constexpr float kButtonHeight=17.f;
    addControl({kind,id,0,sx+x-width/2,sy+y-kButtonHeight*.5f,width,kButtonHeight,name});
}
void WindowsEditor::display(int id,float sx,float sy,float x,float y,float width) {
    addControl({Kind::Display,id,0,sx+x-width/2,sy+y-8.5f,width,17,L""});
}
void WindowsEditor::radio(int id,float sx,float sy,float x,float y,std::vector<std::wstring> choices,
                          int first,bool horizontal,bool trailing) {
    const int n=int(choices.size());
    const float step=horizontal ? (id==71 ? 44.f : 38.f) :
        (id==14 || id==31 || id==32 || id==38 ? 14.f : 13.f);
    for(int i=0;i<n;i++) {
        const float px=sx+x+(horizontal ? (i-(n-1)/2.f)*step : 0.f);
        const float py=sy+y+(horizontal ? 0.f : (i-(n-1)/2.f)*step);
        const bool iconOnly=(id==41 || id==46);
        // Original SwiftUI filter selector centre: x=196 in its section.
        // LED and label remain inside the 218px panel, not over the button.
        if(id==14) {
            addControl({Kind::Radio,id,first+i,px-22.f,py-6.f,
                                 44.f,12.f,choices[size_t(i)],0});
        } else if(id==71) {
            // Oscillator modulation destinations: retain full 'osc2' text
            // without shrinking the font; spread the three choices evenly.
            addControl({Kind::Radio,id,first+i,px-22.f,py-6.f,
                                 44.f,12.f,choices[size_t(i)],0});
        } else if(id==97) {
            // Sub octave switches need a full-width label at the same font
            // size; never shrink '-1oct' just because it is longer.
            addControl({Kind::Radio,id,first+i,
                                 sx+x-42.f+float(i)*54.f,sy+y-6.f,
                                 50.f,12.f,choices[size_t(i)],0});
        } else {
            const float width=iconOnly ? 14.f : horizontal ? step : 65.f;
            addControl({Kind::Radio,id,first+i,
                                 iconOnly ? px-7.f : px-(horizontal?step/2.f:15.f),
                                 py-6.f,width,12.f,choices[size_t(i)],trailing?1:0});
        }
    }
}
void WindowsEditor::waves(int id,float sx,float sy,float x,float y,const std::vector<int>& values,
                          const std::vector<int>& shapes) {
    // Original WaveRadio is a two-by-two 22x14 grid with a 2px gap.
    // Slightly larger cells and a 3px gap, still centred on the original x/y.
    for(size_t i=0;i<values.size();i++) {
        const float cx=sx+x+(i%2==0?-13.5f:13.5f);
        const float cy=sy+y+(i/2==0?-9.f:9.f);
        addControl({Kind::Wave,id,values[i],cx-12.f,cy-7.5f,
                             24.f,15.f,L"",shapes[i]});
    }
}
void WindowsEditor::label(float x,float y,const wchar_t* name) {
    addControl({Kind::Label,-1,0,x-40,y-8,80,16,name});
}
void WindowsEditor::badge(float x,float y,const wchar_t* name,int id) {
    addControl({Kind::Badge,id,0,x-8,y-8,16,16,name});
}

void WindowsEditor::constructLayout() {
    // Layout transcribed from Shared/SynthPanelView.swift, using each section's
    // original local coordinate system. Every visible Synth1 parameter keeps
    // its original ID and is driven by OxoSynth2Hooks.
    const auto& osc=sectionLayout(SectionId::Oscillators);
    const auto& lfo=sectionLayout(SectionId::Lfo);
    const auto& wheelMidi=sectionLayout(SectionId::WheelMidi);
    const auto& amp=sectionLayout(SectionId::Amplifier);
    const auto& filter=sectionLayout(SectionId::Filter);
    const auto& arp=sectionLayout(SectionId::Arpeggiator);
    const auto& effect=sectionLayout(SectionId::Effect);
    const auto& eq=sectionLayout(SectionId::Equalizer);
    const auto& delay=sectionLayout(SectionId::Delay);
    const auto& chorus=sectionLayout(SectionId::Chorus);
    const auto& voice=sectionLayout(SectionId::Voice);
    const float ox=osc.x,oy=osc.y,lx=lfo.x,ly=lfo.y,mx=wheelMidi.x,my=wheelMidi.y,
                ax=amp.x,ay=amp.y,fx=filter.x,fy=filter.y,arx=arp.x,ary=arp.y,
                ex=effect.x,ey=effect.y,qx=eq.x,qy=eq.y,dx=delay.x,dy=delay.y,
                cx=chorus.x,cy=chorus.y,vx=voice.x,vy=voice.y;
    useSection(SectionId::Oscillators);
    badge(ox+14,oy+36,L"1"); // Leave the oscillator on/off button fixed.
    // Move the OSC1 / Sub row down 4px to align the knobs with Amplifier.
    waves(0,ox,oy,54,44,{0,1,2,3},{0,1,2,3});
    knob(76,L"det",ox,oy,104,40);knob(45,L"FM",ox,oy,146,40);
    // Reserve the original (blank) label entry so existing layout.json
    // control indices remain compatible; the redundant "sub" caption is hidden.
    label(ox+194,oy+24,L"");
    waves(96,ox,oy,216,48,{0,1,2,3},{0,3,1,5});
    knob(95,L"sub",ox,oy,282,40);
    radio(97,ox,oy,242,74,{L"0oct",L"-1oct"},0,true);
    // Leave the original section separators fixed; the Osc 2 controls sit
    // a few pixels lower to give the upper separator breathing room.
    badge(ox+14,oy+98,L"2");
    waves(1,ox,oy,54,102,{1,2,3,4},{1,2,3,4});
    button(7,L"ring",Kind::Toggle,ox,oy,108,98);
    button(6,L"sync",Kind::Toggle,ox,oy,154,98);
    display(9,ox,oy,260,94,32);
    label(ox+260,oy+110,L"key shift");
    button(4,L"track",Kind::Toggle,ox,oy,36,140,52);
    knob(2,L"pitch",ox,oy,104,136);knob(3,L"fine",ox,oy,146,136);
    knob(5,L"mix",ox,oy,238,136);knob(8,L"p/w",ox,oy,282,136);
    button(10,L"m.env",Kind::Toggle,ox,oy,36,184,52);
    knob(12,L"A",ox,oy,104,184);knob(13,L"D",ox,oy,146,184);knob(11,L"amt",ox,oy,188,184);
    button(71,L"dest.",Kind::Cycle,ox,oy,36,220,52);
    radio(71,ox,oy,131,220,{L"osc2",L"FM",L"p/w"},0,true);
    knob(91,L"phase",ox,oy,238,201);knob(72,L"tune",ox,oy,282,201);
    useSection(SectionId::Lfo);
    badge(lx+14,ly+30,L"1",57);badge(lx+298,ly+30,L"2",58);
    addControl({Kind::Wave,42,0,lx+42-13,ly+30-8,26,17,L"",0});
    addControl({Kind::Wave,47,0,lx+270-13,ly+30-8,26,17,L"",0});
    button(41,L"dst",Kind::Cycle,lx,ly,78,30,32);
    button(46,L"dst",Kind::Cycle,lx,ly,234,30,32);
    knob(43,L"spd",lx,ly,30,62);knob(44,L"amt",lx,ly,76,62);
    knob(48,L"spd",lx,ly,236,62);knob(49,L"amt",lx,ly,282,62);
    label(lx+52,ly+99,L"-sync-");label(lx+260,ly+99,L"-sync-");
    button(67,L"tempo",Kind::Toggle,lx,ly,32,114,56);
    button(68,L"key",Kind::Toggle,lx,ly,84,114,36);
    button(69,L"tempo",Kind::Toggle,lx,ly,236,114,56);
    button(70,L"key",Kind::Toggle,lx,ly,288,114,36);
    std::vector<std::wstring> dests={L"osc2",L"osc1,2",L"filter",L"amp",L"p/w",L"FM",L"pan"};
    radio(41,lx,ly,124,72,dests,1);
    radio(46,lx,ly,188,72,dests,1,true); // replaced below with vertical mirrored LEDs
    controls_.erase(controls_.end()-int(dests.size()),controls_.end());
    radio(46,lx,ly,188,72,dests,1,false,true);
    for(int i=0;i<7;i++) label(lx+156,ly+72+(i-3)*13,dests[size_t(i)].c_str());
    useSection(SectionId::WheelMidi);
    label(mx+32,my+28,L"p.b range"); display(40,mx,my,32,46,30);
    label(mx+82,my+30,L"src1");label(mx+82,my+51,L"src2");
    display(86,mx,my,158,30,120);display(88,mx,my,158,51,120);
    label(mx+228,my+30,L"->");label(mx+228,my+51,L"->");
    display(87,mx,my,298,30,120);display(89,mx,my,298,51,120);
    knob(50,L"sens1",mx,my,400,34);knob(51,L"sens2",mx,my,446,34);
    useSection(SectionId::Amplifier);
    knob(25,L"A",ax,ay,20,40);knob(26,L"D",ax,ay,55,40);knob(27,L"S",ax,ay,90,40);
    knob(28,L"R",ax,ay,125,40);knob(29,L"gain",ax,ay,162,40);knob(30,L"vel",ax,ay,198,40);
    useSection(SectionId::Filter);
    knob(15,L"A",fx,fy,20,44);knob(16,L"D",fx,fy,52,44);knob(17,L"S",fx,fy,84,44);
    knob(18,L"R",fx,fy,116,44);knob(21,L"amt",fx,fy,150,44);
    // Align Filter type with the upper Amount knob, shifting its LED list as one group.
    button(14,L"type",Kind::Cycle,fx,fy,196,44,36);
    // Factory Filter Type LEDs: six rows at the original 14px spacing.
    // Keep LP12 below the Type button; existing custom layouts are loaded separately.
    radio(14,fx,fy,196,92,{L"LP12",L"LP24",L"HP12",L"BP12",L"LPDL"},0);
    // Bottom Filter knobs align with the Oscillator Phase / Tune row.
    knob(19,L"frq",fx,fy,20,117);knob(20,L"res",fx,fy,52,117);
    knob(23,L"sat",fx,fy,84,117);knob(22,L"trk",fx,fy,116,117);
    // Velocity toggle sits immediately above Track, not beside the bottom knob row.
    button(24,L"vel",Kind::Toggle,fx,fy,116,84,36);
    useSection(SectionId::Arpeggiator);
    button(59,L"ON",Kind::Toggle,arx,ary,28,30,40);
    button(31,L"type",Kind::Cycle,arx,ary,78,30);
    button(32,L"range",Kind::Cycle,arx,ary,128,30,44);
    radio(31,arx,ary,40,78,{L"updown",L"up",L"down",L"random"},1);
    radio(32,arx,ary,126,78,{L"1oct",L"2oct",L"3oct",L"4oct"},0);
    knob(33,L"beat",arx,ary,190,36);knob(34,L"gate",arx,ary,190,88);
    // Effect uses the same stacked ON/type layout as Delay and Chorus.
    useSection(SectionId::Effect);
    button(77,L"ON",Kind::Toggle,ex,ey,25,31,40);
    display(78,ex,ey,25,53,40); // +2px additional separation from ON.
    knob(79,L"ctl1",ex,ey,108,40);knob(80,L"ctl2",ex,ey,150,40);knob(81,L"level",ex,ey,192,40);
    useSection(SectionId::Equalizer);
    knob(61,L"freq",qx,qy,24,31);knob(62,L"level",qx,qy,66,31);
    knob(63,L"Q",qx,qy,108,31);knob(60,L"tone",qx,qy,150,31);knob(90,L"L-R",qx,qy,192,31);
    // Stack the ON switch over its type display, matching the available
    // 58px section height; the free horizontal space gives each FX knob
    // a full, readable label and non-overlapping indicator dots.
    // Leave a visible gap below the section header and between the two rows.
    useSection(SectionId::Delay);
    button(65,L"ON",Kind::Toggle,dx,dy,23,27,40);
    display(82,dx,dy,23,47,40); // Keep type display fixed.
    // Five knobs retain their horizontal spacing; move down 1px.
    knob(35,L"time",dx,dy,68,31,22);knob(83,L"sprd",dx,dy,100,31,22);
    knob(36,L"fdbk",dx,dy,132,31,22);knob(98,L"tone",dx,dy,164,31,22);knob(37,L"d/w",dx,dy,196,31,22);
    useSection(SectionId::Chorus);
    button(66,L"ON",Kind::Toggle,cx,cy,23,27,40);
    display(64,cx,cy,23,47,40); // Keep type display fixed.
    knob(52,L"time",cx,cy,68,31,22);knob(53,L"depth",cx,cy,100,31,22);
    knob(54,L"rate",cx,cy,132,31,22);knob(55,L"fdbk",cx,cy,164,31,22);knob(56,L"level",cx,cy,196,31,22);
    useSection(SectionId::Voice);
    radio(38,vx,vy,34,46,{L"poly",L"mono",L"legato"},0);
    display(94,vx,vy,130,38,30);label(vx+130,vy+55,L"poly");
    button(73,L"unison",Kind::Toggle,vx,vy,36,100,56);
    label(vx+153,vy+77,L"portamento");
    knob(39,L"",vx,vy,128,100);
    button(74,L"auto",Kind::Toggle,vx,vy,176,100,46);
    // Match lower Voice control row to the Wheel / MIDI Sens1/Sens2 centres.
    display(93,vx,vy,24,144,26);label(vx+24,vy+161,L"num");
    knob(75,L"det",vx,vy,68,142);knob(92,L"phase",vx,vy,106,142);
    knob(84,L"sprd",vx,vy,144,142);knob(85,L"pitch",vx,vy,182,142);
    useSection(SectionId::Fixed);
    knob(99,L"vol",6,484,19,29,26);
    // Keep the fifth Arp Type LED immediately below Random. All original
    // parameter IDs and control indices remain unchanged.
    useSection(SectionId::Arpeggiator);
    addControl({Kind::Radio,31,5,arx+25.f,ary+107.f,80.f,12.f,L"as played"});
    // Append instead of inserting: every existing control index in layout.json stays stable.
    useSection(SectionId::Filter);
    addControl({Kind::Radio,14,5,fx+174.f,fy+128.f,44.f,12.f,L"Notch",0});
}

// SynthModel.applyTheme / setThemeColor: this instance wears it (session),
// and it becomes the palette the next new instance starts with.
void WindowsEditor::setTheme(const themes::Palette& palette,bool fromThemeEditor,const std::string& themeFile) {
    // Load the exact selected file's Windows-only colours, not another theme
    // that happens to share its 13 standard colour values.
    if(!fromThemeEditor) {
        voiceGrid_=themeFile.empty()
            ? winvoice::LoadCurrent(storage::Root(),palette)
            : winthemefiles::VoiceForFile(storage::Root()/"Themes",themeFile,palette);
    }
    activeThemeFile_=fromThemeEditor ? std::string{} : themeFile;
    theme_=palette;
    for(size_t i=0;i<colors_.size();++i) colors_[i]=rgb(int(theme_[i]));
    if(hooks_.setThemeText) hooks_.setThemeText(hooks_.ctx,themes::StateText(theme_).c_str());
    sharedThemes().remember(theme_,&voiceGrid_);
    // The checkbox is a user preference, not a colour: keep it when themes change.
    auto extras=sharedThemes().store.CurrentExtras();
    extras["presetTextColours"]=presetTextColours_?"true":"false";
    if(sharedThemes().store.SetCurrentExtras(std::move(extras)))
        sharedThemes().writeDue=GetTickCount64()+400;
    if(!fromThemeEditor && themeEditor_) themeEditor_->Refresh();
    staticDirty_=true;
    repaint();
}
// SynthModel.syncTheme: a session restore may bring another palette; one
// without (older session) keeps the current one, now pinned to the session.
void WindowsEditor::setPresetTextColours(bool enabled) {
    if(presetTextColours_==enabled)return;
    auto extras=sharedThemes().store.CurrentExtras();
    extras["presetTextColours"]=enabled?"true":"false";
    if(!sharedThemes().store.SetCurrentExtras(std::move(extras)) && presetTextColours_==enabled)return;
    if(!sharedThemes().store.Write()) {
        MessageBoxW(window_,L"Unable to save preset-colour preference.",L"Theme settings",MB_OK|MB_ICONWARNING);
        return;
    }
    presetTextColours_=enabled;
    invalidate(kBrowserRect);
}

void WindowsEditor::setVoiceGrid(const winvoice::Palette& palette) {
    if(voiceGrid_==palette)return;
    if(!winvoice::Save(sharedThemes().store,palette)) {
        MessageBoxW(window_,L"Unable to save Windows theme colours. Existing settings were preserved.",
                    L"Theme settings",MB_OK|MB_ICONWARNING);
        return;
    }
    voiceGrid_=palette;
    activeThemeFile_.clear(); // no longer the exact saved file once modified
    // Do not refresh the live hex editor while a user is typing.
    invalidate(kGridRect);
    invalidate(kBrowserRect); // apply Browser Selection to both lists immediately
}
void WindowsEditor::syncTheme() {
    const auto restored=hooks_.themeText ? themes::FromStateText(hooks_.themeText(hooks_.ctx)) : std::nullopt;
    if(!restored) {
        if(hooks_.setThemeText) hooks_.setThemeText(hooks_.ctx,themes::StateText(theme_).c_str());
        return;
    }
    if(*restored==theme_) return;
    theme_=*restored;
    activeThemeFile_.clear();
    voiceGrid_=winvoice::Load(storage::Root(),theme_);
    for(size_t i=0;i<colors_.size();++i) colors_[i]=rgb(int(theme_[i]));
    if(themeEditor_) themeEditor_->Refresh();
    staticDirty_=true;
    repaint();
}
// LogoMenu's theme column: built-in themes followed by individual files.
void WindowsEditor::appendThemes(HMENU menu,std::vector<themes::Named>& picks) const {
    picks=themes::Factory();
    const size_t factoryCount=picks.size();
    for(const auto& entry:sharedThemes().userThemes()) picks.push_back(entry.theme);
    bool checked=false;
    for(size_t i=0;i<picks.size();++i) {
        if(i==factoryCount) AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
        const bool file=i>=factoryCount;
        const bool active=!checked && picks[i].palette==theme_ &&
            ((file && winthemefiles::SameName(picks[i].name,activeThemeFile_)) ||
             (!file && activeThemeFile_.empty()));
        checked=checked || active;
        std::wstring name=toWide(picks[i].name.c_str());
        if(i==0) name+=L" (default)";
        AppendMenuW(menu,MF_STRING|(active?MF_CHECKED:0),UINT_PTR(kThemeCommand+int(i)),name.c_str());
    }
}
bool WindowsEditor::pickTheme(int command,const std::vector<themes::Named>& picks) {
    const int index=command-kThemeCommand;
    if(index<0 || index>=int(picks.size())) return false;
    const auto& selected=picks[size_t(index)];
    setTheme(selected.palette,false,size_t(index)>=themes::Factory().size() ? selected.name : std::string{});
    return true;
}

// Drawing has two layers. The STATIC one (panel, sections, labels, knob
// bodies, button faces, LCD backgrounds) only changes with the theme or the
// size: it is rendered once into a bitmap. Every paint blits it, then draws
// the DYNAMIC parts (knob pointers, LEDs, values, waves, badges, LCD texts,
// voice grid, hover, browser), only where the window is invalid.
void WindowsEditor::text(Graphics& g,const std::wstring& s,float x,float y,float w,float h,float size,
                         Color c,bool,bool centred,bool mono) {
    if(s.empty() || w<=2.f || h<=1.f || !textCache_ || !visible(RectF(x,y,w,h))) return;
    // Font size is chosen by the control, NEVER reduced to fit the string.
    const float fontSize=size<13.f ? std::max(10.8f,size*1.08f) : size;
    const auto& shape=textCache_->get(s,fontSize,w-2.f,mono);
    if(!shape.path) return;
    const float left=centred ? x+(w-shape.ink.Width)*.5f : x+1.f;
    const float top=y+(h-shape.refH)*.5f;
    const GraphicsState saved=g.Save();
    g.SetClip(RectF(x,y,w,h),CombineModeIntersect);
    g.TranslateTransform(left-shape.ink.X,top-shape.refY);
    SolidBrush brush(c);
    g.FillPath(&brush,shape.path.get());
    g.Restore(saved);
}

bool WindowsEditor::visible(const RectF& r) const {
    return r.X<paintClip_.X+paintClip_.Width && paintClip_.X<r.X+r.Width &&
           r.Y<paintClip_.Y+paintClip_.Height && paintClip_.Y<r.Y+r.Height;
}
RectF WindowsEditor::bounds(const Control& c) {
    // Room for the LED / wave glow and the learn frame around a knob.
    return RectF(c.x-5.f,c.y-5.f,c.w+10.f,c.h+10.f);
}
void WindowsEditor::invalidate(const RectF& r) const {
    if(!window_) return;
    const float sx=float(std::max(1,rect.getWidth()))/kDesignWidth;
    const float sy=float(std::max(1,rect.getHeight()))/kDesignHeight;
    RECT device{LONG(std::floor(r.X*sx))-2,LONG(std::floor(r.Y*sy))-2,
                LONG(std::ceil((r.X+r.Width)*sx))+2,LONG(std::ceil((r.Y+r.Height)*sy))+2};
    InvalidateRect(window_,&device,FALSE);
}
// Everything showing parameter `id`: its controls, the LCD readout when it
// is the selected one, the voice grid for voice mode / polyphony.
void WindowsEditor::invalidateParam(int id) const {
    for(const auto& c:controls_) if(c.id==id) invalidate(bounds(c));
    if(id==38 || id==94) invalidate(kGridRect);
}
void WindowsEditor::valueChanged(int id) {
    if(id>=0 && id<int(knownValues_.size())) knownValues_[size_t(id)]=value(id);
    invalidateParam(id);
}
// Hover is visual only: redraw the old and the new target, nothing else.
void WindowsEditor::hoverChanged(int control,int chrome) {
    if(control==hoveredControl_ && chrome==hoveredChrome_) return;
    for(const int c:{hoveredControl_,control})
        if(c>=0 && c<int(controls_.size())) invalidate(bounds(controls_[size_t(c)]));
    for(const int c:{hoveredChrome_,chrome}) if(c) invalidate(chromeRect(c));
    hoveredControl_=control;
    hoveredChrome_=chrome;
}

// MIDI learn: use the cycle button alone for grouped LED selectors.
// Including their radio LEDs in the same bounding box makes the LFO, Arp,
// Filter and Oscillator learn targets overlap nearby controls. The Voice
// mode selector has no corresponding cycle button and retains its LEDs.
// Other controls keep their existing learn targets and parameter IDs.
void WindowsEditor::buildLearnRects() {
    learnRects_.fill(RectF(0,0,0,0));
    learnOrder_.clear();
    for(const auto& c:controls_) {
        if(c.id<0 || c.id>=kLearnCount || (c.id>=86 && c.id<=89) || c.kind==Kind::Label) continue;
        if((c.id==41 || c.id==46 || c.id==71 || c.id==14 || c.id==31 || c.id==32) &&
           c.kind!=Kind::Cycle) continue;
        RectF& r=learnRects_[size_t(c.id)];
        const RectF own(c.x,c.y,c.w,c.h);
        if(r.Width<=0) {r=own;continue;}
        const RectF was=r;
        RectF::Union(r,was,own);
    }
    for(int id=0;id<kLearnCount;id++) {
        RectF& r=learnRects_[size_t(id)];
        if(r.Width<=0) continue;
        r.Inflate(3.f,3.f); // .padding(-3)
        learnOrder_.push_back(id);
    }
    // Drawn larger first, hit smaller first: a control inside a group's
    // box (e.g. beside a radio column) keeps its own hotspot.
    std::stable_sort(learnOrder_.begin(),learnOrder_.end(),[this](int a,int b) {
        const RectF &ra=learnRects_[size_t(a)], &rb=learnRects_[size_t(b)];
        return ra.Width*ra.Height>rb.Width*rb.Height;
    });
}
void WindowsEditor::setLearn(bool on) {
    if(on && browser_) closeBrowser(); // SynthModel.toggleLearn
    midiLearn_=on;
    learnTarget_=learnPrevTarget_=-1;
    if(on) {
        if(hooks_.lastCC) lastCcToken_=hooks_.lastCC(hooks_.ctx);
        for(int id=0;id<kLearnCount;id++) learnCC_[size_t(id)]=hooks_.midiCC ? hooks_.midiCC(hooks_.ctx,id) : 0;
    }
    repaint();
}
void WindowsEditor::learnSelect(int id) {
    for(const int r:{learnTarget_,id}) if(r>=0 && r<kLearnCount) invalidate(learnRects_[size_t(r)]);
    learnTarget_=id;
}
int WindowsEditor::learnHit(float x,float y) const {
    for(auto it=learnOrder_.rbegin();it!=learnOrder_.rend();++it)
        if(learnRects_[size_t(*it)].Contains(x,y)) return *it;
    return -1;
}
void WindowsEditor::drawLearn(Graphics& g) {
    static const Color learn=rgb(0x59A6E8); // Theme.learn
    for(const int id:learnOrder_) {
        const RectF& r=learnRects_[size_t(id)];
        if(!visible(r)) continue;
        const int cc=learnCC_[size_t(id)];
        const bool selected=learnTarget_==id;
        const Control* knob=nullptr;
        for(const auto& c:controls_)if(c.id==id && c.kind==Kind::Knob) {knob=&c;break;}
        const float width=selected ? 2.f : 1.f;
        const Color outline(selected ? 255 : 153,learn.GetR(),learn.GetG(),learn.GetB());
        if(knob) {
            const float size=knob->w/1.3f;
            const float cx=knob->x+knob->w/2.f;
            const float cy=knob->y+size*.65f;
            // Slightly larger circular learn rings, as in the earlier sl23 GUI.
            // Still within the knob's existing padded hit/paint rectangle.
            const float radius=size*.5f+5.f;
            if(cc) {
                SolidBrush fillBrush(Color(102,learn.GetR(),learn.GetG(),learn.GetB()));
                g.FillEllipse(&fillBrush,cx-radius,cy-radius,2*radius,2*radius);
            }
            Pen ring(outline,width);
            g.DrawEllipse(&ring,cx-radius+width/2,cy-radius+width/2,
                          2*radius-width,2*radius-width);
        } else {
            if(cc) {
                GraphicsPath box;roundedPath(box,r.X,r.Y,r.Width,r.Height,4.f);
                SolidBrush fillBrush(Color(102,learn.GetR(),learn.GetG(),learn.GetB()));
                g.FillPath(&fillBrush,&box);
            }
            roundedOutline(g,outline,r.X+width/2,r.Y+width/2,
                           r.Width-width,r.Height-width,4.f-width/2,width);
        }
        if(cc) {
            const std::wstring label=std::to_wstring(cc);
            const float w=6.f+6.2f*float(label.size()), h=13.f;
            const float bx=r.X+(r.Width-w)/2, by=r.Y+(r.Height-h)/2;
            GraphicsPath badge;roundedPath(badge,bx,by,w,h,2.4f);
            SolidBrush badgeBrush(learn);
            g.FillPath(&badgeBrush,&badge);
            roundedOutline(g,Color(191,255,255,255),bx+.5f,by+.5f,w-1.f,h-1.f,2.4f);
            text(g,label,bx,by,w,h,9,rgb(0xFFFFFF),true,true);
        }
    }
}
RectF WindowsEditor::chromeRect(int id) {
    switch(id) {
    case 3: return {420,495,24,20};  case 4: return {449,495,24,20};
    case 5: return {492,488,50,26};  case 6: return {492,522,50,26};
    case 8: return {558,488,186,24}; case 9: return {514,38,84,20};
    case 10: return {604,38,74,20};  case 11: return {430,38,66,20};
    case 12: return {502,38,112,20}; case 13: return {620,38,60,20};
    case 14: return {690,38,64,20};
    case 15: return {348,38,76,20};
    case 16: return {482,5,90,20};
    default: return {0,0,0,0};
    }
}
// The panel's own chrome buttons (< >, write, menu).
void WindowsEditor::drawChrome(Graphics& g,int id,bool hovered) {
    const auto& p=colors_;
    const RectF r=chromeRect(id);
    const Color face=id==5 ? rgb(0xB81715) : p[kButton];
    darkButton(g,face,r.X,r.Y,r.Width,r.Height,hovered);
    if(id==3 || id==4) {
        text(g,id==3?L"<":L">",r.X+1,r.Y,r.Width-3,r.Height,13,rgb(0xFFFFFF),true,true);
    } else if(id==16) {
        const int percent=int(std::lround(100.f*float(rect.getWidth())/kDesignWidth));
        text(g,L"Zoom "+std::to_wstring(percent)+L"%",r.X,r.Y,r.Width,r.Height,10,rgb(0xFFFFFF),true,true);
    } else {
        text(g,id==5?L"write":L"menu",r.X,r.Y,r.Width,r.Height,11,rgb(0xFFFFFF),true,true);
    }
}

void WindowsEditor::drawControlStatic(Graphics& g,const Control& c) {
    const auto& p=colors_;
    switch(c.kind) {
    case Kind::Label:
        text(g,c.label,c.x,c.y,c.w,c.h,9.5f,p[kLabel],false,true);
        return;
    case Kind::Knob: {
        const float size=c.w/1.3f;
        const float x=c.x+c.w/2, y=c.y+size*.65f;
        // Theme.knobTicks: label ink at 30% opacity.
        const SolidBrush dots(Color(77,p[kLabel].GetR(),p[kLabel].GetG(),p[kLabel].GetB()));
        for(int i=0;i<11;i++) {
            const float a=(-140+i*28-90)*kPi/180;
            // Same body-to-dot spacing for full-size and compact knobs,
            // including EQ, Delay, Chorus and Wheel/MIDI Sens controls.
            const float dotRing=size*.65f-1.5f;
            g.FillEllipse(&dots,x+std::cos(a)*dotRing-.8f,y+std::sin(a)*dotRing-.8f,1.6f,1.6f);
        }
        const float radius=size*.5f;
        GraphicsPath circle;
        circle.AddEllipse(x-radius,y-radius,size,size);
        PathGradientBrush body(&circle);
        body.SetCenterPoint(PointF(x-radius*.30f,y-radius*.38f));
        body.SetCenterColor(p[kKnobHighlight]);
        INT count=1;
        Color surround[1]={p[kKnob]};
        body.SetSurroundColors(surround,&count);
        g.FillPath(&body,&circle);
        Pen rim(muted(p[kPanel],.5f),1.f); // Theme.knobRim
        g.DrawEllipse(&rim,x-radius,y-radius,size,size);
        text(g,c.label,x-25,y+radius+1,50,12,9.5f,p[kLabel],false,true);
        return;
    }
    case Kind::Toggle:
    case Kind::Cycle:
        darkButton(g,p[kButton],c.x,c.y,c.w,c.h);
        text(g,c.label,c.x+(c.kind==Kind::Toggle?12.f:0.f),
             c.y+(c.label==L"ON"?1.5f:0.f),
             c.w-(c.kind==Kind::Toggle?14.f:0.f),c.h,10.5f,rgb(0xE8E8E8),true,true);
        return;
    case Kind::Display: {
        LinearGradientBrush lcd(PointF(c.x,c.y),PointF(c.x,c.y+c.h),p[kDisplayTop],p[kDisplayBottom]);
        GraphicsPath lcdPath;roundedPath(lcdPath,c.x,c.y,c.w,c.h,2.f);
        g.FillPath(&lcd,&lcdPath);
        roundedOutline(g,rgb(0x0B0B0B),c.x,c.y,c.w,c.h,2.f);
        return;
    }
    case Kind::Radio:
        if(c.id!=41 && c.id!=46) {
            // Filter Type LEDs have the same visual breathing room as the
            // other LED groups. Move only the label ink, not its hit target.
            // Preserve the LED-to-label gap while giving HP12 enough text
            // width to draw in full (the old 26px glyph limit ellipsized it).
            const float left=c.id==14 ? 11.f : (c.symbol ? 0.f : 15.f);
            const float width=c.id==14 ? c.w-11.f : c.w-(c.symbol?14.f:16.f);
            text(g,c.label,c.x+left,c.y,width,c.h,9.5f,p[kLabel],false,false);
        }
        return;
    case Kind::Badge:
    case Kind::Wave:
        return; // their face depends on the value: dynamic
    }
}

void WindowsEditor::drawControlDynamic(Graphics& g,const Control& c,bool hovered) {
    const auto& p=colors_;
    switch(c.kind) {
    case Kind::Label:
        return;
    case Kind::Badge: {
        const bool on=(c.id<0 || value(c.id)!=0);
        darkButton(g,on?rgb(0x0F1111):rgb(0x393939),c.x,c.y,c.w,c.h,hovered);
        // Move the four Osc/LFO badge digits down exactly one design pixel.
        text(g,c.label,c.x,c.y+1.f,c.w,c.h-1.f,12,c.id<0?rgb(0xFFFFFF):
             on?rgb(0xFFDA59):rgb(0x777777),true,true);
        return;
    }
    case Kind::Knob: {
        const float size=c.w/1.3f, radius=size*.5f;
        const float x=c.x+c.w/2, y=c.y+size*.65f;
        int low=0,high=127,def=64;
        if(hooks_.paramInfo) hooks_.paramInfo(hooks_.ctx,c.id,&low,&high,&def);
        const float frac=std::clamp((float(value(c.id))-float(low))/float(std::max(1,high-low)),0.f,1.f);
        // SwiftUI uses a short capsule near the rim, not a red line from
        // the knob's centre. Match its -140 to +140 degree travel.
        const float angle=(-140+280*frac-90)*kPi/180;
        const float dx=std::cos(angle), dy=std::sin(angle);
        Pen hand(p[kDial],2.f);
        hand.SetStartCap(LineCapRound);hand.SetEndCap(LineCapRound);
        g.DrawLine(&hand,PointF(x+dx*radius*.18f,y+dy*radius*.18f),
                   PointF(x+dx*radius*.84f,y+dy*radius*.84f));
        return;
    }
    case Kind::Toggle:
    case Kind::Cycle:
        if(hovered) {
            darkButton(g,p[kButton],c.x,c.y,c.w,c.h,true);
            text(g,c.label,c.x+(c.kind==Kind::Toggle?12.f:0.f),
                 c.y+(c.label==L"ON"?1.5f:0.f),
                 c.w-(c.kind==Kind::Toggle?14.f:0.f),c.h,10.5f,rgb(0xE8E8E8),true,true);
        }
        if(c.kind==Kind::Toggle) {
            const bool on=value(c.id)!=0;
            led(g,on?p[kLed]:muted(p[kLed],.3f),c.x+8,c.y+c.h/2,2.8f,on);
        }
        return;
    case Kind::Display: {
        std::wstring v=formatted(c.id);
        if(c.id==9 || c.id==40) { // 2-digit displays, as in SynthPanelView.swift
            wchar_t out[32];std::swprintf(out,32,L"%02d",value(c.id));v=out;
        }
        text(g,v,c.x+1,c.y+1,c.w-2,c.h-2,11,p[kDisplayText],true,true,true);
        return;
    }
    case Kind::Radio: {
        const bool on=value(c.id)==c.value;
        const bool justLed=(c.id==41 || c.id==46);
        const float ledx=justLed ? c.x+c.w*.5f : c.id==14 ? c.x+3.f :
                         c.symbol ? c.x+c.w-7.f : c.x+7.f;
        // All red selector LEDs gain 2px diameter: radius 3.4 -> 4.4.
        led(g,on?p[kRadio]:muted(p[kRadio],.42f),ledx,c.y+c.h/2,4.4f,on);
        return;
    }
    case Kind::Wave:
        drawWave(g,c);
        return;
    }
}

void WindowsEditor::drawWave(Graphics& g,const Control& c) {
    const auto& p=colors_;
    const bool on=(value(c.id)==c.value) || (c.id==42||c.id==47);
    // Original WaveCell: FLAT fill (no gradient), 2 px corner radius.
    const Color waveFace=on?p[kDisplayText]:p[kButton];
    // Original WaveCell glows only while selected. Three thin, low-alpha
    // rings add a soft 2px aura without changing button dimensions.
    if(on) {
        const BYTE r=waveFace.GetR(),green=waveFace.GetG(),b=waveFace.GetB();
        roundedOutline(g,Color(20,r,green,b),c.x-2.5f,c.y-2.5f,c.w+5.f,c.h+5.f,4.5f,1.5f);
        roundedOutline(g,Color(35,r,green,b),c.x-1.5f,c.y-1.5f,c.w+3.f,c.h+3.f,3.5f,1.2f);
        roundedOutline(g,Color(55,r,green,b),c.x-.5f,c.y-.5f,c.w+1.f,c.h+1.f,2.5f,1.f);
    }
    roundedFill(g,waveFace,c.x,c.y,c.w,c.h,2.f);
    roundedOutline(g,Color(170,0,0,0),c.x,c.y,c.w,c.h,2.f);
    // Slightly stronger strokes, while preserving the original shape paths.
    Pen pen(on ? rgb(0x101719) : rgb(0xDDDDDD),1.7f);
    pen.SetStartCap(LineCapRound);pen.SetEndCap(LineCapRound);
    pen.SetLineJoin(LineJoinRound);
    const float x=c.x+4,y=c.y+3.5f,w=c.w-8,h=c.h-7;
    int shape=c.symbol;
    if(c.id==42 || c.id==47) {
        constexpr int cycleShapes[]={6,3,5,7,8,0};
        shape=cycleShapes[std::clamp(value(c.id),0,5)];
    }
    switch(shape) {
        case 0: { // sine
            PointF pts[25];
            for(int i=0;i<25;i++) {
                const float t=i/24.f;
                pts[i]=PointF(x+w*t,y+h*(.5f-.5f*std::sin(2*kPi*t)));
            }
            g.DrawLines(&pen,pts,25);break;
        }
        case 1:case 6: { // saw / falling saw, original two-stroke shape
            PointF pts[]={{x,y+h},{x,y},{x+w,y+h}};
            g.DrawLines(&pen,pts,3);break;
        }
        case 2:case 5: { // pulse (30%) / square (50%)
            const float duty=(shape==2 ? .30f : .50f);
            PointF pts[]={{x,y+h},{x,y},{x+w*duty,y},
                          {x+w*duty,y+h},{x+w,y+h},{x+w,y}};
            g.DrawLines(&pen,pts,6);break;
        }
        case 3: { // triangle
            PointF pts[]={{x,y+h},{x+w*.5f,y},{x+w,y+h}};
            g.DrawLines(&pen,pts,3);break;
        }
        case 4: { // fixed sample of noise; same samples as WaveIcon.noise
            const float ys[]={.5f,.1f,.8f,.3f,.95f,.2f,.6f,0.f,.7f,.4f,.9f,.25f,.55f};
            PointF pts[13];
            for(int i=0;i<13;i++)pts[i]=PointF(x+w*i/12.f,y+h*ys[i]);
            g.DrawLines(&pen,pts,13);break;
        }
        case 7:case 8: { // stepped or smoothly interpolated random LFO
            const float ys[]={.6f,.1f,.8f,.35f,.95f};
            const float step=w/5.f, curve=step*.35f;
            GraphicsPath path;
            path.StartFigure();
            path.AddLine(x,y+h*ys[0],x+step-(shape==8?curve:0),y+h*ys[0]);
            for(int i=1;i<5;i++) {
                const float edge=x+step*i;
                const float prev=y+h*ys[i-1], level=y+h*ys[i];
                if(shape==8) {
                    path.AddBezier(edge-curve,prev,edge,prev,edge,level,edge+curve,level);
                } else path.AddLine(edge,prev,edge,level);
                path.AddLine(edge+(shape==8?curve:0),level,
                             x+step*(i+1)-(shape==8 && i<4?curve:0),level);
            }
            g.DrawPath(&pen,&path);break;
        }
        default:break;
    }
}

void WindowsEditor::drawStatic(Graphics& g) {
    const auto& p=colors_;
    fill(g,p[kPanel],0,0,kDesignWidth,kDesignHeight);
    // Original project title. Version/repository link belongs in About only.
    text(g,L"Synth2",8,5,72,23,18,p[kLabel],true);
    text(g,L"Virtual Analog",80,8,139,20,12,muted(p[kLabel],.85f));
    // Keep the fork's Windows version prominent. The product title stays
    // "Synth2 Virtual Analog", and credits/repository links live in About.
    const std::wstring version=L"v"+toWide(OXO_VERSION)+L" Windows x64";
    const float ink=textCache_ ? textCache_->get(version,10.8f,400.f,false).ink.Width : 160.f; // text()'s size for 10
    const float boxWidth=std::clamp(ink+20.f,168.f,400.f);
    versionLeft_=761.f-boxWidth;
    // Same 3-pixel corner radius as the GUI buttons.
    roundedFill(g,rgb(0xAD0F0C),versionLeft_,5,boxWidth,20,3.f);
    roundedOutline(g,rgb(0x101010),versionLeft_,5,boxWidth,20,3.f);
    text(g,version,versionLeft_+3,6,boxWidth-6,18,10,rgb(0xFFFFFF),true,true);
    for(const auto& sec:sectionLayouts_) {
        // Header fill meets the section border exactly, with no one-pixel
        // panel-colour gutter between the title band and its outline.
        fill(g,p[kHeader],sec.x,sec.y,sec.w,16.f);
        stroke(g,muted(p[kHeader],.6f),sec.x,sec.y,sec.w,sec.h);
        text(g,sec.title,sec.x+5,sec.y+1.f,sec.w-10,16,12,p[kHeaderText],true);
    }
    Pen div(muted(p[kHeader],.45f),.8f);
    for(const auto& separator:separatorLayouts_) {
        const auto& sec=sectionLayout(separator.owner);
        g.DrawLine(&div,sec.x+separator.x1,sec.y+separator.y1,
                   sec.x+separator.x2,sec.y+separator.y2);
    }
    // A separate movable/resizeable Portamento box within the Voice section.
    // It follows Voice when that whole section moves; its controls may still
    // be positioned individually while Layout Edit is enabled.
    const auto& voiceSection=sectionLayout(SectionId::Voice);
    const auto& port=portamentoLayout_;
    stroke(g,muted(p[kHeader],.6f),voiceSection.x+port.x,voiceSection.y+port.y,
           port.w,port.h);
    for(const auto& c:controls_) drawControlStatic(g,c);
    // Original LCD + write/menu + voice grid geometry from Shared/SynthLibraryViews.swift.
    LinearGradientBrush lcd(PointF(50,484),PointF(50,554),p[kDisplayTop],p[kDisplayBottom]);
    GraphicsPath mainLcd;roundedPath(mainLcd,50,484,432,70,4.f);
    g.FillPath(&lcd,&mainLcd);
    roundedOutline(g,rgb(0x070707),50,484,432,70,4.f,1.5f);
    for(const int id:{3,4,5,6,16}) drawChrome(g,id,false);
    // Small bottom-right host-resize grip. All drawing scales as one canvas.
    Pen grip(blend(p[kPanel],p[kLabel],.65f),1.f);
    for(int i=0;i<3;i++) {
        const float a=float(i*5);
        g.DrawLine(&grip,752.f+a,557.f,767.f,542.f+a);
    }
}

void WindowsEditor::drawDynamic(Graphics& g) {
    const auto& p=colors_;
    // The browser covers the whole panel area: only the bottom bar shows.
    for(size_t i=0;i<controls_.size();++i) {
        const auto& c=controls_[i];
        if((browser_ && c.y<kBrowserRect.Y+kBrowserRect.Height) || !visible(bounds(c))) continue;
        drawControlDynamic(g,c,hoveredControl_==int(i));
    }
    if(midiLearn_ && !browser_) drawLearn(g);
    if(layoutEditing_ && !browser_) drawLayoutGuides(g);
    for(const int id:{3,4,5,6,16})
        if(hoveredChrome_==id) drawChrome(g,id,true);
    if(visible(kLcdRect)) {
        int loadedBank=-1,loadedSlot=-1;
        if(knownRevision_>0 && hooks_.loadedProgram)
            hooks_.loadedProgram(hooks_.ctx,&loadedBank,&loadedSlot);
        loadedBankForPaint_=loadedBank;
        loadedSlotForPaint_=loadedSlot;
        const int prefix=(loadedSlot>=0 ? loadedSlot+1 : 0);
        wchar_t formattedNumber[16];
        if(prefix) std::swprintf(formattedNumber,16,L"%03d: ",prefix);
        else std::wcscpy(formattedNumber,L"---: ");
        text(g,std::wstring(formattedNumber)+patchName(),60,489,330,25,17,rgb(0xDFFAFF),true,false,true);
        std::wstring second=flashText_;
        if(second.empty()) {
            if(loadedBank>=0 && loadedBank<int(banks_.size())) {
                wchar_t num[16];std::swprintf(num,16,L"%02d: ",displayBankNumber(loadedBank));
                second=std::wstring(num)+toWide(banks_[size_t(loadedBank)].name.c_str());
            } else second=knownRevision_>0 ? L"Click here to browse banks" : L"Scanning preset banks...";
        }
        text(g,second,60,526,392,23,13,p[kDisplayText],false,false,true);
    }
    if(!midiLearn_) {
        if(visible(kGridRect)) {
            const uint64_t states=hooks_.voiceStates ? hooks_.voiceStates(hooks_.ctx) : 0;
            // Poly shows the allowed voice count; Mono and Legato show one
            // allocated square. Playing/released voices remain visible in all modes.
            const int allocated=value(38)==0 ? std::clamp(value(94),1,32) : 1;
            constexpr float cell=215.f/16.f;
            // Vertically centre the two-row grid beside the Write/Menu button pair.
            const float left=kGridRect.X,top=kGridRect.Y;
            constexpr float inset=1.75f; // clear gap from the grid lines
            // Windows-only voice-grid theme: playing, released, empty, border.
            const Color held=rgb(int(voiceGrid_[2]));
            const Color releasing=rgb(int(voiceGrid_[3]));
            const Color available=rgb(int(voiceGrid_[1]));
            for(int i=0;i<32;i++) {
                const int state=int((states>>(i*2))&3);
                const Color* color=state==2?&held:state==1?&releasing:i<allocated?&available:nullptr;
                if(color) {
                    const float x=left+(i%16)*cell, y=top+(i/16)*cell;
                    fill(g,*color,x+inset,y+inset,cell-2*inset,cell-2*inset);
                }
            }
            // Draw one continuous grid instead of 32 overlapping rectangles.
            Pen grid(rgb(int(voiceGrid_[0])),.75f);
            for(int column=0;column<=16;column++) {
                const float x=left+column*cell+.5f;
                g.DrawLine(&grid,x,top,x,top+2*cell);
            }
            for(int row=0;row<=2;row++) {
                const float y=top+row*cell+.5f;
                g.DrawLine(&grid,left,y,left+16*cell,y);
            }
        }
    } else {
        darkButton(g,rgb(0xA30D0D),558,488,186,24,hoveredChrome_==8);
        text(g,L"Exit MIDI Learn",558,488,186,24,11,rgb(0xFFFFFF),true,true);
        text(g,L"Select a control, then move a MIDI CC",548,518,206,20,9,p[kLabel],true,true);
    }
    if(browser_ && visible(kBrowserRect)) drawBrowser(g);
    if(press_.active && press_.dragging && visible(dragLabelRect())) {
        // slotDraggable's preview: the label on the display colour
        const RectF r=dragLabelRect();
        GraphicsPath box;roundedPath(box,r.X,r.Y,r.Width,r.Height,2.f);
        SolidBrush face(Color(230,p[themes::kDisplayTop].GetR(),p[themes::kDisplayTop].GetG(),p[themes::kDisplayTop].GetB()));
        g.FillPath(&face,&box);
        text(g,press_.label,r.X+3,r.Y,r.Width-6,r.Height,10.2f,rgb(kLcdText),false,false,true);
    }
}

void WindowsEditor::refreshBanks() {
    // Even a request to obtain Json() is a blocking scan before revision 1.
    if(knownRevision_<=0 || !hooks_.libraryJSON) return;
    const char* data=hooks_.libraryJSON(hooks_.ctx);
    if(!data) return;
    std::string json=data; // the hooks share one scratch buffer
    // SynthModel.loadLibrary: a zip found in the library was imported
    // ("X.zip imported and moved to the Recycle Bin"), shown 10 s.
    if(const char* notice=hooks_.libraryNotice ? hooks_.libraryNotice(hooks_.ctx) : nullptr; notice && *notice)
        flash(toWide(notice),10000);
    data=json.c_str();
    json::Value root;
    if(!json::Parse(data,root) || !root.IsObject()) return;
    std::vector<Bank> scanned;
    std::string libraryRoot;
    if(const auto* v=root.Find("root"); v && v->IsString()) libraryRoot=v->string;
    if(const auto* list=root.Find("banks"); list && list->type==json::Value::Type::Array) {
        for(const auto& item:list->array) {
            Bank b;
            if(const auto* v=item.Find("name"); v && v->IsString()) b.name=v->string;
            if(const auto* v=item.Find("source"); v && v->IsString()) b.source=v->string;
            if(const auto* v=item.Find("readOnly")) b.readOnly=v->type==json::Value::Type::Bool && v->boolean;
            if(const auto* v=item.Find("slots"); v && v->type==json::Value::Type::Array)
                for(const auto& slot:v->array) b.slots.push_back(slot.IsString() ? slot.string : std::string());
            scanned.push_back(std::move(b));
        }
    }
    // Backend order/indices remain unchanged. Natural sort is UI-only, so
    // the .sy1 slot numbers, host state and bank hook indices are untouched.
    // The browser stays on the bank it shows (SynthModel.loadLibrary keeps
    // currentBank by its id = source).
    const std::string previousSelection = selectedBank_>=0 && selectedBank_<int(banks_.size()) ?
        banks_[size_t(selectedBank_)].source : std::string();
    libraryRoot_=std::move(libraryRoot);
    banks_=std::move(scanned);
    for(auto& b:banks_) b.slotColors.assign(b.slots.size(),0); // lazy preset colour cache
    favouritesLookup_.RebuildBanks(std::filesystem::path(libraryRoot_),banks_);
    for(auto& b:banks_) b.wideName=toWide(b.name.c_str()); // once per refresh, not per painted row
    if(!previousSelection.empty()) {
        selectedBank_=bankBySource(previousSelection);
        if(selectedBank_<0) selectedSlot_=-1;
    }
    bankOrder_.resize(banks_.size());
    std::iota(bankOrder_.begin(),bankOrder_.end(),0);
    std::stable_sort(bankOrder_.begin(),bankOrder_.end(),[this](int a,int b) {
        // The read-only factory bank stays first.
        if(a==0 && banks_[size_t(a)].readOnly) return true;
        if(b==0 && banks_[size_t(b)].readOnly) return false;
        return NaturalLess(banks_[size_t(a)].wideName,banks_[size_t(b)].wideName);
    });
    int loadedBank=-1,loadedSlot=-1;
    if(hooks_.loadedProgram) hooks_.loadedProgram(hooks_.ctx,&loadedBank,&loadedSlot);
    if(selectedBank_==-1 && loadedBank>=0) selectedBank_=loadedBank;
    if(selectedBank_==-1 && !banks_.empty()) selectedBank_=bankOrder_.front();
    if(selectedBank_>=int(banks_.size())) selectedBank_=banks_.empty()?-1:bankOrder_.front();
    if(selectedSlot_<0 && loadedSlot>=0 && selectedBank_==loadedBank) selectedSlot_=loadedSlot;
    // The write dialog never targets the read-only factory bank: out of it
    // (or out of no bank) it opens on the "User" bank, proposed even when
    // the library has none (the write makes it). A writable bank in view
    // stays (the loaded one: written back). As PatchBrowserView.leaveReadOnlyBank.
    if(writing_ && !banks_.empty() && (selectedBank_<0 || banks_[size_t(selectedBank_)].readOnly)) {
        const int user=userBank();
        const int pick=user>=0 ? user : kNewUserBank;
        if(pick!=selectedBank_) {
            selectedBank_=pick;selectedSlot_=-1;
            scrollBankIntoView(pick);
        }
    }
    if(writing_ && selectedSlot_<0 && selectedBank_==kNewUserBank) selectedSlot_=0;
    if(writing_ && selectedSlot_<0 && selectedBank_>=0) {
        const auto& slots=banks_[size_t(selectedBank_)].slots;
        auto it=std::find(slots.begin(),slots.end(),"");
        selectedSlot_=it==slots.end()?0:int(it-slots.begin());
    }
    bankScroll_=std::clamp(bankScroll_,0,std::max(0,bankRowCount()-20));
    repaint();
}
int WindowsEditor::userBank() const {
    for(size_t b=0;b<banks_.size();b++)
        if(!banks_[b].readOnly && banks_[b].name==s2::kUserBankName) return int(b);
    return -1;
}
bool WindowsEditor::offersNewUserBank() const {
    return writing_ && !banks_.empty() && userBank()<0;
}
int WindowsEditor::bankRowCount() const {
    return int(bankOrder_.size())+(writing_?0:1)+(offersNewUserBank()?1:0);
}
int WindowsEditor::bankRowOf(int bank) const {
    if(bank==kFavouritesBank)return writing_?-1:0;
    if(bank==kNewUserBank) return offersNewUserBank() ? int(bankOrder_.size()) : -1;
    const auto it=std::find(bankOrder_.begin(),bankOrder_.end(),bank);
    return it==bankOrder_.end() ? -1 : int(it-bankOrder_.begin())+(writing_?0:1);
}
void WindowsEditor::scrollBankIntoView(int bank) {
    const int row=bankRowOf(bank);
    if(row>=0 && (row<bankScroll_ || row>=bankScroll_+20)) bankScroll_=std::max(0,row-19);
}
int WindowsEditor::bankBySource(const std::string& source) const {
    for(size_t i=0;i<banks_.size();i++) if(banks_[i].source==source) return int(i);
    return -1;
}
// The library changed under us (a hook rescanned): take the new list.
void WindowsEditor::adoptLibrary() {
    if(hooks_.libraryRevision) knownRevision_=hooks_.libraryRevision(hooks_.ctx);
    refreshBanks();
}
// A bank's index in the hooks' list as of NOW, -1 = gone: menus, message
// boxes and drags are modal loops in which the timer runs, and a scan
// landing meanwhile renumbers the banks.
int WindowsEditor::bankNow(const std::string& source) {
    if(hooks_.libraryRevision && hooks_.libraryRevision(hooks_.ctx)!=knownRevision_) adoptLibrary();
    return bankBySource(source);
}
// "NNN:name" of a slot (LCD messages, drag label).
std::wstring WindowsEditor::slotText(int bank,int slot) const {
    wchar_t number[8];std::swprintf(number,8,L"%03d:",slot+1);
    return number+toWide(banks_[size_t(bank)].slots[size_t(slot)].c_str());
}
std::string WindowsEditor::bankKey(int bank) const {
    return favouritesLookup_.BankKeyAt(bank);
}
bool WindowsEditor::resolveFavourite(const winfav::Item& f,int& bank,int& slot) const {
    int found=-1;
    if(!favouritesLookup_.FindBank(f.bank,found))return false;
    const auto& slots=banks_[size_t(found)].slots;
    if(f.slot<0 || f.slot>=int(slots.size()) || slots[size_t(f.slot)].empty())
        return false;
    bank=found;slot=f.slot;return true;
}
bool WindowsEditor::isFavourite(int bank,int slot) const {
    return favouritesLookup_.Contains(bank,slot);
}
void WindowsEditor::toggleFavourite() {
    if(!favouritesReadable_) {
        MessageBoxW(window_,L"Cannot read favourites.json. No changes were made to protect that file.",
                    L"Synth2 Favourites",MB_OK|MB_ICONWARNING);return;
    }
    int bank=-1,slot=-1;
    if(selectedBank_==kFavouritesBank && selectedSlot_>=0 && selectedSlot_<int(favourites_.size())) {
        // Removing an item from the virtual list never modifies the original preset.
        const auto& item=favourites_[size_t(selectedSlot_)];
        resolveFavourite(item,bank,slot);
        std::vector<winfav::Item> revised=favourites_;
        revised.erase(revised.begin()+selectedSlot_);
        if(!winfav::Save(storage::Root(),revised)) {
            MessageBoxW(window_,L"Unable to save favourites.json; your previous list is unchanged.",
                        L"Synth2 Favourites",MB_OK|MB_ICONWARNING);return;
        }
        favourites_=std::move(revised);
        favouritesLookup_.RebuildFavourites(favourites_);
        selectedSlot_=std::min(selectedSlot_,int(favourites_.size())-1);
        repaint();return;
    }
    if(selectedBank_>=0 && selectedBank_<int(banks_.size()) &&
       selectedSlot_>=0 && selectedSlot_<int(banks_[size_t(selectedBank_)].slots.size()) &&
       !banks_[size_t(selectedBank_)].slots[size_t(selectedSlot_)].empty()) {
        bank=selectedBank_;slot=selectedSlot_;
    } else if(knownRevision_>0 && hooks_.loadedProgram) {
        hooks_.loadedProgram(hooks_.ctx,&bank,&slot);
    }
    if(bank<0 || bank>=int(banks_.size()) || slot<0 ||
       slot>=int(banks_[size_t(bank)].slots.size()) || banks_[size_t(bank)].slots[size_t(slot)].empty()) {
        MessageBoxW(window_,L"Select a preset first, then click Favourite.",
                    L"Synth2 Favourites",MB_OK|MB_ICONINFORMATION);return;
    }
    const std::string key=bankKey(bank);
    std::vector<winfav::Item> revised=favourites_;
    auto it=std::find_if(revised.begin(),revised.end(),[&](const winfav::Item& f){
        return f.bank==key && f.slot==slot;
    });
    if(it!=revised.end())revised.erase(it); // toggle off
    else revised.push_back({key,slot,banks_[size_t(bank)].slots[size_t(slot)]});
    if(!winfav::Save(storage::Root(),revised)) {
        MessageBoxW(window_,L"Unable to save favourites.json; your previous list is unchanged.",
                    L"Synth2 Favourites",MB_OK|MB_ICONWARNING);return;
    }
    favourites_=std::move(revised);
    favouritesLookup_.RebuildFavourites(favourites_);
    repaint();
}
// Update favourite slot references when an in-bank drag MOVE or SWAP succeeds.
void WindowsEditor::updateFavouritesAfterMove(int bank,int from,int to) {
    if(!favouritesReadable_ || bank<0)return;
    const std::string key=bankKey(bank);
    auto list=favourites_;bool changed=false;
    for(auto& item:list)if(item.bank==key) {
        if(item.slot==from) {item.slot=to;changed=true;}
        else if(item.slot==to) {item.slot=from;changed=true;}
    }
    if(!changed)return;
    if(!winfav::Save(storage::Root(),list)) {
        MessageBoxW(window_,L"The preset moved, but favourites.json could not be updated.\nPlease check the favourites list.",
                    L"Favourites",MB_OK|MB_ICONWARNING);return;
    }
    favourites_=std::move(list);favouritesLookup_.RebuildFavourites(favourites_);
}
void WindowsEditor::pruneFavourites(const std::string& bank,int slot) {
    if(!favouritesReadable_ || bank.empty())return;
    auto list=favourites_;
    list.erase(std::remove_if(list.begin(),list.end(),[&](const winfav::Item& item){
        return item.bank==bank && (slot<0 || item.slot==slot);
    }),list.end());
    if(list.size()==favourites_.size())return;
    if(!winfav::Save(storage::Root(),list)) {
        MessageBoxW(window_,L"The preset was deleted, but favourites.json could not be updated.",
                    L"Favourites",MB_OK|MB_ICONWARNING);return;
    }
    favourites_=std::move(list);favouritesLookup_.RebuildFavourites(favourites_);
}
void WindowsEditor::openBrowser(bool writing) {
    browser_=true;writing_=writing;
    bankScroll_=slotScroll_=0;selectedSlot_=-1;
    // SynthModel.openBrowser: opening selects the loaded program's bank (the
    // write dialog: refreshBanks then leaves a read-only one for User).
    int loadedBank=-1,loadedSlot=-1;
    if(knownRevision_>0 && hooks_.loadedProgram) hooks_.loadedProgram(hooks_.ctx,&loadedBank,&loadedSlot);
    if(loadedBank>=0 && loadedBank<int(banks_.size())) {
        selectedBank_=loadedBank;
        if(!writing_)selectedSlot_=loadedSlot;
    }
    else if(selectedBank_==kNewUserBank || (writing_ && selectedBank_==kFavouritesBank)) selectedBank_=-1;
    // SynthModel.openBrowser: a background rescan on every open (the list
    // in place stays usable; a scan already running is not restarted).
    if(hooks_.startLibraryScan) hooks_.startLibraryScan(hooks_.ctx);
    refreshBanks();
    scrollBankIntoView(selectedBank_); // as the list's scrollTo(currentBank)
    updateEditControl();
    repaint();
}
void WindowsEditor::closeBrowser() {
    browser_=writing_=false;
    if(selectedBank_==kNewUserBank) selectedBank_=-1; // the next refresh selects the loaded bank
    updateEditControl();
    repaint();
}
int WindowsEditor::bankRowAt(float x,float y) const {
    if(x<7 || x>=196 || y<64 || y>469) return -1;
    const int visual=bankScroll_+int((y-64)/20);
    if(!writing_ && visual==0)return kFavouritesBank;
    const int actual=visual-(writing_?0:1);
    if(actual>=0 && actual<int(bankOrder_.size())) return bankOrder_[size_t(actual)];
    return actual==int(bankOrder_.size()) && offersNewUserBank() ? kNewUserBank : -1;
}
int WindowsEditor::slotAt(float x,float y) const {
    if(x<204 || x>=760 || y<64 || y>469) return -1;
    int count=-1;
    if(writing_ && selectedBank_==kNewUserBank) count=s2::kBankSlots;
    else if(!writing_ && selectedBank_==kFavouritesBank) count=int(favourites_.size());
    else if(selectedBank_>=0 && selectedBank_<int(banks_.size())) count=int(banks_[size_t(selectedBank_)].slots.size());
    if(count<=0) return -1;
    const bool favouriteView=!writing_ && selectedBank_==kFavouritesBank;
    const int rows=std::max(1,(count+3)/4);
    const int row=slotScroll_+int((y-64)/20);
    if(row>=rows) return -1; // below a short grid, not the next column
    const int col=std::clamp(int((x-204)/138.5f),0,3);
    const int index=col*rows+row;
    return index<count ? index : -1;
}

// Right click in the browser = PatchBrowserView's libraryMenu: a bank =
// Export bank... / Show in Explorer [/ Delete bank...], a patch = Show in
// Explorer [/ Delete]; nothing on read-only banks' deletes, nothing on
// banks the write dialog cannot pick. Elsewhere in the browser: no menu.
bool WindowsEditor::showLibraryMenu(float x,float y,int screenX,int screenY) {
    if(!browser_ || !kBrowserRect.Contains(x,y)) return false;
    enum {kExport=1,kReveal,kDeleteBank,kDelete};
    // What the menu acts on, taken NOW: the menu and the message boxes are
    // modal loops in which the timer still runs, and a background scan
    // landing meanwhile renumbers the banks.
    std::string source,slotFile,slotFavouriteBank;
    int slotFavouriteNumber=-1;
    std::wstring slotLabel; // "NNN:name", for the LCD
    HMENU menu=CreatePopupMenu();
    if(!menu) return true;
    if(const int row=bankRowAt(x,y); row>=0 && row<int(banks_.size())) {
        const Bank& b=banks_[size_t(row)];
        if(!(writing_ && b.readOnly)) {
            source=b.source;
            AppendMenuW(menu,MF_STRING,kExport,L"Export bank...");
            AppendMenuW(menu,MF_STRING,kReveal,L"Show in Explorer");
            if(!b.readOnly) {
                AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
                AppendMenuW(menu,MF_STRING,kDeleteBank,L"Delete bank...");
            }
        }
    } else if(const int index=slotAt(x,y); index>=0 && selectedBank_!=kNewUserBank) {
        const int b=selectedBank_,s=index;
        if(b>=0 && b<int(banks_.size()) && !banks_[size_t(b)].slots[size_t(s)].empty() && hooks_.slotPath) {
            slotFile=hooks_.slotPath(hooks_.ctx,b,s);
            slotFavouriteBank=bankKey(b);slotFavouriteNumber=s;
            slotLabel=slotText(b,s);
            AppendMenuW(menu,MF_STRING,kReveal,L"Show in Explorer");
            if(!banks_[size_t(b)].readOnly) {
                AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
                AppendMenuW(menu,MF_STRING,kDelete,L"Delete");
            }
        }
    }
    const int choice=GetMenuItemCount(menu)>0 ?
        int(TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY,screenX,screenY,0,window_,nullptr)) : 0;
    DestroyMenu(menu);
    auto adopt=[this] {adoptLibrary();};
    auto bankNow=[&] {return this->bankNow(source);};
    switch(choice) {
    case kReveal:
        revealInExplorer(slotFile.empty() ? source : slotFile);
        break;
    case kExport: {
        std::wstring dir;
        if(!pickFolder(L"Export bank: a folder holding its patches is created in the folder you choose",dir)) break;
        const int bank=bankNow();
        const std::string made=bank>=0 && hooks_.exportBank ? hooks_.exportBank(hooks_.ctx,bank,toUtf8(dir).c_str()) : "";
        if(made.empty()) MessageBoxW(window_,L"Unable to export this bank.",L"Synth2",MB_ICONWARNING);
        else {
            flash(L"bank exported as "+std::filesystem::path(toWide(made.c_str())).filename().wstring(),4000);
            revealInExplorer(made);
        }
        break;
    }
    case kDeleteBank: {
        // DeleteBankDialog
        int bank=bankNow();
        if(bank<0) break;
        const Bank& b=banks_[size_t(bank)];
        const auto count=std::count_if(b.slots.begin(),b.slots.end(),[](const std::string& s){return !s.empty();});
        const std::wstring name=toWide(b.name.c_str()); // b may not survive the box's modal loop
        const std::wstring detail=L"\u201C"+name+L"\u201D ("+std::to_wstring(count)+
            (count==1?L" patch":L" patches")+L") will be moved to the Recycle Bin.";
        if(MessageBoxW(window_,detail.c_str(),L"Delete bank?",MB_YESNO|MB_ICONWARNING|MB_DEFBUTTON2)!=IDYES) break;
        bank=bankNow();
        if(bank<0 || !hooks_.deleteBank || !hooks_.deleteBank(hooks_.ctx,bank))
            MessageBoxW(window_,L"Unable to delete this bank.",L"Synth2",MB_ICONWARNING);
        else {
            pruneFavourites(winfav::BankKey(std::filesystem::path(libraryRoot_),
                           std::filesystem::path(source),false));
            flash(L"bank "+name+L" moved to the Recycle Bin");
        }
        adopt();
        break;
    }
    case kDelete: {
        // SynthModel.deleteSlot: the .sy1 to the Recycle Bin (PatchManager's
        // rule: deleted where there is none), then a rescan.
        if(slotFile.empty()) break;
        const std::filesystem::path file(toWide(slotFile.c_str()));
        if(MoveToTrash(file)) {
            pruneFavourites(slotFavouriteBank,slotFavouriteNumber);
            flash(slotLabel+L" moved to the Recycle Bin");
        }
        else {
            MessageBoxW(window_,L"This file could not be moved to the Recycle Bin. Nothing was deleted.",
                        L"Delete cancelled",MB_OK|MB_ICONWARNING);
            break;
        }
        if(hooks_.rescanLibrary) hooks_.rescanLibrary(hooks_.ctx);
        adopt();
        break;
    }
    default: break;
    }
    repaint();
    return true;
}
// Finder.reveal: the containing folder opens with the item selected.
void WindowsEditor::revealInExplorer(const std::string& utf8Path) const {
    if(utf8Path.empty()) return;
    std::wstring path=toWide(utf8Path.c_str());
    std::replace(path.begin(),path.end(),L'/',L'\\');
    const HRESULT com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    if(auto item=ILCreateFromPathW(path.c_str())) {
        SHOpenFolderAndSelectItems(item,0,nullptr,0);
        ILFree(item);
    }
    if(SUCCEEDED(com)) CoUninitialize();
}
bool WindowsEditor::pickFolder(const wchar_t* title,std::wstring& folder) const {
    BROWSEINFOW browse{};
    browse.hwndOwner=window_;
    browse.lpszTitle=title;
    browse.ulFlags=BIF_RETURNONLYFSDIRS|BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE item=SHBrowseForFolderW(&browse);
    if(!item) return false;
    wchar_t path[MAX_PATH]={};
    const BOOL gotPath=SHGetPathFromIDListW(item,path);
    CoTaskMemFree(item);
    if(!gotPath) return false;
    folder=path;
    return true;
}
// The bank's number in the list ("NN:"), as the browser shows it.
int WindowsEditor::displayBankNumber(int bank) const {
    const int row=bankRowOf(bank);
    return row>=0 ? row : bank;
}
bool WindowsEditor::isVersionBox(float x,float y) const {
    return x>=versionLeft_ && x<762.f && y>=2.f && y<31.f;
}

// Standard Windows About dialog. Two attribution links, no Common Controls v6.
void WindowsEditor::showAbout() {
    AboutDialogState state{};
    state.editorWindow=window_;
    std::vector<WORD> words; // DLGTEMPLATE and DLGITEMTEMPLATE; DWORD aligned
    const auto dword=[&](DWORD value) {
        words.push_back(LOWORD(value));
        words.push_back(HIWORD(value));
    };
    const auto str=[&](const wchar_t* value) {
        while(*value)words.push_back(WORD(*value++));
        words.push_back(0);
    };
    dword(DS_MODALFRAME|DS_SETFONT|WS_POPUP|WS_CAPTION|WS_SYSMENU);
    dword(0); // extended style
    words.push_back(10); // title, subtitle, credits, four rows/two links, Close
    // Preserve the original About dialog size: 265 x 120 dialog units.
    for(WORD value:{WORD(0),WORD(0),WORD(265),WORD(120)})words.push_back(value);
    words.push_back(0); // menu
    words.push_back(0); // window class
    str(L"About Synth2");
    words.push_back(9); // default dialog font size in points
    str(L"Segoe UI");
    const auto item=[&](DWORD style,WORD x,WORD y,WORD w,WORD h,
                        WORD id,WORD klass,const wchar_t* title) {
        if(words.size()%2)words.push_back(0);
        dword(style|WS_CHILD|WS_VISIBLE);
        dword(0);
        for(WORD value:{x,y,w,h,id})words.push_back(value);
        words.push_back(0xFFFF); // predefined window class
        words.push_back(klass);
        str(title);
        words.push_back(0); // no creation data
    };
    constexpr WORD kButtonClass=0x0080,kStaticClass=0x0082;
    item(SS_LEFT,10,7,245,20,kAboutTitle,kStaticClass,L"Synth2 Virtual Analog");
    item(SS_LEFT,10,23,245,12,0xFFFF,kStaticClass,L"A Synth1 recreation");
    item(SS_LEFT,10,42,245,12,kAboutCredits,kStaticClass,L"Credits:");
    // Four credit lines with no extra paragraph spacing.
    item(SS_LEFT,10,55,245,12,0xFFFF,kStaticClass,L"Original Synth1: Daichi Laboratory");
    item(SS_LEFT,10,67,170,12,kAboutOxoLabel,kStaticClass,L"Synth1 recreated as Synth2:");
    item(SS_LEFT|SS_NOTIFY,180,67,50,12,kAboutOxoLink,kStaticClass,L"Oxo Music");
    item(SS_LEFT,10,79,245,12,0xFFFF,kStaticClass,L"Synth2 - Windows version: sl23");
    item(SS_LEFT,10,91,170,12,kAboutForkLabel,kStaticClass,L"Synth2 - Current fork:");
    item(SS_LEFT|SS_NOTIFY,180,91,50,12,kAboutForkLink,kStaticClass,L"sl23");
    item(BS_DEFPUSHBUTTON|WS_TABSTOP,205,103,50,14,IDOK,kButtonClass,L"Close");
    const HWND owner=window_ ? GetAncestor(window_,GA_ROOT) : nullptr;
    if(DialogBoxIndirectParamW(win::ModuleInstance(),
         reinterpret_cast<LPCDLGTEMPLATEW>(words.data()),owner,
         AboutDialogProc,reinterpret_cast<LPARAM>(&state))==-1) {
        const std::wstring fallback=L"Synth2 Virtual Analog\nA Synth1 recreation\n\n"
            L"Credits:\nOriginal Synth1: Daichi Laboratory\n"
            L"Synth1 recreated as Synth2: Oxo Music\n"
            L"Synth2 - Windows version: sl23\nSynth2 - Current fork: sl23";
        MessageBoxW(window_,fallback.c_str(),L"About Synth2",MB_OK|MB_ICONINFORMATION);
    }
}
void WindowsEditor::updateEditControl() {
    if(!window_) return;
    if(!browser_ || !writing_) {
        if(editName_) {DestroyWindow(editName_);editName_=nullptr;}
        return;
    }
    const float sx=float(std::max(1,rect.getWidth()))/kDesignWidth;
    const float sy=float(std::max(1,rect.getHeight()))/kDesignHeight;
    if(!editName_) {
        editName_=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",patchName().c_str(),
            WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL|WS_TABSTOP,
            int(300*sx),int(38*sy),int(200*sx),int(19*sy),
            window_,nullptr,win::ModuleInstance(),nullptr);
        if(editName_) {
            SendMessageW(editName_,WM_SETFONT,WPARAM(GetStockObject(DEFAULT_GUI_FONT)),TRUE);
            // Return / Esc in the field: write / cancel (nameFieldProc); the
            // previous procedure is kept first, before any message comes.
            SetWindowLongPtrW(editName_,GWLP_USERDATA,GetWindowLongPtrW(editName_,GWLP_WNDPROC));
            SetWindowLongPtrW(editName_,GWLP_WNDPROC,LONG_PTR(&WindowsEditor::nameFieldProc));
        }
    } else MoveWindow(editName_,int(300*sx),int(38*sy),int(200*sx),int(19*sy),TRUE);
}

void WindowsEditor::drawBrowser(Graphics& g) {
    const auto& p=colors_;
    // Keep the browser surface independently themeable. Existing theme files
    // without browserBackground use the previous Display Gradient Bottom colour.
    const Color browserBackground=rgb(int(voiceGrid_[5]));
    const Color selectionFill=rgb(int(voiceGrid_[4]));
    const int luminance=(299*selectionFill.GetR()+587*selectionFill.GetG()+114*selectionFill.GetB())/1000;
    const Color selectionInk=luminance<140 ? rgb(0xFFFFFF) : rgb(0x101010);
    const Color nameInk=rgb(kLcdText);
    const Color emptyInk=blend(browserBackground,nameInk,.3f);
    // Numbering is warm yellow on dark rows and dark gold on pale selections.
    // Keep the bank/preset names in their existing theme colours.
    const Color numberInk=rgb(0xE9D56A);
    const Color favouriteNumberInk=rgb(0x56E496);
    const Color selectedNumberInk=selectionInk;
    // Read only visible preset headers, once per slot per library refresh.
    // Files may be nested; the host provides their actual path, not an
    // assumed Presets/bank/NNN.sy1 path. Unknown colours keep theme ink.
    const auto presetInk=[&](int bank,int slot)->Color {
        if(!presetTextColours_)return nameInk;
        if(bank<0 || bank>=int(banks_.size()) || slot<0 ||
           slot>=int(banks_[size_t(bank)].slotColors.size())) return nameInk;
        auto& cache=banks_[size_t(bank)].slotColors[size_t(slot)];
        if(cache==0) {
            cache=1; // default; also covers unreadable files
            if(hooks_.slotPath) {
                const char* raw=hooks_.slotPath(hooks_.ctx,bank,slot);
                if(raw && *raw) {
                    std::ifstream file(std::filesystem::u8path(raw),std::ios::binary);
                    std::string line;
                    for(int n=0;n<5 && std::getline(file,line);++n) {
                        if(line.rfind("color=",0)!=0) continue;
                        while(!line.empty() && (line.back()=='\r' || line.back()==' ')) line.pop_back();
                        const std::string value=line.substr(6);
                        if(value=="red") cache=2;
                        else if(value=="blue") cache=3;
                        else if(value=="purple") cache=4;
                        else if(value=="green") cache=5;
                        else if(value=="cyan") cache=6;
                        else if(value=="magenta") cache=7;
                        else if(value=="yellow") cache=8;
                        break;
                    }
                }
            }
        }
        switch(cache) {
            case 2:return rgb(0xFF7777); // red
            case 3:return rgb(0x7FB8FF); // blue
            case 4:return rgb(0xC49BFA); // purple
            case 5:return rgb(0x56E496); // green
            case 6:return rgb(0x64E6E6); // cyan
            case 7:return rgb(0xFF80DA); // magenta
            case 8:return rgb(0xF4A340); // preset yellow: distinct orange-amber from the yellow slot numbers
            default:return nameInk;       // default / unknown
        }
    };
    const float x=6,y=34,w=758,h=444;
    fill(g,p[kPanel],x,y,w,h);
    stroke(g,p[kHeader],x,y,w,h,1.5f);
    fill(g,p[kHeader],x+1,y+1,w-2,27);
    text(g,writing_?L"Write patch":L"Banks",x+7,y+2,135,24,12,p[kHeaderText],true);
    if(writing_) {
        darkButton(g,p[kButton],514,38,84,20,hoveredChrome_==9);
        text(g,L"Write patch",514,38,84,20,10,rgb(0xFFFFFF),true,true);
        darkButton(g,p[kButton],604,38,74,20,hoveredChrome_==10);
        text(g,L"New bank...",604,38,74,20,10,rgb(0xFFFFFF),true,true);
    } else {
        darkButton(g,p[kButton],348,38,76,20,hoveredChrome_==15);
        text(g,L"Favourite",348,38,76,20,10,rgb(0xFFFFFF),true,true);
        darkButton(g,p[kButton],430,38,66,20,hoveredChrome_==11);
        text(g,L"Import...",430,38,66,20,10,rgb(0xFFFFFF),true,true);
        darkButton(g,p[kButton],502,38,112,20,hoveredChrome_==12);
        text(g,L"Library folder",502,38,112,20,10,rgb(0xFFFFFF),true,true);
        darkButton(g,p[kButton],620,38,60,20,hoveredChrome_==13);
        text(g,L"Refresh",620,38,60,20,10,rgb(0xFFFFFF),true,true);
    }
    darkButton(g,p[kButton],690,38,64,20,hoveredChrome_==14);
    text(g,L"Close",690,38,64,20,10,rgb(0xFFFFFF),true,true);
    fill(g,browserBackground,x+1,y+28,190,h-30);
    fill(g,browserBackground,x+191,y+28,w-192,h-30);
    stroke(g,p[kHeader],x+190,y+28,1,h-30);
    constexpr float row=20;
    const int visualCount=bankRowCount(); // favourites (+ banks, or new User)
    for(int i=0;i<20;i++) {
        const int visual=bankScroll_+i;
        if(visual<0 || visual>=visualCount)break;
        const bool isFavouriteBank=!writing_ && visual==0;
        const int actual=visual-(writing_?0:1);
        const int bi=isFavouriteBank?kFavouritesBank:
            actual<int(bankOrder_.size())?bankOrder_[size_t(actual)]:kNewUserBank;
        const bool isNewUser=bi==kNewUserBank;
        const float ry=64+row*i;
        if(bi==selectedBank_)fill(g,selectionFill,8,ry,187,19);
        int count=isFavouriteBank?int(favourites_.size()):0;
        if(bi>=0)
            for(const auto& slot:banks_[size_t(bi)].slots)if(!slot.empty())++count;
        wchar_t prefix[12];std::swprintf(prefix,12,L"%02d:",visual);
        const Color bankText=bi==selectedBank_?selectionInk:
            isFavouriteBank?rgb(0x56E496):p[kDisplayText];
        // the bank to be made has no number yet
        text(g,isNewUser?L"--:":prefix,12,ry,27,18,10.5f,
             bi==selectedBank_?selectedNumberInk:numberInk,false,false,true);
        text(g,isFavouriteBank?L"Favourites":isNewUser?toWide(s2::kUserBankName):
             ([&]() { const auto& name=banks_[size_t(bi)].wideName;
                      const size_t split=name.rfind(L" / ");
                      return split==std::wstring::npos ? name : name.substr(split+3); })(),
             39,ry,116,18,10.5f,bankText,false,false,true);
        text(g,isNewUser?L"new":std::to_wstring(count),164,ry,30,18,10.5f,bankText,false,true,true);
        if(bi>=0 && bi==dropBank_) stroke(g,p[kDisplayText],8.75f,ry+.75f,185.5f,17.5f,1.5f); // copy target
    }
    if(knownRevision_<=0) {
        text(g,L"Scanning preset banks in the background...",204,90,510,30,12,rgb(0xC1DCE0));
        return;
    }
    if(selectedBank_==-1) {
        text(g,L"Select a bank or Favourites.",204,90,510,30,12,rgb(0xC1DCE0));
        return;
    }
    const bool newUserView=writing_ && selectedBank_==kNewUserBank; // all slots free
    const bool favouriteView=!writing_ && selectedBank_==kFavouritesBank;
    if(!newUserView && !favouriteView && (selectedBank_<0 || selectedBank_>=int(banks_.size())))return;
    const int count=favouriteView?int(favourites_.size()):
        newUserView?s2::kBankSlots:int(banks_[size_t(selectedBank_)].slots.size());
    const int rows=std::max(1,(count+3)/4);
    const int columns=4;
    for(int rr=0;rr<20;rr++) {
        const int br=rr+slotScroll_;
        if(br>=rows)break;
        for(int col=0;col<columns;col++) {
            const int slotIndex=col*rows+br;
            if(slotIndex>=count)continue;
            const float x0=204.f+float(col)*138.5f;
            const float y0=65.f+float(rr)*20.f;
            std::string label;
            bool valid=false,favourited=false;
            int actualBank=selectedBank_,actualSlot=slotIndex;
            if(favouriteView) {
                const auto& fav=favourites_[size_t(slotIndex)];
                valid=resolveFavourite(fav,actualBank,actualSlot);
                label=valid?banks_[size_t(actualBank)].slots[size_t(actualSlot)]:fav.name+" (missing)";
                favourited=true;
            } else if(!newUserView) {
                label=banks_[size_t(selectedBank_)].slots[size_t(slotIndex)];
                valid=!label.empty();
                favourited=valid && isFavourite(selectedBank_,slotIndex);
            }
            const bool selected=slotIndex==selectedSlot_;
            const float width=136.f;
            fill(g,selected?selectionFill:browserBackground,x0,y0,width,18);
            wchar_t prefix[16];
            std::swprintf(prefix,16,L"%03d:",slotIndex+1);
            text(g,prefix,x0+3,y0,35,18,10.2f,
                 presetTextColours_ && valid && favourited ? favouriteNumberInk :
                 selected && !presetTextColours_ ? selectedNumberInk : numberInk,false,false,true);
            const Color labelInk=selected?selectionInk:
                !valid?(favouriteView?rgb(0xB07777):emptyInk):
                !presetTextColours_ && favourited ? rgb(0x56E496) :
                presetInk(actualBank,actualSlot);
            text(g,toWide(label.c_str()),x0+39,y0,93.f,18,10.2f,
                 labelInk,false,false,true);
            if(slotIndex==dropSlot_) roundedOutline(g,p[kDisplayText],x0+.75f,y0+.75f,width-1.5f,16.5f,2.f,1.5f); // move target
        }
    }
    if(writing_ && selectedBank_>=0 && banks_[size_t(selectedBank_)].readOnly)
        text(g,L"Factory bank is read-only. Choose a user bank.",213,445,490,19,10,rgb(0xE2A3A3));
}

void WindowsEditor::setValueOnce(int id,int v) {
    if(!hooks_.paramInfo || !hooks_.beginEdit || !hooks_.setValue || !hooks_.endEdit) return;
    int low=0,high=127,def=0;
    hooks_.paramInfo(hooks_.ctx,id,&low,&high,&def);
    v=std::clamp(v,low,high);
    const bool changed=v!=value(id); // SynthModel.set: no change, no readout
    hooks_.beginEdit(hooks_.ctx,id);
    hooks_.setValue(hooks_.ctx,id,v);
    hooks_.endEdit(hooks_.ctx,id);
    valueChanged(id);
    if(changed) show(id);
}
void WindowsEditor::cycle(int id,int step) {
    int low=0,high=127,def=0;
    if(hooks_.paramInfo) hooks_.paramInfo(hooks_.ctx,id,&low,&high,&def);
    const int v=value(id)+step;
    setValueOnce(id,v>high?low:v<low?high:v);
}
// < > (SynthModel.step): the next / previous non-empty slot, rolling into
// the neighbouring banks (list order) at the ends.
void WindowsEditor::stepPatch(int direction) {
    if(banks_.empty()) refreshBanks();
    const int n=int(bankOrder_.size());
    if(n==0) return;
    int bank=-1,slot=-1;
    if(knownRevision_>0 && hooks_.loadedProgram) hooks_.loadedProgram(hooks_.ctx,&bank,&slot);
    int row=bankRowOf(bank)-(writing_?0:1);
    if(row<0 || row>=n) {
        row=std::clamp(bankRowOf(selectedBank_)-(writing_?0:1),0,n-1);
        slot=-1;
    }
    int p=slot>=0 ? slot : direction>0 ? -1 : int(banks_[size_t(bankOrder_[size_t(row)])].slots.size());
    for(int guard=0;guard<n*s2::kBankSlots+1;guard++) {
        const auto& slots=banks_[size_t(bankOrder_[size_t(row)])].slots;
        p+=direction>0?1:-1;
        if(p>=int(slots.size())) {row=(row+1)%n;p=-1;continue;}
        if(p<0) {row=(row-1+n)%n;p=int(banks_[size_t(bankOrder_[size_t(row)])].slots.size());continue;}
        if(!slots[size_t(p)].empty()) {loadSlot(bankOrder_[size_t(row)],p);return;}
    }
}

// The write dialog's Write (its button, Return / Enter, or the bottom bar's
// write clicked again: PatchBrowserView.commitWrite).
void WindowsEditor::commitWrite() {
    if(!browser_ || !writing_) return;
    const bool newUser=selectedBank_==kNewUserBank;
    if(selectedSlot_<0)return;
    if(!newUser && (selectedBank_<0 || selectedBank_>=int(banks_.size()) ||
                    banks_[size_t(selectedBank_)].readOnly))return;
    std::wstring name=patchName();
    if(editName_) {const int n=GetWindowTextLengthW(editName_);name.resize(size_t(n)+1);
        GetWindowTextW(editName_,name.data(),n+1);name.resize(size_t(n));}
    // The proposed User bank is made now, by the write (newBank
    // rescans: its index is valid for the write right away).
    int bank=selectedBank_;
    if(newUser && (!hooks_.newBank || (bank=hooks_.newBank(hooks_.ctx,s2::kUserBankName))<0)) {
        MessageBoxW(window_,L"Cannot create the User bank. Check the library folder is writable.",
                    L"Synth2",MB_ICONWARNING);
        return;
    }
    if(hooks_.writeLibraryPatch && hooks_.writeLibraryPatch(hooks_.ctx,bank,selectedSlot_,toUtf8(name).c_str())) {
        const int slot=selectedSlot_;
        closeBrowser();refreshBanks();
        if(bank<int(banks_.size())) { // SynthModel.write
            wchar_t where[24];
            std::swprintf(where,24,L"written to %02d:",displayBankNumber(bank));
            wchar_t number[8];std::swprintf(number,8,L" %03d",slot+1);
            flash(where+toWide(banks_[size_t(bank)].name.c_str())+number);
        }
    } else MessageBoxW(window_,L"Cannot write that preset. Check the library folder is writable.",L"Synth2",MB_ICONWARNING);
}

// Keys (SynthModel.browserKey, fed by WM_KEYDOWN and IPlugView::onKeyDown):
// Esc cancels a drag / closes the browser / leaves MIDI learn; with the
// browser open, up / down = previous / next patch (rolling into the
// neighbouring banks, as < >), left / right = the neighbouring column
// (nearest patch), Return / Enter close; in the write dialog Return /
// Enter write. With Ctrl / Alt / Win held: the host's. False = not ours.
bool WindowsEditor::keyDown(UINT key) {
    if(layoutEditing_) {
        const bool ctrl=(GetKeyState(VK_CONTROL)&0x8000)!=0;
        const bool shift=(GetKeyState(VK_SHIFT)&0x8000)!=0;
        if(ctrl && (key==L'Z' || key==L'Y')) {
            if(key==L'Z')undoLayout();else redoLayout();
            return true;
        }
        if(ctrl && key==L'S') {
            const auto result=saveLayout();
            if(result==LayoutSaveResult::Failed)
                MessageBoxW(window_,L"Could not save layout.json.",L"Synth2 Layout",MB_OK|MB_ICONERROR);
            else if(result==LayoutSaveResult::Saved)flash(L"Layout saved",1500);
            return true;
        }
        if(key==VK_ESCAPE) {
            if(layoutDragging_)finishLayoutDrag(false);
            else if(layoutPart_!=LayoutPart::None) {layoutPart_=LayoutPart::None;layoutIndex_=-1;repaint();}
            else toggleLayoutEditor();
            return true;
        }
        if(key==VK_LEFT || key==VK_RIGHT || key==VK_UP || key==VK_DOWN) {
            if(!ctrl && layoutIndex_>=0) {
                const int delta=shift?5:1;
                nudgeLayout((key==VK_RIGHT?delta:key==VK_LEFT?-delta:0),
                            (key==VK_DOWN?delta:key==VK_UP?-delta:0));
            }
            return true;
        }
        return false;
    }
    if((GetKeyState(VK_CONTROL)|GetKeyState(VK_MENU)|GetKeyState(VK_LWIN)|GetKeyState(VK_RWIN))&0x8000) return false;
    if(key==VK_ESCAPE) {
        if(press_.active) dragEnd(false);
        else if(browser_) closeBrowser();
        else if(midiLearn_) setLearn(false);
        else return false;
        return true;
    }
    if(!browser_) {
        // the closed panel: left / right = < >
        if(key!=VK_LEFT && key!=VK_RIGHT) return false;
        stepPatch(key==VK_LEFT?-1:1);
        return true;
    }
    if(key==VK_RETURN) {
        if(writing_) commitWrite(); else closeBrowser();
        return true;
    }
    if(writing_ || (key!=VK_UP && key!=VK_DOWN && key!=VK_LEFT && key!=VK_RIGHT)) return false;
    if(selectedBank_<0 || selectedBank_>=int(banks_.size())) return true;
    const auto& slots=banks_[size_t(selectedBank_)].slots;
    int loadedBank=-1,loadedSlot=-1;
    if(knownRevision_>0 && hooks_.loadedProgram) hooks_.loadedProgram(hooks_.ctx,&loadedBank,&loadedSlot);
    if(loadedBank!=selectedBank_ || loadedSlot<0) { // another bank shown: its first patch
        const auto first=std::find_if(slots.begin(),slots.end(),[](const std::string& s){return !s.empty();});
        if(first!=slots.end()) loadSlot(selectedBank_,int(first-slots.begin()));
        return true;
    }
    if(key==VK_UP || key==VK_DOWN) {stepPatch(key==VK_DOWN?1:-1);return true;}
    const int count=int(slots.size()), rows=std::max(1,(count+3)/4);
    const int col=loadedSlot/rows+(key==VK_RIGHT?1:-1);
    if(col<0 || col*rows>=count) return true;
    const int wanted=col*rows+loadedSlot%rows;
    int best=-1;
    for(int s=col*rows;s<std::min((col+1)*rows,count);s++)
        if(!slots[size_t(s)].empty() && (best<0 || std::abs(s-wanted)<std::abs(best-wanted))) best=s;
    if(best>=0) loadSlot(selectedBank_,best);
    return true;
}
tresult PLUGIN_API WindowsEditor::onKeyDown(char16 key,int16 keyCode,int16 modifiers) {
    (void)key;
    if(modifiers&(kAlternateKey|kCommandKey|kControlKey)) return kResultFalse;
    UINT vk=0;
    switch(keyCode) {
    case KEY_UP: vk=VK_UP;break;       case KEY_DOWN: vk=VK_DOWN;break;
    case KEY_LEFT: vk=VK_LEFT;break;   case KEY_RIGHT: vk=VK_RIGHT;break;
    case KEY_RETURN: case KEY_ENTER: vk=VK_RETURN;break;
    case KEY_ESCAPE: vk=VK_ESCAPE;break;
    default: return kResultFalse;
    }
    return keyDown(vk) ? kResultTrue : kResultFalse;
}
// The write dialog's name field: Return / Enter and Esc go to keyDown (by
// a posted message: Esc / a write destroy this field), no beep.
LRESULT CALLBACK WindowsEditor::nameFieldProc(HWND field,UINT msg,WPARAM wp,LPARAM lp) {
    const auto previous=reinterpret_cast<WNDPROC>(GetWindowLongPtrW(field,GWLP_USERDATA));
    if(msg==WM_KEYDOWN && (wp==VK_RETURN || wp==VK_ESCAPE)) {
        PostMessageW(GetParent(field),kFieldKeyMessage,wp,0);
        return 0;
    }
    if(msg==WM_CHAR && (wp==L'\r' || wp==0x1B)) return 0;
    return CallWindowProcW(previous,field,msg,wp,lp);
}
// SynthModel.load: the browser follows the loaded patch.
void WindowsEditor::loadSlot(int bank,int slot) {
    if(!hooks_.loadLibraryPatch || !hooks_.loadLibraryPatch(hooks_.ctx,bank,slot)) return;
    if(bank!=selectedBank_) slotScroll_=0;
    selectedBank_=bank;selectedSlot_=slot;
    scrollBankIntoView(bank);
    const int rows=std::max(1,(int(banks_[size_t(bank)].slots.size())+3)/4), row=slot%rows;
    if(row<slotScroll_) slotScroll_=row; else if(row>=slotScroll_+20) slotScroll_=row-19;
    repaint();
}

void WindowsEditor::browserClick(float x,float y,bool doubleClick) {
    if(y>=37 && y<=61) {
        if(x>=690 && x<760) {closeBrowser();return;}
        if(!writing_ && x>=348 && x<426) {toggleFavourite();return;}
        if(writing_) {
            if(x>=514 && x<601) {commitWrite();return;}
            if(x>=604 && x<680) {
                // SynthModel.newBank: a uniquely named folder, then write into it.
                std::wstring name=L"New Bank";
                if(!hooks_.newBank || !win::AskText(window_,L"New bank",L"Bank name:",name)) return;
                const int bank=hooks_.newBank(hooks_.ctx,toUtf8(name).c_str());
                if(bank<0) {
                    MessageBoxW(window_,L"Cannot create that bank. Check the library folder is writable.",
                                L"Synth2",MB_ICONWARNING);
                    return;
                }
                if(hooks_.libraryRevision) knownRevision_=hooks_.libraryRevision(hooks_.ctx);
                refreshBanks();
                if(bank<int(banks_.size())) {selectedBank_=bank;selectedSlot_=0;slotScroll_=0;}
                repaint();
                return;
            }
        } else {
            if(x>=430 && x<498) {POINT p;GetCursorPos(&p);showImportDialog(p.x,p.y);return;}
            if(x>=502 && x<617) {
                if(hooks_.libraryRoot) {
                    auto path=toWide(hooks_.libraryRoot(hooks_.ctx));
                    ShellExecuteW(window_,L"open",path.c_str(),nullptr,nullptr,SW_SHOWNORMAL);
                }
                return;
            }
            if(x>=620 && x<681) {
                if(hooks_.startLibraryScan)hooks_.startLibraryScan(hooks_.ctx);
                else if(hooks_.rescanLibrary)hooks_.rescanLibrary(hooks_.ctx);
                refreshBanks();return;
            }
        }
    }
    if(y<64 || y>469)return;
    if(x>=7 && x<196) {
        const int bank=bankRowAt(x,y);
        if(bank==kFavouritesBank || bank==kNewUserBank ||
           (bank>=0 && bank<int(banks_.size()) && !(writing_ && banks_[size_t(bank)].readOnly))) {
            selectedBank_=bank;selectedSlot_=-1;slotScroll_=0;
            if(!writing_ && bank==kFavouritesBank && hooks_.loadedProgram) {
                int loadedBank=-1,loadedSlot=-1;
                hooks_.loadedProgram(hooks_.ctx,&loadedBank,&loadedSlot);
                for(size_t i=0;i<favourites_.size();++i) {
                    int source=-1,slot=-1;
                    if(resolveFavourite(favourites_[i],source,slot) &&
                       source==loadedBank && slot==loadedSlot) {selectedSlot_=int(i);break;}
                }
            }
            if(bank==kNewUserBank) selectedSlot_=0;
            if(writing_ && bank>=0) {
                const auto& slots=banks_[size_t(bank)].slots;
                auto it=std::find(slots.begin(),slots.end(),"");
                selectedSlot_=it==slots.end()?0:int(it-slots.begin());
            }
        }
        repaint();return;
    }
    {
        if(const int index=slotAt(x,y); index>=0) {
            // A patch may be dragged: its click acts on release (as a tap),
            // unless it turned into a drag. Empty slots act at once.
            const bool patch=selectedBank_>=0 && !banks_[size_t(selectedBank_)].slots[size_t(index)].empty();
            if(selectedBank_==kFavouritesBank || doubleClick || !patch) {slotClick(index,doubleClick);return;}
            press_={true,false,selectedBank_,index,banks_[size_t(selectedBank_)].source,{},x,y,
                    slotText(selectedBank_,index)};
            GetCursorPos(&press_.start);
            SetCapture(window_);
        }
    }
}
// A click on a slot of the shown bank: the write dialog picks it, the
// browser loads it (a double click also closes).
void WindowsEditor::slotClick(int index,bool doubleClick) {
    int bank=selectedBank_,slot=index;
    const bool valid=bank==kFavouritesBank && index>=0 && index<int(favourites_.size()) ?
        resolveFavourite(favourites_[size_t(index)],bank,slot):
        bank>=0 && bank<int(banks_.size()) && index<int(banks_[size_t(bank)].slots.size()) &&
        !banks_[size_t(bank)].slots[size_t(index)].empty();
    if(!writing_ && valid && hooks_.loadLibraryPatch) {
        if(hooks_.loadLibraryPatch(hooks_.ctx,bank,slot)) {
            selectedSlot_=index;
            if(doubleClick)closeBrowser();
        }
    } else if(writing_)selectedSlot_=index;
    repaint();
}

// Browser drag and drop (PatchBrowserView's SlotDrop / BankDrop), inside
// our own window: a patch dragged onto another slot of its bank MOVES there
// (an occupied one swaps; the files are renamed), onto a bank row COPIES
// into that bank's first free slot ("+" cursor; the browser stays on the
// bank shown). Never into the read-only factory bank (its patches can be
// copied out); no bank drop in the write dialog.
void WindowsEditor::dragMove(float x,float y) {
    if(!press_.active) return;
    if(!press_.dragging) {
        POINT now{};GetCursorPos(&now);
        if(std::abs(now.x-press_.start.x)<GetSystemMetrics(SM_CXDRAG) &&
           std::abs(now.y-press_.start.y)<GetSystemMetrics(SM_CYDRAG)) return;
        press_.dragging=true;
    }
    invalidate(dragLabelRect());
    press_.x=x;press_.y=y;
    int slot=-1,bank=-1,full=-1;
    const bool sameBank=press_.bank==selectedBank_ && press_.bank>=0 && press_.bank<int(banks_.size());
    if(const int s=slotAt(x,y); s>=0 && sameBank && !banks_[size_t(press_.bank)].readOnly) slot=s;
    else if(const int b=bankRowAt(x,y); b>=0 && !writing_ && !banks_[size_t(b)].readOnly) {
        // a full bank refuses the copy: forbidden cursor, said once on entering
        const auto& slots=banks_[size_t(b)].slots;
        if(std::any_of(slots.begin(),slots.end(),[](const std::string& s){return s.empty();})) bank=b;
        else full=b;
    }
    if(full>=0 && full!=dragFullBank_) flash(L"Bank is full (128 patches): cannot add a patch",3000);
    dragFullBank_=full;
    if(slot!=dropSlot_ || bank!=dropBank_) {
        dropSlot_=slot;dropBank_=bank;
        invalidate(kBrowserRect);
    }
    invalidate(dragLabelRect());
    SetCursor(dragCursor());
}
void WindowsEditor::dragEnd(bool drop) {
    if(!press_.active) return;
    const SlotPress press=press_;
    const int toSlot=dropSlot_, toBank=dropBank_;
    press_.active=false;dropSlot_=dropBank_=dragFullBank_=-1;
    if(GetCapture()==window_) ReleaseCapture();
    invalidate(kBrowserRect);
    if(!press.dragging) { // a plain click
        if(drop && press.bank==selectedBank_) slotClick(press.slot,false);
        return;
    }
    if(!drop) return;
    // the indices as of now (a scan may have landed during the drag)
    const int from=bankNow(press.source);
    if(from<0) return;
    if(toSlot>=0 && toSlot!=press.slot) { // SynthModel.movePatch
        if(!hooks_.movePatch || !hooks_.movePatch(hooks_.ctx,from,press.slot,toSlot)) return;
        updateFavouritesAfterMove(from,press.slot,toSlot);
        adoptLibrary();
        wchar_t moved[32];std::swprintf(moved,32,L"moved %03d -> %03d",press.slot+1,toSlot+1);
        flash(moved);
    } else if(toBank>=0 && toBank<int(banks_.size()) && hooks_.copyPatch) { // SynthModel.copyPatch
        const std::string target=banks_[size_t(toBank)].source;
        const int to=bankBySource(target);
        if(to<0) return;
        const std::wstring name=toWide(banks_[size_t(to)].name.c_str());
        const int number=displayBankNumber(to);
        const int s=hooks_.copyPatch(hooks_.ctx,from,press.slot,to);
        adoptLibrary();
        if(s>=0) {
            wchar_t where[48];std::swprintf(where,48,L"copied to %02d:",number);
            wchar_t at[8];std::swprintf(at,8,L" %03d",s+1);
            flash(where+name+at,3000);
        } else flash(s==-1 ? L"Bank is full (128 patches): cannot add a patch" : L"copy failed",s==-1?5000:1500);
    }
}
// The dragged patch's label follows the pointer (the mac's drag preview).
RectF WindowsEditor::dragLabelRect() const {
    return RectF(press_.x+10.f,press_.y+6.f,140.f,18.f);
}
HCURSOR WindowsEditor::dragCursor() {
    if(dropBank_>=0) {
        if(!copyCursor_) copyCursor_=makeCopyCursor();
        if(copyCursor_) return copyCursor_;
    }
    return LoadCursorW(nullptr,dropSlot_>=0 || dropBank_>=0 ? IDC_ARROW : IDC_NO);
}

// Hover uses the same design-space hit rectangles as clicks. It only
// invalidates the window when the hovered target changes; idle redraws remain off.
void WindowsEditor::updateHover(float x,float y) {
    int nextControl=-1;
    int nextChrome=0;
    // The Zoom button uses the former sl23 top-right position.
    // The browser covers only the central panel; bottom controls remain active.
    if(x>=482.f && x<572.f && y>=5.f && y<25.f) nextChrome=16;
    else if(browser_ && x>=6.f && x<764.f && y>=34.f && y<478.f) {
        if(y>=37.f && y<=61.f) {
            if(x>=690.f && x<760.f) nextChrome=14; // Close
            else if(!writing_ && x>=348.f && x<426.f) nextChrome=15; // Favourite
            else if(writing_) {
                if(x>=514.f && x<601.f) nextChrome=9; // Write patch
                else if(x>=604.f && x<680.f) nextChrome=10; // New bank
            } else {
                if(x>=430.f && x<498.f) nextChrome=11; // Import
                else if(x>=502.f && x<617.f) nextChrome=12; // Library folder
                else if(x>=620.f && x<681.f) nextChrome=13; // Refresh
            }
        }
    } else {
        if(midiLearn_ && x>=558.f && x<744.f && y>=488.f && y<512.f)
            nextChrome=8; // Exit MIDI Learn
        else if(y>=484.f && y<=554.f) {
            if(x>=492.f && x<542.f) nextChrome=y<518.f ? 5 : y>=522.f ? 6 : 0;
            else if(x>=420.f && x<448.f && y>=495.f && y<515.f) nextChrome=3;
            else if(x>=449.f && x<473.f && y>=495.f && y<515.f) nextChrome=4;
        }
        if(nextChrome==0 && !midiLearn_) { // learn mode: the hotspots take the clicks
            // Match mouseDown's reverse draw order so overlapping hit areas
            // always highlight the control that will receive the click.
            for(size_t i=controls_.size();i>0;--i) {
                const auto& c=controls_[i-1];
                // Highlight clickable buttons only, not waves, LEDs or LCD displays.
                if(c.id<0 || (c.kind!=Kind::Toggle && c.kind!=Kind::Cycle && c.kind!=Kind::Badge)) continue;
                if(x>=c.x && x<c.x+c.w && y>=c.y && y<c.y+c.h) {
                    nextControl=int(i-1);break;
                }
            }
        }
    }
    hoverChanged(nextControl,nextChrome);
}

void WindowsEditor::mouseDown(float x,float y,bool doubleClick) {
    if(x>=482.f && x<572.f && y>=5.f && y<25.f) {
        static constexpr int levels[]={75,100,125,150,175,200,250,300};
        const int current=int(std::lround(100.f*float(rect.getWidth())/kDesignWidth));
        HMENU menu=CreatePopupMenu();
        if(menu) {
            for(int i=0;i<8;i++) {
                const std::wstring caption=std::to_wstring(levels[i])+L"%";
                AppendMenuW(menu,MF_STRING|(current==levels[i]?MF_CHECKED:0),
                            UINT_PTR(3000+i),caption.c_str());
            }
            POINT pt{};GetCursorPos(&pt);
            const int chosen=int(TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY,
                                               pt.x,pt.y,0,window_,nullptr));
            DestroyMenu(menu);
            if(chosen>=3000 && chosen<3008)
                requestWidth(int(std::lround(kDesignWidth*levels[chosen-3000]/100.f)));
        }
        return;
    }
    if(x>=kDesignWidth-22.f && y>=kDesignHeight-22.f && !doubleClick) {
        if(!plugFrame)return;
        GetCursorPos(&resizeStartScreen_);
        resizeStartWidth_=rect.getWidth();
        resizeDragging_=true;
        SetCapture(window_);
        return;
    }
    // The red version box opens the local About dialog, as in the sl23 port.
    if(isVersionBox(x,y)) {showAbout();return;}
    if(layoutEditing_ && y>=34.f && y<478.f && x>=6.f && x<764.f) {
        beginLayoutDrag(x,y);return;
    }
    // Route to the browser only inside the browser's actual visible bounds.
    // Header controls and the bottom strip keep their normal mouse actions.
    if(browser_ && x>=6.f && x<764.f && y>=34.f && y<478.f) {
        browserClick(x,y,doubleClick);
        return;
    }
    if(midiLearn_ && x>=558.f && x<744.f && y>=488.f && y<512.f) {setLearn(false);return;}
    if(y>=484 && y<=554) {
        if(x>=492 && x<542) {
            // write again while the write dialog is open = its Write (save
            // in place: click, click)
            if(y<518) {
                if(layoutEditing_) {flash(L"Exit Layout Edit before writing a preset");return;}
                if(browser_ && writing_)commitWrite();else openBrowser(true);
                return;
            }
            POINT p;GetCursorPos(&p);showMainMenu(p.x,p.y);return;
        }
        if(x>=420 && x<473 && y<520) {stepPatch(x<448?-1:1);return;}
        if(x>=50 && x<482) {
            if(layoutEditing_) {flash(L"Exit Layout Edit to browse banks");return;}
            browser_?closeBrowser():openBrowser(false);return;
        }
    }
    if(y<32 && x<220) {
        POINT p;GetCursorPos(&p);showThemeMenu(p.x,p.y);return;
    }
    // Learn mode: a hotspot takes the click (the controls under it stop
    // taking input); the others (volume, ctrl src / assign menus) still work.
    if(const int id=midiLearn_ ? learnHit(x,y) : -1; id>=0) {
        if(doubleClick) {
            // The first click of the pair armed it: the mac's double tap
            // leaves the selection as it was.
            if(learnCC_[size_t(id)] && hooks_.setMidiCC) {
                hooks_.setMidiCC(hooks_.ctx,id,0);
                flash(L"MIDI LEARN: "+toWide(hooks_.paramName ? hooks_.paramName(hooks_.ctx,id) : "")+L" cleared");
            }
            learnCC_[size_t(id)]=0;
            learnSelect(learnPrevTarget_);
            invalidate(learnRects_[size_t(id)]);
        } else {
            learnPrevTarget_=learnTarget_;
            learnSelect(id);
        }
        return;
    }
    for(auto it=controls_.rbegin();it!=controls_.rend();++it) {
        const auto& c=*it;
        if(x<c.x || x>=c.x+c.w || y<c.y || y>=c.y+c.h)continue;
        if(c.id<0)continue;
        // Double-click resets knobs, displays and *individual radio LEDs*
        // to the loaded patch value. Toggle/cycle/wave buttons must keep
        // responding to rapid successive clicks instead of unexpectedly
        // restoring the preset value on the second click.
        const bool resetByDoubleClick=(c.kind==Kind::Knob ||
                                       c.kind==Kind::Display ||
                                       c.kind==Kind::Radio);
        if(doubleClick && resetByDoubleClick && c.id<s2::kParamCount) {
            setValueOnce(c.id,controller_->LoadedPatchValue(c.id));
            return;
        }
        // Master Volume is not part of a .sy1 preset; retain its original
        // default-reset behaviour rather than claiming it has patch data.
        if(doubleClick && resetByDoubleClick && c.id==s2::kVolumeParamId) {
            int a=0,b=127,d=0;
            if(hooks_.paramInfo) hooks_.paramInfo(hooks_.ctx,c.id,&a,&b,&d);
            setValueOnce(c.id,d);return;
        }
        switch(c.kind) {
        case Kind::Knob:
        case Kind::Display:
            if(c.kind==Kind::Display && (c.id==78 || c.id==86 || c.id==87 || c.id==88 || c.id==89)) {
                POINT screen;GetCursorPos(&screen);
                showParameterMenu(c.id,screen.x,screen.y);return;
            }
            dragId_=c.id;dragStartY_=y;dragStartValue_=value(c.id);
            if(hooks_.beginEdit)hooks_.beginEdit(hooks_.ctx,c.id);
            SetCapture(window_);return;
        case Kind::Toggle:
        case Kind::Badge: setValueOnce(c.id,value(c.id)?0:1);return;
        case Kind::Cycle: cycle(c.id,1);return;
        case Kind::Wave:
            if(c.id==42 || c.id==47) cycle(c.id,1);
            else setValueOnce(c.id,c.value);
            return;
        case Kind::Radio: setValueOnce(c.id,c.value);return;
        default:return;
        }
    }
}
void WindowsEditor::mouseMove(float x, float y) {
    if(layoutEditing_) {
        if(layoutDragging_)moveLayoutDrag(x,y);
        return;
    }
    updateHover(x,y);
    if(press_.active) {dragMove(x,y);return;}
    if(resizeDragging_) {
        POINT current{};GetCursorPos(&current);
        requestWidth(resizeStartWidth_+(current.x-resizeStartScreen_.x));
        return;
    }
    if(dragId_<0)return;
    int low=0,high=127,def=0;
    if(hooks_.paramInfo) hooks_.paramInfo(hooks_.ctx,dragId_,&low,&high,&def);
    const int span=std::max(1,high-low);
    const bool fine=(GetKeyState(VK_SHIFT)&0x8000)!=0;
    const float pixels=(fine?900.f:180.f);
    int newValue=std::clamp(dragStartValue_+int(std::lround((dragStartY_-y)*span/pixels)),low,high);
    if(newValue==value(dragId_)) return;
    if(hooks_.setValue)hooks_.setValue(hooks_.ctx,dragId_,newValue);
    valueChanged(dragId_);
    show(dragId_);
}
void WindowsEditor::mouseUp() {
    if(layoutDragging_) {finishLayoutDrag(true);return;}
    if(press_.active) {dragEnd(true);return;}
    if(resizeDragging_) {
        resizeDragging_=false;
        requestedZoomWidth_=-1;
        // Save just once on mouse-up rather than writing on every drag frame.
        winzoom::SaveWidth(storage::Root(),rect.getWidth(),kMinWidth,kMaxWidth);
        if(GetCapture()==window_)ReleaseCapture();
        return;
    }
    if(dragId_<0)return;
    if(hooks_.endEdit)hooks_.endEdit(hooks_.ctx,dragId_);
    dragId_=-1;
    if(GetCapture()==window_)ReleaseCapture();
}
void WindowsEditor::wheel(int direction,float x,float y) {
    if(layoutEditing_) return; // never change parameter values during layout editing
    // Scrolling over the master volume must change volume, not the browser.
    if(browser_ && x>=6.f && x<764.f && y>=34.f && y<478.f) {
        if(x<196) {
            // Wheel input is expressed as rows, not merely the sign of one
            // notch. A standard wheel step now moves eight bank entries.
            const int next=std::clamp(bankScroll_-direction,0,std::max(0,bankRowCount()-20));
            if(next!=bankScroll_) {bankScroll_=next;invalidate(kBrowserRect);}
        } else if(selectedBank_==kFavouritesBank || selectedBank_==kNewUserBank ||
                  (selectedBank_>=0 && selectedBank_<int(banks_.size()))) {
            const int count=selectedBank_==kFavouritesBank?int(favourites_.size()):
                selectedBank_==kNewUserBank?s2::kBankSlots:int(banks_[size_t(selectedBank_)].slots.size());
            const int rows=(count+3)/4;
            const int amount=(direction>0?1:-1)*std::max(1,(std::abs(direction)+1)/2);
            const int next=std::clamp(slotScroll_-amount,0,std::max(0,rows-20));
            if(next!=slotScroll_) {slotScroll_=next;invalidate(kBrowserRect);}
        }
        return;
    }
    if(direction==0 || midiLearn_) return;
    for(auto it=controls_.rbegin();it!=controls_.rend();++it) {
        const auto& c=*it;
        if(c.id<0 || x<c.x || x>c.x+c.w || y<c.y || y>c.y+c.h) continue;

        // Buttons are click-only. Do not change toggles, numbered buttons or
        // cycling buttons when the user scrolls over them.
        if(c.kind==Kind::Toggle || c.kind==Kind::Cycle ||
           c.kind==Kind::Badge || c.kind==Kind::Label) return;

        const int id=c.id;
        const int current=value(id);
        const int listStep=direction>0?-1:1; // Down = next item as displayed in menus.
        const int numericStep=direction>0?1:-1; // Up = increase on continuous values.

        if(c.kind==Kind::Radio || c.kind==Kind::Wave) {
            // LED groups and waveform selectors intentionally wrap around.
            cycle(id,listStep);
            return;
        }

        if(c.kind==Kind::Knob) {
            // Knobs stop at their end points rather than wrapping to the other end.
            int low=0,high=127,def=0;
            if(hooks_.paramInfo) hooks_.paramInfo(hooks_.ctx,id,&low,&high,&def);
            const int next=std::clamp(current+numericStep,low,high);
            if(next!=current) setValueOnce(id,next);
            return;
        }

        if(c.kind==Kind::Display) {
            if(id==86 || id==88) {
                // MIDI source display order is: None, Pitch Bend, Channel
                // Pressure, CC0 ... CC127. Packed MIDI values are not consecutive.
                int index=-1;
                if(current==0 || current==-1) index=0;
                else if(current==0xE000) index=1;
                else if(current==0xD000) index=2;
                else if((current&0xFF80)==0xB000) index=3+(current&0x7F);
                if(index<0) return; // Preserve an unknown legacy source value.
                const int next=std::clamp(index+listStep,0,130);
                if(next==index) return;
                const int v=next==0?0:next==1?0xE000:next==2?0xD000:0xB000|(next-3);
                setValueOnce(id,v);
                return;
            }
            if(id==87 || id==89) {
                // Keep this order identical to the popup in showParameterMenu.
                static constexpr int assignments[]={
                    -1,76,45,95,2,12,13,11,5,8,91,15,16,17,18,19,20,21,23,22,
                    25,26,27,28,29,79,80,81,61,62,63,60,90,35,36,37,83,98,
                    52,53,54,55,56,39,75,92,84,85,34,43,44,48,49
                };
                constexpr int count=int(sizeof(assignments)/sizeof(assignments[0]));
                int index=-1;
                for(int k=0;k<count;k++) {
                    if(assignments[k]==current) {index=k;break;}
                }
                if(index<0) return; // Preserve an unlisted existing assignment.
                const int next=std::clamp(index+listStep,0,count-1);
                if(next!=index) setValueOnce(id,assignments[next]);
                return;
            }
            if(id==64 || id==78 || id==82) {
                // Chorus, Effect and Delay type displays advance down their lists.
                cycle(id,listStep);
                return;
            }
            // Numeric displays (pitch, key shift, polyphony, etc.) do not wrap.
            int low=0,high=127,def=0;
            if(hooks_.paramInfo) hooks_.paramInfo(hooks_.ctx,id,&low,&high,&def);
            const int next=std::clamp(current+numericStep,low,high);
            if(next!=current) setValueOnce(id,next);
            return;
        }
        return;
    }
}

void WindowsEditor::showImportDialog(int screenX,int screenY) {
    HMENU menu=CreatePopupMenu();
    AppendMenuW(menu,MF_STRING,1,L"Import .sy1 or ZIP bank...");
    AppendMenuW(menu,MF_STRING,2,L"Import folder...");
    const int selected=int(TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY,screenX,screenY,0,window_,nullptr));
    DestroyMenu(menu);
    if(!selected || !hooks_.importItems)return;
    std::wstring path;
    if(selected==1) {
        wchar_t file[4096]={};
        OPENFILENAMEW dialog{};
        dialog.lStructSize=sizeof(dialog);
        dialog.hwndOwner=window_;
        dialog.lpstrFile=file;
        dialog.nMaxFile=4096;
        dialog.lpstrFilter=L"Synth1 presets / ZIP banks (*.sy1;*.zip)\0*.sy1;*.zip\0All files (*.*)\0*.*\0";
        dialog.Flags=OFN_EXPLORER|OFN_PATHMUSTEXIST|OFN_FILEMUSTEXIST;
        if(!GetOpenFileNameW(&dialog))return;
        path=file;
    } else if(!pickFolder(L"Choose a folder containing Synth1 .sy1 presets",path)) return;
    const std::string bytes=toUtf8(path);
    const char* imported=hooks_.importItems(hooks_.ctx,bytes.c_str());
    // newline-separated added items (SynthModel.importItems)
    const std::string added=imported ? imported : "";
    if(added.empty())
        MessageBoxW(window_,L"No new preset bank was imported. The files may already exist or may not contain valid .sy1 presets.",
                    L"Synth2",MB_ICONINFORMATION);
    refreshBanks();
    if(!added.empty())
        flash(L"imported "+std::to_wstring(std::count(added.begin(),added.end(),'\n')+(added.back()=='\n'?0:1))+L" item(s)");
}

void WindowsEditor::showFileDialog(bool save) {
    wchar_t filename[4096]={};
    OPENFILENAMEW dialog{};
    dialog.lStructSize=sizeof(dialog);
    dialog.hwndOwner=window_;
    dialog.lpstrFile=filename;
    dialog.nMaxFile=4096;
    dialog.lpstrFilter=L"Synth1 presets (*.sy1)\0*.sy1\0All files (*.*)\0*.*\0";
    dialog.lpstrDefExt=L"sy1";
    dialog.Flags=OFN_EXPLORER|OFN_PATHMUSTEXIST|(save?OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST);
    BOOL chosen=save?GetSaveFileNameW(&dialog):GetOpenFileNameW(&dialog);
    if(!chosen)return;
    const std::string path=toUtf8(filename);
    if(save) {
        if(hooks_.savePatchFile && !hooks_.savePatchFile(hooks_.ctx,path.c_str()))
            MessageBoxW(window_,L"Unable to save this patch.",L"Synth2",MB_ICONWARNING);
    } else {
        if(hooks_.loadPatchFile && hooks_.loadPatchFile(hooks_.ctx,path.c_str())) {
            knownSerial_=-1;
            refreshBanks();
        } else MessageBoxW(window_,L"Unable to load this .sy1 patch.",L"Synth2",MB_ICONWARNING);
    }
}

void WindowsEditor::showThemeEditor() {
    if(!themeEditor_)themeEditor_=std::make_unique<ThemeEditorWindow>();
    themeEditor_->Show(window_,{[this]{return theme_;},
                                [this]{return activeThemeFile_;},
                                [this](const themes::Palette& p,const std::string& filename){setTheme(p,false,filename);},
                                [this](const themes::Palette& p){setTheme(p,true);},
                                [this]{return voiceGrid_;},
                                [this](const winvoice::Palette& p){setVoiceGrid(p);},
                                [this]{return presetTextColours_;},
                                [this](bool enabled){setPresetTextColours(enabled);},
                                []{return sharedThemes().userThemes();},
                                []{return storage::Root()/"Themes";}});
}
void WindowsEditor::showThemeMenu(int x,int y) {
    HMENU m=CreatePopupMenu();
    static constexpr int levels[]={75,100,125,150,200,250,300};
    const int current=int(std::lround(100.f*float(rect.getWidth())/kDesignWidth));
    for(int i=0;i<7;i++)
        AppendMenuW(m,MF_STRING|(current==levels[i] ? MF_CHECKED : 0),UINT_PTR(120+i),
                    (std::to_wstring(levels[i])+L"% size").c_str());
    AppendMenuW(m,MF_SEPARATOR,0,nullptr);
    std::vector<themes::Named> picks;
    appendThemes(m,picks);
    int choice=int(TrackPopupMenu(m,TPM_RETURNCMD|TPM_NONOTIFY,x,y,0,window_,nullptr));
    DestroyMenu(m);
    if(pickTheme(choice,picks)) return;
    if(choice>=120&&choice<=126) requestWidth(int(std::lround(kDesignWidth*levels[choice-120]/100.f)));
}
void WindowsEditor::requestWidth(int width) {
    if(!plugFrame)return;
    width=std::clamp(width,kMinWidth,kMaxWidth);
    ViewRect r(0,0,width,0);
    checkSizeConstraint(&r);
    requestedZoomWidth_=r.getWidth();
    if(plugFrame->resizeView(this,&r)!=kResultOk)
        requestedZoomWidth_=-1;
}
void WindowsEditor::showMainMenu(int x,int y) {
    HMENU menu=CreatePopupMenu();
    AppendMenuW(menu,MF_STRING,10,L"Initialize patch");
    AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
    AppendMenuW(menu,MF_STRING,11,L"Load patch file...");
    AppendMenuW(menu,MF_STRING,12,L"Save patch file...");
    AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
    AppendMenuW(menu,MF_STRING|(layoutEditing_?MF_GRAYED:0),13,midiLearn_?L"Exit MIDI Learn":L"MIDI Learn");
    const bool programChange=hooks_.programChange && hooks_.programChange(hooks_.ctx)!=0;
    AppendMenuW(menu,MF_STRING|(programChange?MF_CHECKED:0),17,L"Program change");
    AppendMenuW(menu,MF_STRING,14,L"All notes off");
    HMENU themeMenu=CreatePopupMenu();
    std::vector<themes::Named> picks;
    appendThemes(themeMenu,picks);
    AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
    AppendMenuW(menu,MF_POPUP,UINT_PTR(themeMenu),L"Select theme");
    AppendMenuW(menu,MF_STRING,15,L"Themes...");
    AppendMenuW(menu,MF_STRING|(layoutEditing_?MF_CHECKED:0),18,
                layoutEditing_?L"Exit layout edit":L"Edit layout...");
    if(layoutEditing_) {
        AppendMenuW(menu,MF_STRING,19,L"Save layout");
        AppendMenuW(menu,MF_STRING,20,L"Reset to factory layout");
        AppendMenuW(menu,MF_STRING|(layoutUndo_.empty()?MF_GRAYED:0),21,L"Undo layout");
        AppendMenuW(menu,MF_STRING|(layoutRedo_.empty()?MF_GRAYED:0),22,L"Redo layout");
        AppendMenuW(menu,MF_STRING|(layoutSnap_?MF_CHECKED:0),23,L"Snap moves to 4 px");
    }
    // UpdateCheck.enabled: no product slug (forks) = no entry
    if(*OXO_UPDATE_PRODUCT) {
        AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
        AppendMenuW(menu,MF_STRING|(updateCheck_.Running()?MF_GRAYED:0),16,L"Check for updates...");
    }
    int selected=int(TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY,x,y,0,window_,nullptr));
    DestroyMenu(menu);
    switch(selected) {
        case 10:
            if(hooks_.initializePatch)hooks_.initializePatch(hooks_.ctx);
            break;
        case 11: showFileDialog(false);break;
        case 12: showFileDialog(true);break;
        case 13: if(!layoutEditing_)setLearn(!midiLearn_);break;
        case 18: toggleLayoutEditor();break;
        case 19: {
            const auto result=saveLayout();
            if(result==LayoutSaveResult::Failed)
                MessageBoxW(window_,L"Could not save layout.json. Check folder permissions and any existing file.",
                            L"Synth2 Layout",MB_OK|MB_ICONERROR);
            else if(result==LayoutSaveResult::Saved)flash(L"Layout saved",1500);
            break;
        }
        case 20:
            if(MessageBoxW(window_,L"Reset all section, control and separator positions to the built-in layout?\n"
                            L"The saved layout file will not be changed until Save layout.",
                            L"Synth2 Layout",MB_YESNO|MB_ICONQUESTION)==IDYES)resetLayout();
            break;
        case 21: undoLayout();break;
        case 22: redoLayout();break;
        case 23: layoutSnap_=!layoutSnap_;break;
        case 14: if(hooks_.allNotesOff) {hooks_.allNotesOff(hooks_.ctx);flash(L"all notes off");} break;
        case 17: // SynthModel.programChange
            if(hooks_.setProgramChange) {
                hooks_.setProgramChange(hooks_.ctx,programChange?0:1);
                flash(programChange?L"program change: off":L"program change: on");
            }
            break;
        case 15: showThemeEditor();break;
        case 16: checkForUpdates();break;
        default: pickTheme(selected,picks);break;
    }
    repaint();
}

// SynthModel.checkForUpdates on Windows: the LCD says "Checking…" while
// releases.json comes in the background, then a message box answers (the
// mac's UpdateDialog texts, EditorCore/UpdateCheck.hpp). Closing the
// editor cancels it.
void WindowsEditor::checkForUpdates() {
    const std::string site=OXO_VENDOR_URL;
    if(!window_ || !updateCheck_.Start(toWide((site+"/releases.json").c_str()),window_,kUpdateMessage)) return;
    flash(toWide(update::Message({update::Kind::Checking,{},{},{}},update::SiteName(site),"").c_str()),30000);
}
void WindowsEditor::updateArrived() {
    const std::string site=OXO_VENDOR_URL;
    const std::optional<std::string> body=updateCheck_.Take();
    const update::State state=body ? update::Evaluate(*body,OXO_UPDATE_PRODUCT,"win",OXO_VERSION,
                                                       std::atoi(OXO_BUILD_NUMBER),site)
                                   : update::State{};
    flash({});
    std::wstring text=toWide(update::Message(state,update::SiteName(site),update::Email(OXO_VENDOR_EMAIL)).c_str());
    if(state.kind==update::Kind::Available) {
        text+=L"\n\nOpen the download page?";
        if(MessageBoxW(window_,text.c_str(),L"Check for updates",MB_YESNO|MB_ICONINFORMATION)==IDYES)
            ShellExecuteW(window_,L"open",toWide(state.page.c_str()).c_str(),nullptr,nullptr,SW_SHOWNORMAL);
    } else {
        MessageBoxW(window_,text.c_str(),L"Check for updates",
                    MB_OK|(state.kind==update::Kind::Latest?MB_ICONINFORMATION:MB_ICONWARNING));
    }
}
void WindowsEditor::showParameterMenu(int id,int x,int y) {
    HMENU menu=CreatePopupMenu();
    std::vector<int> values;
    // Use a compact multi-column box of choices, rather than a single
    // long menu that runs off screen. Native popup menus preserve reliable
    // keyboard and mouse operation inside VST3 hosts.
    auto add=[&](const std::wstring& name,int value) {
        const int index=int(values.size());
        const int command=2000+index;
        values.push_back(value);
        const bool source=(id==86 || id==88);
        // The source grid contains the three special sources followed by
        // eight columns of MIDI CC choices (16 per column).
        const bool newColumn=source ? (index>=19 && (index-3)%16==0)
                                    : (id==87 || id==89) && index>0 && index%12==0;
        const UINT flags=MF_STRING | (newColumn ? MF_MENUBREAK : 0) |
                         ((this->value(id)==value) ? MF_CHECKED : 0);
        AppendMenuW(menu,flags,UINT_PTR(command),name.c_str());
    };
    if(id==78) {
        int low=0,high=9,def=0;
        if(hooks_.paramInfo)hooks_.paramInfo(hooks_.ctx,id,&low,&high,&def);
        for(int v=low;v<=high;v++) {
            add(toWide(hooks_.formatValue ? hooks_.formatValue(hooks_.ctx,id,v):""),v);
        }
    } else if(id==86 || id==88) {
        add(L"None",0);add(L"Pitch bend",0xE000);add(L"Channel pressure",0xD000);
        for(int cc=0;cc<128;cc++) add(L"CC "+std::to_wstring(cc),0xB000|cc);
    } else if(id==87 || id==89) {
        add(L"None",-1);
        static const int assigns[]={76,45,95,2,12,13,11,5,8,91,15,16,17,18,19,20,21,23,22,25,26,27,28,29,
            79,80,81,61,62,63,60,90,35,36,37,83,98,52,53,54,55,56,39,75,92,84,85,34,43,44,48,49};
        for(int item:assigns) add(toWide(hooks_.paramName?hooks_.paramName(hooks_.ctx,item):""),item);
    }
    int selected=int(TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY,x,y,0,window_,nullptr));
    DestroyMenu(menu);
    if(selected>=2000&&selected<2000+int(values.size()))setValueOnce(id,values[size_t(selected-2000)]);
}
void WindowsEditor::tick() {
    sharedThemes().flush(false);
    if(flashUntil_ && GetTickCount64()>=flashUntil_) flash({});
    bool dirty=false;
    if(hooks_.patchSerial) {
        const int serial=hooks_.patchSerial(hooks_.ctx);
        if(serial!=knownSerial_) {
            knownSerial_=serial;syncTheme();dirty=true;
        }
    }
    // Poll even with the browser closed: the first full scan must finish
    // asynchronously, without any synchronous EnsureScanned() on startup.
    if(hooks_.libraryRevision) {
        const int revision=hooks_.libraryRevision(hooks_.ctx);
        if(revision!=knownRevision_) {
            knownRevision_=revision;refreshBanks();dirty=true;
        }
    }
    // SynthModel.incomingCC: every new CC is consumed (none learned while
    // nothing is armed), CC 1..127 only; the control stays armed.
    if(midiLearn_ && hooks_.lastCC) {
        const int token=hooks_.lastCC(hooks_.ctx);
        if(token!=lastCcToken_ && token>0) {
            lastCcToken_=token;
            const int cc=token&127;
            if(learnTarget_>=0 && learnTarget_<kLearnCount && cc>=1 && learnCC_[size_t(learnTarget_)]!=cc && hooks_.setMidiCC) {
                hooks_.setMidiCC(hooks_.ctx,learnTarget_,cc);
                learnCC_[size_t(learnTarget_)]=cc;
                invalidate(learnRects_[size_t(learnTarget_)]);
                flash(L"MIDI LEARN: "+toWide(hooks_.paramName ? hooks_.paramName(hooks_.ctx,learnTarget_) : "")+
                      L" = CC "+std::to_wstring(cc));
            }
        }
    }
    if(dirty) repaint();
    // Host automation / MIDI / patch edits: redraw what shows a changed
    // value (SynthModel polls the same way).
    for(size_t id=0;id<knownValues_.size();++id)
        if(value(int(id))!=knownValues_[id]) valueChanged(int(id));
    if(!midiLearn_ && hooks_.voiceStates) {
        const uint64_t states=hooks_.voiceStates(hooks_.ctx);
        if(states!=knownVoiceStates_) {knownVoiceStates_=states;invalidate(kGridRect);}
    }
}

bool WindowsEditor::ensureBuffers(HDC screen,int width,int height) {
    if(static_.dc && back_.dc && width==bufferWidth_ && height==bufferHeight_) return true;
    releaseBuffers();
    for(Buffer* b:{&static_,&back_}) {
        b->dc=CreateCompatibleDC(screen);
        b->bitmap=b->dc ? CreateCompatibleBitmap(screen,width,height) : nullptr;
        b->previous=b->bitmap ? SelectObject(b->dc,b->bitmap) : nullptr;
        if(!b->previous || b->previous==HGDI_ERROR) {releaseBuffers();return false;}
    }
    bufferWidth_=width;bufferHeight_=height;
    staticDirty_=true;
    return true;
}
void WindowsEditor::releaseBuffers() {
    for(Buffer* b:{&static_,&back_}) {
        if(b->dc && b->previous && b->previous!=HGDI_ERROR) SelectObject(b->dc,b->previous);
        if(b->bitmap) DeleteObject(b->bitmap);
        if(b->dc) DeleteDC(b->dc);
        *b={};
    }
    bufferWidth_=bufferHeight_=0;
}
void WindowsEditor::paint(HDC screen,const RECT& dirty) {
    RECT client{};GetClientRect(window_,&client);
    const int width=client.right-client.left, height=client.bottom-client.top;
    if(width<=0 || height<=0) return;
    const float sx=float(width)/kDesignWidth, sy=float(height)/kDesignHeight;
    auto prepare=[&](Graphics& g) {
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintAntiAlias);
        g.SetPixelOffsetMode(PixelOffsetModeHighQuality);
        g.ScaleTransform(sx,sy);
    };
    const RectF full(0,0,kDesignWidth,kDesignHeight);
    if(!ensureBuffers(screen,width,height)) { // no bitmaps: everything, straight to the screen
        Graphics g(screen);
        prepare(g);
        paintClip_=full;
        drawStatic(g);
        drawDynamic(g);
        return;
    }
    if(staticDirty_) {
        Graphics g(static_.dc);
        prepare(g);
        paintClip_=full;
        drawStatic(g);
        staticDirty_=false;
    }
    const int x=dirty.left, y=dirty.top, w=dirty.right-dirty.left, h=dirty.bottom-dirty.top;
    BitBlt(back_.dc,x,y,w,h,static_.dc,x,y,SRCCOPY);
    {
        Graphics g(back_.dc);
        g.SetClip(Rect(x,y,w,h)); // device pixels: before the scale
        prepare(g);
        paintClip_=RectF(float(x)/sx-1.f,float(y)/sy-1.f,float(w)/sx+2.f,float(h)/sy+2.f);
        drawDynamic(g);
    }
    paintClip_=full;
    BitBlt(screen,x,y,w,h,back_.dc,x,y,SRCCOPY);
}

LRESULT CALLBACK WindowsEditor::wndProc(HWND window,UINT msg,WPARAM wp,LPARAM lp) {
    WindowsEditor* editor=reinterpret_cast<WindowsEditor*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(msg==WM_NCCREATE) {
        auto* create=reinterpret_cast<CREATESTRUCTW*>(lp);
        editor=static_cast<WindowsEditor*>(create->lpCreateParams);
        if(!editor) return FALSE;
        editor->window_=window; // WM_CREATE arrives before CreateWindowExW returns.
        SetWindowLongPtrW(window,GWLP_USERDATA,LONG_PTR(editor));
        return TRUE;
    }
    return editor?editor->windowMessage(msg,wp,lp):DefWindowProcW(window,msg,wp,lp);
}
LRESULT WindowsEditor::windowMessage(UINT msg,WPARAM wp,LPARAM lp) {
    const float sx=float(std::max(1,rect.getWidth()))/kDesignWidth;
    const float sy=float(std::max(1,rect.getHeight()))/kDesignHeight;
    switch(msg) {
    case WM_ERASEBKGND:return 1;
    // Everything scales with the window: a size change redraws all of it
    // (Windows itself would only invalidate the newly exposed strip).
    case WM_SIZE:InvalidateRect(window_,nullptr,FALSE);return 0;
    case WM_SETCURSOR: {
        if(press_.active && press_.dragging) {SetCursor(dragCursor());return TRUE;}
        if(LOWORD(lp)==HTCLIENT) {
            POINT pos{};GetCursorPos(&pos);ScreenToClient(window_,&pos);
            const float dx=float(pos.x)/sx, dy=float(pos.y)/sy;
            if(layoutEditing_ && dx>=6.f && dx<764.f && dy>=34.f && dy<478.f) {
                LayoutPart kind;int index;
                if(layoutHit(dx,dy,kind,index)) {
                    SetCursor(LoadCursorW(nullptr,
                        (kind==LayoutPart::ResizeSection || kind==LayoutPart::ResizePortamento) ? IDC_SIZENWSE :
                        kind==LayoutPart::Control ? IDC_CROSS : IDC_SIZEALL));
                    return TRUE;
                }
            }
            if(hoveredChrome_!=0 || hoveredControl_>=0 || isVersionBox(dx,dy) ||
               (dx>=8.f && dx<230.f && dy>=2.f && dy<31.f) ||
               (midiLearn_ && !browser_ && learnHit(dx,dy)>=0)) {
                SetCursor(LoadCursorW(nullptr,IDC_HAND));return TRUE;
            }
            RECT client{};GetClientRect(window_,&client);
            const int gripW=std::max(8,int(std::lround(22.f*sx)));
            const int gripH=std::max(8,int(std::lround(22.f*sy)));
            if(pos.x>=client.right-gripW && pos.y>=client.bottom-gripH) {
                SetCursor(LoadCursorW(nullptr,IDC_SIZENWSE));return TRUE;
            }
        }
        return DefWindowProcW(window_,msg,wp,lp);
    }
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        if(HDC screen=BeginPaint(window_,&ps)) paint(screen,ps.rcPaint);
        EndPaint(window_,&ps);
        return 0;
    }
    case WM_TIMER:if(wp==1)tick();return 0;
    case kUpdateMessage:updateArrived();return 0;
    case WM_LBUTTONDOWN:
        SetFocus(window_);
        mouseDown(float(GET_X_LPARAM(lp))/sx,float(GET_Y_LPARAM(lp))/sy,false);return 0;
    case WM_LBUTTONDBLCLK:
        SetFocus(window_);
        mouseDown(float(GET_X_LPARAM(lp))/sx,float(GET_Y_LPARAM(lp))/sy,true);return 0;
    case WM_MOUSEMOVE:
        if(!trackingMouseLeave_) {
            TRACKMOUSEEVENT tm{};
            tm.cbSize=sizeof(tm);
            tm.dwFlags=TME_LEAVE;
            tm.hwndTrack=window_;
            trackingMouseLeave_=TrackMouseEvent(&tm)!=FALSE;
        }
        mouseMove(float(GET_X_LPARAM(lp))/sx,float(GET_Y_LPARAM(lp))/sy);return 0;
    case WM_MOUSELEAVE:
        trackingMouseLeave_=false;
        hoverChanged(-1,0);
        return 0;
    case WM_LBUTTONUP:mouseUp();return 0;
    case WM_CAPTURECHANGED: // lost the capture (another window, a menu): a drag is cancelled
        if(layoutDragging_) finishLayoutDrag(false);
        else if(press_.active) dragEnd(false); else mouseUp();
        return 0;
    case WM_MOUSEWHEEL: {
        POINT screen{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
        ScreenToClient(window_,&screen);
        // Preserve the wheel magnitude for fast browser navigation.
        // GUI knobs still change by just one step inside wheel().
        const int delta=GET_WHEEL_DELTA_WPARAM(wp);
        if(delta!=0) {
            const int rows=std::max(1,(std::abs(delta)*8+WHEEL_DELTA/2)/WHEEL_DELTA);
            wheel(delta>0?rows:-rows,float(screen.x)/sx,float(screen.y)/sy);
        }
        return 0;
    }
    case WM_GETDLGCODE: return DLGC_WANTALLKEYS|DLGC_WANTARROWS; // hosts with dialog-style windows
    case WM_KEYDOWN:
        if(keyDown(UINT(wp))) return 0;
        return DefWindowProcW(window_,msg,wp,lp);
    case kFieldKeyMessage:keyDown(UINT(wp));return 0;
    case WM_CONTEXTMENU: {
        POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
        if(p.x!=-1 || p.y!=-1) { // -1, -1 = from the keyboard
            POINT client=p;ScreenToClient(window_,&client);
            if(!layoutEditing_ && showLibraryMenu(float(client.x)/sx,float(client.y)/sy,p.x,p.y)) return 0;
        }
        showMainMenu(p.x,p.y);return 0;
    }
    default:return DefWindowProcW(window_,msg,wp,lp);
    }
}
} // namespace oxo
