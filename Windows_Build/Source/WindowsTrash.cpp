// Match the Mac editor's "move to Trash" contract using the Windows Recycle
// Bin instead. This file is used only in the Windows build.
#include "EditorCore/Trash.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <string>

namespace oxo {
bool MoveToTrash(const std::filesystem::path &path) {
    if (path.empty()) return false;
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
