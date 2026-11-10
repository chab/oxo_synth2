#pragma once

// A modal one-line text prompt (Win32 has none built in), from an
// in-memory dialog template: no resource file. UI thread only.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>
#include <vector>

#include "WindowsModule.hpp"

namespace oxo::win {

namespace detail {
inline INT_PTR CALLBACK PromptProc(HWND dialog, UINT msg, WPARAM wp, LPARAM lp) {
    constexpr int kEdit = 100;
    switch (msg) {
    case WM_INITDIALOG: {
        SetWindowLongPtrW(dialog, DWLP_USER, lp);
        const auto *text = reinterpret_cast<const std::wstring *>(lp);
        SetDlgItemTextW(dialog, kEdit, text->c_str());
        SendDlgItemMessageW(dialog, kEdit, EM_SETSEL, 0, -1);
        SetFocus(GetDlgItem(dialog, kEdit));
        return FALSE; // focus set by hand
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK) {
            auto *text = reinterpret_cast<std::wstring *>(GetWindowLongPtrW(dialog, DWLP_USER));
            const HWND edit = GetDlgItem(dialog, kEdit);
            text->assign(size_t(GetWindowTextLengthW(edit)) + 1, L'\0');
            text->resize(size_t(GetWindowTextW(edit, text->data(), int(text->size()))));
            EndDialog(dialog, IDOK);
            return TRUE;
        }
        if (LOWORD(wp) == IDCANCEL) {
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        }
        return FALSE;
    default: return FALSE;
    }
}
} // namespace detail

// true = OK with `text` (in: the proposed value, out: what was typed).
inline bool AskText(HWND owner, const wchar_t *title, const wchar_t *prompt, std::wstring &text) {
    std::vector<WORD> t; // DLGTEMPLATE + DLGITEMTEMPLATEs, in dialog units
    const auto dword = [&](DWORD v) { t.push_back(LOWORD(v)), t.push_back(HIWORD(v)); };
    const auto str = [&](const wchar_t *s) {
        for (; *s; ++s) t.push_back(WORD(*s));
        t.push_back(0);
    };
    dword(DS_MODALFRAME | DS_CENTER | DS_SETFONT | WS_POPUP | WS_CAPTION | WS_SYSMENU);
    dword(0);
    t.push_back(4); // items
    for (const WORD v : {0, 0, 220, 62}) t.push_back(v);
    t.push_back(0); // no menu
    t.push_back(0); // default class
    str(title);
    t.push_back(9); // DS_SETFONT: point size, face
    str(L"Segoe UI");
    const auto item = [&](DWORD style, WORD x, WORD y, WORD cx, WORD cy, WORD id, WORD atom, const wchar_t *caption) {
        if (t.size() % 2) t.push_back(0); // items are DWORD aligned
        dword(style | WS_CHILD | WS_VISIBLE);
        dword(0);
        for (const WORD v : {x, y, cx, cy, id}) t.push_back(v);
        t.push_back(0xFFFF); // predefined class by atom
        t.push_back(atom);
        str(caption);
        t.push_back(0); // no creation data
    };
    constexpr WORD kButton = 0x0080, kEditClass = 0x0081, kStatic = 0x0082;
    item(SS_LEFT, 7, 7, 206, 9, 0xFFFF, kStatic, prompt);
    item(ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, 7, 19, 206, 13, 100, kEditClass, L"");
    item(BS_DEFPUSHBUTTON | WS_TABSTOP, 109, 40, 50, 14, IDOK, kButton, L"OK");
    item(BS_PUSHBUTTON | WS_TABSTOP, 163, 40, 50, 14, IDCANCEL, kButton, L"Cancel");
    std::wstring value = text;
    const INT_PTR result = DialogBoxIndirectParamW(ModuleInstance(), reinterpret_cast<LPCDLGTEMPLATEW>(t.data()),
                                                   owner, detail::PromptProc, reinterpret_cast<LPARAM>(&value));
    if (result != IDOK) return false;
    text = std::move(value);
    return true;
}

} // namespace oxo::win
