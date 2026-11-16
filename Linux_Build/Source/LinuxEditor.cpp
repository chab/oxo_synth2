// The X11 window of the Linux editor: the panel itself (layout, drawing,
// browser, menus, input) is DesktopEditor/DesktopEditor.cpp, shared with
// Windows. Here: the child window and the host's run loop (events + the
// 50 ms timer), the double-buffered cairo paint, mouse / wheel / keys, the
// write dialog's name field (drawn in the panel), menus and dialogs (never
// modal: they answer from the run loop), the Win32 calls that need the
// window (cursor, keys), the theme editor.
#include "DesktopEditor/DesktopEditor.hpp"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/base/keycodes.h"
#include "base/source/fobject.h"
// X11 last: Xlib's macros (Bool, None, Status...) break the headers above.
#include "LinuxThemeEditor.hpp"
#include "LinuxX11.hpp"
#include <X11/cursorfont.h>

#include <algorithm>
#include <cmath>
#include <cstring>

using namespace Steinberg;
using namespace Gdiplus;

namespace oxo {

// The host's run loop calls these: X events on the display's socket, and
// the editor's 50 ms timer (Windows: WM_TIMER 1).
class LinuxRunLoopHandler final : public Linux::IEventHandler, public Linux::ITimerHandler, public FObject {
public:
    explicit LinuxRunLoopHandler(DesktopEditor* editor) : editor_(editor) {}
    void detach() { editor_ = nullptr; }
    void PLUGIN_API onFDIsSet(Linux::FileDescriptor) override { if(editor_) editor_->onEvents(); }
    void PLUGIN_API onTimer() override { if(editor_) editor_->onTimer(); }
    DELEGATE_REFCOUNT(FObject)
    DEFINE_INTERFACES
        DEF_INTERFACE(Linux::IEventHandler)
        DEF_INTERFACE(Linux::ITimerHandler)
    END_DEFINE_INTERFACES(FObject)
private:
    DesktopEditor* editor_;
};

const char* const DesktopEditor::kUpdatePlatform="linux";

std::wstring DesktopEditor::toWide(const char* utf8) { return lx::Wide(utf8); }
std::string DesktopEditor::toUtf8(const std::wstring& wide) { return lx::Utf8(wide); }

bool DesktopEditor::startGraphics() {
    dialogs_.fallback=[this](const std::string& s){flash(toWide(s.c_str()),5000);};
    return true;
}
void DesktopEditor::endGraphics() {}

tresult PLUGIN_API DesktopEditor::isPlatformTypeSupported(FIDString type) {
    return type && strcmp(type,kPlatformTypeX11EmbedWindowID)==0 ? kResultTrue : kResultFalse;
}

bool DesktopEditor::openWindow() {
    // The host's run loop is the only way to get events and a timer.
    if(!plugFrame || plugFrame->queryInterface(Linux::IRunLoop::iid,reinterpret_cast<void**>(&runLoop_))!=kResultOk
       || !runLoop_) {
        runLoop_=nullptr;
        return false;
    }
    display_=XOpenDisplay(nullptr);
    if(!display_) {runLoop_->release();runLoop_=nullptr;return false;}
    const Window parent=Window(reinterpret_cast<uintptr_t>(systemWindow));
    XSetWindowAttributes a{};
    a.event_mask=ExposureMask|ButtonPressMask|ButtonReleaseMask|PointerMotionMask|LeaveWindowMask|
                 KeyPressMask|StructureNotifyMask|FocusChangeMask;
    a.background_pixmap=None;
    const int width=std::max(1,rect.getWidth()), height=std::max(1,rect.getHeight());
    window_=XCreateWindow(display_,parent,0,0,unsigned(width),unsigned(height),0,CopyFromParent,InputOutput,
                          CopyFromParent,CWEventMask|CWBackPixmap,&a);
    cursors_={XCreateFontCursor(display_,XC_left_ptr),XCreateFontCursor(display_,XC_hand2),
              XCreateFontCursor(display_,XC_X_cursor),XCreateFontCursor(display_,XC_bottom_right_corner),
              XCreateFontCursor(display_,XC_plus)};
    cursorShown_=IDC_ARROW;
    windowSurface_=cairo_xlib_surface_create(display_,window_,DefaultVisual(display_,DefaultScreen(display_)),
                                             width,height);
    dirty_=cairo_region_create();
    XMapWindow(display_,window_);
    XFlush(display_);
    handler_=new LinuxRunLoopHandler(this);
    runLoop_->registerEventHandler(handler_,ConnectionNumber(display_));
    runLoop_->registerTimer(handler_,50);
    return true;
}
void DesktopEditor::closeWindow() {
    nameFieldOpen_=false;
    // An open menu / dialog goes with the window, its `then` never runs.
    menu_.reset();
    menuThen_=nullptr;
    dialogs_.Cancel();
    // The theme editor's window lives on this display connection.
    if(themeEditor_) {themeEditor_->Close();themeEditor_.reset();}
    if(runLoop_) {
        if(handler_) {runLoop_->unregisterTimer(handler_);runLoop_->unregisterEventHandler(handler_);}
        runLoop_->release();runLoop_=nullptr;
    }
    if(handler_) {handler_->detach();handler_->release();handler_=nullptr;}
    if(static_) cairo_surface_destroy(static_);
    if(back_) cairo_surface_destroy(back_);
    static_=back_=nullptr;
    bufferWidth_=bufferHeight_=0;
    if(windowSurface_) {cairo_surface_destroy(windowSurface_);windowSurface_=nullptr;}
    if(dirty_) {cairo_region_destroy(dirty_);dirty_=nullptr;}
    if(display_) {
        if(window_) XDestroyWindow(display_,window_);
        for(auto c:cursors_) if(c) XFreeCursor(display_,c);
        XCloseDisplay(display_);
    }
    cursors_={};
    window_=0;display_=nullptr;
}
void DesktopEditor::layoutChild() {
    if(display_ && window_) {
        const int width=std::max(1,rect.getWidth()), height=std::max(1,rect.getHeight());
        XResizeWindow(display_,window_,unsigned(width),unsigned(height));
        if(windowSurface_) cairo_xlib_surface_set_size(windowSurface_,width,height);
        repaint();
    }
    updateEditControl();
}
void DesktopEditor::repaint() const {
    invalidateDevice(0,0,std::max(1,rect.getWidth()),std::max(1,rect.getHeight()));
}
void DesktopEditor::invalidateDevice(int left,int top,int right,int bottom) const {
    if(!dirty_) return;
    const cairo_rectangle_int_t r{left,top,right-left,bottom-top};
    cairo_region_union_rectangle(dirty_,&r);
}

// ---- the write dialog's name field --------------------------------------------
// Windows: a native EDIT control at the same place. Here it is drawn in the
// panel and typed into from X key events, or the host's onKeyDown. It opens
// holding the patch name, all selected, so typing replaces it.
static const RectF kNameField(300,38,200,19);
void DesktopEditor::updateEditControl() {
    const bool open=browser_ && writing_;
    if(open && !nameFieldOpen_) {
        nameText_=patchName();
        nameCaret_=nameText_.size();
        nameAllSelected_=true;
    }
    nameFieldOpen_=open;
    invalidate(kNameField);
}
std::optional<std::wstring> DesktopEditor::nameFieldText() const {
    if(!nameFieldOpen_) return std::nullopt;
    return nameText_;
}
void DesktopEditor::drawNameField(Graphics& g) {
    if(!nameFieldOpen_ || !visible(kNameField)) return;
    const RectF& r=kNameField;
    cairo_t* cr=g.Context();
    static const FontFamily family(L"Segoe UI");
    cairo_save(cr);
    cairo_rectangle(cr,r.X,r.Y,r.Width,r.Height);
    cairo_set_source_rgb(cr,1,1,1);
    cairo_fill(cr);
    cairo_rectangle(cr,r.X+.5,r.Y+.5,r.Width-1,r.Height-1);
    cairo_set_source_rgb(cr,0.48,0.48,0.48);
    cairo_set_line_width(cr,1);
    cairo_stroke(cr);
    cairo_rectangle(cr,r.X+2,r.Y+1,r.Width-4,r.Height-2);
    cairo_clip(cr);
    cairo_select_font_face(cr,family.Name().c_str(),CAIRO_FONT_SLANT_NORMAL,CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr,11.5);
    cairo_font_extents_t fe;cairo_font_extents(cr,&fe);
    const std::string all=toUtf8(nameText_), before=toUtf8(nameText_.substr(0,nameCaret_));
    cairo_text_extents_t whole,head;
    cairo_text_extents(cr,all.c_str(),&whole);
    cairo_text_extents(cr,before.c_str(),&head);
    const double room=r.Width-8;
    const double shift=head.x_advance>room ? room-head.x_advance : 0.0; // keep the caret in view
    const double x=r.X+4+shift, base=r.Y+(r.Height-fe.height)/2+fe.ascent;
    if(nameAllSelected_ && !nameText_.empty()) {
        cairo_set_source_rgb(cr,0.0,0.47,0.84);
        cairo_rectangle(cr,x,r.Y+3,whole.x_advance,r.Height-6);
        cairo_fill(cr);
        cairo_set_source_rgb(cr,1,1,1);
    } else cairo_set_source_rgb(cr,0,0,0);
    cairo_move_to(cr,x,base);
    cairo_show_text(cr,all.c_str());
    if(!nameAllSelected_) {
        cairo_set_source_rgb(cr,0,0,0);
        cairo_rectangle(cr,x+head.x_advance,r.Y+3,1,r.Height-6);
        cairo_fill(cr);
    }
    cairo_restore(cr);
}
bool DesktopEditor::nameFieldEdit(unsigned long sym,const std::string& utf8,bool ctrl) {
    if(!nameFieldOpen_) return false;
    std::wstring& s=nameText_;
    auto clearSelection=[&] {if(nameAllSelected_) {s.clear();nameCaret_=0;nameAllSelected_=false;}};
    if(ctrl) {
        if(sym==XK_a || sym==XK_A) nameAllSelected_=true;
        else return false;
    } else if(sym==XK_BackSpace) {
        if(nameAllSelected_) clearSelection();
        else if(nameCaret_>0) s.erase(--nameCaret_,1);
    } else if(sym==XK_Delete || sym==XK_KP_Delete) {
        if(nameAllSelected_) clearSelection();
        else if(nameCaret_<s.size()) s.erase(nameCaret_,1);
    } else if(sym==XK_Left || sym==XK_KP_Left) {
        nameCaret_=nameAllSelected_ ? 0 : nameCaret_>0 ? nameCaret_-1 : 0;nameAllSelected_=false;
    } else if(sym==XK_Right || sym==XK_KP_Right) {
        nameCaret_=nameAllSelected_ ? s.size() : std::min(s.size(),nameCaret_+1);nameAllSelected_=false;
    } else if(sym==XK_Home || sym==XK_KP_Home) {nameCaret_=0;nameAllSelected_=false;}
    else if(sym==XK_End || sym==XK_KP_End) {nameCaret_=s.size();nameAllSelected_=false;}
    else if(!utf8.empty() && static_cast<unsigned char>(utf8[0])>=32 && utf8[0]!=127) {
        clearSelection();
        const std::wstring add=toWide(utf8.c_str());
        s.insert(nameCaret_,add);
        nameCaret_+=add.size();
    } else return false;
    invalidate(kNameField);
    return true;
}
// Hosts that keep the keyboard deliver keys through onKeyDown.
bool DesktopEditor::nameFieldKey(char16 key,int16 keyCode,int16 modifiers) {
    if(!nameFieldOpen_) return false;
    unsigned long sym=0;
    std::string utf8;
    switch(keyCode) {
    case KEY_BACK: sym=XK_BackSpace;break;   case KEY_DELETE: sym=XK_Delete;break;
    case KEY_LEFT: sym=XK_Left;break;        case KEY_RIGHT: sym=XK_Right;break;
    case KEY_HOME: sym=XK_Home;break;        case KEY_END: sym=XK_End;break;
    default:
        if((modifiers&kControlKey) && (key==u'a' || key==u'A')) sym=XK_a;
        else if(key>=32 && !(modifiers&kControlKey)) utf8=toUtf8(std::wstring(1,wchar_t(key)));
    }
    return (sym || !utf8.empty()) && nameFieldEdit(sym,utf8,(modifiers&kControlKey)!=0);
}

// ---- dialogs and the desktop -------------------------------------------------------
// zenity / kdialog, started and left running: Dialogs::Poll (onTimer)
// hands their answer to `then`. Meanwhile the panel's input waits (modal).
HCURSOR DesktopEditor::copyCursor() { return IDC_COPY; } // X's plus
static lx::Dialogs::Then wideThen(std::function<void(const std::wstring&)> then) {
    return [then](const std::string& s){if(then) then(lx::Wide(s.c_str()));};
}
void DesktopEditor::messageBox(const std::wstring& text,const wchar_t* title,UINT flags,std::function<void(int)> then) {
    const bool yesNo=(flags&MB_YESNO)!=0;
    dialogs_.Message(toUtf8(title),toUtf8(text),(flags&0xF0)==MB_ICONWARNING,yesNo,[yesNo,then](bool yes) {
        if(then) then(yesNo ? (yes ? IDYES : IDNO) : IDOK);
    });
}
void DesktopEditor::askText(const wchar_t* title,const wchar_t* label,const std::wstring& text,
                            std::function<void(const std::wstring&)> then) {
    dialogs_.AskText(toUtf8(title),toUtf8(label),toUtf8(text),wideThen(std::move(then)));
}
void DesktopEditor::chooseFile(FileDialog kind,std::function<void(const std::wstring&)> then) {
    switch(kind) {
    case FileDialog::ImportFile:
        dialogs_.OpenFile("Import Synth1 presets","Synth1 presets / ZIP banks","*.sy1 *.SY1 *.zip *.ZIP",wideThen(std::move(then)));
        break;
    case FileDialog::OpenPatch:
        dialogs_.OpenFile("Load patch file","Synth1 presets","*.sy1 *.SY1",wideThen(std::move(then)));
        break;
    case FileDialog::SavePatch:
        dialogs_.SaveFile("Save patch file","Synth1 presets","*.sy1 *.SY1","sy1",wideThen(std::move(then)));
        break;
    }
}
// Finder.reveal: the file manager shows the item (FileManager1), else its folder.
void DesktopEditor::revealInExplorer(const std::string& utf8Path) const {
    lx::Reveal(utf8Path);
}
void DesktopEditor::pickFolder(const wchar_t* title,std::function<void(const std::wstring&)> then) {
    dialogs_.PickFolder(toUtf8(title),wideThen(std::move(then)));
}
bool DesktopEditor::startUpdateCheck(const std::string& url) {
    return updateCheck_.Start(url); // curl; onTimer sees it done
}

void DesktopEditor::ThemeEditorDeleter::operator()(ThemeEditorWindow* w) const { delete w; }
void DesktopEditor::showThemeEditor() {
    if(!display_) return;
    if(!themeEditor_) themeEditor_.reset(new ThemeEditorWindow);
    ThemeEditorWindow::Host host{[this]{return theme_;},
                                 [this](const themes::Palette& p){setTheme(p,true);paint();},
                                 []()->themes::Store&{return themeStore();},
                                 []{return themeFiles();},
                                 [this](HMENU m,int x,int y,std::function<void(int)> then){popupMenu(m,x,y,std::move(then));},
                                 &dialogs_};
    themeEditor_->Show(display_,window_,std::move(host));
}
void DesktopEditor::refreshThemeEditor() {
    if(themeEditor_) themeEditor_->Refresh();
}
void DesktopEditor::closeThemeEditor() {
    if(themeEditor_) themeEditor_->Close();
}

// ---- popup menus --------------------------------------------------------------------
// Open, grabbing the pointer and keyboard, and fed by onEvents until done:
// no loop of ours, the host's UI thread runs on (TrackPopupMenu's modal
// loop keeps the host's windows alive on Windows; X has no such thing).
void DesktopEditor::MenuDeleter::operator()(lx::MenuTracker* m) const { delete m; }
void DesktopEditor::popupMenu(HMENU m,int x,int y,std::function<void(int)> then) {
    std::unique_ptr<lx::Menu> menu(m);
    if(!display_ || menu_ || !menu || menu->items.empty()) {if(then) then(0);return;}
    menu_.reset(new lx::MenuTracker(display_,std::move(menu),x,y));
    menuThen_=std::move(then);
}
void DesktopEditor::finishMenu() {
    const int choice=menu_ ? menu_->Result() : 0;
    auto then=std::move(menuThen_);
    menuThen_=nullptr;
    menu_.reset(); // ungrabbed, its windows gone
    paint();
    if(then) then(choice);
}
bool DesktopEditor::modal() const { return menu_ || dialogs_.Busy(); }

// ---- the Win32 calls that need the window ---------------------------------------
short DesktopEditor::GetKeyState(UINT key) const {
    unsigned mask=0;
    switch(key) {
    case VK_SHIFT: mask=ShiftMask;break;
    case VK_CONTROL: mask=ControlMask;break;
    case VK_MENU: mask=Mod1Mask;break;
    case VK_LWIN: mask=Mod4Mask;break;
    default: break;
    }
    return (modifiers_&mask) ? short(0x8000) : short(0);
}
void DesktopEditor::SetCursor(HCURSOR c) {
    if(!display_ || !window_ || c==cursorShown_) return;
    cursorShown_=c;
    XDefineCursor(display_,window_,cursors_[size_t(c)]);
}

// ---- painting ---------------------------------------------------------------------
// As the Windows paint: the static layer is drawn once (theme / size); each
// dirty rectangle copies it, draws the dynamic parts over it in the back
// buffer and goes to the window.
void DesktopEditor::paint() {
    if(!display_ || !window_ || !windowSurface_ || !dirty_ || cairo_region_is_empty(dirty_)) return;
    const int width=rect.getWidth(), height=rect.getHeight();
    if(width<=0 || height<=0) return;
    if(!static_ || !back_ || width!=bufferWidth_ || height!=bufferHeight_) {
        if(static_) cairo_surface_destroy(static_);
        if(back_) cairo_surface_destroy(back_);
        static_=cairo_image_surface_create(CAIRO_FORMAT_RGB24,width,height);
        back_=cairo_image_surface_create(CAIRO_FORMAT_RGB24,width,height);
        bufferWidth_=width;bufferHeight_=height;
        staticDirty_=true;
    }
    const float sx=float(width)/kDesignWidth, sy=float(height)/kDesignHeight;
    auto prepare=[&](Graphics& g) {
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.ScaleTransform(sx,sy);
    };
    const RectF full(0,0,kDesignWidth,kDesignHeight);
    if(staticDirty_) {
        cairo_t* cr=cairo_create(static_);
        {Graphics g(cr);prepare(g);paintClip_=full;drawStatic(g);}
        cairo_destroy(cr);
        staticDirty_=false;
    }
    cairo_region_t* todo=dirty_;
    dirty_=cairo_region_create();
    const cairo_rectangle_int_t all{0,0,width,height};
    cairo_region_intersect_rectangle(todo,&all);
    cairo_t* back=cairo_create(back_);
    cairo_t* screen=cairo_create(windowSurface_);
    cairo_set_operator(screen,CAIRO_OPERATOR_SOURCE);
    for(int i=0,n=cairo_region_num_rectangles(todo);i<n;i++) {
        cairo_rectangle_int_t r;
        cairo_region_get_rectangle(todo,i,&r);
        cairo_save(back);
        cairo_rectangle(back,r.x,r.y,r.width,r.height);
        cairo_clip(back);
        cairo_set_operator(back,CAIRO_OPERATOR_SOURCE);
        cairo_set_source_surface(back,static_,0,0);
        cairo_paint(back);
        cairo_set_operator(back,CAIRO_OPERATOR_OVER);
        {
            Graphics g(back);
            prepare(g);
            paintClip_=RectF(float(r.x)/sx-1.f,float(r.y)/sy-1.f,float(r.width)/sx+2.f,float(r.height)/sy+2.f);
            drawDynamic(g);
            drawNameField(g);
        }
        cairo_restore(back);
        cairo_surface_flush(back_);
        cairo_save(screen);
        cairo_rectangle(screen,r.x,r.y,r.width,r.height);
        cairo_clip(screen);
        cairo_set_source_surface(screen,back_,0,0);
        cairo_paint(screen);
        cairo_restore(screen);
    }
    cairo_destroy(back);
    cairo_destroy(screen);
    cairo_region_destroy(todo);
    paintClip_=full;
    cairo_surface_flush(windowSurface_);
    XFlush(display_);
}

// ---- events -----------------------------------------------------------------------
// While a menu is open, its events (and all input: its grab) are its own;
// while a dialog is open, the panel only paints.
void DesktopEditor::onEvents() {
    while(display_ && XPending(display_)) {
        XEvent e;
        XNextEvent(display_,&e);
        if(menu_ && menu_->Handle(e)) {
            if(menu_->Done()) finishMenu();
            continue;
        }
        handleEvent(&e,!modal());
    }
    paint();
}
void DesktopEditor::onTimer() {
    if(!display_) return;
    dialogs_.Poll(); // a closed dialog answers
    tick();
    if(!modal() && updateCheck_.Done()) updateArrived(); // its message box waits for an open one
    onEvents(); // also paints
}
// The Windows editor's windowMessage, for X events.
void DesktopEditor::handleEvent(void* event,bool input) {
    XEvent& e=*static_cast<XEvent*>(event);
    if(themeEditor_ && themeEditor_->Owns(e)) {
        if(input || e.type==Expose) themeEditor_->Handle(e);
        return;
    }
    if(e.xany.window!=window_) return;
    const float sx=float(std::max(1,rect.getWidth()))/kDesignWidth;
    const float sy=float(std::max(1,rect.getHeight()))/kDesignHeight;
    if(e.type==Expose) {
        invalidateDevice(e.xexpose.x,e.xexpose.y,e.xexpose.x+e.xexpose.width,e.xexpose.y+e.xexpose.height);
        return;
    }
    if(!input) return;
    // WM_SETCURSOR: a hand over what reacts to a click, the resize arrow over the grip.
    auto updateCursor=[&](int px,int py) {
        if(press_.active && press_.dragging) {SetCursor(dragCursor());return;}
        const float dx=float(px)/sx, dy=float(py)/sy;
        if(hoveredChrome_!=0 || hoveredControl_>=0 || isVersionBox(dx,dy) ||
           (midiLearn_ && !browser_ && learnHit(dx,dy)>=0)) {SetCursor(IDC_HAND);return;}
        const int gripW=std::max(8,int(std::lround(22.f*sx))), gripH=std::max(8,int(std::lround(22.f*sy)));
        if(px>=rect.getWidth()-gripW && py>=rect.getHeight()-gripH) {SetCursor(IDC_SIZENWSE);return;}
        SetCursor(IDC_ARROW);
    };
    switch(e.type) {
    case ButtonPress: {
        rootX_=e.xbutton.x_root;rootY_=e.xbutton.y_root;modifiers_=e.xbutton.state;
        const float x=float(e.xbutton.x)/sx, y=float(e.xbutton.y)/sy;
        if(e.xbutton.button==Button1) {
            // WM_LBUTTONDBLCLK: the second press of a quick pair at the same place
            const bool twice=!lastWasDouble_ && e.xbutton.time-lastClickTime_<400 &&
                             std::abs(e.xbutton.x-lastClickX_)<5 && std::abs(e.xbutton.y-lastClickY_)<5;
            lastWasDouble_=twice;
            lastClickTime_=e.xbutton.time;lastClickX_=e.xbutton.x;lastClickY_=e.xbutton.y;
            XSetInputFocus(display_,window_,RevertToParent,CurrentTime);
            mouseDown(x,y,twice);
        } else if(e.xbutton.button==Button3) { // WM_CONTEXTMENU
            if(typeMenu_>=0) {closeTypeMenu();break;} // like a click outside it
            if(!showLibraryMenu(x,y,int(rootX_),int(rootY_))) showMainMenu(int(rootX_),int(rootY_));
        } else if(e.xbutton.button==Button4 || e.xbutton.button==Button5) {
            wheel(e.xbutton.button==Button4 ? 8 : -8,x,y); // one notch = WHEEL_DELTA: eight rows
        }
        break;
    }
    case ButtonRelease:
        rootX_=e.xbutton.x_root;rootY_=e.xbutton.y_root;modifiers_=e.xbutton.state;
        if(e.xbutton.button==Button1) mouseUp();
        break;
    case MotionNotify: {
        XEvent next;
        while(XCheckTypedWindowEvent(display_,window_,MotionNotify,&next)) e=next; // the latest only
        rootX_=e.xmotion.x_root;rootY_=e.xmotion.y_root;modifiers_=e.xmotion.state;
        mouseMove(float(e.xmotion.x)/sx,float(e.xmotion.y)/sy);
        updateCursor(e.xmotion.x,e.xmotion.y);
        break;
    }
    case LeaveNotify: // WM_MOUSELEAVE
        if(e.xcrossing.state&Button1Mask) break; // a drag left the window: it goes on
        hoverChanged(-1,0);
        if(typeMenuHover_>=0) {typeMenuHover_=-1;invalidateTypeMenu();}
        break;
    case KeyPress: {
        modifiers_=e.xkey.state;
        char buffer[32]={};
        KeySym sym=0;
        const int n=XLookupString(&e.xkey,buffer,sizeof buffer-1,&sym,nullptr);
        std::string utf8;
        for(int i=0;i<n;i++) { // Latin-1 -> UTF-8
            const unsigned char c=static_cast<unsigned char>(buffer[i]);
            if(c<0x80) utf8+=char(c); else {utf8+=char(0xC0|(c>>6));utf8+=char(0x80|(c&63));}
        }
        if(nameFieldEdit(sym,utf8,(e.xkey.state&ControlMask)!=0)) break;
        UINT vk=0;
        switch(sym) {
        case XK_Escape: vk=VK_ESCAPE;break;
        case XK_Left: case XK_KP_Left: vk=VK_LEFT;break;
        case XK_Right: case XK_KP_Right: vk=VK_RIGHT;break;
        case XK_Up: case XK_KP_Up: vk=VK_UP;break;
        case XK_Down: case XK_KP_Down: vk=VK_DOWN;break;
        case XK_Return: case XK_KP_Enter: vk=VK_RETURN;break;
        default: break;
        }
        if(vk) keyDown(vk);
        break;
    }
    default: break;
    }
}
} // namespace oxo
