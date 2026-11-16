// Linux counterpart of WindowsTrash.cpp: the freedesktop.org home Trash
// ($XDG_DATA_HOME/Trash), so file managers can restore what was deleted.
// false = no usable Trash (another filesystem, I/O error): the caller then
// deletes for good and says so, same contract as the mac / Windows builds.
#include "EditorCore/Trash.hpp"

#include <cctype>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <string>
#include <system_error>
#include <sys/stat.h>

namespace oxo {
namespace {
std::filesystem::path TrashRoot() {
    if (const char *x = std::getenv("XDG_DATA_HOME"); x && *x) return std::filesystem::path(x) / "Trash";
    if (const char *h = std::getenv("HOME"); h && *h) return std::filesystem::path(h) / ".local/share/Trash";
    return {};
}
// The device of `p`, or of its nearest existing parent (-1 = none).
dev_t DeviceOf(std::filesystem::path p) {
    struct stat st{};
    while (!p.empty()) {
        if (::stat(p.c_str(), &st) == 0) return st.st_dev;
        if (p == p.parent_path()) break;
        p = p.parent_path();
    }
    return dev_t(-1);
}
std::string PercentEncode(const std::string &s) {
    static const char *hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '/' || c == '-' || c == '_' || c == '.' || c == '~') out += char(c);
        else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
    }
    return out;
}
} // namespace

// The home Trash takes what is on its own filesystem (a rename); items on
// another drive would need that drive's .Trash-$UID: those are deleted.
bool CanRecycle(const std::filesystem::path &path) {
    if (path.empty() || !path.is_absolute()) return false;
    const std::filesystem::path trash = TrashRoot();
    if (trash.empty()) return false;
    const dev_t device = DeviceOf(path);
    return device != dev_t(-1) && device == DeviceOf(trash);
}

bool MoveToTrash(const std::filesystem::path &path) {
    namespace fs = std::filesystem;
    if (!CanRecycle(path)) return false;
    std::error_code ec;
    const fs::path trash = TrashRoot(), files = trash / "files", info = trash / "info";
    fs::create_directories(files, ec);
    fs::create_directories(info, ec);
    if (ec) return false;

    const std::string base = path.filename().string();
    std::string name = base;
    for (int n = 2; fs::exists(files / name) || fs::exists(info / (name + ".trashinfo")); ++n)
        name = base + "." + std::to_string(n);

    char date[32];
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);
    std::strftime(date, sizeof date, "%Y-%m-%dT%H:%M:%S", &tm);
    {
        std::ofstream f(info / (name + ".trashinfo"), std::ios::trunc);
        if (!f) return false;
        f << "[Trash Info]\nPath=" << PercentEncode(path.string()) << "\nDeletionDate=" << date << "\n";
        if (!f) return false;
    }
    fs::rename(path, files / name, ec); // fails across filesystems: no copy, caller deletes
    if (ec) {
        fs::remove(info / (name + ".trashinfo"), ec);
        return false;
    }
    return true;
}
} // namespace oxo
