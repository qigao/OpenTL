/* Native Windows GUI runtime acceptance for official WTL10 + pinned OpenATL.
 * This is intentionally NOT the full interactive upstream wtltest sample.
 * It does create, message, and destroy real (hidden) Win32 window/dialog
 * objects and executes the architecture-specific WndProc thunk.
 */
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#ifndef WINVER
#define WINVER 0x0601
#endif
#ifndef _WIN32_IE
#define _WIN32_IE 0x0601
#endif
#include <windows.h>
#include <objbase.h>
#include <stdint.h>
#include <stdio.h>
#include <atlbase.h>
#include <atlwin.h>
#include <atlapp.h>

// WTL's ordinary CMemDlgTemplate uses only these two on-disk structs;
// ActiveX hosting remains forbidden in the UI build.
#ifndef _ATL_NO_HOSTING
#error "OpenATL WTL10 UI must disable ActiveX hosting"
#endif
static_assert(sizeof(ATL::_DialogSplitHelper::DLGTEMPLATEEX) == 26,
              "Win32 extended dialog template size changed");
static_assert(sizeof(ATL::_DialogSplitHelper::DLGITEMTEMPLATEEX) == 24,
              "Win32 extended item template size changed");
#include <atlctrls.h>
#include <atlmisc.h>
#include "gui_runtime_resource.h"

#if !defined(OPENATL_REPOSITORY_HEADERS) || OPENATL_REPOSITORY_HEADERS != 0x4F41544CUL
#error "This target must compile repository-owned OpenATL headers"
#endif

static_assert(_WTL_VER >= 0x1000, "Official WTL10 headers required");

WTL::CAppModule _Module;

static bool executable_read_only(const void* ptr)
{
    if (ptr == NULL) return false;
    MEMORY_BASIC_INFORMATION page = {};
    if (::VirtualQuery(ptr, &page, sizeof(page)) != sizeof(page))
        return false;
    return page.State == MEM_COMMIT &&
        (page.Protect & 0xFFu) == PAGE_EXECUTE_READ &&
        (page.Protect & PAGE_GUARD) == 0;
}

struct ThunkState {
    int invocations;
    ThunkState() : invocations(0) {}
};

static ThunkState* expected_state = NULL;
static LRESULT CALLBACK thunk_dispatch(HWND first, UINT message, WPARAM w, LPARAM l)
{
    if (message != WM_APP + 71 || (void*)first != expected_state)
        return -100;
    ++expected_state->invocations;
    return (LRESULT)(w + l);
}

static bool check_thunk()
{
    ThunkState state;
    expected_state = &state;
    ATL::CWndProcThunk thunk;
    if (!thunk.Init(NULL, NULL) ||
        !executable_read_only(thunk.GetCodeAddress()))
        return false;
    if (!thunk.Init(thunk_dispatch, &state) ||
        !executable_read_only(thunk.GetCodeAddress()))
        return false;
    WNDPROC dispatch = thunk.GetWNDPROC();
    if (dispatch == NULL) return false;
    for (int i = 0; i < 10; ++i) {
        if (dispatch(NULL, WM_APP + 71, (WPARAM)17, (LPARAM)25) != 42)
            return false;
        if (!executable_read_only(thunk.GetCodeAddress()))
            return false;
    }
    return state.invocations == 10;
}

class ProbeWindow : public ATL::CWindowImpl<ProbeWindow> {
public:
    int messages = 0;
    int destroyed = 0;
    int finalized = 0;
    int reentrant = 0;
    int nested_contract_broken = 0;
    bool handle_nc_destroy = false;

    DECLARE_WND_CLASS(L"WTL10OpenATLNativeRuntimeProbe")

    BEGIN_MSG_MAP(ProbeWindow)
        MESSAGE_HANDLER(WM_APP + 31, OnProbe)
        MESSAGE_HANDLER(WM_APP + 33, OnReentrantDestroy)
        MESSAGE_HANDLER(WM_NCDESTROY, OnNcDestroy)
    END_MSG_MAP()

    LRESULT OnProbe(UINT, WPARAM w, LPARAM l, BOOL&) {
        ++messages;
        return (LRESULT)(w + l + 100);
    }

    LRESULT OnReentrantDestroy(UINT, WPARAM, LPARAM, BOOL&) {
        ++reentrant;
        if (finalized != 0 || m_hWnd == NULL || !::DestroyWindow(m_hWnd) ||
            finalized != 0)
            nested_contract_broken = 1;
        return 0;
    }

    LRESULT OnNcDestroy(UINT, WPARAM, LPARAM, BOOL& handled) {
        ++destroyed;
        handled = handle_nc_destroy ? TRUE : FALSE;
        return 0;
    }

    void OnFinalMessage(HWND) override {
        ++finalized;
    }
};

class ProbeDialog : public ATL::CDialogImpl<ProbeDialog> {
public:
    enum { IDD = IDD_POC_DIALOG };
    int initialized = 0;
    int messages = 0;
    int destroyed = 0;
    int finalized = 0;
    int reentrant = 0;
    int nested_contract_broken = 0;
    bool handle_nc_destroy = false;

    BEGIN_MSG_MAP(ProbeDialog)
        MESSAGE_HANDLER(WM_INITDIALOG, OnInit)
        MESSAGE_HANDLER(WM_APP + 32, OnProbe)
        MESSAGE_HANDLER(WM_APP + 34, OnReentrantDestroy)
        MESSAGE_HANDLER(WM_NCDESTROY, OnNcDestroy)
    END_MSG_MAP()

    LRESULT OnInit(UINT, WPARAM, LPARAM, BOOL&) {
        ++initialized;
        return TRUE;
    }

    LRESULT OnProbe(UINT, WPARAM, LPARAM, BOOL&) {
        ++messages;
        return 0;
    }

    LRESULT OnReentrantDestroy(UINT, WPARAM, LPARAM, BOOL&) {
        ++reentrant;
        if (finalized != 0 || m_hWnd == NULL || !::DestroyWindow(m_hWnd) ||
            finalized != 0)
            nested_contract_broken = 1;
        // A handled outer message must still retire nested WM_NCDESTROY.
        return 0;
    }

    LRESULT OnNcDestroy(UINT, WPARAM, LPARAM, BOOL& handled) {
        ++destroyed;
        handled = handle_nc_destroy ? TRUE : FALSE;
        return 0;
    }

    void OnFinalMessage(HWND) override {
        ++finalized;
    }
};

// Exercise the contained-window map independently of CWindowImpl.
class ContainedDestroyMap : public ATL::CMessageMap {
public:
    int destroyed = 0;
    bool handle_nc_destroy = false;
    BEGIN_MSG_MAP(ContainedDestroyMap)
        MESSAGE_HANDLER(WM_NCDESTROY, OnNcDestroy)
    END_MSG_MAP()
    LRESULT OnNcDestroy(UINT, WPARAM, LPARAM, BOOL& handled) {
        ++destroyed;
        handled = handle_nc_destroy ? TRUE : FALSE;
        return 0;
    }
};

static bool check_window_text()
{
    HWND raw = ::CreateWindowExW(0, L"EDIT", L"", WS_POPUP,
                                0, 0, 80, 30, NULL, NULL,
                                ::GetModuleHandleW(NULL), NULL);
    if (!raw) return false;

    // Weasel's CEdit consumer uses the inherited CWindow CString overload.
    // Keep its real text/control behavior in this existing UI-only test.
    WTL::CEdit edit(raw);
    const ATL::CWindow& window = edit;
    std::wstring expected(4096, L'\u4e2d');
    expected += L"-\u6587-\U0001f642";
    ATL::CString text(L"stale");
    bool ok = edit.SetWindowText(expected.c_str()) &&
        edit.GetWindowText(text) == static_cast<int>(expected.size()) &&
        text.GetLength() == static_cast<int>(expected.size()) &&
        text.Compare(expected.c_str()) == 0;

    wchar_t bounded[4] = {};
    ok = ok && window.GetWindowText(bounded, 4) == 3 &&
        bounded[0] == L'\u4e2d' && bounded[2] == L'\u4e2d' && bounded[3] == 0;
    ok = ok && edit.SetWindowText(L"short") &&
        window.GetWindowText(text) == 5 && text.Compare(L"short") == 0;
    ok = ok && edit.SetWindowText(L"") &&
        window.GetWindowText(text) == 0 && text.IsEmpty();

    const bool destroyed = ::DestroyWindow(raw) != FALSE;
    return ok && destroyed;
}

static bool check_window()
{
    if (!check_window_text()) return false;
    for (int i = 0; i < 12; ++i) {
        ProbeWindow wnd;
        wnd.handle_nc_destroy = (i & 1) != 0;
        RECT bounds = {0, 0, 128, 56};
        HWND h = wnd.Create(NULL, bounds, L"WTL10 hidden runtime",
                            WS_POPUP, 0);
        if (h == NULL || !::IsWindow(h) || !executable_read_only(wnd.m_thunk.GetCodeAddress()))
            return false;
        if ((WNDPROC)::GetWindowLongPtrW(h, GWLP_WNDPROC) != wnd.m_thunk.GetWNDPROC())
            return false;
        if (::SendMessageW(h, WM_APP + 31, 13, 29) != 142)
            return false;
        if (wnd.messages != 1 || !::DestroyWindow(h) || ::IsWindow(h))
            return false;
        if (wnd.destroyed != 1 || wnd.finalized != 1 || wnd.m_hWnd != NULL)
            return false;
    }
    // Nested WM_NCDESTROY defers finalization until the outer dispatch unwinds.
    for (int i = 0; i < 12; ++i) {
        ProbeWindow wnd;
        wnd.handle_nc_destroy = (i & 1) != 0;
        RECT bounds = {0, 0, 128, 56};
        HWND h = wnd.Create(NULL, bounds, L"WTL10 nested destroy", WS_POPUP, 0);
        if (!h || !executable_read_only(wnd.m_thunk.GetCodeAddress()))
            return false;
        ::SendMessageW(h, WM_APP + 33, 0, 0);
        if (::IsWindow(h) || wnd.reentrant != 1 ||
            wnd.nested_contract_broken != 0 || wnd.destroyed != 1 ||
            wnd.finalized != 1 || wnd.m_hWnd != NULL)
            return false;
    }
    // A subclassed Win32 control must retire an explicitly handled
    // WM_NCDESTROY and notify its owner exactly once.
    for (int i = 0; i < 12; ++i) {
        HWND raw = ::CreateWindowExW(0, L"STATIC", L"OpenATL subclassed HWND",
                                    WS_POPUP, 0, 0, 80, 30,
                                    NULL, NULL, ::GetModuleHandleW(NULL), NULL);
        if (!raw) return false;
        ProbeWindow wnd;
        wnd.handle_nc_destroy = (i & 1) != 0;
        const bool attached = wnd.SubclassWindow(raw) != FALSE;
        const void* liveCode = wnd.m_thunk.GetCodeAddress();
        const bool rx = attached && executable_read_only(liveCode);
        // Re-admitting the same owner must fail WITHOUT invalidating the
        // first HWND's WndProc or changing its RX thunk.
        const BOOL duplicate = attached ? wnd.SubclassWindow(raw) : FALSE;
        const DWORD duplicateError = ::GetLastError();
        const bool stable = attached && !duplicate &&
            duplicateError == ERROR_ALREADY_EXISTS && wnd.m_hWnd == raw &&
            wnd.m_thunk.GetCodeAddress() == liveCode &&
            (WNDPROC)::GetWindowLongPtrW(raw, GWLP_WNDPROC) == wnd.m_thunk.GetWndProc();
        const bool destroyed = ::DestroyWindow(raw) != FALSE;
        if (!attached || !rx || !stable || !destroyed || ::IsWindow(raw) ||
            wnd.destroyed != 1 || wnd.finalized != 1 || wnd.m_hWnd != NULL)
            return false;
    }
    // CContainedWindowT has no OnFinalMessage hook, but must clear its
    // borrowed HWND for both handled and unhandled WM_NCDESTROY.
    for (int i = 0; i < 12; ++i) {
        HWND raw = ::CreateWindowExW(0, L"STATIC", L"OpenATL contained HWND",
                                    WS_POPUP, 0, 0, 80, 30,
                                    NULL, NULL, ::GetModuleHandleW(NULL), NULL);
        if (!raw) return false;
        ContainedDestroyMap map;
        map.handle_nc_destroy = (i & 1) != 0;
        ATL::CContainedWindow contained(&map);
        const bool attached = contained.SubclassWindow(raw) != FALSE;
        const void* liveCode = contained.m_thunk.GetCodeAddress();
        const bool rx = attached && executable_read_only(liveCode);
        const BOOL duplicate = attached ? contained.SubclassWindow(raw) : FALSE;
        const DWORD duplicateError = ::GetLastError();
        const bool stable = attached && !duplicate &&
            duplicateError == ERROR_ALREADY_EXISTS && contained.m_hWnd == raw &&
            contained.m_thunk.GetCodeAddress() == liveCode &&
            (WNDPROC)::GetWindowLongPtrW(raw, GWLP_WNDPROC) == contained.m_thunk.GetWndProc();
        const bool destroyed = ::DestroyWindow(raw) != FALSE;
        if (!attached || !rx || !stable || !destroyed || ::IsWindow(raw) ||
            map.destroyed != 1 || contained.m_hWnd != NULL)
            return false;
    }
    return true;
}

static bool check_dialog()
{
    HINSTANCE executable = ::GetModuleHandleW(NULL);
    wchar_t resource_string[64] = {};
    if (::FindResourceW(executable, MAKEINTRESOURCEW(IDD_POC_DIALOG), RT_DIALOG) == NULL ||
        ATL::AtlLoadString(IDS_POC_STRING, resource_string, 64) != 24 ||
        ::lstrcmpW(resource_string, L"WTL10_RESOURCE_NATIVE_OK") != 0)
        return false;

    for (int i = 0; i < 6; ++i) {
        ProbeDialog dlg;
        dlg.handle_nc_destroy = (i & 1) != 0;
        HWND h = dlg.Create(NULL);
        if (!h || !::IsWindow(h) || dlg.initialized != 1)
            return false;
        if (!executable_read_only(dlg.m_thunk.GetCodeAddress()))
            return false;
        if (::GetDlgItem(h, IDOK) == NULL)
            return false;
        ::SendMessageW(h, WM_APP + 32, 0, 0);
        if (dlg.messages != 1 || !dlg.DestroyWindow() || ::IsWindow(h))
            return false;
        if (dlg.destroyed != 1 || dlg.finalized != 1 || dlg.m_hWnd != NULL)
            return false;
    }
    for (int i = 0; i < 6; ++i) {
        ProbeDialog dlg;
        dlg.handle_nc_destroy = (i & 1) != 0;
        HWND h = dlg.Create(NULL);
        if (!h || dlg.initialized != 1 ||
            !executable_read_only(dlg.m_thunk.GetCodeAddress()))
            return false;
        ::SendMessageW(h, WM_APP + 34, 0, 0);
        if (::IsWindow(h) || dlg.reentrant != 1 ||
            dlg.nested_contract_broken != 0 || dlg.destroyed != 1 ||
            dlg.finalized != 1 || dlg.m_hWnd != NULL) {
            fprintf(stderr,
                "Nested dialog cleanup failed: nc=%d final=%d hwnd=%p bad=%d\n",
                dlg.destroyed, dlg.finalized, (void*)dlg.m_hWnd,
                dlg.nested_contract_broken);
            return false;
        }
    }
    return true;
}

// Exercise the public prebuilt modal dialog with real resources/messages.
// A hook restricted to this test thread closes it without an interactive user.
static int simple_dialog_activations = 0;
static int simple_dialog_button = IDOK;
static HHOOK simple_dialog_hook = NULL;
static LRESULT CALLBACK close_simple_dialog(int code, WPARAM w, LPARAM l)
{
    if (code == HCBT_ACTIVATE && ::GetDlgItem(reinterpret_cast<HWND>(w), IDOK)) {
        ++simple_dialog_activations;
        ::PostMessageW(reinterpret_cast<HWND>(w), WM_COMMAND, 99, 0);
        ::PostMessageW(reinterpret_cast<HWND>(w), WM_COMMAND, simple_dialog_button, 0);
    }
    return ::CallNextHookEx(simple_dialog_hook, code, w, l);
}

static bool check_simple_dialog()
{
    simple_dialog_hook = ::SetWindowsHookExW(WH_CBT, close_simple_dialog, NULL,
                                             ::GetCurrentThreadId());
    if (!simple_dialog_hook) return false;
    ATL::CSimpleDialog<IDD_POC_DIALOG> centered;
    const INT_PTR ok = centered.DoModal(NULL);
    simple_dialog_button = IDNO;
    ATL::CSimpleDialog<IDD_POC_DIALOG, FALSE> uncentered;
    const INT_PTR no = uncentered.DoModal(NULL);
    const BOOL removed = ::UnhookWindowsHookEx(simple_dialog_hook);
    simple_dialog_hook = NULL;
    return removed && ok == IDOK && no == IDNO && simple_dialog_activations == 2 &&
           centered.m_hWnd == NULL && uncentered.m_hWnd == NULL;
}


struct OwnedPopupMenu {
    HMENU menu;
    OwnedPopupMenu() : menu(::CreatePopupMenu()) {}
    ~OwnedPopupMenu() { if (menu != NULL) ::DestroyMenu(menu); }
    OwnedPopupMenu(const OwnedPopupMenu&) = delete;
    OwnedPopupMenu& operator=(const OwnedPopupMenu&) = delete;
};

struct MRURegistryFixture {
    wchar_t path[160] = {};
    bool owned = false;
    bool Create() {
        LARGE_INTEGER counter = {};
        if (!::QueryPerformanceCounter(&counter))
            return false;
        swprintf_s(path, _countof(path), L"Software\\OpenATL_MRU_%lu_%08lx_%08lx",
                   static_cast<unsigned long>(::GetCurrentProcessId()),
                   static_cast<unsigned long>(counter.HighPart),
                   static_cast<unsigned long>(counter.LowPart));
        HKEY fresh = nullptr;
        DWORD disposition = 0;
        const LSTATUS status = ::RegCreateKeyExW(
            HKEY_CURRENT_USER, path, 0, nullptr, REG_OPTION_NON_VOLATILE,
            KEY_READ | KEY_WRITE, nullptr, &fresh, &disposition);
        if (status != ERROR_SUCCESS)
            return false;
        owned = (disposition == REG_CREATED_NEW_KEY);
        const LSTATUS closeStatus = ::RegCloseKey(fresh);
        return owned && closeStatus == ERROR_SUCCESS;
    }
    bool Cleanup() {
        if (!owned || ::RegDeleteTreeW(HKEY_CURRENT_USER, path) !=
                          ERROR_SUCCESS)
            return false;
        owned = false;
        HKEY probe = nullptr;
        const LSTATUS status = ::RegOpenKeyExW(
            HKEY_CURRENT_USER, path, 0, KEY_READ, &probe);
        if (status == ERROR_SUCCESS)
            (void)::RegCloseKey(probe);
        return status == ERROR_FILE_NOT_FOUND;
    }
    ~MRURegistryFixture() {
        if (owned)
            (void)::RegDeleteTreeW(HKEY_CURRENT_USER, path);
    }
};

static bool mru_fail(const char* stage) {
    fprintf(stderr, "WTL10_MRU_FAIL stage=%s winerr=%lu\n",
            stage, static_cast<unsigned long>(::GetLastError()));
    return false;
}

static bool mru_equals(WTL::CRecentDocumentList& recent,
                       int menuId, const wchar_t* expected) {
    wchar_t value[MAX_PATH] = {};
    return recent.GetFromList(menuId, value, _countof(value)) &&
           ::lstrcmpW(value, expected) == 0;
}

static bool check_mru_registry_and_menu() {
    OwnedPopupMenu original;
    if (original.menu == NULL ||
        !::AppendMenuW(original.menu, MF_STRING | MF_GRAYED,
                       ID_FILE_MRU_FIRST, L"(empty)"))
        return mru_fail("popup_menu");

    WTL::CRecentDocumentList recent;
    recent.SetMenuHandle(original.menu);
    recent.SetMaxEntries(3);
    if (!recent.AddToList(L"C:\\Alpha.txt") ||
        !recent.AddToList(L"C:\\Beta.txt") ||
        !recent.AddToList(L"C:\\\u6771\u4eac.txt") ||
        !recent.AddToList(L"C:\\Delta.txt"))
        return mru_fail("add_capacity");

    // Fourth insertion evicts Alpha; newest-first menu IDs map D, Tokyo, B.
    if (recent.m_arrDocs.GetSize() != 3 ||
        !mru_equals(recent, ID_FILE_MRU_FIRST, L"C:\\Delta.txt") ||
        !mru_equals(recent, ID_FILE_MRU_FIRST + 1, L"C:\\\u6771\u4eac.txt") ||
        !mru_equals(recent, ID_FILE_MRU_FIRST + 2, L"C:\\Beta.txt"))
        return mru_fail("eviction_order");

    if (!recent.MoveToTop(ID_FILE_MRU_FIRST + 1) ||
        !mru_equals(recent, ID_FILE_MRU_FIRST, L"C:\\\u6771\u4eac.txt") ||
        !recent.RemoveFromList(ID_FILE_MRU_FIRST + 2) ||
        recent.m_arrDocs.GetSize() != 2)
        return mru_fail("reorder_remove");

    // Case-insensitive duplicate must move existing entry, not add another.
    if (!recent.AddToList(L"c:\\\u6771\u4eac.txt") ||
        recent.m_arrDocs.GetSize() != 2 ||
        !mru_equals(recent, ID_FILE_MRU_FIRST, L"c:\\\u6771\u4eac.txt") ||
        !mru_equals(recent, ID_FILE_MRU_FIRST + 1, L"C:\\Delta.txt"))
        return mru_fail("dedup");

    wchar_t menuLabel[MAX_PATH + 16] = {};
    if (::GetMenuStringW(original.menu, ID_FILE_MRU_FIRST,
                         menuLabel, _countof(menuLabel), MF_BYCOMMAND) == 0 ||
        ::lstrcmpW(menuLabel, L"&1 c:\\\u6771\u4eac.txt") != 0)
        return mru_fail("menu_text");

    wchar_t tooShort[2] = {};
    if (recent.GetFromList(ID_FILE_MRU_FIRST, tooShort,
                           _countof(tooShort)) ||
        recent.GetFromList(ID_FILE_MRU_FIRST + 2, menuLabel,
                           _countof(menuLabel)))
        return mru_fail("item_bounds");

    MRURegistryFixture scope;
    if (!scope.Create() || !recent.WriteToRegistry(scope.path))
        return mru_fail("registry_write");
    OwnedPopupMenu restoredMenu;
    if (restoredMenu.menu == NULL ||
        !::AppendMenuW(restoredMenu.menu, MF_STRING | MF_GRAYED,
                       ID_FILE_MRU_FIRST, L"(empty)"))
        return mru_fail("restored_menu");
    WTL::CRecentDocumentList restored;
    restored.SetMenuHandle(restoredMenu.menu);
    if (!restored.ReadFromRegistry(scope.path) ||
        restored.GetMaxEntries() != 3 ||
        restored.m_arrDocs.GetSize() != 2 ||
        !mru_equals(restored, ID_FILE_MRU_FIRST, L"c:\\\u6771\u4eac.txt") ||
        !mru_equals(restored, ID_FILE_MRU_FIRST + 1, L"C:\\Delta.txt"))
        return mru_fail("registry_roundtrip");

    if (!scope.Cleanup())
        return mru_fail("registry_cleanup");
    ULONG_PTR context = 0;
    return ::CoGetContextToken(&context) == CO_E_NOTINITIALIZED;
}

int main(void)
{
    // UI-only contract: no COM apartment is initialized by this process.
    // CoGetContextToken does NOT initialize one, it only checks the state.
    ULONG_PTR context_token = 0;
    if (::CoGetContextToken(&context_token) != CO_E_NOTINITIALIZED) {
        fprintf(stderr, "WTL-only GUI must start with COM uninitialized\n");
        return 10;
    }
    HRESULT init = _Module.Init(NULL, ::GetModuleHandleW(NULL));
    if (FAILED(init)) {
        fprintf(stderr, "WTL CAppModule init failed without COM\n");
        return 11;
    }

    // Do not keep a live GUI window when releasing executable thunk pages.
    const bool thunk_ok = check_thunk();
    const bool window_ok = thunk_ok && check_window();
    const bool dialog_ok = window_ok && check_dialog();
    const bool simple_dialog_ok = dialog_ok && check_simple_dialog();
    const bool mru_ok = simple_dialog_ok && check_mru_registry_and_menu();

    _Module.Term();
    const bool no_com = (::CoGetContextToken(&context_token) == CO_E_NOTINITIALIZED);

    if (!thunk_ok || !window_ok || !dialog_ok || !simple_dialog_ok || !mru_ok || !no_com) {
        fprintf(stderr, "WTL10 GUI runtime FAILURE thunk=%d window=%d dialog=%d mru=%d no_com=%d err=%lu\n",
                (int)thunk_ok, (int)window_ok, (int)dialog_ok, (int)mru_ok,
                (int)no_com, (unsigned long)::GetLastError());
        return !thunk_ok ? 20 : (!window_ok ? 21 : (!dialog_ok ? 22 : (!mru_ok ? 24 : 23)));
    }
    puts("WTL10 GUI runtime PASS: no COM apartment, W^X thunk, message maps, resource dialog, NCDESTROY, OnFinalMessage, MRU");
    return 0;
}
