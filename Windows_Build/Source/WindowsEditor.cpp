// The Win32 window of the Windows editor: the panel itself (layout,
// drawing, browser, menus, input) is DesktopEditor/DesktopEditor.cpp, shared
// with Linux. Here: the child window and its messages, the double-buffered
// GDI+ paint, the name field (an EDIT control), the copy cursor, the
// common dialogs, WinHTTP, the theme editor window.
#include "DesktopEditor/DesktopEditor.hpp"
#include "pluginterfaces/gui/iplugview.h"
#include "WindowsThemeEditor.hpp"
#include "WindowsPrompt.hpp"
#include <shellapi.h>
#include <shlobj.h>
#include <commdlg.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <cstring>

using namespace Steinberg;
using namespace Gdiplus;

namespace oxo {
namespace {
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
} // namespace

const char* const DesktopEditor::kUpdatePlatform="win";

std::wstring DesktopEditor::toWide(const char* str) {
    if(!str || !*str) return {};
    int n=MultiByteToWideChar(CP_UTF8,0,str,-1,nullptr,0);
    if(n<=0) return L"?";
    std::wstring out(size_t(n),L'\0');
    MultiByteToWideChar(CP_UTF8,0,str,-1,out.data(),n);
    out.pop_back();
    return out;
}
std::string DesktopEditor::toUtf8(const std::wstring& s) {
    if(s.empty()) return {};
    int n=WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),nullptr,0,nullptr,nullptr);
    if(n<=0) return {};
    std::string out(size_t(n),'\0');
    WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),out.data(),n,nullptr,nullptr);
    return out;
}

bool DesktopEditor::startGraphics() {
    GdiplusStartupInput gdiplusInput;
    return GdiplusStartup(&gdiplusToken_,&gdiplusInput,nullptr)==Ok;
}
void DesktopEditor::endGraphics() {
    if(copyCursor_) {DestroyCursor(copyCursor_);copyCursor_=nullptr;}
    if(gdiplusToken_) {GdiplusShutdown(gdiplusToken_);gdiplusToken_=0;}
}
tresult PLUGIN_API DesktopEditor::isPlatformTypeSupported(FIDString type) {
    return type && strcmp(type,kPlatformTypeHWND)==0 ? kResultTrue : kResultFalse;
}
win::WindowClass& DesktopEditor::windowClass() {
    static win::WindowClass c(L"OxoSynth2DesktopEditorView",&DesktopEditor::wndProc,CS_DBLCLKS);
    return c;
}
bool DesktopEditor::openWindow() {
    if(!windowClass().Acquire()) return false;
    window_=CreateWindowExW(0,windowClass().Name(),L"",WS_CHILD|WS_VISIBLE|WS_CLIPSIBLINGS,
                          0,0,rect.getWidth(),rect.getHeight(),
                          reinterpret_cast<HWND>(systemWindow),nullptr,win::ModuleInstance(),this);
    if(!window_) {windowClass().Release();return false;}
    SetTimer(window_,1,50,nullptr);
    return true;
}
void DesktopEditor::closeWindow() {
    releaseKeyFocus();
    if(editName_) {DestroyWindow(editName_);editName_=nullptr;}
    if(window_) {KillTimer(window_,1);DestroyWindow(window_);window_=nullptr;windowClass().Release();}
    releaseBuffers();
}
void DesktopEditor::layoutChild() {
    if(window_) MoveWindow(window_,0,0,rect.getWidth(),rect.getHeight(),TRUE);
    updateEditControl();
}
void DesktopEditor::repaint() const {
    if(window_) InvalidateRect(window_,nullptr,FALSE);
}
void DesktopEditor::invalidateDevice(int left,int top,int right,int bottom) const {
    if(!window_) return;
    RECT device{LONG(left),LONG(top),LONG(right),LONG(bottom)};
    InvalidateRect(window_,&device,FALSE);
}

// Finder.reveal: the containing folder opens with the item selected.
void DesktopEditor::revealInExplorer(const std::string& utf8Path) const {
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
// Menus and dialogs: Win32's modal loops (the host's windows run on in
// them), so `then` runs before these return.
void DesktopEditor::popupMenu(HMENU menu,int x,int y,std::function<void(int)> then) {
    const int choice=menu && GetMenuItemCount(menu)>0 ?
        int(TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY,x,y,0,window_,nullptr)) : 0;
    if(menu) DestroyMenu(menu); // and its sub-menus
    if(then) then(choice);
}
void DesktopEditor::messageBox(const std::wstring& text,const wchar_t* title,UINT flags,std::function<void(int)> then) {
    const int answer=MessageBoxW(window_,text.c_str(),title,flags);
    if(then) then(answer);
}
void DesktopEditor::pickFolder(const wchar_t* title,std::function<void(const std::wstring&)> then) {
    BROWSEINFOW browse{};
    browse.hwndOwner=window_;
    browse.lpszTitle=title;
    browse.ulFlags=BIF_RETURNONLYFSDIRS|BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE item=SHBrowseForFolderW(&browse);
    if(!item) return;
    wchar_t path[MAX_PATH]={};
    const BOOL gotPath=SHGetPathFromIDListW(item,path);
    CoTaskMemFree(item);
    if(gotPath && then) then(path);
}
void DesktopEditor::chooseFile(FileDialog kind,std::function<void(const std::wstring&)> then) {
    wchar_t file[4096]={};
    OPENFILENAMEW dialog{};
    dialog.lStructSize=sizeof(dialog);
    dialog.hwndOwner=window_;
    dialog.lpstrFile=file;
    dialog.nMaxFile=4096;
    if(kind==FileDialog::ImportFile) {
        dialog.lpstrFilter=L"Synth1 presets / ZIP banks (*.sy1;*.zip)\0*.sy1;*.zip\0All files (*.*)\0*.*\0";
        dialog.Flags=OFN_EXPLORER|OFN_PATHMUSTEXIST|OFN_FILEMUSTEXIST;
    } else {
        dialog.lpstrFilter=L"Synth1 presets (*.sy1)\0*.sy1\0All files (*.*)\0*.*\0";
        dialog.lpstrDefExt=L"sy1";
        dialog.Flags=OFN_EXPLORER|OFN_PATHMUSTEXIST|
                     (kind==FileDialog::SavePatch?OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST);
    }
    const BOOL chosen=kind==FileDialog::SavePatch?GetSaveFileNameW(&dialog):GetOpenFileNameW(&dialog);
    if(chosen && then) then(file);
}
void DesktopEditor::askText(const wchar_t* title,const wchar_t* label,const std::wstring& text,
                            std::function<void(const std::wstring&)> then) {
    std::wstring typed=text;
    if(win::AskText(window_,title,label,typed) && then) then(typed);
}
HCURSOR DesktopEditor::copyCursor() {
    if(!copyCursor_) copyCursor_=makeCopyCursor();
    return copyCursor_ ? copyCursor_ : LoadCursorW(nullptr,IDC_ARROW);
}

void DesktopEditor::updateEditControl() {
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
            SetWindowLongPtrW(editName_,GWLP_WNDPROC,LONG_PTR(&DesktopEditor::nameFieldProc));
        }
    } else MoveWindow(editName_,int(300*sx),int(38*sy),int(200*sx),int(19*sy),TRUE);
}
std::optional<std::wstring> DesktopEditor::nameFieldText() const {
    if(!editName_) return std::nullopt;
    std::wstring name;
    const int n=GetWindowTextLengthW(editName_);
    name.resize(size_t(n)+1);
    GetWindowTextW(editName_,name.data(),n+1);
    name.resize(size_t(n));
    return name;
}
// The EDIT control takes its own keys.
bool DesktopEditor::nameFieldKey(char16,int16,int16) { return false; }
// The write dialog's name field: Return / Enter and Esc go to keyDown (by
// a posted message: Esc / a write destroy this field), no beep.
LRESULT CALLBACK DesktopEditor::nameFieldProc(HWND field,UINT msg,WPARAM wp,LPARAM lp) {
    const auto previous=reinterpret_cast<WNDPROC>(GetWindowLongPtrW(field,GWLP_USERDATA));
    if(msg==WM_SETFOCUS)
        if(auto* editor=reinterpret_cast<DesktopEditor*>(GetWindowLongPtrW(GetParent(field),GWLP_USERDATA)))
            editor->noteFocusFrom(HWND(wp));
    if(msg==WM_KEYDOWN && (wp==VK_RETURN || wp==VK_ESCAPE)) {
        PostMessageW(GetParent(field),kFieldKeyMessage,wp,0);
        return 0;
    }
    if(msg==WM_CHAR && (wp==L'\r' || wp==0x1B)) return 0;
    return CallWindowProcW(previous,field,msg,wp,lp);
}

bool DesktopEditor::hasKeyFocus() const {
    const HWND focus=GetFocus();
    return window_ && focus && (focus==window_ || IsChild(window_,focus));
}
void DesktopEditor::noteFocusFrom(HWND from) {
    if(from && from!=window_ && !IsChild(window_,from)) focusBefore_=from;
}
void DesktopEditor::takeKeyFocus() {
    if(window_ && !hasKeyFocus()) SetFocus(window_); // WM_SETFOCUS notes who had it
}
HWND DesktopEditor::hostFocusWindow() const {
    return focusBefore_ && IsWindow(focusBefore_) ? focusBefore_ : GetAncestor(window_,GA_ROOT);
}
void DesktopEditor::releaseKeyFocus() {
    if(hasKeyFocus()) SetFocus(hostFocusWindow());
    focusBefore_=nullptr;
}

bool DesktopEditor::startUpdateCheck(const std::string& url) {
    return updateCheck_.Start(toWide(url.c_str()),window_,kUpdateMessage);
}

void DesktopEditor::ThemeEditorDeleter::operator()(ThemeEditorWindow* w) const { delete w; }
void DesktopEditor::showThemeEditor() {
    if(!themeEditor_)themeEditor_.reset(new ThemeEditorWindow);
    themeEditor_->Show(window_,{[this]{return theme_;},
                                [this](const themes::Palette& p){setTheme(p,true);},
                                []()->themes::Store&{return themeStore();},
                                []{return themeFiles();}});
}
void DesktopEditor::refreshThemeEditor() {
    if(themeEditor_) themeEditor_->Refresh();
}
void DesktopEditor::closeThemeEditor() {
    if(themeEditor_) themeEditor_->Close();
}

bool DesktopEditor::ensureBuffers(HDC screen,int width,int height) {
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
void DesktopEditor::releaseBuffers() {
    for(Buffer* b:{&static_,&back_}) {
        if(b->dc && b->previous && b->previous!=HGDI_ERROR) SelectObject(b->dc,b->previous);
        if(b->bitmap) DeleteObject(b->bitmap);
        if(b->dc) DeleteDC(b->dc);
        *b={};
    }
    bufferWidth_=bufferHeight_=0;
}
void DesktopEditor::paint(HDC screen,const RECT& dirty) {
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

LRESULT CALLBACK DesktopEditor::wndProc(HWND window,UINT msg,WPARAM wp,LPARAM lp) {
    DesktopEditor* editor=reinterpret_cast<DesktopEditor*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(msg==WM_NCCREATE) {
        auto* create=reinterpret_cast<CREATESTRUCTW*>(lp);
        editor=static_cast<DesktopEditor*>(create->lpCreateParams);
        if(!editor) return FALSE;
        editor->window_=window; // WM_CREATE arrives before CreateWindowExW returns.
        SetWindowLongPtrW(window,GWLP_USERDATA,LONG_PTR(editor));
        return TRUE;
    }
    return editor?editor->windowMessage(msg,wp,lp):DefWindowProcW(window,msg,wp,lp);
}
LRESULT DesktopEditor::windowMessage(UINT msg,WPARAM wp,LPARAM lp) {
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
            if(hoveredChrome_!=0 || hoveredControl_>=0 || isVersionBox(dx,dy) ||
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
    // A click takes the keyboard only for the browser (a knob leaves it to the host).
    case WM_LBUTTONDOWN:
        if(browser_) takeKeyFocus();
        mouseDown(float(GET_X_LPARAM(lp))/sx,float(GET_Y_LPARAM(lp))/sy,false);return 0;
    case WM_LBUTTONDBLCLK:
        if(browser_) takeKeyFocus();
        mouseDown(float(GET_X_LPARAM(lp))/sx,float(GET_Y_LPARAM(lp))/sy,true);return 0;
    case WM_SETFOCUS:noteFocusFrom(HWND(wp));return 0;
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
        if(typeMenuHover_>=0) {typeMenuHover_=-1;invalidateTypeMenu();}
        return 0;
    case WM_LBUTTONUP:mouseUp();return 0;
    case WM_CAPTURECHANGED: // lost the capture (another window, a menu): a drag is cancelled
        if(press_.active) dragEnd(false); else mouseUp();
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
    case WM_GETDLGCODE: { // hosts with dialog-style windows: the browser's keys only
        if(!browser_) return 0;
        const auto* m=reinterpret_cast<const MSG*>(lp);
        const bool returnOrEsc=m && m->message==WM_KEYDOWN && (m->wParam==VK_RETURN || m->wParam==VK_ESCAPE);
        return DLGC_WANTARROWS|(returnOrEsc?DLGC_WANTMESSAGE:0);
    }
    case WM_KEYDOWN: { // (Alt keys: WM_SYSKEYDOWN, DefWindowProc hands them to the host's window)
        if(keyDown(UINT(wp))) return 0;
        // A key a host sent us without the focus: as before (forwarding it
        // could bounce between us).
        if(!hasKeyFocus()) return DefWindowProcW(window_,msg,wp,lp);
        // Not ours: the keyboard goes back to the host, with this key (posted:
        // the host's message loop translates it and runs its shortcuts).
        const HWND host=hostFocusWindow();
        releaseKeyFocus();
        if(host) PostMessageW(host,msg,wp,lp);
        return 0;
    }
    // The characters of those keys: ours mean nothing, the forwarded ones
    // are translated again from the host's copy.
    case WM_CHAR: case WM_DEADCHAR: return 0;
    case kFieldKeyMessage:keyDown(UINT(wp));return 0;
    case WM_CONTEXTMENU: {
        if(typeMenu_>=0) {closeTypeMenu();return 0;} // like a click outside it
        POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
        if(p.x!=-1 || p.y!=-1) { // -1, -1 = from the keyboard
            POINT client=p;ScreenToClient(window_,&client);
            if(showLibraryMenu(float(client.x)/sx,float(client.y)/sy,p.x,p.y)) return 0;
        }
        showMainMenu(p.x,p.y);return 0;
    }
    default:return DefWindowProcW(window_,msg,wp,lp);
    }
}
} // namespace oxo
