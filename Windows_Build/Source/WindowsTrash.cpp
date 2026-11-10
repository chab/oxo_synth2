// Match the Mac editor's "move to Trash" contract using the Windows Recycle
// Bin instead. This file is used only in the Windows build.
// EditorCore/Trash.hpp: true = the item IS in the Recycle Bin, false = there
// is none for it (the caller then deletes it and says so). SHFileOperation
// with FOF_ALLOWUNDO silently deletes for good where Windows cannot
// recycle (removable and network drives, relative paths) and still reports
// success, so those cases are refused before the call.
#include "EditorCore/Trash.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <string>

namespace oxo {
bool MoveToTrash(const std::filesystem::path &path) {
    if (path.empty() || !path.is_absolute()) return false;
    const std::wstring root = path.root_path().wstring();
    if (GetDriveTypeW(root.c_str()) != DRIVE_FIXED) return false;
    SHQUERYRBINFO bin{};
    bin.cbSize = sizeof(bin);
    if (FAILED(SHQueryRecycleBinW(root.c_str(), &bin))) return false;
    std::wstring from = path.wstring();
    from.push_back(L'\0');
    from.push_back(L'\0'); // SHFileOperation needs a double-NUL list
    SHFILEOPSTRUCTW operation{};
    operation.wFunc = FO_DELETE;
    operation.pFrom = from.c_str();
    operation.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
    return SHFileOperationW(&operation) == 0 && !operation.fAnyOperationsAborted;
}
} // namespace oxo
