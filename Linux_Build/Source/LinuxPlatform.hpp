#pragma once

// Linux services for the shared editor (DesktopEditor/), standing in for
// what Windows gives it: message boxes / text prompts / file and folder
// pickers / colour picker (zenity, else kdialog), opening a URL or folder
// (xdg-open, the FileManager1 D-Bus call to select a file), one background
// HTTP GET (curl) for "Check for updates", popup menu contents (the X11
// menu itself: LinuxX11.hpp), and at the end the Win32 names the shared
// editor calls. No X11 here: this file is included by DesktopEditor.hpp.

#include "LinuxGdiplus.hpp"

#include <atomic>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <string>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

extern char **environ;

namespace oxo::lx {

inline uint64_t NowMs() {
    using namespace std::chrono;
    return uint64_t(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

// ---- UTF-8 <-> wchar_t (UTF-32 on Linux) ----------------------------------------
inline std::wstring Wide(const char *s) {
    std::wstring out;
    if (!s) return out;
    const auto *p = reinterpret_cast<const unsigned char *>(s);
    while (*p) {
        uint32_t c = *p++;
        int more = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
        if (more) c &= (0x3F >> more);
        for (; more && (*p & 0xC0) == 0x80; --more) c = (c << 6) | (*p++ & 0x3F);
        out += more ? L'?' : wchar_t(c);
    }
    return out;
}
inline std::string Utf8(const std::wstring &s) { return Gdiplus::GraphicsPath::Utf8(s.c_str()); }

// ---- processes ---------------------------------------------------------------------
inline bool OnPath(const char *tool) {
    const char *path = std::getenv("PATH");
    std::string dirs = path ? path : "/usr/bin:/bin";
    size_t start = 0;
    while (start <= dirs.size()) {
        const size_t end = dirs.find(':', start);
        const std::string dir = dirs.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!dir.empty() && access((dir + "/" + tool).c_str(), X_OK) == 0) return true;
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return false;
}

// A pidfd for a child: signals sent through it reach that process or
// nobody, even once its PID is reaped and reused (hosts that ignore
// SIGCHLD reap children themselves). -1 = no pidfd (kernel < 5.3).
inline int PidFd(pid_t pid) {
#ifdef SYS_pidfd_open
    return int(syscall(SYS_pidfd_open, pid, 0));
#else
    (void)pid;
    return -1;
#endif
}
inline void SignalPidFd(int pidfd, int sig) {
#ifdef SYS_pidfd_send_signal
    if (pidfd >= 0) syscall(SYS_pidfd_send_signal, pidfd, sig, nullptr, 0);
#else
    (void)pidfd; (void)sig;
#endif
}

// Runs argv, returns its exit status (-1 = could not start) and its stdout.
// `pump` is called about every 50 ms while it runs (the editor keeps
// painting and polling, as Windows' modal dialog loops let it). `owned`
// gets the child's pidfd once it starts and -1 before Run reaps it.
inline int Run(const std::vector<std::string> &argv, std::string *out, const std::function<void()> &pump = {},
               const std::function<void(int)> &owned = {}) {
    int fds[2];
    if (pipe2(fds, O_CLOEXEC) != 0) return -1;
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, fds[1], 1);
    posix_spawn_file_actions_addopen(&actions, 2, "/dev/null", O_WRONLY, 0);
    std::vector<char *> args;
    for (const auto &a : argv) args.push_back(const_cast<char *>(a.c_str()));
    args.push_back(nullptr);
    pid_t pid = -1;
    const int spawned = posix_spawnp(&pid, args[0], &actions, nullptr, args.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    close(fds[1]);
    if (spawned != 0) { close(fds[0]); return -1; }
    const int pidfd = owned ? PidFd(pid) : -1;
    if (owned) owned(pidfd);
    std::string data;
    char buffer[4096];
    for (;;) {
        pollfd p{fds[0], POLLIN, 0};
        const int r = poll(&p, 1, 50);
        if (r > 0) {
            const ssize_t n = read(fds[0], buffer, sizeof buffer);
            if (n > 0) { data.append(buffer, size_t(n)); continue; }
            if (n == 0 || (n < 0 && errno != EINTR && errno != EAGAIN)) break;
        }
        if (pump) pump();
    }
    close(fds[0]);
    if (owned) owned(-1);
    if (pidfd >= 0) close(pidfd);
    int status = 0;
    pid_t w;
    do w = waitpid(pid, &status, 0); while (w < 0 && errno == EINTR);
    if (out) *out = data;
    if (w < 0) return data.empty() ? 1 : 0; // the host reaps children (SIGCHLD ignored): judge by output
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

// Starts a program and forgets it. posix_spawnp, not fork(): the host is
// multithreaded, and a forked child may only call async-signal-safe
// functions. sh backgrounds the program and exits at once (reaped here),
// so it runs on in its own session (POSIX_SPAWN_SETSID), never a zombie.
inline void Detach(const std::vector<std::string> &argv) {
    if (argv.empty()) return;
    std::vector<std::string> shell{"sh", "-c", "\"$0\" \"$@\" </dev/null >/dev/null 2>&1 &"};
    shell.insert(shell.end(), argv.begin(), argv.end());
    std::vector<char *> args;
    for (const auto &a : shell) args.push_back(const_cast<char *>(a.c_str()));
    args.push_back(nullptr);
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
#ifdef POSIX_SPAWN_SETSID
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSID);
#endif
    pid_t pid = -1;
    if (posix_spawnp(&pid, "sh", nullptr, &attr, args.data(), environ) == 0) {
        int s;
        while (waitpid(pid, &s, 0) < 0 && errno == EINTR) {}
    }
    posix_spawnattr_destroy(&attr);
}

inline void OpenExternal(const std::string &target) {
    if (!target.empty()) Detach({"xdg-open", target});
}

inline std::string FileUri(const std::string &path) {
    std::string out = "file://";
    static const char *hex = "0123456789ABCDEF";
    for (unsigned char c : path) {
        if (std::isalnum(c) || std::strchr("/-_.~", c)) out += char(c);
        else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
    }
    return out;
}

// Finder.reveal: the file manager shows the item selected (FileManager1 on
// D-Bus: Nautilus, Dolphin, Nemo, Thunar...), else its folder opens.
inline void Reveal(const std::string &path) {
    if (path.empty()) return;
    std::string ignored;
    const int r = Run({"dbus-send", "--session", "--print-reply", "--dest=org.freedesktop.FileManager1",
                       "/org/freedesktop/FileManager1", "org.freedesktop.FileManager1.ShowItems",
                       "array:string:" + FileUri(path), "string:"}, &ignored);
    if (r == 0) return;
    std::string folder = path;
    struct stat st{};
    if (stat(path.c_str(), &st) == 0 && !S_ISDIR(st.st_mode)) {
        const size_t slash = folder.find_last_of('/');
        if (slash != std::string::npos) folder = folder.substr(0, slash ? slash : 1);
    }
    OpenExternal(folder);
}

// ---- dialogs (zenity, else kdialog) ------------------------------------------------
class Dialogs {
public:
    enum class Tool { Missing, Zenity, KDialog };
    static Tool Which() {
        static const Tool tool = OnPath("zenity") ? Tool::Zenity : OnPath("kdialog") ? Tool::KDialog : Tool::Missing;
        return tool;
    }
    static bool Available() { return Which() != Tool::Missing; }

    std::function<void()> pump; // the editor's, while a dialog is open

    // false = No / closed (or no dialog tool: then `fallback` says it).
    bool Message(const std::string &title, const std::string &text, bool warning, bool yesNo) {
        std::string out;
        switch (Which()) {
        case Tool::Zenity:
            return Run({"zenity", yesNo ? "--question" : warning ? "--warning" : "--info", "--title=" + title,
                        "--text=" + text, "--no-markup", "--width=380"}, &out, pump) == 0;
        case Tool::KDialog:
            return Run({"kdialog", "--title", title, yesNo ? "--warningyesno" : warning ? "--sorry" : "--msgbox", text},
                       &out, pump) == 0;
        default:
            if (fallback) fallback(text);
            return false;
        }
    }
    bool AskText(const std::string &title, const std::string &label, std::string &text) {
        std::string out;
        int r = -1;
        if (Which() == Tool::Zenity)
            r = Run({"zenity", "--entry", "--title=" + title, "--text=" + label, "--entry-text=" + text}, &out, pump);
        else if (Which() == Tool::KDialog)
            r = Run({"kdialog", "--title", title, "--inputbox", label, "--", text}, &out, pump); // "--": a text may start with "-"
        else { if (fallback) fallback("install zenity or kdialog for this dialog"); return false; }
        if (r != 0) return false;
        text = Line(out);
        return true;
    }
    // patterns: "*.sy1 *.zip"
    std::optional<std::string> OpenFile(const std::string &title, const std::string &label, const std::string &patterns) {
        std::string out;
        int r = -1;
        if (Which() == Tool::Zenity)
            r = Run({"zenity", "--file-selection", "--title=" + title, "--file-filter=" + label + " | " + patterns,
                     "--file-filter=All files | *"}, &out, pump);
        else if (Which() == Tool::KDialog)
            r = Run({"kdialog", "--title", title, "--getopenfilename", Home(), patterns + "|" + label}, &out, pump);
        else return NoTool();
        return Path(r, out);
    }
    std::optional<std::string> SaveFile(const std::string &title, const std::string &label, const std::string &patterns,
                                        const std::string &extension) {
        std::string out;
        int r = -1;
        if (Which() == Tool::Zenity)
            r = Run({"zenity", "--file-selection", "--save", "--confirm-overwrite", "--title=" + title,
                     "--file-filter=" + label + " | " + patterns, "--file-filter=All files | *"}, &out, pump);
        else if (Which() == Tool::KDialog)
            r = Run({"kdialog", "--title", title, "--getsavefilename", Home(), patterns + "|" + label}, &out, pump);
        else return NoTool();
        auto path = Path(r, out);
        // as OPENFILENAME's lpstrDefExt: a name without an extension gets it
        if (path && !extension.empty()) {
            const size_t slash = path->find_last_of('/'), dot = path->find_last_of('.');
            if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) *path += "." + extension;
        }
        return path;
    }
    std::optional<std::string> PickFolder(const std::string &title) {
        std::string out;
        int r = -1;
        if (Which() == Tool::Zenity)
            r = Run({"zenity", "--file-selection", "--directory", "--title=" + title}, &out, pump);
        else if (Which() == Tool::KDialog)
            r = Run({"kdialog", "--title", title, "--getexistingdirectory", Home()}, &out, pump);
        else return NoTool();
        return Path(r, out);
    }
    std::optional<uint32_t> PickColor(const std::string &title, uint32_t rgb) {
        char hex[8];
        std::snprintf(hex, sizeof hex, "#%06X", unsigned(rgb & 0xFFFFFF));
        std::string out;
        int r = -1;
        if (Which() == Tool::Zenity)
            r = Run({"zenity", "--color-selection", "--title=" + title, std::string("--color=") + hex}, &out, pump);
        else if (Which() == Tool::KDialog)
            r = Run({"kdialog", "--title", title, "--getcolor", "--default", hex}, &out, pump);
        else { if (fallback) fallback("install zenity or kdialog to pick colours"); return std::nullopt; }
        if (r != 0) return std::nullopt;
        const std::string s = Line(out);
        unsigned R = 0, G = 0, B = 0;
        if (std::sscanf(s.c_str(), "rgb(%u,%u,%u)", &R, &G, &B) == 3 || std::sscanf(s.c_str(), "rgba(%u,%u,%u", &R, &G, &B) == 3)
            return (std::min(R, 255u) << 16) | (std::min(G, 255u) << 8) | std::min(B, 255u);
        if (s.size() >= 7 && s[0] == '#') {
            const unsigned long v = std::strtoul(s.c_str() + 1, nullptr, 16);
            if (s.size() >= 13) // #rrrrggggbbbb (some GTK versions)
                return uint32_t(((v >> 40) & 255) << 16 | ((v >> 24) & 255) << 8 | ((v >> 8) & 255));
            return uint32_t(v & 0xFFFFFF);
        }
        return std::nullopt;
    }
    std::function<void(const std::string &)> fallback; // no dialog tool: say it on the LCD

private:
    static std::string Line(std::string s) {
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        return s;
    }
    static std::optional<std::string> Path(int r, const std::string &out) {
        const std::string s = Line(out);
        if (r != 0 || s.empty()) return std::nullopt;
        return s;
    }
    std::optional<std::string> NoTool() {
        if (fallback) fallback("install zenity or kdialog for file dialogs");
        return std::nullopt;
    }
    static std::string Home() {
        const char *h = std::getenv("HOME");
        return h ? std::string(h) + "/" : "/";
    }
};

// ---- one background HTTP GET (curl) ----------------------------------------------
// As win::HttpGet: Start, Running, Take, Cancel (stops curl and joins: no
// thread outlives the editor).
class HttpGet {
public:
    ~HttpGet() { Cancel(); }
    bool Start(const std::string &url) {
        if (Running()) return false;
        Cancel();
        done_ = false;
        body_.reset();
        thread_ = std::thread([this, url] {
            std::string out;
            const int r = Run({"curl", "-fsSL", "--connect-timeout", "10", "--max-time", "20", url}, &out, {},
                              [this](int pidfd) { std::lock_guard<std::mutex> lock(mutex_); pidfd_ = pidfd; });
            std::lock_guard<std::mutex> lock(mutex_);
            if (r == 0 && !out.empty()) body_ = out;
            done_ = true;
        });
        return true;
    }
    bool Running() const { return thread_.joinable() && !done_; }
    bool Done() const { return thread_.joinable() && done_; }
    // the answer (nullopt = failed), once Done()
    std::optional<std::string> Take() {
        if (thread_.joinable()) thread_.join();
        std::lock_guard<std::mutex> lock(mutex_);
        auto b = std::move(body_);
        body_.reset();
        return b;
    }
    void Cancel() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            SignalPidFd(pidfd_, SIGTERM); // only while Run still owns curl
        }
        if (thread_.joinable()) thread_.join();
        done_ = false;
    }

private:
    std::thread thread_;
    std::mutex mutex_;                 // body_, pidfd_
    std::optional<std::string> body_;
    std::atomic<bool> done_{false};
    int pidfd_ = -1;                   // the running curl, -1 = none
};

// ---- popup menus ------------------------------------------------------------------
// Win32-shaped, so the shared editor's menu code reads as on Windows: built
// with AppendMenuW (MF_STRING / MF_SEPARATOR / MF_POPUP / MF_CHECKED /
// MF_GRAYED / MF_MENUBREAK), shown by MenuTracker (LinuxX11.hpp).
inline constexpr unsigned MF_STRING = 0, MF_GRAYED = 1, MF_CHECKED = 8, MF_POPUP = 0x10, MF_MENUBREAK = 0x40,
                          MF_SEPARATOR = 0x800;

struct Menu {
    struct Item {
        std::wstring text;
        int id = 0;
        unsigned flags = 0;
        Menu *sub = nullptr;
    };
    std::vector<Item> items;
    ~Menu() {
        for (auto &i : items) delete i.sub;
    }
};

} // namespace oxo::lx

// ---- the Win32 names the shared editor calls ---------------------------------------
// DesktopEditor.cpp is written against Win32 + GDI+ (GDI+: LinuxGdiplus.hpp).
// The stateless calls are here; those needing the editor's window
// (TrackPopupMenu, MessageBoxW, GetCursorPos, GetKeyState, SetCursor,
// capture) are DesktopEditor members (Linux_Build/Source/LinuxEditor.cpp).
namespace oxo {
using UINT = unsigned;
using UINT_PTR = uintptr_t;
using LONG = long;
using ULONGLONG = uint64_t;
using HWND = unsigned long; // an X11 Window
using HMENU = lx::Menu *;
struct POINT {
    long x = 0, y = 0;
};
enum HCURSOR { IDC_ARROW, IDC_HAND, IDC_NO, IDC_SIZENWSE, IDC_COPY };
// Plain integers, as Win32's macros (cond ? MF_CHECKED : 0 mixes no enum).
inline constexpr UINT VK_RETURN = 0x0D, VK_SHIFT = 0x10, VK_CONTROL = 0x11, VK_MENU = 0x12, VK_ESCAPE = 0x1B,
                      VK_LEFT = 0x25, VK_UP = 0x26, VK_RIGHT = 0x27, VK_DOWN = 0x28, VK_LWIN = 0x5B, VK_RWIN = 0x5C;
inline constexpr UINT MB_OK = 0, MB_YESNO = 4, MB_ICONWARNING = 0x30, MB_ICONINFORMATION = 0x40, MB_DEFBUTTON2 = 0x100;
inline constexpr int IDOK = 1, IDYES = 6, IDNO = 7;
inline constexpr UINT TPM_NONOTIFY = 0x80, TPM_RETURNCMD = 0x100;
inline constexpr int SM_CXDRAG = 68, SM_CYDRAG = 69, SW_SHOWNORMAL = 1;
using lx::MF_STRING, lx::MF_GRAYED, lx::MF_CHECKED, lx::MF_POPUP, lx::MF_MENUBREAK, lx::MF_SEPARATOR;

inline HMENU CreatePopupMenu() { return new lx::Menu; }
inline void DestroyMenu(HMENU m) { delete m; } // and its submenus, as Win32
inline void AppendMenuW(HMENU m, UINT flags, UINT_PTR id, const wchar_t *text) {
    if (!m) return;
    lx::Menu::Item item;
    item.flags = flags;
    item.text = text ? text : L"";
    if (flags & MF_POPUP) item.sub = reinterpret_cast<lx::Menu *>(id);
    else item.id = int(id);
    m->items.push_back(std::move(item));
}
inline int GetMenuItemCount(HMENU m) { return m ? int(m->items.size()) : 0; }
inline HCURSOR LoadCursorW(std::nullptr_t, HCURSOR c) { return c; }
inline int GetSystemMetrics(int) { return 4; } // SM_CXDRAG / SM_CYDRAG: the drag threshold
inline ULONGLONG GetTickCount64() { return lx::NowMs(); }
inline void ShellExecuteW(HWND, const wchar_t *, const wchar_t *target, std::nullptr_t, std::nullptr_t, int) {
    if (target) lx::OpenExternal(lx::Utf8(target));
}
} // namespace oxo
