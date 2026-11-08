#pragma once
// Windows-only, modeless JSON theme editor. All changes are explicit:
// Preview updates the current plugin view; Save writes the selected JSON file.
#include "WindowsThemes.hpp"
#include <windows.h>
#include <commdlg.h>
#include <array>
#include <filesystem>
#include <functional>
#include <utility>
#include <fstream>
#include <string>
#include <cwchar>
#include <cstdio>
#include <vector>
#include <iterator>
#include <algorithm>
#include <cwctype>

namespace oxo {
class ThemeEditorWindow {
public:
    using PreviewCallback = std::function<void(int,const wintheme::Palette&)>;
    ThemeEditorWindow()=default;
    ThemeEditorWindow(const ThemeEditorWindow&)=delete;
    ThemeEditorWindow& operator=(const ThemeEditorWindow&)=delete;
    ~ThemeEditorWindow() { Close(); }
    void Show(HWND owner, const std::filesystem::path& root, int selected, PreviewCallback preview) {
        if(hwnd_) { ShowWindow(hwnd_,SW_SHOWNORMAL); SetForegroundWindow(hwnd_); return; }
        root_=root;
        preview_=std::move(preview);
        index_=selected>=0?selected:0;
        static const bool registered=[] {
            WNDCLASSW wc{};
            wc.lpfnWndProc=&ThemeEditorWindow::Proc;
            wc.hInstance=GetModuleHandleW(nullptr);
            wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
            wc.hbrBackground=GetSysColorBrush(COLOR_BTNFACE);
            wc.lpszClassName=L"OxoSynth2ThemeEditorWindow";
            return RegisterClassW(&wc)!=0 || GetLastError()==ERROR_CLASS_ALREADY_EXISTS;
        }();
        if(!registered)return;
        HWND parent=owner ? GetAncestor(owner,GA_ROOT) : nullptr;
        hwnd_=CreateWindowExW(WS_EX_TOOLWINDOW,L"OxoSynth2ThemeEditorWindow",L"Synth2 - Theme Editor",
            WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,
            CW_USEDEFAULT,CW_USEDEFAULT,520,760,parent,nullptr,GetModuleHandleW(nullptr),this);
        if(!hwnd_)return;
        ShowWindow(hwnd_,SW_SHOWNORMAL);
        UpdateWindow(hwnd_);
    }
    void Close() {
        if(hwnd_) DestroyWindow(hwnd_);
    }
private:
    static constexpr int kThemeSelect=300;
    static constexpr int kPreview=400,kSave=401,kReload=402,kNew=403,kSaveAs=404;
    static constexpr int kHexBase=100,kPickBase=200;
    HWND hwnd_=nullptr,combo_=nullptr;
    std::array<HWND,wintheme::kColourCount> hex_{};
    std::array<HWND,wintheme::kColourCount> swatch_{};
    std::filesystem::path root_;
    wintheme::Palette colours_{};
    std::vector<wintheme::Entry> themes_;
    int index_=0;
    PreviewCallback preview_;
    HFONT font_=nullptr;
    bool populating_=false;
    static std::wstring Wide(const char* utf8) {
        int n=MultiByteToWideChar(CP_UTF8,0,utf8,-1,nullptr,0);
        if(n<=0)return {};
        std::wstring s(size_t(n),L'\0');
        MultiByteToWideChar(CP_UTF8,0,utf8,-1,s.data(),n);
        s.pop_back();return s;
    }
    HWND Make(const wchar_t* klass,const wchar_t* title,DWORD style,int x,int y,int w,int h,int id=0) {
        HWND control=CreateWindowExW(0,klass,title,WS_CHILD|WS_VISIBLE|style,
            x,y,w,h,hwnd_,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
        if(control && font_) SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(font_),TRUE);
        return control;
    }
    static std::wstring Hex(unsigned colour) {
        wchar_t buffer[8]{};
        std::swprintf(buffer,8,L"#%06X",colour&0xFFFFFFu);
        return buffer;
    }
    static bool ParseHex(const std::wstring& text,unsigned& value) {
        if(text.size()!=7 || text[0]!=L'#')return false;
        unsigned result=0;
        for(size_t i=1;i<7;++i) {
            wchar_t c=text[i];unsigned digit;
            if(c>=L'0'&&c<=L'9')digit=unsigned(c-L'0');
            else if(c>=L'a'&&c<=L'f')digit=unsigned(c-L'a'+10);
            else if(c>=L'A'&&c<=L'F')digit=unsigned(c-L'A'+10);
            else return false;
            result=(result<<4)|digit;
        }
        value=result;return true;
    }
    void Populate() {
        populating_=true;
        for(size_t i=0;i<hex_.size();++i) {
            SetWindowTextW(hex_[i],Hex(colours_[i]).c_str());
        }
        populating_=false;
        for(HWND swatch:swatch_)if(swatch)InvalidateRect(swatch,nullptr,FALSE);
    }
    bool ReadInputs() {
        wintheme::Palette next{};
        for(size_t i=0;i<hex_.size();++i) {
            wchar_t value[64]{};
            GetWindowTextW(hex_[i],value,64);
            if(!ParseHex(value,next[i])) {
                std::wstring message=L"Invalid colour for "+Wide(wintheme::Keys[i])+L".\nUse #RRGGBB, for example #A0B1C2.";
                MessageBoxW(hwnd_,message.c_str(),L"Invalid theme colour",MB_OK|MB_ICONWARNING);
                SetFocus(hex_[i]);
                return false;
            }
        }
        colours_=next;return true;
    }
    // Rebuild from disk, so newly saved custom themes appear immediately.
    void RefreshThemeList(const std::filesystem::path& desired={}) {
        std::filesystem::path preferred=desired;
        if(preferred.empty() && index_>=0 && index_<int(themes_.size()))
            preferred=themes_[size_t(index_)].file;
        const int oldIndex=index_;
        themes_=wintheme::List(root_);
        index_=std::clamp(oldIndex,0,std::max(0,int(themes_.size())-1));
        if(!preferred.empty()) {
            for(size_t i=0;i<themes_.size();++i) {
                if(CompareStringOrdinal(preferred.c_str(),-1,themes_[i].file.c_str(),-1,TRUE)==CSTR_EQUAL) {
                    index_=static_cast<int>(i);break;
                }
            }
        }
        SendMessageW(combo_,CB_RESETCONTENT,0,0);
        for(const auto& theme:themes_)
            SendMessageW(combo_,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(theme.name.c_str()));
        SendMessageW(combo_,CB_SETCURSEL,index_,0);
    }
    bool Load() {
        if(index_<0 || index_>=int(themes_.size()))return false;
        if(!wintheme::Read(themes_[size_t(index_)].file,colours_)) {
            MessageBoxW(hwnd_,L"Cannot load this theme file. The original 13 colours are required.",
                L"Theme editor",MB_OK|MB_ICONWARNING);
            return false;
        }
        Populate();return true;
    }
    void Save() {
        if(!ReadInputs() || index_<0 || index_>=int(themes_.size()))return;
        const auto file=themes_[size_t(index_)].file;
        std::error_code ec;
        std::filesystem::create_directories(file.parent_path(),ec);
        if(ec) {MessageBoxW(hwnd_,L"Cannot create Themes folder.",L"Theme editor",MB_OK|MB_ICONERROR);return;}
        const std::wstring temp=file.wstring()+L".tmp";
        const std::string contents=wintheme::ToJSON(colours_);
        {
            std::ofstream out(std::filesystem::path(temp),std::ios::binary|std::ios::trunc);
            if(!out){MessageBoxW(hwnd_,L"Cannot write the theme file.",L"Theme editor",MB_OK|MB_ICONERROR);return;}
            out.write(contents.data(),std::streamsize(contents.size()));
            out.flush();
            if(!out){MessageBoxW(hwnd_,L"Cannot finish writing the theme file.",L"Theme editor",MB_OK|MB_ICONERROR);return;}
        }
        if(!MoveFileExW(temp.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {
            DeleteFileW(temp.c_str());
            MessageBoxW(hwnd_,L"Cannot replace the theme file. Check folder permissions.",L"Theme editor",MB_OK|MB_ICONERROR);
            return;
        }
        if(preview_)preview_(index_,colours_);
    }
    // The native Save dialog supplies the filename field for both New and Save As.
    // A theme must live in the active Themes directory; neither action may
    // replace another theme, even if the name differs only by capitalization.
    bool AskNewName(bool copy, std::filesystem::path& result) {
        const auto folder=wintheme::Folder(root_);
        std::error_code ec;
        std::filesystem::create_directories(folder,ec);
        if(ec) {
            MessageBoxW(hwnd_,L"Cannot create Themes folder.",L"Theme editor",MB_OK|MB_ICONERROR);
            return false;
        }
        wchar_t filename[4096]{};
        const std::wstring suggested=copy && index_>=0 && index_<int(themes_.size())
            ? themes_[size_t(index_)].name+L" Copy.json" : L"New Theme.json";
        if(suggested.size()<std::size(filename))
            std::wcscpy(filename,suggested.c_str());
        const std::wstring dir=folder.wstring();
        OPENFILENAMEW dialog{};
        dialog.lStructSize=sizeof(dialog);
        dialog.hwndOwner=hwnd_;
        dialog.lpstrFile=filename;
        dialog.nMaxFile=static_cast<DWORD>(std::size(filename));
        dialog.lpstrInitialDir=dir.c_str();
        dialog.lpstrTitle=copy ? L"Save theme as..." : L"Name the new theme";
        dialog.lpstrDefExt=L"json";
        dialog.lpstrFilter=L"Synth2 theme files (*.json)\0*.json\0All files (*.*)\0*.*\0";
        dialog.Flags=OFN_EXPLORER|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
        if(!GetSaveFileNameW(&dialog))return false;
        const std::filesystem::path file(filename);
        auto extension=wintheme::FoldCase(file.extension().wstring());
        const auto stem=file.stem().wstring();
        if(extension!=L".json" || stem.empty() || stem.size()>80 ||
           stem.back()==L'.' || stem.back()==L' ' || stem.front()==L'.' ||
           stem.find_first_of(L"<>:\"/\\|?*")!=std::wstring::npos ||
           std::any_of(stem.begin(),stem.end(),[](wchar_t ch){return ch<32;})) {
            MessageBoxW(hwnd_,L"Use a theme name of 1-80 characters, without Windows filename symbols.",
                L"Invalid theme name",MB_OK|MB_ICONWARNING);
            return false;
        }
        std::wstring base=wintheme::FoldCase(stem.substr(0,stem.find(L'.')));
        if(base==L"con" || base==L"prn" || base==L"aux" || base==L"nul" ||
           (base.size()==4 && ((base.substr(0,3)==L"com") || (base.substr(0,3)==L"lpt")) &&
           base[3]>=L'1' && base[3]<=L'9')) {
            MessageBoxW(hwnd_,L"This name is reserved by Windows. Choose another.",
                L"Invalid theme name",MB_OK|MB_ICONWARNING);
            return false;
        }
        const auto normalizedFolder=std::filesystem::weakly_canonical(folder,ec);
        if(ec)return false;
        const auto normalizedSelected=std::filesystem::weakly_canonical(file.parent_path(),ec);
        if(ec || CompareStringOrdinal(normalizedFolder.c_str(),-1,normalizedSelected.c_str(),-1,TRUE)!=CSTR_EQUAL) {
            MessageBoxW(hwnd_,L"Themes must be saved inside Synth2's active Themes folder.",
                L"Theme location",MB_OK|MB_ICONWARNING);
            return false;
        }
        result=file;
        return true;
    }
    bool WriteNewFile(const std::filesystem::path& file,const wintheme::Palette& colours) {
        const auto content=wintheme::ToJSON(colours);
        HANDLE handle=CreateFileW(file.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,
                                  CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(handle==INVALID_HANDLE_VALUE) {
            MessageBoxW(hwnd_,L"That theme name already exists, or the folder is not writable. No file was replaced.",
                L"Cannot create theme",MB_OK|MB_ICONWARNING);
            return false;
        }
        DWORD written=0;
        const bool ok=content.size()<=MAXDWORD &&
            WriteFile(handle,content.data(),static_cast<DWORD>(content.size()),&written,nullptr)!=0 &&
            written==content.size() && FlushFileBuffers(handle)!=0;
        CloseHandle(handle);
        if(!ok) {
            DeleteFileW(file.c_str());
            MessageBoxW(hwnd_,L"Could not write the new theme file.",L"Theme editor",MB_OK|MB_ICONERROR);
            return false;
        }
        return true;
    }
    void CreateNamedTheme(bool copy) {
        wintheme::Palette next=wintheme::Originals[0];
        if(copy) {
            if(!ReadInputs())return;
            next=colours_; // Save As includes edits currently visible, even if unsaved.
        }
        std::filesystem::path path;
        if(!AskNewName(copy,path))return;
        if(!WriteNewFile(path,next))return;
        RefreshThemeList(path);
        colours_=next;
        Populate();
        if(preview_)preview_(index_,colours_);
    }
    void Choose(int row) {
        if(row<0 || row>=static_cast<int>(wintheme::kColourCount))return;
        if(!ReadInputs())return;
        static COLORREF custom[16]{};
        CHOOSECOLORW cc{};
        cc.lStructSize=sizeof(cc);
        cc.hwndOwner=hwnd_;
        const unsigned c=colours_[size_t(row)];
        cc.rgbResult=RGB((c>>16)&255,(c>>8)&255,c&255);
        cc.lpCustColors=custom;
        cc.Flags=CC_FULLOPEN|CC_RGBINIT;
        if(!ChooseColorW(&cc))return;
        colours_[size_t(row)]=(unsigned(GetRValue(cc.rgbResult))<<16)|
                              (unsigned(GetGValue(cc.rgbResult))<<8)|unsigned(GetBValue(cc.rgbResult));
        Populate();
        if(preview_)preview_(index_,colours_); // live preview after choosing a colour
    }
    void DrawSwatch(const DRAWITEMSTRUCT* item) {
        if(!item || item->CtlID<kPickBase ||
           item->CtlID>=kPickBase+static_cast<UINT>(wintheme::kColourCount))return;
        const size_t i=item->CtlID-kPickBase;
        wchar_t text[16]{};
        GetWindowTextW(hex_[i],text,16);
        unsigned colour=colours_[i];
        ParseHex(text,colour); // Preview current hex field; incomplete edits keep the last colour.
        RECT frame=item->rcItem;
        FillRect(item->hDC,&frame,GetSysColorBrush(COLOR_BTNFACE));
        DrawEdge(item->hDC,&frame,EDGE_RAISED,BF_RECT);
        RECT inside=frame;InflateRect(&inside,-4,-4);
        const HBRUSH brush=CreateSolidBrush(RGB((colour>>16)&255,(colour>>8)&255,colour&255));
        FillRect(item->hDC,&inside,brush);
        DeleteObject(brush);
        if(item->itemState&ODS_FOCUS) {
            RECT focus=frame;InflateRect(&focus,-2,-2);
            DrawFocusRect(item->hDC,&focus);
        }
    }
    void CreateControls() {
        font_=CreateFontW(-16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
                          OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,
                          DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
        Make(L"STATIC",L"Theme:",0,18,16,58,24);
        combo_=Make(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL,90,12,230,250,kThemeSelect);
        Make(L"BUTTON",L"New...",BS_PUSHBUTTON|WS_TABSTOP,328,12,72,26,kNew);
        Make(L"BUTTON",L"Save As...",BS_PUSHBUTTON|WS_TABSTOP,406,12,94,26,kSaveAs);
        RefreshThemeList();
        Make(L"STATIC",L"Edit hex or click the colour swatch. Preview doesn't save.",0,18,48,462,24);
        for(int i=0;i<static_cast<int>(wintheme::kColourCount);i++) {
            const int y=78+i*34;
            Make(L"STATIC",wintheme::Labels[size_t(i)],0,18,y+3,207,25);
            hex_[size_t(i)]=Make(L"EDIT",L"#000000",WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP,
                234,y,112,26,kHexBase+i);
            swatch_[size_t(i)]=Make(L"BUTTON",L"",BS_OWNERDRAW|WS_TABSTOP,
                359,y-1,64,28,kPickBase+i);
        }
        Make(L"BUTTON",L"Preview",BS_PUSHBUTTON|WS_TABSTOP,18,665,138,31,kPreview);
        Make(L"BUTTON",L"Save",BS_DEFPUSHBUTTON|WS_TABSTOP,175,665,138,31,kSave);
        Make(L"BUTTON",L"Reload",BS_PUSHBUTTON|WS_TABSTOP,332,665,138,31,kReload);
        Load();
    }
    LRESULT Message(UINT msg,WPARAM wp,LPARAM lp) {
        switch(msg) {
        case WM_CREATE:CreateControls();return 0;
        case WM_DRAWITEM: {
            const auto* item=reinterpret_cast<const DRAWITEMSTRUCT*>(lp);
            if(item && item->CtlID>=kPickBase &&
               item->CtlID<kPickBase+static_cast<UINT>(wintheme::kColourCount)) {
                DrawSwatch(item);return TRUE;
            }
            return FALSE;
        }
        case WM_COMMAND: {
            const int id=LOWORD(wp),notification=HIWORD(wp);
            if(id==kThemeSelect && notification==CBN_SELCHANGE) {
                const int next=int(SendMessageW(combo_,CB_GETCURSEL,0,0));
                if(next>=0 && next<int(themes_.size())) {
                    index_=next;Load();
                }
                return 0;
            }
            if(id>=kHexBase && id<kHexBase+static_cast<int>(wintheme::kColourCount) &&
               notification==EN_CHANGE && !populating_) {
                if(swatch_[size_t(id-kHexBase)])
                    InvalidateRect(swatch_[size_t(id-kHexBase)],nullptr,FALSE);
                return 0;
            }
            if(id>=kPickBase && id<kPickBase+static_cast<int>(wintheme::kColourCount) &&
               notification==BN_CLICKED) {
                Choose(id-kPickBase);return 0;
            }
            if(notification==BN_CLICKED) {
                if(id==kPreview) {if(ReadInputs() && preview_)preview_(index_,colours_);return 0;}
                if(id==kSave) {Save();return 0;}
                if(id==kReload) {RefreshThemeList();Load();return 0;}
                if(id==kNew) {CreateNamedTheme(false);return 0;}
                if(id==kSaveAs) {CreateNamedTheme(true);return 0;}
            }
            return 0;
        }
        case WM_CLOSE:DestroyWindow(hwnd_);return 0;
        case WM_NCDESTROY: {
            const HWND closing=hwnd_;
            hwnd_=nullptr;
            if(font_) {DeleteObject(font_);font_=nullptr;}
            return DefWindowProcW(closing,msg,wp,lp);
        }
        default:return DefWindowProcW(hwnd_,msg,wp,lp);
        }
    }
    static LRESULT CALLBACK Proc(HWND h,UINT msg,WPARAM wp,LPARAM lp) {
        ThemeEditorWindow* self=reinterpret_cast<ThemeEditorWindow*>(GetWindowLongPtrW(h,GWLP_USERDATA));
        if(msg==WM_NCCREATE) {
            const auto* cs=reinterpret_cast<CREATESTRUCTW*>(lp);
            self=static_cast<ThemeEditorWindow*>(cs->lpCreateParams);
            if(!self)return FALSE;
            self->hwnd_=h;
            SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));
            return TRUE;
        }
        return self?self->Message(msg,wp,lp):DefWindowProcW(h,msg,wp,lp);
    }
};
} // namespace oxo
