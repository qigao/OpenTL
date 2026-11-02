// OpenATL opt-in ownership boundary for native modeless sheet creation.
#ifndef OPENATL_PROPERTY_SHEET_CREATION_H
#define OPENATL_PROPERTY_SHEET_CREATION_H
#pragma once
#include <windows.h>
#include <exception>

namespace ATL {
// The owner and this scope belong to the window thread and the same module as
// its OpenATL host. The owner must outlive this scope. Check IsInstalled() and
// fail creation if installing the hook fails; never call DoModal() in the scope.
// Bind the WTL owner's m_hWnd BEFORE calling Create(). While native creation
// is on the stack, a direct DestroyWindow(sheet) is rejected by a thread-local
// CBT hook. ActiveX hosting observes CancellationPending() and aborts loading.
// Complete() removes the hook and retires the cancelled sheet after native
// creation unwinds. This scope is for modeless Create(), not modal DoModal().
class CPropertySheetCreationGuard {
public:
    explicit CPropertySheetCreationGuard(HWND& ownerWindow) noexcept
        : ownerWindow_(ownerWindow), previous_(Current()), thread_(::GetCurrentThreadId()) {
        hook_ = ::SetWindowsHookExW(WH_CBT, Hook, nullptr, thread_);
        if (hook_) Current() = this;
    }
    CPropertySheetCreationGuard(const CPropertySheetCreationGuard&) = delete;
    CPropertySheetCreationGuard& operator=(const CPropertySheetCreationGuard&) = delete;
    ~CPropertySheetCreationGuard() noexcept {
        const DWORD error = ::GetLastError();
        End();
        RetireCancelled();
        ::SetLastError(error);
    }
    bool IsInstalled() const noexcept { return hook_ != nullptr; }
    bool IsCancelled() const noexcept { return cancelledWindow_ != nullptr; }
    HWND Complete(HWND created) noexcept {
        const DWORD error = ::GetLastError();
        End();
        if (!IsCancelled()) { ::SetLastError(error); return created; }
        RetireCancelled();
        ::SetLastError(ERROR_CANCELLED);
        return nullptr;
    }
    // Used after external COM calls, before activation or application init.
    static bool CancellationPending(HWND window) noexcept {
        if (!window) return false;
        for (auto guard = Current(); guard; guard = guard->previous_) {
            const HWND sheet = guard->cancelledWindow_;
            if (sheet && (window == sheet || ::IsChild(sheet, window))) return true;
        }
        return false;
    }
private:
    static CPropertySheetCreationGuard*& Current() noexcept {
        static thread_local CPropertySheetCreationGuard* current = nullptr;
        return current;
    }
    static LRESULT CALLBACK Hook(int code, WPARAM w, LPARAM l) noexcept {
        if (code == HCBT_DESTROYWND) {
            const HWND window = reinterpret_cast<HWND>(w);
            bool blocked = false;
            for (auto guard = Current(); guard; guard = guard->previous_) {
                if (window && window == guard->ownerWindow_) {
                    guard->cancelledWindow_ = window;
                    blocked = true;
                }
            }
            if (blocked) return 1; // Before WM_DESTROY or any native free.
        }
        return ::CallNextHookEx(nullptr, code, w, l);
    }
    void End() noexcept {
        if (!hook_) return;
        // Hook callbacks and TLS may never outlive their scope or thread.
        if (::GetCurrentThreadId() != thread_ || Current() != this ||
            !::UnhookWindowsHookEx(hook_)) std::terminate();
        hook_ = nullptr;
        Current() = previous_;
    }
    void RetireCancelled() noexcept {
        if (retired_ || !cancelledWindow_) return;
        // An outer scope for this same owner still protects native creation.
        for (auto guard = Current(); guard; guard = guard->previous_)
            if (guard->cancelledWindow_ == cancelledWindow_) return;
        retired_ = true;
        if (::IsWindow(cancelledWindow_) && !::DestroyWindow(cancelledWindow_))
            std::terminate(); // Never return a live sheet to a dead owner.
    }
    HWND& ownerWindow_;
    CPropertySheetCreationGuard* previous_;
    DWORD thread_;
    HHOOK hook_ = nullptr;
    HWND cancelledWindow_ = nullptr;
    bool retired_ = false;
};
} // namespace ATL
#endif
