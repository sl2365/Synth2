#pragma once
// Windows-only, modeless theme editor: the Mac ThemeDialog
// (Shared/SynthTheme.swift) as a native window. Load (factory, then user
// themes) and every colour edit apply LIVE to the editor that opened it.
// Save writes a standalone Themes/<name>.json, Delete archives such a file.
// No combined theme store or legacy themes.json support.
#include "EditorCore/Themes.hpp"
#include "WindowsVoiceGrid.hpp"
#include "WindowsThemeFiles.hpp"
#include "WindowsModule.hpp"
#include <windows.h>
#include <commdlg.h>
#include <array>
#include <algorithm>
#include <functional>
#include <string>
#include <vector>

namespace oxo {
class ThemeEditorWindow {
public:
    struct Host {
        std::function<themes::Palette()> theme;          // the instance's palette
        std::function<std::string()> activeFile;         // selected file stem, or empty
        std::function<void(const themes::Palette&,const std::string&)> apply; // select theme
        std::function<void(const themes::Palette&)> edit;  // edit one core colour
        std::function<winvoice::Palette()> voice;          // Windows-only voice grid and selection colours
        std::function<void(const winvoice::Palette&)> setVoice;
        std::function<bool()> presetTextColours;
        std::function<void(bool)> setPresetTextColours;
        std::function<std::vector<winthemefiles::Entry>()> userThemes;
        std::function<std::filesystem::path()> themeFolder;
    };
    ThemeEditorWindow()=default;
    ThemeEditorWindow(const ThemeEditorWindow&)=delete;
    ThemeEditorWindow& operator=(const ThemeEditorWindow&)=delete;
    ~ThemeEditorWindow() { Close(); }

    void Show(HWND owner, Host host) {
        host_=std::move(host);
        if(hwnd_) { Refresh(); ShowWindow(hwnd_,SW_SHOWNORMAL); SetForegroundWindow(hwnd_); return; }
        if(!Class().Acquire())return;
        RECT frame{0,0,kWidth,kHeight};
        AdjustWindowRectEx(&frame,kStyle,FALSE,WS_EX_TOOLWINDOW);
        HWND parent=owner ? GetAncestor(owner,GA_ROOT) : nullptr;
        hwnd_=CreateWindowExW(WS_EX_TOOLWINDOW,Class().Name(),L"Synth2 Themes",kStyle,CW_USEDEFAULT,CW_USEDEFAULT,
                              frame.right-frame.left,frame.bottom-frame.top,parent,nullptr,win::ModuleInstance(),this);
        if(!hwnd_){Class().Release();return;}
        ShowWindow(hwnd_,SW_SHOWNORMAL);
        UpdateWindow(hwnd_);
    }
    void Close() { if(hwnd_) DestroyWindow(hwnd_); }
    // The instance's palette changed elsewhere (panel menu, session restore).
    void Refresh() {
        if(!hwnd_)return;
        Populate();
        SelectCurrent();
    }

private:
    static constexpr int kWidth=360,kRowHeight=30,kRowsTop=86;
    static constexpr size_t kRows=themes::kSlotCount+winvoice::Keys.size();
    static constexpr int kHeight=kRowsTop+int(kRows)*kRowHeight+44;
    static constexpr DWORD kStyle=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX;
    static constexpr int kLoad=300,kDelete=301,kName=302,kSave=303,kPresetColours=304,kHexBase=100,kPickBase=200;

    static win::WindowClass& Class() {
        static win::WindowClass c(L"OxoSynth2ThemeEditorWindow",&ThemeEditorWindow::Proc,0,GetSysColorBrush(COLOR_BTNFACE));
        return c;
    }
    static std::wstring Wide(const std::string& utf8) {
        if(utf8.empty())return {};
        const int n=MultiByteToWideChar(CP_UTF8,0,utf8.data(),int(utf8.size()),nullptr,0);
        std::wstring s(size_t(std::max(n,0)),L'\0');
        MultiByteToWideChar(CP_UTF8,0,utf8.data(),int(utf8.size()),s.data(),n);
        return s;
    }
    static std::string Utf8(const std::wstring& wide) {
        if(wide.empty())return {};
        const int n=WideCharToMultiByte(CP_UTF8,0,wide.data(),int(wide.size()),nullptr,0,nullptr,nullptr);
        std::string s(size_t(std::max(n,0)),'\0');
        WideCharToMultiByte(CP_UTF8,0,wide.data(),int(wide.size()),s.data(),n,nullptr,nullptr);
        return s;
    }
    static std::wstring Text(HWND control) {
        std::wstring s(size_t(GetWindowTextLengthW(control))+1,L'\0');
        s.resize(size_t(GetWindowTextW(control,s.data(),int(s.size()))));
        return s;
    }
    HWND Make(const wchar_t* klass,const wchar_t* title,DWORD style,int x,int y,int w,int h,int id=0) {
        HWND control=CreateWindowExW(0,klass,title,WS_CHILD|WS_VISIBLE|style,
            x,y,w,h,hwnd_,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),win::ModuleInstance(),nullptr);
        if(control) SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE);
        return control;
    }
    // Built-in themes followed by individual custom theme files.
    void RebuildList() {
        entries_=themes::Factory();
        userBegin_=entries_.size();
        fileFlags_.assign(userBegin_,false);
        if(host_.userThemes) for(auto& item:host_.userThemes()) {
            entries_.push_back(std::move(item.theme));
            fileFlags_.push_back(item.file);
        }
        SendMessageW(load_,CB_RESETCONTENT,0,0);
        for(size_t i=0;i<entries_.size();++i) {
            std::wstring name=Wide(entries_[i].name);
            if(i==0) name+=L" (default)";
            SendMessageW(load_,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name.c_str()));
        }
        SelectCurrent();
    }
    // Built-in themes cannot be removed; theme files can be archived.
    bool IsFile(int index) const {
        return index>=0 && size_t(index)<fileFlags_.size() && fileFlags_[size_t(index)];
    }
    void SelectCurrent() {
        const themes::Palette p=host_.theme();
        int index=-1;
        const std::string active=host_.activeFile ? host_.activeFile() : std::string{};
        if(!active.empty())
            for(size_t i=userBegin_;i<entries_.size();++i)
                if(winthemefiles::SameName(entries_[i].name,active)) {index=int(i);break;}
        if(index<0)
            for(size_t i=0;i<entries_.size();++i)
                if(entries_[i].palette==p) {index=int(i);break;}
        SendMessageW(load_,CB_SETCURSEL,WPARAM(index),0);
        EnableWindow(remove_,IsFile(index));
    }
    void Populate() {
        populating_=true;
        const themes::Palette p=host_.theme();
        for(size_t i=0;i<themes::kSlotCount;++i) {
            SetWindowTextW(hex_[i],Wide(themes::Hex(p[i])).c_str());
            InvalidateRect(swatch_[i],nullptr,FALSE);
        }
        if(host_.voice) {
            const auto voice=host_.voice();
            for(size_t i=0;i<voice.size();++i) {
                SetWindowTextW(hex_[themes::kSlotCount+i],Wide(themes::Hex(voice[i])).c_str());
                InvalidateRect(swatch_[themes::kSlotCount+i],nullptr,FALSE);
            }
        }
        if(presetColours_ && host_.presetTextColours)
            SendMessageW(presetColours_,BM_SETCHECK,host_.presetTextColours()?BST_CHECKED:BST_UNCHECKED,0);
        populating_=false;
    }
    void Edit(size_t slot,uint32_t rgb) {
        if(slot>=themes::kSlotCount) {
            if(!host_.voice || !host_.setVoice)return;
            auto v=host_.voice();
            const size_t index=slot-themes::kSlotCount;
            if(v[index]==rgb)return;
            v[index]=rgb;host_.setVoice(v);
        } else {
            themes::Palette p=host_.theme();
            if(p[slot]==rgb)return;
            p[slot]=rgb;
            if(host_.edit)host_.edit(p);else host_.apply(p,{});
        }
        InvalidateRect(swatch_[slot],nullptr,FALSE);
        SelectCurrent();
    }
    void Choose(size_t slot) {
        static COLORREF custom[16]{};
        const uint32_t c=slot<themes::kSlotCount ? host_.theme()[slot] : host_.voice()[slot-themes::kSlotCount];
        CHOOSECOLORW cc{};
        cc.lStructSize=sizeof(cc);
        cc.hwndOwner=hwnd_;
        cc.rgbResult=RGB((c>>16)&255,(c>>8)&255,c&255);
        cc.lpCustColors=custom;
        cc.Flags=CC_FULLOPEN|CC_RGBINIT;
        if(!ChooseColorW(&cc))return;
        Edit(slot,(uint32_t(GetRValue(cc.rgbResult))<<16)|(uint32_t(GetGValue(cc.rgbResult))<<8)|GetBValue(cc.rgbResult));
        Populate();
    }
    void DrawSwatch(const DRAWITEMSTRUCT* item) {
        const size_t slot=item->CtlID-kPickBase;
        RECT frame=item->rcItem;
        FillRect(item->hDC,&frame,GetSysColorBrush(COLOR_BTNFACE));
        DrawEdge(item->hDC,&frame,EDGE_RAISED,BF_RECT);
        RECT inside=frame;InflateRect(&inside,-4,-4);
        const uint32_t c=slot<themes::kSlotCount ? host_.theme()[slot] : host_.voice()[slot-themes::kSlotCount];
        const HBRUSH brush=CreateSolidBrush(RGB((c>>16)&255,(c>>8)&255,c&255));
        FillRect(item->hDC,&inside,brush);
        DeleteObject(brush);
        if(item->itemState&ODS_FOCUS) {RECT focus=frame;InflateRect(&focus,-2,-2);DrawFocusRect(item->hDC,&focus);}
    }
    void CreateControls() {
        Make(L"STATIC",L"Load",0,12,15,50,20);
        load_=Make(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP,64,11,200,300,kLoad);
        remove_=Make(L"BUTTON",L"Delete",BS_PUSHBUTTON|WS_TABSTOP,274,10,74,24,kDelete);
        Make(L"STATIC",L"Name",0,12,49,50,20);
        name_=Make(L"EDIT",L"",WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP,64,46,200,22,kName);
        Make(L"BUTTON",L"Save",BS_PUSHBUTTON|WS_TABSTOP,274,45,74,24,kSave);
        for(size_t i=0;i<kRows;++i) {
            const int y=kRowsTop+int(i)*kRowHeight;
            const wchar_t* label=i<themes::kSlotCount ? nullptr : winvoice::Labels[i-themes::kSlotCount];
            static constexpr const wchar_t* names[]={
                L"Main Panel",L"Section Headers",L"Section Header Text",L"Parameter Labels",
                L"Knob Body",L"Knob Gradient",L"Knob Dial",L"Buttons",L"Button LED's",
                L"Radio LED's",L"Display Text",L"Display Gradient Top",L"Display Gradient Bottom"};
            const std::wstring caption=label ? std::wstring(label) : std::wstring(names[i]);
            Make(L"STATIC",caption.c_str(),0,12,y+4,180,20);
            hex_[i]=Make(L"EDIT",L"",WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP,194,y,78,22,kHexBase+int(i));
            swatch_[i]=Make(L"BUTTON",L"",BS_OWNERDRAW|WS_TABSTOP,284,y-1,60,24,kPickBase+int(i));
        }
        presetColours_=Make(L"BUTTON",L"Use browser preset colours",BS_AUTOCHECKBOX|WS_TABSTOP,
                            12,kRowsTop+int(kRows)*kRowHeight+7,235,24,kPresetColours);
        Populate();
        RebuildList();
    }
    void Command(int id,int notification) {
        if(id==kLoad && notification==CBN_SELCHANGE) {
            const int index=int(SendMessageW(load_,CB_GETCURSEL,0,0));
            if(index<0 || index>=int(entries_.size()))return;
            host_.apply(entries_[size_t(index)].palette,
                        IsFile(index) ? entries_[size_t(index)].name : std::string{});
            SetWindowTextW(name_,Wide(entries_[size_t(index)].name).c_str());
            Populate();
            EnableWindow(remove_,IsFile(index));
        } else if(id==kPresetColours && notification==BN_CLICKED && host_.setPresetTextColours) {
            host_.setPresetTextColours(SendMessageW(presetColours_,BM_GETCHECK,0,0)==BST_CHECKED);
        } else if(id==kDelete && notification==BN_CLICKED) {
            const int index=int(SendMessageW(load_,CB_GETCURSEL,0,0));
            if(IsFile(index) && host_.themeFolder) {
                const std::wstring prompt=L"Archive theme '"+Wide(entries_[size_t(index)].name)+
                                          L"'? The original file will be kept as a .deleted.bak backup.";
                if(MessageBoxW(hwnd_,prompt.c_str(),L"Archive theme",MB_YESNO|MB_ICONQUESTION)==IDYES) {
                    if(winthemefiles::Archive(host_.themeFolder(),entries_[size_t(index)].name)) {
                        host_.apply(host_.theme(),{}); // keep the live colours but clear archived filename
                        RebuildList();
                    }
                    else MessageBoxW(hwnd_,L"Unable to archive theme file.",L"Themes",MB_OK|MB_ICONWARNING);
                }
            }
        } else if(id==kSave && notification==BN_CLICKED) {
            const std::string name=winthemefiles::SafeName(Utf8(Text(name_)));
            if(host_.themeFolder && !name.empty() &&
               winthemefiles::Save(host_.themeFolder(),name,host_.theme(),host_.voice())) {
                host_.apply(host_.theme(),name); // mark the exact saved file active
                RebuildList();
            } else MessageBoxW(hwnd_,L"Unable to save theme file. Check the name and Themes folder permissions."
                                   L" Existing theme files have been preserved.",L"Themes",MB_OK|MB_ICONWARNING);
        } else if(id>=kHexBase && id<kHexBase+int(kRows) && notification==EN_CHANGE && !populating_) {
            uint32_t rgb=0; // a complete colour applies at once, an unfinished one waits
            if(themes::ParseHex(Utf8(Text(hex_[size_t(id-kHexBase)])),rgb))Edit(size_t(id-kHexBase),rgb);
        } else if(id>=kPickBase && id<kPickBase+int(kRows) && notification==BN_CLICKED) {
            Choose(size_t(id-kPickBase));
        }
    }
    LRESULT Message(UINT msg,WPARAM wp,LPARAM lp) {
        switch(msg) {
        case WM_CREATE:CreateControls();return 0;
        case WM_DRAWITEM: {
            const auto* item=reinterpret_cast<const DRAWITEMSTRUCT*>(lp);
            if(!item || item->CtlID<kPickBase || item->CtlID>=kPickBase+kRows)return FALSE;
            DrawSwatch(item);return TRUE;
        }
        case WM_COMMAND:Command(LOWORD(wp),HIWORD(wp));return 0;
        case WM_CLOSE:DestroyWindow(hwnd_);return 0;
        case WM_NCDESTROY: {
            const HWND closing=hwnd_;
            hwnd_=nullptr;
            const LRESULT result=DefWindowProcW(closing,msg,wp,lp);
            Class().Release();
            return result;
        }
        default:return DefWindowProcW(hwnd_,msg,wp,lp);
        }
    }
    static LRESULT CALLBACK Proc(HWND h,UINT msg,WPARAM wp,LPARAM lp) {
        auto* self=reinterpret_cast<ThemeEditorWindow*>(GetWindowLongPtrW(h,GWLP_USERDATA));
        if(msg==WM_NCCREATE) {
            self=static_cast<ThemeEditorWindow*>(reinterpret_cast<const CREATESTRUCTW*>(lp)->lpCreateParams);
            if(!self)return FALSE;
            self->hwnd_=h;
            SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));
            return TRUE;
        }
        return self?self->Message(msg,wp,lp):DefWindowProcW(h,msg,wp,lp);
    }

    Host host_;
    HWND hwnd_=nullptr,load_=nullptr,remove_=nullptr,name_=nullptr,presetColours_=nullptr;
    std::array<HWND,kRows> hex_{},swatch_{};
    std::vector<themes::Named> entries_;
    size_t userBegin_=0;
    std::vector<bool> fileFlags_;
    bool populating_=false;
};
} // namespace oxo
