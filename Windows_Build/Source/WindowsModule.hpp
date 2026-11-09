#pragma once

// This DLL's module handle and its window classes. A class registered on
// the HOST's module (GetModuleHandleW(nullptr)) outlives the DLL: after the
// host unloads and reloads the plugin it still points at the old, freed
// WndProc. So classes are registered on our own module and unregistered
// when their last window is gone.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace oxo::win {

inline HINSTANCE ModuleInstance() {
    static const int anchor = 0;
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&anchor), &module);
    return module;
}

// One per class (a function-local static), used from the UI thread only.
// Acquire before CreateWindowExW, Release after the window is destroyed.
class WindowClass {
public:
    WindowClass(const wchar_t *name, WNDPROC proc, UINT style = 0, HBRUSH background = nullptr)
        : name_(name), proc_(proc), style_(style), background_(background) {}

    bool Acquire() {
        if (users_ == 0 && !Register()) return false;
        ++users_;
        return true;
    }
    void Release() {
        if (users_ > 0 && --users_ == 0) UnregisterClassW(name_, ModuleInstance());
    }
    const wchar_t *Name() const { return name_; }

private:
    bool Register() const {
        WNDCLASSW wc{};
        wc.style = style_;
        wc.lpfnWndProc = proc_;
        wc.hInstance = ModuleInstance();
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = background_;
        wc.lpszClassName = name_;
        if (RegisterClassW(&wc)) return true;
        // Left over by an earlier load of this DLL at the same address
        // (its WndProc is gone): replace it.
        if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
        UnregisterClassW(name_, wc.hInstance);
        return RegisterClassW(&wc) != 0;
    }

    const wchar_t *name_;
    WNDPROC proc_;
    UINT style_;
    HBRUSH background_;
    int users_ = 0;
};

} // namespace oxo::win
