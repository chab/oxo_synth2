#include "WindowsEditor.hpp"
#include "OxoSynth2VST3/source/OxoSynth2Controller.h"
#include "pluginterfaces/gui/iplugview.h"
#include "OxoSynth2Identity.h"
#include "EditorCore/Storage.hpp"
#include "WindowsZoomSettings.hpp"
#include "WindowsFavourites.hpp"
#include "WindowsThemes.hpp"
#include "WindowsThemeEditor.hpp"
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
#include <sstream>
#include <string_view>

using namespace Steinberg;
using namespace Gdiplus;

namespace oxo {
namespace {
constexpr float kDesignWidth = 770.f, kDesignHeight = 560.f;
constexpr int kMinWidth = 578;  // 75% of the 770-pixel design width, rounded up
constexpr int kMaxWidth = 2310; // 300% of the design width
constexpr float kPi = 3.14159265358979323846f;
constexpr int kPanel=0, kHeader=1, kHeaderText=2, kLabel=3, kKnob=4, kKnobHighlight=5,
              kDial=6, kButton=7, kLed=8, kRadio=9, kDisplayText=10, kDisplayTop=11, kDisplayBottom=12,
              kVoiceGridBorder=13, kVoiceGridEmpty=14, kVoiceGridPlaying=15, kVoiceGridReleased=16;
constexpr const wchar_t kWindowsDevBuild[] = L"19.3";

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
// Draw text as antialiased outlines in design-space coordinates. Unlike
// grid-fitted glyph bitmaps, the letter positions do not snap to different
// pixels while the VST3 view is resized. Using actual ink bounds also centres
// short LCD values vertically instead of centring the larger font line box.
// Fixed-size, natural-proportion text. Long labels are ellipsized instead
// of shrinking the font. A shared ascender/descender reference keeps each
// line's baseline stable even when its characters contain p, g, j, or y.
void text(Graphics& g, const std::wstring& s, float x,float y,float w,float h, float size,
          Color c, bool bold=false, bool centred=false, bool mono=false) {
    if (s.empty() || w <= 2.f || h <= 1.f) return;
    (void)bold; // Match the current Windows theme: all panel text uses bold.
    const FontFamily family(mono ? L"Consolas" : L"Segoe UI");
    StringFormat format;
    format.SetFormatFlags(StringFormatFlagsNoWrap | StringFormatFlagsNoClip);
    // Font size is chosen by the control, NEVER reduced to fit the string.
    const float fontSize = size < 13.f ? std::max(10.8f, size * 1.08f) : size;
    auto makePath = [&](const std::wstring& string, GraphicsPath& path,
                        RectF& bounds) -> bool {
        if (path.AddString(string.c_str(), -1, &family, FontStyleBold,
                           fontSize, PointF(0.f,0.f), &format) != Ok) return false;
        return path.GetBounds(&bounds) == Ok;
    };

    GraphicsPath path;
    RectF ink{};
    if (!makePath(s, path, ink)) return;
    const float maxInkWidth = w - 2.f;
    if (ink.Width > maxInkWidth) {
        // Reserve the full-size ellipsis and find the longest fitting prefix.
        const std::wstring ellipsis(1, wchar_t(0x2026));
        GraphicsPath dots;
        RectF dotBounds{};
        if (!makePath(ellipsis, dots, dotBounds) || dotBounds.Width > maxInkWidth) return;
        size_t low=0, high=s.size();
        while (low<high) {
            const size_t mid=low+(high-low+1)/2;
            const std::wstring trial=s.substr(0,mid)+ellipsis;
            GraphicsPath probe;
            RectF probeBounds{};
            if (!makePath(trial,probe,probeBounds)) return;
            if (probeBounds.Width <= maxInkWidth) low=mid;
            else high=mid-1;
        }
        std::wstring shortened=s.substr(0,low);
        // Windows wchar_t is UTF-16; don't cut a surrogate pair in half.
        if (!shortened.empty() && shortened.back()>=wchar_t(0xD800) &&
            shortened.back()<=wchar_t(0xDBFF)) shortened.pop_back();
        shortened+=ellipsis;
        path.Reset();
        if (!makePath(shortened,path,ink)) return;
    }

    GraphicsPath reference;
    RectF metrics{};
    if (!makePath(L"Agjpqy",reference,metrics)) return;
    const float left = centred ? x+(w-ink.Width)*.5f : x+1.f;
    const float top = y+(h-metrics.Height)*.5f;
    Matrix translate;
    translate.Translate(left-ink.X,top-metrics.Y);
    path.Transform(&translate);
    SolidBrush brush(c);
    const GraphicsState saved=g.Save();
    g.SetClip(RectF(x,y,w,h),CombineModeIntersect);
    g.FillPath(&brush,&path);
    g.Restore(saved);
}

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
// A compact parser for PatchManager::Json(). Only fields used by the browser
// are consumed, all other JSON values are skipped. No third-party dependency.
struct Reader {
    const std::string& s;
    size_t p=0;
    void ws() { while(p<s.size() && (s[p]==' '||s[p]=='\n'||s[p]=='\r'||s[p]=='\t')) ++p; }
    bool eat(char c) { ws(); if(p<s.size() && s[p]==c) {++p; return true;} return false; }
    static void appendUtf8(std::string& out, unsigned v) {
        if (v<0x80) out+=char(v);
        else if(v<0x800) {out+=char(0xc0|(v>>6));out+=char(0x80|(v&63));}
        else {out+=char(0xe0|(v>>12));out+=char(0x80|((v>>6)&63));out+=char(0x80|(v&63));}
    }
    bool str(std::string& out) {
        out.clear(); if(!eat('"')) return false;
        while(p<s.size()) {
            char c=s[p++]; if(c=='"') return true;
            if(c!='\\') {out+=c;continue;}
            if(p==s.size()) return false;
            char e=s[p++];
            switch(e) {
                case '"': out+='"';break; case '\\':out+='\\';break;
                case '/':out+='/';break;case 'n':out+='\n';break;case 'r':out+='\r';break;case 't':out+='\t';break;
                case 'u': {
                    if(p+4>s.size()) return false;
                    unsigned u=0;
                    for(int i=0;i<4;i++) {char d=s[p++];u<<=4;
                        if(d>='0'&&d<='9') u+=d-'0';
                        else if(d>='A'&&d<='F') u+=d-'A'+10;
                        else if(d>='a'&&d<='f') u+=d-'a'+10;
                        else return false;
                    }
                    appendUtf8(out,u);break;
                }
                default:return false;
            }
        }
        return false;
    }
    bool skip() {
        ws(); if(p>=s.size()) return false;
        if(s[p]=='"') {std::string unused;return str(unused);}
        if(eat('{')) {
            if(eat('}')) return true;
            do {std::string key;if(!str(key)||!eat(':')||!skip())return false;} while(eat(','));
            return eat('}');
        }
        if(eat('[')) {
            if(eat(']')) return true;
            do {if(!skip())return false;} while(eat(','));
            return eat(']');
        }
        while(p<s.size() && s[p]!=',' && s[p]!=']' && s[p]!='}' && s[p]!=' ' && s[p]!='\n') ++p;
        return true;
    }
};

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
    GdiplusStartupInput gdiplusInput;
    GdiplusStartup(&gdiplusToken_,&gdiplusInput,nullptr);
    constructLayout();
    wintheme::CreateMissing(storage::Root()); // seeds seven editable factory files once
    applyTheme(0);
    readTheme();
    favouritesReadable_=winfav::Load(storage::Root(),favourites_);
}
WindowsEditor::~WindowsEditor() {
    if(themeEditor_)themeEditor_->Close();
    removedFromParent();
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
void WindowsEditor::attachedToParent() {
    if(window_ || !systemWindow) return;
    static const wchar_t* name=L"OxoSynth2WindowsEditorView";
    static const bool registered=[] {
        WNDCLASSW wc{};
        wc.style=CS_DBLCLKS;
        wc.lpfnWndProc=&WindowsEditor::wndProc;
        wc.hInstance=GetModuleHandleW(nullptr);
        wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
        wc.lpszClassName=L"OxoSynth2WindowsEditorView";
        return RegisterClassW(&wc)!=0 || GetLastError()==ERROR_CLASS_ALREADY_EXISTS;
    }();
    if(!registered) return;
    window_=CreateWindowExW(0,name,L"",WS_CHILD|WS_VISIBLE|WS_CLIPSIBLINGS,
                          0,0,rect.getWidth(),rect.getHeight(),
                          reinterpret_cast<HWND>(systemWindow),nullptr,GetModuleHandleW(nullptr),this);
    if(window_) {
        SetTimer(window_,1,50,nullptr);
        // Critical: Json() and loadedProgram() call EnsureScanned(), which
        // scans all banks synchronously. Never call them before a background
        // scan has produced a usable revision (large libraries took ~1 min).
        const int revision=hooks_.libraryRevision ? hooks_.libraryRevision(hooks_.ctx) : 0;
        if(revision>0) { knownRevision_=revision; refreshBanks(); }
        // No index yet: build one asynchronously. With a saved index, avoid
        // traversing every preset on every startup; Refresh is explicit.
        else if(hooks_.startLibraryScan) hooks_.startLibraryScan(hooks_.ctx);
    }
}
void WindowsEditor::removedFromParent() {
    if(editName_) {DestroyWindow(editName_);editName_=nullptr;}
    if(window_) {KillTimer(window_,1); DestroyWindow(window_);window_=nullptr;}
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
std::wstring WindowsEditor::readout() const {
    if(selectedId_ < 0) return L"";
    std::wstring out = toWide(hooks_.displayName ? hooks_.displayName(hooks_.ctx,selectedId_) : "");
    out+=L":  "; out+=formatted(selectedId_);
    return out;
}

void WindowsEditor::knob(int id,const wchar_t* name,float sx,float sy,float x,float y,float size) {
    // KnobView's SwiftUI stack is centred at y+6.5, but its circular knob
    // centre is y; the label sits underneath. Avoid an extra 6.5 px offset.
    controls_.push_back({Kind::Knob,id,0,sx+x-size*.65f,sy+y-size*.65f,size*1.3f,size*1.65f,name});
}
void WindowsEditor::button(int id,const wchar_t* name,Kind kind,float sx,float sy,float x,float y,float width) {
    controls_.push_back({kind,id,0,sx+x-width/2,sy+y-8.5f,width,17,name});
}
void WindowsEditor::display(int id,float sx,float sy,float x,float y,float width) {
    controls_.push_back({Kind::Display,id,0,sx+x-width/2,sy+y-8.5f,width,17,L""});
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
            controls_.push_back({Kind::Radio,id,first+i,px-22.f,py-6.f,
                                 44.f,12.f,choices[size_t(i)],0});
        } else if(id==71) {
            // Oscillator modulation destinations: retain full 'osc2' text
            // without shrinking the font; spread the three choices evenly.
            controls_.push_back({Kind::Radio,id,first+i,px-22.f,py-6.f,
                                 44.f,12.f,choices[size_t(i)],0});
        } else if(id==97) {
            // Sub octave switches need a full-width label at the same font
            // size; never shrink '-1oct' just because it is longer.
            controls_.push_back({Kind::Radio,id,first+i,
                                 sx+x-42.f+float(i)*54.f,sy+y-6.f,
                                 50.f,12.f,choices[size_t(i)],0});
        } else {
            const float width=iconOnly ? 14.f : horizontal ? step : 65.f;
            controls_.push_back({Kind::Radio,id,first+i,
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
        controls_.push_back({Kind::Wave,id,values[i],cx-12.f,cy-7.5f,
                             24.f,15.f,L"",shapes[i]});
    }
}
void WindowsEditor::label(float x,float y,const wchar_t* name) {
    controls_.push_back({Kind::Label,-1,0,x-40,y-8,80,16,name});
}
void WindowsEditor::badge(float x,float y,const wchar_t* name,int id) {
    controls_.push_back({Kind::Badge,id,0,x-8,y-8,16,16,name});
}

void WindowsEditor::constructLayout() {
    // Layout transcribed from Shared/SynthPanelView.swift, using each section's
    // original local coordinate system. Every visible Synth1 parameter keeps
    // its original ID and is driven by OxoSynth2Hooks.
    constexpr float ox=6,oy=34,lx=6,ly=278,mx=6,my=412,ax=324,ay=34,fx=324,fy=118,
                    arx=324,ary=278,ex=548,ey=34,qx=548,qy=118,dx=548,dy=180,cx=548,cy=242,
                    vx=548,vy=304;
    badge(ox+14,oy+36,L"1"); // Leave the oscillator on/off button fixed.
    // Move the OSC1 / Sub row down 4px to align the knobs with Amplifier.
    waves(0,ox,oy,54,44,{0,1,2,3},{0,1,2,3});
    knob(76,L"det",ox,oy,104,40);knob(45,L"FM",ox,oy,146,40);
    label(ox+194,oy+24,L"sub");
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
    badge(lx+14,ly+30,L"1",57);badge(lx+298,ly+30,L"2",58);
    controls_.push_back({Kind::Wave,42,0,lx+42-13,ly+30-8,26,17,L"",0});
    controls_.push_back({Kind::Wave,47,0,lx+270-13,ly+30-8,26,17,L"",0});
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
    label(mx+32,my+28,L"p.b range"); display(40,mx,my,32,46,30);
    label(mx+82,my+30,L"src1");label(mx+82,my+51,L"src2");
    display(86,mx,my,158,30,120);display(88,mx,my,158,51,120);
    label(mx+228,my+30,L"->");label(mx+228,my+51,L"->");
    display(87,mx,my,298,30,120);display(89,mx,my,298,51,120);
    knob(50,L"sens1",mx,my,400,34,22);knob(51,L"sens2",mx,my,446,34,22);
    knob(25,L"A",ax,ay,20,40);knob(26,L"D",ax,ay,55,40);knob(27,L"S",ax,ay,90,40);
    knob(28,L"R",ax,ay,125,40);knob(29,L"gain",ax,ay,162,40);knob(30,L"vel",ax,ay,198,40);
    knob(15,L"A",fx,fy,20,44);knob(16,L"D",fx,fy,52,44);knob(17,L"S",fx,fy,84,44);
    knob(18,L"R",fx,fy,116,44);knob(21,L"amt",fx,fy,150,44);
    // Align Filter type with the upper Amount knob, shifting its LED list as one group.
    button(14,L"type",Kind::Cycle,fx,fy,196,44,36);
    radio(14,fx,fy,196,96,{L"LP12",L"LP24",L"HP12",L"BP12",L"LPDL"},0);
    // Bottom Filter knobs align with the Oscillator Phase / Tune row.
    knob(19,L"frq",fx,fy,20,117);knob(20,L"res",fx,fy,52,117);
    knob(23,L"sat",fx,fy,84,117);knob(22,L"trk",fx,fy,116,117);
    // Velocity toggle sits immediately above Track, not beside the bottom knob row.
    button(24,L"vel",Kind::Toggle,fx,fy,116,84,36);
    button(59,L"ON",Kind::Toggle,arx,ary,28,30,44);
    button(31,L"type",Kind::Cycle,arx,ary,78,30);
    button(32,L"range",Kind::Cycle,arx,ary,128,30,44);
    radio(31,arx,ary,40,78,{L"updown",L"up",L"down",L"random"},1);
    radio(32,arx,ary,126,78,{L"1oct",L"2oct",L"3oct",L"4oct"},0);
    knob(33,L"beat",arx,ary,190,36);knob(34,L"gate",arx,ary,190,88);
    // Effect uses the same stacked ON/type layout as Delay and Chorus.
    button(77,L"ON",Kind::Toggle,ex,ey,25,31,40);
    display(78,ex,ey,25,53,40); // +2px additional separation from ON.
    knob(79,L"ctl1",ex,ey,108,40);knob(80,L"ctl2",ex,ey,150,40);knob(81,L"level",ex,ey,192,40);
    knob(61,L"freq",qx,qy,24,31,22);knob(62,L"level",qx,qy,66,31,22);
    knob(63,L"Q",qx,qy,108,31,22);knob(60,L"tone",qx,qy,150,31,22);knob(90,L"L-R",qx,qy,192,31,22);
    // Stack the ON switch over its type display, matching the available
    // 58px section height; the free horizontal space gives each FX knob
    // a full, readable label and non-overlapping indicator dots.
    // Leave a visible gap below the section header and between the two rows.
    button(65,L"ON",Kind::Toggle,dx,dy,23,27,40);
    display(82,dx,dy,23,47,40); // Keep type display fixed.
    // Five knobs retain their horizontal spacing; move down 1px.
    knob(35,L"time",dx,dy,68,31,22);knob(83,L"sprd",dx,dy,100,31,22);
    knob(36,L"fdbk",dx,dy,132,31,22);knob(98,L"tone",dx,dy,164,31,22);knob(37,L"d/w",dx,dy,196,31,22);
    button(66,L"ON",Kind::Toggle,cx,cy,23,27,40);
    display(64,cx,cy,23,47,40); // Keep type display fixed.
    knob(52,L"time",cx,cy,68,31,22);knob(53,L"depth",cx,cy,100,31,22);
    knob(54,L"rate",cx,cy,132,31,22);knob(55,L"fdbk",cx,cy,164,31,22);knob(56,L"level",cx,cy,196,31,22);
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
    knob(99,L"vol",6,484,19,29,26);
}

void WindowsEditor::applyTheme(int index) {
    const int i=std::clamp(index,0,6);
    // Use the saved file even for the default theme. If its location is not
    // writable on first run, keep the original built-in palette as a fallback.
    wintheme::Palette colors=wintheme::Originals[static_cast<size_t>(i)];
    wintheme::Read(wintheme::File(storage::Root(),i),colors);
    for(size_t k=0;k<palette_.c.size();k++) palette_.c[k]=rgb(int(colors[k]));
    repaint();
}
bool WindowsEditor::selectTheme(int index) {
    const auto entries=wintheme::List(storage::Root());
    if(index<0 || index>=static_cast<int>(entries.size()))return false;
    wintheme::Palette colors{};
    const auto file=entries[size_t(index)].file;
    if(!wintheme::Read(file,colors)) {
        // Do not overwrite an invalid edited file or silently ignore it.
        MessageBoxW(window_,
            (L"Could not load theme file:\n"+file.wstring()+
             L"\n\nCheck that it contains all original colours as valid #RRGGBB values.").c_str(),
            L"Synth2 Themes",MB_OK|MB_ICONWARNING);
        return false;
    }
    for(size_t k=0;k<palette_.c.size();k++)palette_.c[k]=rgb(int(colors[k]));
    currentThemeIndex_=index;
    // Preserve the existing session-state format (for DAW projects) exactly.
    // The theme file is the source when choosing a theme; the current patch
    // continues using the existing bridge until a different file is selected.
    const std::string state=wintheme::ToJSON(colors);
    if(hooks_.setThemeText)hooks_.setThemeText(hooks_.ctx,state.c_str());
    repaint();
    return true;
}
void WindowsEditor::readTheme() {
    if(!hooks_.themeText) return;
    std::string state=hooks_.themeText(hooks_.ctx);
    for(size_t i=0;i<wintheme::Keys.size();i++) {
        const std::string key="\""+std::string(wintheme::Keys[i])+"\"";
        const size_t p=state.find(key);
        if(p==std::string::npos) continue;
        const size_t hash=state.find('#',p+key.size());
        if(hash==std::string::npos || hash>p+key.size()+9 || hash+7>state.size()) continue;
        unsigned v=0;
        if(std::sscanf(state.c_str()+hash+1,"%6x",&v)==1) palette_.c[i]=rgb(int(v));
    }
}

void WindowsEditor::drawControl(Graphics& g,const Control& c,bool hovered) {
    const auto& p=palette_.c;
    switch(c.kind) {
    case Kind::Label:
        text(g,c.label,c.x,c.y,c.w,c.h,9.5f,p[kLabel],false,true);
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
        const float size=c.w/1.3f;
        const float x=c.x+c.w/2, y=c.y+size*.65f;
        int low=0,high=127,def=64;
        if(hooks_.paramInfo) hooks_.paramInfo(hooks_.ctx,c.id,&low,&high,&def);
        const float frac=std::clamp((float(value(c.id))-float(low))/float(std::max(1,high-low)),0.f,1.f);
        // macOS theme.knobTicks is label ink composited at 30% opacity over
        // the panel; the previous code used a near-black header colour.
        // Solid medium-grey tick dots, without the dark LED outline.
        const SolidBrush dots(rgb(0x6E6E6E));
        for(int i=0;i<11;i++) {
            const float a=(-140+i*28-90)*kPi/180;
            // Five closely-spaced FX knobs have 27px centre spacing.
            // Keep their dot rings distinct without moving the panel geometry.
            const float dotRing=size<=22.f ? size*.65f-2.3f : size*.65f-1.5f;
            const float cx=x+std::cos(a)*dotRing;
            const float cy=y+std::sin(a)*dotRing;
            g.FillEllipse(&dots,cx-.8f,cy-.8f,1.6f,1.6f);
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
        Pen rim(blend(p[kPanel],p[kLabel],.25f),1.f);
        g.DrawEllipse(&rim,x-radius,y-radius,size,size);
        // SwiftUI uses a short capsule near the rim, not a red line from
        // the knob's centre. Match its -140 to +140 degree travel.
        const float angle=(-140+280*frac-90)*kPi/180;
        const float dx=std::cos(angle), dy=std::sin(angle);
        // The supplied Mac reference uses the red Synth1-style indicator.
        // Respect other custom dial colours; remap only the factory white dial.
        const bool factoryWhite = p[kDial].GetR()==255 && p[kDial].GetG()==255 && p[kDial].GetB()==255;
        Pen hand(factoryWhite ? rgb(0xFF2D2D) : p[kDial],2.f);
        hand.SetStartCap(LineCapRound);hand.SetEndCap(LineCapRound);
        g.DrawLine(&hand,PointF(x+dx*radius*.18f,y+dy*radius*.18f),
                   PointF(x+dx*radius*.84f,y+dy*radius*.84f));
        text(g,c.label,x-25,y+radius+1,50,12,9.5f,p[kLabel],false,true);
        if(midiLearn_ && c.id<99) {
            Pen frame(rgb(0x59A7E8),1.4f);
            g.DrawEllipse(&frame,x-radius-4,y-radius-4,size+8,size+8);
        }
        return;
    }
    case Kind::Toggle:
    case Kind::Cycle: {
        darkButton(g,p[kButton],c.x,c.y,c.w,c.h,hovered);
        if(c.kind==Kind::Toggle) {
            const Color on=value(c.id)!=0?p[kLed]:muted(p[kLed],.28f);
            led(g,on,c.x+8,c.y+c.h/2,2.8f,value(c.id)!=0);
        }
        text(g,c.label,c.x+(c.kind==Kind::Toggle?12.f:0.f),c.y,
             c.w-(c.kind==Kind::Toggle?14.f:0.f),c.h,10.5f,rgb(0xE8E8E8),true,true);
        return;
    }
    case Kind::Display: {
        LinearGradientBrush lcd(PointF(c.x,c.y),PointF(c.x,c.y+c.h),p[kDisplayTop],p[kDisplayBottom]);
        GraphicsPath lcdPath;roundedPath(lcdPath,c.x,c.y,c.w,c.h,2.f);
        g.FillPath(&lcd,&lcdPath);
        roundedOutline(g,rgb(0x0B0B0B),c.x,c.y,c.w,c.h,2.f);
        std::wstring v=formatted(c.id);
        if(c.id==9 || c.id==40) { // 2-digit displays, as in SynthPanelView.swift
            wchar_t out[32];std::swprintf(out,32,L"%02d",value(c.id));v=out;
        }
        text(g,v,c.x+1,c.y+1,c.w-2,c.h-2,11,p[kDisplayText],true,true,true);
        return;
    }
    case Kind::Radio: {
        const bool on=value(c.id)==c.value;
        const Color light=on?p[kRadio]:muted(p[kRadio],.35f);
        const float y=c.y+c.h/2;
        const bool justLed=(c.id==41 || c.id==46);
        const float ledx=justLed ? c.x+c.w*.5f : c.symbol ? c.x+c.w-7.f : c.x+7.f;
        // All red selector LEDs gain 2px diameter: radius 3.4 -> 4.4.
        led(g,light,ledx,y,4.4f,on);
        if(!justLed) {
            const float left=c.id==14 ? 13.f : (c.symbol ? 0.f : 15.f);
            const float width=c.id==14 ? c.w-14.f : c.w-(c.symbol?14.f:16.f);
            text(g,c.label,c.x+left,c.y,width,c.h,9.5f,p[kLabel],false,false);
        }
        return;
    }
    case Kind::Wave: {
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
        return;
    }
    }
}

void WindowsEditor::draw(Graphics& g) {
    const auto& p=palette_.c;
    fill(g,p[kPanel],0,0,kDesignWidth,kDesignHeight);
    text(g,L"Synth2",8,5,94,23,20,p[kLabel],true);
    text(g,L"Virtual Analog",84,7,135,20,12,muted(p[kLabel],.75f));
    // Persistent exact zoom readout: 770 design pixels = 100%.
    const int zoomPercent=int(std::lround(100.f*float(rect.getWidth())/kDesignWidth));
    const std::wstring zoomLabel=L"Zoom "+std::to_wstring(zoomPercent)+L"%";
    darkButton(g,p[kButton],492.f,5.f,94.f,20.f,hoveredChrome_==16);
    text(g,zoomLabel,494.f,5.f,90.f,20.f,11.f,rgb(0xE8E8E8),true,true);
    fill(g,rgb(0xAD0F0C),593,5,168,20);
    stroke(g,rgb(0x101010),593,5,168,20);
    text(g,L"V"+toWide(OXO_VERSION)+L"  Windows dev v"+std::wstring(kWindowsDevBuild),
         596,6,163,18,10,rgb(0xFFFFFF),true,true);
    struct Section {float x,y,w,h;const wchar_t* name;};
    static constexpr Section sections[]={
        {6,34,312,240,L"Oscillators"},{6,278,312,130,L"LFO"},
        {6,412,536,66,L"Wheel / MIDI"},{324,34,218,80,L"Amplifier"},
        {324,118,218,156,L"Filter"},{324,278,218,130,L"Arpeggiator"},
        {548,34,216,80,L"Effect"},{548,118,216,58,L"Equalizer / Pan"},
        {548,180,216,58,L"Tempo Delay"},{548,242,216,58,L"Chorus / Flanger"},
        {548,304,216,174,L"Voice"},
    };
    for(const auto& sec:sections) {
        // Header fill meets the section border exactly, with no one-pixel
        // panel-colour gutter between the title band and its outline.
        fill(g,p[kHeader],sec.x,sec.y,sec.w,16.f);
        stroke(g,muted(p[kHeader],.6f),sec.x,sec.y,sec.w,sec.h);
        text(g,sec.name,sec.x+5,sec.y,sec.w-10,16,12,p[kHeaderText],true);
    }
    Pen div(muted(p[kHeader],.45f),.8f);
    for(const auto& a:std::vector<std::array<float,4>>{
        {180,56,180,110},{14,115,310,115},{218,121,218,268},{14,197,210,197}})
        g.DrawLine(&div,a[0],a[1],a[2],a[3]);
    for(size_t i=0;i<controls_.size();++i)
        drawControl(g,controls_[i],hoveredControl_==int(i));
    // Original LCD + write/menu + voice grid geometry from Shared/SynthLibraryViews.swift.
    LinearGradientBrush lcd(PointF(50,484),PointF(50,554),p[kDisplayTop],p[kDisplayBottom]);
    GraphicsPath mainLcd;roundedPath(mainLcd,50,484,432,70,4.f);
    g.FillPath(&lcd,&mainLcd);
    roundedOutline(g,rgb(0x070707),50,484,432,70,4.f,1.5f);
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
    std::wstring second=readout();
    if(second.empty()) {
        if(loadedBank>=0 && loadedBank<int(banks_.size())) {
            wchar_t num[16];std::swprintf(num,16,L"%02d: ",displayBankNumber(loadedBank));
            second=std::wstring(num)+toWide(banks_[size_t(loadedBank)].name.c_str());
        } else second=knownRevision_>0 ? L"Click here to browse banks" : L"Scanning preset banks...";
    }
    text(g,second,60,526,392,23,13,p[kDisplayText],false,false,true);
    darkButton(g,p[kButton],420,495,24,20,hoveredChrome_==3);
    darkButton(g,p[kButton],449,495,24,20,hoveredChrome_==4);
    text(g,L"<",421,495,21,20,13,rgb(0xFFFFFF),true,true);
    text(g,L">",450,495,21,20,13,rgb(0xFFFFFF),true,true);
    darkButton(g,rgb(0xB81715),492,488,50,26,hoveredChrome_==5);
    text(g,L"write",492,488,50,26,11,rgb(0xFFFFFF),true,true);
    darkButton(g,p[kButton],492,522,50,26,hoveredChrome_==6);
    text(g,L"menu",492,522,50,26,11,rgb(0xFFFFFF),true,true);
    if(!midiLearn_) {
        const uint64_t states=hooks_.voiceStates ? hooks_.voiceStates(hooks_.ctx) : 0;
        // Poly shows the allowed voice count; Mono and Legato show one
        // allocated square. Playing/released voices remain visible in all modes.
        const int allocated=value(38)==0 ? std::clamp(value(94),1,32) : 1;
        constexpr float cell=215.f/16.f;
        // Vertically centre the two-row grid beside the Write/Menu button pair.
        constexpr float left=548.f,top=505.f;
        constexpr float inset=1.75f; // clear gap from the grid lines
        for(int i=0;i<32;i++) {
            const int state=int((states>>(i*2))&3);
            const Color* color=state==2?&p[kVoiceGridPlaying]:
                               state==1?&p[kVoiceGridReleased]:
                               i<allocated?&p[kVoiceGridEmpty]:nullptr;
            if(color) {
                const float x=left+(i%16)*cell, y=top+(i/16)*cell;
                fill(g,*color,x+inset,y+inset,cell-2*inset,cell-2*inset);
            }
        }
        // Draw one continuous grid instead of 32 overlapping rectangles.
        Pen grid(p[kVoiceGridBorder],.75f);
        for(int column=0;column<=16;column++) {
            const float x=left+column*cell+.5f;
            g.DrawLine(&grid,x,top,x,top+2*cell);
        }
        for(int row=0;row<=2;row++) {
            const float y=top+row*cell+.5f;
            g.DrawLine(&grid,left,y,left+16*cell,y);
        }
    } else {
        darkButton(g,rgb(0xA30D0D),558,488,186,24,hoveredChrome_==8);
        text(g,L"Exit MIDI Learn",558,488,186,24,11,rgb(0xFFFFFF),true,true);
        text(g,L"Select a control, then move a MIDI CC",548,518,206,20,9,p[kLabel],true,true);
    }
    if(browser_) drawBrowser(g);
    // Small bottom-right host-resize grip. All drawing scales as one canvas.
    Pen grip(blend(p[kPanel],p[kLabel],.65f),1.f);
    for(int i=0;i<3;i++) {
        const float a=float(i*5);
        g.DrawLine(&grip,752.f+a,557.f,767.f,542.f+a);
    }
}

void WindowsEditor::refreshBanks() {
    // Even a request to obtain Json() is a blocking scan before revision 1.
    if(knownRevision_<=0 || !hooks_.libraryJSON) return;
    const char* data=hooks_.libraryJSON(hooks_.ctx);
    if(!data) return;
    const std::string json(data);
    Reader r{json};
    if(!r.eat('{')) return;
    std::vector<Bank> scanned;
    std::string libraryRoot;
    do {
        if(r.eat('}')) break;
        std::string key;
        if(!r.str(key)||!r.eat(':')) return;
        if(key=="root") {if(!r.str(libraryRoot))return;}
        else if(key!="banks") {if(!r.skip())return;}
        else {
            if(!r.eat('['))return;
            while(!r.eat(']')) {
                if(!r.eat('{'))return;
                Bank b;
                do {
                    if(r.eat('}'))break;
                    std::string k;
                    if(!r.str(k)||!r.eat(':'))return;
                    if(k=="name") {if(!r.str(b.name))return;}
                    else if(k=="source") {if(!r.str(b.source))return;}
                    else if(k=="readOnly") {
                        r.ws();
                        if(json.compare(r.p,4,"true")==0){b.readOnly=true;r.p+=4;}
                        else if(json.compare(r.p,5,"false")==0){r.p+=5;}
                        else return;
                    } else if(k=="slots") {
                        if(!r.eat('['))return;
                        while(!r.eat(']')) {
                            std::string slot;
                            if(!r.str(slot))return;
                            b.slots.push_back(std::move(slot));
                            if(!r.eat(',')) {if(!r.eat(']'))return;break;}
                        }
                    } else if(!r.skip())return;
                    if(!r.eat(',')){if(!r.eat('}'))return;break;}
                } while(true);
                scanned.push_back(std::move(b));
                if(!r.eat(',')){if(!r.eat(']'))return;break;}
            }
        }
        if(!r.eat(',')){if(!r.eat('}'))return;break;}
    } while(true);
    // Backend order/indices remain unchanged. Natural sort is UI-only, so
    // the .sy1 slot numbers, host state and bank hook indices are untouched.
    const std::string previousSelection = selectedBank_>=0 ? bankKey(selectedBank_) : std::string();
    libraryRoot_=std::move(libraryRoot);
    banks_=std::move(scanned);
    if(!previousSelection.empty()) {
        selectedBank_=-1;
        for(size_t i=0;i<banks_.size();i++)
            if(bankKey(int(i))==previousSelection) {selectedBank_=int(i);break;}
        if(selectedBank_<0) selectedSlot_=-1;
    }
    bankOrder_.resize(banks_.size());
    std::iota(bankOrder_.begin(),bankOrder_.end(),0);
    std::stable_sort(bankOrder_.begin(),bankOrder_.end(),[this](int a,int b) {
        // Keep the real read-only factory bank immediately after Favourites.
        if(a==0 && banks_[size_t(a)].readOnly) return true;
        if(b==0 && banks_[size_t(b)].readOnly) return false;
        return winfav::NaturalLess(toWide(banks_[size_t(a)].name.c_str()),
                                   toWide(banks_[size_t(b)].name.c_str()));
    });
    int loadedBank=-1,loadedSlot=-1;
    if(hooks_.loadedProgram) hooks_.loadedProgram(hooks_.ctx,&loadedBank,&loadedSlot);
    if(selectedBank_==-1 && loadedBank>=0) selectedBank_=loadedBank;
    if(selectedBank_==-1 && !banks_.empty()) selectedBank_=bankOrder_.front();
    if(selectedBank_>=int(banks_.size())) selectedBank_=banks_.empty()?-1:bankOrder_.front();
    if(selectedSlot_<0 && loadedSlot>=0 && selectedBank_==loadedBank) selectedSlot_=loadedSlot;
    if(writing_ && (selectedBank_==kFavouritesBank ||
                    (selectedBank_>=0 && banks_[size_t(selectedBank_)].readOnly))) {
        for(size_t b=0;b<banks_.size();b++) if(!banks_[b].readOnly) {selectedBank_=int(b);break;}
    }
    if(writing_ && selectedSlot_<0 && selectedBank_>=0) {
        const auto& slots=banks_[size_t(selectedBank_)].slots;
        auto it=std::find(slots.begin(),slots.end(),"");
        selectedSlot_=it==slots.end()?0:int(it-slots.begin());
    }
    bankScroll_=std::clamp(bankScroll_,0,std::max(0,int(bankOrder_.size())+1-20));
    repaint();
}
void WindowsEditor::openBrowser(bool writing) {
    browser_=true;writing_=writing;
    bankScroll_=slotScroll_=0;selectedSlot_=-1;
    // Opening an already-scanned browser must not request a new full scan.
    // The separate Refresh button still offers a manual asynchronous rescan.
    if(knownRevision_<=0 && hooks_.startLibraryScan) hooks_.startLibraryScan(hooks_.ctx);
    else refreshBanks();
    updateEditControl();
    repaint();
}
void WindowsEditor::closeBrowser() {
    browser_=writing_=false;
    updateEditControl();
    repaint();
}
int WindowsEditor::displayBankNumber(int bank) const {
    auto it=std::find(bankOrder_.begin(),bankOrder_.end(),bank);
    return it==bankOrder_.end()?bank+1:int(it-bankOrder_.begin())+1;
}
std::string WindowsEditor::bankKey(int bank) const {
    if(bank<0 || bank>=int(banks_.size()))return {};
    const auto& b=banks_[size_t(bank)];
    return winfav::BankKey(std::filesystem::path(libraryRoot_),
                           std::filesystem::path(b.source),b.readOnly);
}
bool WindowsEditor::resolveFavourite(const winfav::Item& f,int& bank,int& slot) const {
    for(size_t b=0;b<banks_.size();b++) {
        if(bankKey(int(b))!=f.bank)continue;
        if(f.slot<0 || f.slot>=int(banks_[b].slots.size()) || banks_[b].slots[size_t(f.slot)].empty())
            return false;
        bank=int(b);slot=f.slot;return true;
    }
    return false;
}
bool WindowsEditor::isFavourite(int bank,int slot) const {
    if(bank<0 || slot<0)return false;
    const auto key=bankKey(bank);
    return std::any_of(favourites_.begin(),favourites_.end(),[&](const winfav::Item& f){
        return f.bank==key && f.slot==slot;
    });
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
    repaint();
}
void WindowsEditor::showAbout() {
    const std::wstring info=std::wstring(L"A Synth1 recreation by\nOxo Synth2 V")+toWide(OXO_VERSION)+
        (L"\nWindows x64 VST3 - development build v"+std::wstring(kWindowsDevBuild)+L"\n\n")+
        L"Original Synth1: Daichi Laboratory\n"
        L"Synth2 recreation: Oxo Music\n"
        L"Windows port: sl23\n\n"
        L"Synth1 and Synth2 remain credited to their respective developers.";
    MessageBoxW(window_,info.c_str(),L"About Synth2 - Windows port",MB_OK|MB_ICONINFORMATION);
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
            window_,nullptr,GetModuleHandleW(nullptr),nullptr);
        if(editName_) SendMessageW(editName_,WM_SETFONT,
            WPARAM(GetStockObject(DEFAULT_GUI_FONT)),TRUE);
    } else MoveWindow(editName_,int(300*sx),int(38*sy),int(200*sx),int(19*sy),TRUE);
}

void WindowsEditor::drawBrowser(Graphics& g) {
    const auto& p=palette_.c;
    // The Warm factory palette's selection colours, shared by every theme.
    // In Warm these are already the original values; other theme colours stay intact.
    const Color selectionFill=rgb(0xC2D3E1);
    const Color selectionInk=rgb(0x0D100C);
    // Numbering is warm yellow on dark rows and dark gold on pale selections.
    // Keep the bank/preset names in their existing theme colours.
    const Color numberInk=rgb(0xE9D56A);
    const Color selectedNumberInk=rgb(0x816000);
    const float x=6,y=34,w=758,h=444;
    fill(g,p[kPanel],x,y,w,h);
    stroke(g,p[kHeader],x,y,w,h,1.5f);
    fill(g,p[kHeader],x+1,y+1,w-2,27);
    text(g,writing_?L"Write patch":L"Banks",x+7,y+2,135,24,12,p[kHeaderText],true);
    if(writing_) {
        darkButton(g,p[kButton],514,38,84,20,hoveredChrome_==9);
        text(g,L"Write patch",514,38,84,20,10,rgb(0xFFFFFF),true,true);
        darkButton(g,p[kButton],604,38,74,20,hoveredChrome_==10);
        text(g,L"New bank",604,38,74,20,10,rgb(0xFFFFFF),true,true);
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
    fill(g,p[kDisplayBottom],x+1,y+28,190,h-30);
    fill(g,p[kDisplayBottom],x+191,y+28,w-192,h-30);
    stroke(g,p[kHeader],x+190,y+28,1,h-30);
    constexpr float row=20;
    const int visualCount=int(bankOrder_.size())+1; // 00: Favourites
    for(int i=0;i<20;i++) {
        const int visual=bankScroll_+i;
        if(visual<0 || visual>=visualCount)break;
        const int bi=visual==0 ? kFavouritesBank : bankOrder_[size_t(visual-1)];
        const bool isFavouriteBank=bi==kFavouritesBank;
        const float ry=64+row*i;
        if(bi==selectedBank_)fill(g,selectionFill,8,ry,187,19);
        int count=isFavouriteBank?int(favourites_.size()):0;
        if(!isFavouriteBank)
            for(const auto& slot:banks_[size_t(bi)].slots)if(!slot.empty())++count;
        wchar_t prefix[12];std::swprintf(prefix,12,L"%02d:",visual);
        const Color bankText=bi==selectedBank_?selectionInk:p[kDisplayText];
        text(g,prefix,12,ry,27,18,10.5f,
             bi==selectedBank_?selectedNumberInk:numberInk,false,false,true);
        text(g,isFavouriteBank?L"Favourites":toWide(banks_[size_t(bi)].name.c_str()),
             39,ry,116,18,10.5f,bankText,false,false,true);
        text(g,std::to_wstring(count),164,ry,30,18,10.5f,bankText,false,true,true);
    }
    if(knownRevision_<=0) {
        text(g,L"Scanning preset banks in the background...",204,90,510,30,12,rgb(0xC1DCE0));
        return;
    }
    if(selectedBank_==-1) {
        text(g,L"Select a bank or Favourites.",204,90,510,30,12,rgb(0xC1DCE0));
        return;
    }
    const bool favouriteView=selectedBank_==kFavouritesBank;
    if(!favouriteView && (selectedBank_<0 || selectedBank_>=int(banks_.size())))return;
    const int count=favouriteView?int(favourites_.size()):
        int(banks_[size_t(selectedBank_)].slots.size());
    const int rows=std::max(1,(count+3)/4);
    const int loadedPairBank=loadedBankForPaint_;
    const int loadedPairSlot=loadedSlotForPaint_;
    for(int rr=0;rr<20;rr++) {
        const int br=rr+slotScroll_;
        if(br>=rows)break;
        for(int col=0;col<4;col++) {
            const int slotIndex=col*rows+br;
            if(slotIndex>=count)continue;
            const float x0=204.f+float(col)*138.5f;
            const float y0=65.f+float(rr)*20.f;
            int bank=selectedBank_,slot=slotIndex;
            std::string label;
            bool valid=true;
            bool favourited=favouriteView;
            if(favouriteView) {
                const auto& fav=favourites_[size_t(slotIndex)];
                label=fav.name;
                valid=resolveFavourite(fav,bank,slot);
                if(valid)label=banks_[size_t(bank)].slots[size_t(slot)].empty()?label:
                    banks_[size_t(bank)].slots[size_t(slot)];
            } else {
                label=banks_[size_t(bank)].slots[size_t(slot)];
                favourited=isFavourite(bank,slot);
                valid=!label.empty();
            }
            const bool selected=writing_ ? (!favouriteView && slotIndex==selectedSlot_) :
                (valid && bank==loadedPairBank && slot==loadedPairSlot);
            fill(g,selected?selectionFill:p[kDisplayBottom],x0,y0,136,18);
            wchar_t prefix[16];
            std::swprintf(prefix,16,L"%03d:",slotIndex+1);
            const Color slotText=favourited ? (selected?rgb(0x17683B):rgb(0x56E496)) :
                selected?selectionInk:valid?rgb(0xD6F1F2):rgb(0x44656A);
            text(g,prefix,x0+3,y0,35,18,10.2f,
                 selected?selectedNumberInk:numberInk,false,false,true);
            text(g,toWide(label.c_str()),x0+39,y0,93,18,10.2f,
                 slotText,false,false,true);
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
    hooks_.beginEdit(hooks_.ctx,id);
    hooks_.setValue(hooks_.ctx,id,v);
    hooks_.endEdit(hooks_.ctx,id);
    selectedId_=id;
    repaint();
}
void WindowsEditor::cycle(int id,int step) {
    int low=0,high=127,def=0;
    if(hooks_.paramInfo) hooks_.paramInfo(hooks_.ctx,id,&low,&high,&def);
    const int v=value(id)+step;
    setValueOnce(id,v>high?low:v<low?high:v);
}
void WindowsEditor::stepPatch(int direction) {
    if(banks_.empty()) refreshBanks();
    int bank=-1,slot=-1;
    if(hooks_.loadedProgram) hooks_.loadedProgram(hooks_.ctx,&bank,&slot);
    if(bank<0 || bank>=int(banks_.size()))bank=selectedBank_>=0?selectedBank_:0;
    if(bank<0 || bank>=int(banks_.size()))return;
    const auto& items=banks_[size_t(bank)].slots;
    const int count=int(items.size());
    if(!count)return;
    for(int i=1;i<=count;i++) {
        const int next=(slot+(direction>0?i:-i)+count*2)%count;
        if(items[size_t(next)].empty())continue;
        if(hooks_.loadLibraryPatch && hooks_.loadLibraryPatch(hooks_.ctx,bank,next)) {
            selectedBank_=bank;selectedSlot_=next;selectedId_=-1;
            refreshBanks();
        }
        break;
    }
}

void WindowsEditor::browserClick(float x,float y,bool doubleClick) {
    if(y>=37 && y<=61) {
        if(x>=690 && x<760) {closeBrowser();return;}
        if(writing_) {
            if(x>=514 && x<601) {
                if(selectedBank_<0 || selectedBank_>=int(banks_.size()))return;
                if(banks_[size_t(selectedBank_)].readOnly || selectedSlot_<0)return;
                std::wstring name=patchName();
                if(editName_) {const int n=GetWindowTextLengthW(editName_);name.resize(size_t(n)+1);
                    GetWindowTextW(editName_,name.data(),n+1);name.resize(size_t(n));}
                if(hooks_.writeLibraryPatch && hooks_.writeLibraryPatch(hooks_.ctx,selectedBank_,selectedSlot_,toUtf8(name).c_str())) {
                    closeBrowser();refreshBanks();
                } else MessageBoxW(window_,L"Cannot write that preset. Check the library folder is writable.",L"Synth2",MB_ICONWARNING);
                return;
            }
            if(x>=604 && x<680) {
                MessageBoxW(window_,L"The native New Bank naming dialog is not yet implemented.\nExisting writable banks can be used now.",
                            L"Synth2 Windows editor - work in progress",MB_OK|MB_ICONINFORMATION);
                return;
            }
        } else {
            if(x>=348 && x<426) {toggleFavourite();return;}
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
        const int visual=bankScroll_+int((y-64)/20);
        const int bank=visual==0?kFavouritesBank:
            (visual>0 && visual<=int(bankOrder_.size()) ? bankOrder_[size_t(visual-1)]:-1);
        if(bank==kFavouritesBank ||
           (bank>=0 && bank<int(banks_.size()) && !(writing_ && banks_[size_t(bank)].readOnly))) {
            if(writing_ && bank==kFavouritesBank)return;
            selectedBank_=bank;selectedSlot_=-1;slotScroll_=0;
            if(writing_ && bank>=0) {
                const auto& slots=banks_[size_t(bank)].slots;
                auto it=std::find(slots.begin(),slots.end(),"");
                selectedSlot_=it==slots.end()?0:int(it-slots.begin());
            }
        }
        repaint();return;
    }
    if(x>=204 && x<760 && (selectedBank_==kFavouritesBank ||
                            (selectedBank_>=0 && selectedBank_<int(banks_.size())))) {
        const bool favouriteView=selectedBank_==kFavouritesBank;
        const int count=favouriteView?int(favourites_.size()):
            int(banks_[size_t(selectedBank_)].slots.size());
        const int rows=std::max(1,(count+3)/4);
        const int col=std::clamp(int((x-204)/138.5f),0,3);
        const int rr=slotScroll_+int((y-64)/20);
        const int index=col*rows+rr;
        if(index>=0 && index<count) {
            selectedSlot_=index;
            int bank=selectedBank_,slot=index;
            const bool valid=favouriteView?resolveFavourite(favourites_[size_t(index)],bank,slot):
                !banks_[size_t(bank)].slots[size_t(slot)].empty();
            if(!writing_ && valid && hooks_.loadLibraryPatch) {
                hooks_.loadLibraryPatch(hooks_.ctx,bank,slot);
                selectedId_=-1;
                if(doubleClick)closeBrowser();
            }
            repaint();
        }
    }
}

// Hover uses the same design-space hit rectangles as clicks. It only
// invalidates the window when the hovered target changes; idle redraws remain off.
void WindowsEditor::updateHover(float x,float y) {
    int nextControl=-1;
    int nextChrome=0;
    // Zoom is now a regular themed hoverable button; LCDs and waveforms are not.
    // The browser covers only the central panel. Keep the chrome underneath
    // it (Write, Menu and main LCD) interactive while browsing.
    if(x>=492.f && x<586.f && y>=5.f && y<25.f) {
        nextChrome=16; // Zoom button (same subtle hover as other buttons)
    } else if(browser_ && x>=6.f && x<764.f && y>=34.f && y<478.f) {
        if(y>=37.f && y<=61.f) {
            if(x>=690.f && x<760.f) nextChrome=14; // Close
            else if(writing_) {
                if(x>=514.f && x<601.f) nextChrome=9; // Write patch
                else if(x>=604.f && x<680.f) nextChrome=10; // New bank
            } else {
                if(x>=348.f && x<426.f) nextChrome=15; // Favourite toggle
                else if(x>=430.f && x<498.f) nextChrome=11; // Import
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
        if(nextChrome==0) {
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
    if(nextControl!=hoveredControl_ || nextChrome!=hoveredChrome_) {
        hoveredControl_=nextControl;
        hoveredChrome_=nextChrome;
        repaint();
    }
}

void WindowsEditor::mouseDown(float x,float y,bool doubleClick) {
    // The top Zoom readout remains usable even when the bottom-right resize
    // grip is off-screen. Use the existing VST3 host resize request.
    if(x>=492.f && x<586.f && y>=5.f && y<25.f) {
        static constexpr int levels[]={75,100,125,150,175,200,250,300};
        const int current=int(std::lround(100.f*float(rect.getWidth())/kDesignWidth));
        HMENU menu=CreatePopupMenu();
        if(menu) {
            for(int i=0;i<8;i++) {
                const std::wstring label=std::to_wstring(levels[i])+L"%";
                const UINT flags=MF_STRING | (current==levels[i] ? MF_CHECKED : 0);
                AppendMenuW(menu,flags,1000+i,label.c_str());
            }
            POINT where{};
            GetCursorPos(&where);
            const int selection=int(TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY,
                where.x,where.y,0,window_,nullptr));
            DestroyMenu(menu);
            if(selection>=1000 && selection<1008)
                requestWidth(int(std::lround(kDesignWidth*levels[selection-1000]/100.f)));
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
    // The red version label opens the Windows-port credits without changing
    // the plugin's version metadata or affecting host automation.
    if(x>=593.f && x<762.f && y>=2.f && y<31.f) {showAbout();return;}
    // Route to the browser only inside the browser's actual visible bounds.
    // Header controls and the bottom strip keep their normal mouse actions.
    if(browser_ && x>=6.f && x<764.f && y>=34.f && y<478.f) {
        browserClick(x,y,doubleClick);
        return;
    }
    if(midiLearn_ && x>=558.f && x<744.f && y>=488.f && y<512.f) {
        midiLearn_=false;learnTarget_=-1;repaint();return;
    }
    if(y>=484 && y<=554) {
        if(x>=492 && x<542) {
            if(y<518) {openBrowser(true);return;}
            POINT p;GetCursorPos(&p);showMainMenu(p.x,p.y);return;
        }
        if(x>=420 && x<473 && y<520) {stepPatch(x<448?-1:1);return;}
        if(x>=50 && x<482) {browser_?closeBrowser():openBrowser(false);return;}
    }
    if(y<32 && x<100) {
        POINT p;GetCursorPos(&p);showThemeMenu(p.x,p.y);return;
    }
    for(auto it=controls_.rbegin();it!=controls_.rend();++it) {
        const auto& c=*it;
        if(x<c.x || x>=c.x+c.w || y<c.y || y>=c.y+c.h)continue;
        if(c.id<0)continue;
        selectedId_=c.id;
        if(midiLearn_) {
            if(doubleClick && hooks_.setMidiCC) hooks_.setMidiCC(hooks_.ctx,c.id,0);
            else learnTarget_=c.id;
            repaint();return;
        }
        // WM_LBUTTONDBLCLK is delivered for the SECOND rapid click.
        // Only knobs and non-cycling value displays reset on a double click,
        // exactly as in SwiftUI. Cycle buttons, LEDs, waveform selectors and
        // toggles must treat every fast click as another normal selection.
        const bool doubleTapResets = c.kind==Kind::Knob ||
            (c.kind==Kind::Display && c.id!=64 && c.id!=78 && c.id!=82);
        if(doubleClick && doubleTapResets) {
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
            SetCapture(window_);repaint();return;
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
    updateHover(x,y);
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
    if(hooks_.setValue)hooks_.setValue(hooks_.ctx,dragId_,newValue);
    repaint();
}
void WindowsEditor::mouseUp() {
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
    // Scrolling over the master volume must change volume, not the browser.
    if(browser_ && x>=6.f && x<764.f && y>=34.f && y<478.f) {
        if(x<196) {
            // Wheel input is expressed as rows, not merely the sign of one
            // notch. A standard wheel step now moves eight bank entries.
            const int next=std::clamp(bankScroll_-direction,0,
                                      std::max(0,int(bankOrder_.size())+1-20));
            if(next!=bankScroll_) {bankScroll_=next;repaint();}
        } else if(selectedBank_==kFavouritesBank ||
                  (selectedBank_>=0 && selectedBank_<int(banks_.size()))) {
            const int count=selectedBank_==kFavouritesBank?int(favourites_.size()):
                int(banks_[size_t(selectedBank_)].slots.size());
            const int rows=(count+3)/4;
            const int amount=(direction>0?1:-1)*std::max(1,(std::abs(direction)+1)/2);
            const int next=std::clamp(slotScroll_-amount,0,std::max(0,rows-20));
            if(next!=slotScroll_) {slotScroll_=next;repaint();}
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
        selectedId_=id;

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
    } else {
        BROWSEINFOW browse{};
        browse.hwndOwner=window_;
        browse.lpszTitle=L"Choose a folder containing Synth1 .sy1 presets";
        browse.ulFlags=BIF_RETURNONLYFSDIRS|BIF_NEWDIALOGSTYLE;
        PIDLIST_ABSOLUTE item=SHBrowseForFolderW(&browse);
        if(!item)return;
        wchar_t folder[MAX_PATH]={};
        BOOL gotPath=SHGetPathFromIDListW(item,folder);
        CoTaskMemFree(item);
        if(!gotPath)return;
        path=folder;
    }
    const std::string bytes=toUtf8(path);
    const char* imported=hooks_.importItems(hooks_.ctx,bytes.c_str());
    if(!imported || !*imported)
        MessageBoxW(window_,L"No new preset bank was imported. The files may already exist or may not contain valid .sy1 presets.",
                    L"Synth2",MB_ICONINFORMATION);
    refreshBanks();
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
            knownSerial_=-1;selectedId_=-1;
            refreshBanks();
        } else MessageBoxW(window_,L"Unable to load this .sy1 patch.",L"Synth2",MB_ICONWARNING);
    }
}

void WindowsEditor::previewTheme(int index,const std::array<std::uint32_t,17>& colors) {
    currentThemeIndex_=index;
    for(size_t i=0;i<palette_.c.size();++i)palette_.c[i]=rgb(int(colors[i]));
    if(hooks_.setThemeText) {
        const std::string json=wintheme::ToJSON(colors);
        hooks_.setThemeText(hooks_.ctx,json.c_str());
    }
    repaint();
}
void WindowsEditor::showThemeEditor() {
    if(!themeEditor_)themeEditor_=std::make_unique<ThemeEditorWindow>();
    themeEditor_->Show(window_,storage::Root(),currentThemeIndex_,
        [this](int index,const wintheme::Palette& colors){previewTheme(index,colors);});
}
void WindowsEditor::showThemeMenu(int x,int y) {
    HMENU m=CreatePopupMenu();
    const auto entries=wintheme::List(storage::Root());
    for(size_t i=0;i<entries.size() && i<300;++i) {
        if(i==wintheme::Names.size())AppendMenuW(m,MF_SEPARATOR,0,nullptr);
        const UINT command=i<wintheme::Names.size()?100+UINT(i):500+UINT(i-wintheme::Names.size());
        AppendMenuW(m,MF_STRING,command,entries[i].name.c_str());
    }
    AppendMenuW(m,MF_SEPARATOR,0,nullptr);
    AppendMenuW(m,MF_STRING,120,L"75% size");
    AppendMenuW(m,MF_STRING,121,L"100% size");
    AppendMenuW(m,MF_STRING,122,L"125% size");
    AppendMenuW(m,MF_STRING,123,L"150% size");
    AppendMenuW(m,MF_STRING,124,L"200% size");
    AppendMenuW(m,MF_STRING,125,L"250% size");
    AppendMenuW(m,MF_STRING,126,L"300% size");
    int choice=int(TrackPopupMenu(m,TPM_RETURNCMD|TPM_NONOTIFY,x,y,0,window_,nullptr));
    DestroyMenu(m);
    if(choice>=100&&choice<107)selectTheme(choice-100);
    else if(choice>=500&&choice<793)selectTheme(int(wintheme::Names.size())+choice-500);
    else if(choice>=120&&choice<=126) {
        static const int widths[]={578,770,963,1155,1540,1925,2310};
        requestWidth(widths[choice-120]);
    }
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
    AppendMenuW(menu,MF_STRING,13,midiLearn_?L"Exit MIDI Learn":L"MIDI Learn");
    AppendMenuW(menu,MF_STRING,14,L"All notes off");
    HMENU themes=CreatePopupMenu();
    const auto entries=wintheme::List(storage::Root());
    for(size_t i=0;i<entries.size() && i<300;++i) {
        if(i==wintheme::Names.size())AppendMenuW(themes,MF_SEPARATOR,0,nullptr);
        const UINT command=i<wintheme::Names.size()?100+UINT(i):500+UINT(i-wintheme::Names.size());
        AppendMenuW(themes,MF_STRING,command,entries[i].name.c_str());
    }
    AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
    AppendMenuW(menu,MF_POPUP,UINT_PTR(themes),L"Select theme");
    AppendMenuW(menu,MF_STRING,15,L"Themes...");
    int selected=int(TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY,x,y,0,window_,nullptr));
    DestroyMenu(menu);
    switch(selected) {
        case 10: if(hooks_.initializePatch)hooks_.initializePatch(hooks_.ctx);selectedId_=-1;break;
        case 11: showFileDialog(false);break;
        case 12: showFileDialog(true);break;
        case 13: midiLearn_=!midiLearn_;learnTarget_=-1;
                 if(hooks_.lastCC)lastCcToken_=hooks_.lastCC(hooks_.ctx);break;
        case 14: if(hooks_.allNotesOff)hooks_.allNotesOff(hooks_.ctx);break;
        case 15: showThemeEditor();break;
        default:
            if(selected>=100&&selected<107) selectTheme(selected-100);
            else if(selected>=500&&selected<793)
                selectTheme(int(wintheme::Names.size())+selected-500);
            break;
    }
    repaint();
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
    bool dirty=false;
    if(hooks_.patchSerial) {
        const int serial=hooks_.patchSerial(hooks_.ctx);
        if(serial!=knownSerial_) {
            knownSerial_=serial;readTheme();selectedId_=-1;dirty=true;
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
    if(midiLearn_&&learnTarget_>=0&&hooks_.lastCC&&hooks_.setMidiCC) {
        const int token=hooks_.lastCC(hooks_.ctx);
        if(token!=lastCcToken_ && token>0) {
            lastCcToken_=token;hooks_.setMidiCC(hooks_.ctx,learnTarget_,token&127);
            learnTarget_=-1;dirty=true;
        }
    }
    if(!browser_ && !midiLearn_) {
        const int mode=value(38), poly=value(94);
        if(mode!=knownGridMode_ || poly!=knownGridPolyphony_) {
            knownGridMode_=mode;knownGridPolyphony_=poly;dirty=true;
        }
        if(hooks_.voiceStates) {
            const uint64_t states=hooks_.voiceStates(hooks_.ctx);
            if(states!=knownVoiceStates_) {knownVoiceStates_=states;dirty=true;}
        }
    }
    if(dirty) repaint();
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
    case WM_SETCURSOR: {
        if(LOWORD(lp)==HTCLIENT) {
            POINT pos{};GetCursorPos(&pos);ScreenToClient(window_,&pos);
            // Indicate that the zoom readout is a clickable size menu.
            if(pos.x>=int(492.f*sx) && pos.x<int(586.f*sx) &&
               pos.y>=int(5.f*sy) && pos.y<int(25.f*sy)) {
                SetCursor(LoadCursorW(nullptr,IDC_HAND));return TRUE;
            }
            if(hoveredChrome_!=0 || hoveredControl_>=0) {
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
        HDC screenDC=BeginPaint(window_,&ps);
        if(screenDC) {
            RECT client{};GetClientRect(window_,&client);
            const int width=client.right-client.left;
            const int height=client.bottom-client.top;
            if(width>0 && height>0) {
                auto paint=[&](HDC target) {
                    Graphics g(target);
                    g.SetSmoothingMode(SmoothingModeAntiAlias);
                    g.SetTextRenderingHint(TextRenderingHintAntiAlias);
                    g.SetPixelOffsetMode(PixelOffsetModeHighQuality);
                    g.ScaleTransform(float(width)/kDesignWidth,float(height)/kDesignHeight);
                    draw(g);
                };
                HDC memoryDC=CreateCompatibleDC(screenDC);
                HBITMAP bitmap=memoryDC ? CreateCompatibleBitmap(screenDC,width,height) : nullptr;
                if(memoryDC && bitmap) {
                    HGDIOBJ previous=SelectObject(memoryDC,bitmap);
                    if(previous && previous!=HGDI_ERROR) {
                        paint(memoryDC);
                        BitBlt(screenDC,0,0,width,height,memoryDC,0,0,SRCCOPY);
                        SelectObject(memoryDC,previous);
                    } else paint(screenDC);
                    DeleteObject(bitmap);
                } else paint(screenDC);
                if(memoryDC)DeleteDC(memoryDC);
            }
        }
        EndPaint(window_,&ps);
        return 0;
    }
    case WM_TIMER:if(wp==1)tick();return 0;
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
        if(hoveredControl_!=-1 || hoveredChrome_!=0) {
            hoveredControl_=-1;
            hoveredChrome_=0;
            repaint();
        }
        return 0;
    case WM_LBUTTONUP:mouseUp();return 0;
    case WM_CAPTURECHANGED:mouseUp();return 0;
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
    case WM_KEYDOWN:
        if(wp==VK_ESCAPE && browser_) {closeBrowser();return 0;}
        if(wp==VK_ESCAPE && midiLearn_) {midiLearn_=false;learnTarget_=-1;repaint();return 0;}
        if(wp==VK_LEFT) {stepPatch(-1);return 0;}
        if(wp==VK_RIGHT) {stepPatch(1);return 0;}
        return 0;
    case WM_CONTEXTMENU: {
        POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
        showMainMenu(p.x,p.y);return 0;
    }
    default:return DefWindowProcW(window_,msg,wp,lp);
    }
}
} // namespace oxo
