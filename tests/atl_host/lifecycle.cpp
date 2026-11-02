// Registered COM factories and actual HWNDs exercise host transaction ordering
// and reentrant retirement without relying on a particular installed control.
#include <atlhost.h>
#include <atlctl.h>
#include <cstdio>
#include <new>
#include <cstring>
#include <cwchar>
#include <thread>
#include <stdexcept>

#define REQUIRE(x) do { if (!(x)) { std::fprintf(stderr, "HOST_LIFECYCLE_FAIL line=%d: %s\n", __LINE__, #x); return false; } } while (0)
namespace {
template<class T> class OwnedWindow : public T {
public:
    ~OwnedWindow() { if (::IsWindow(this->m_hWnd)) ::DestroyWindow(this->m_hWnd); }
};
struct UIObjectState { ULONG refs = 1; int destroyed = 0; };
class UIObject : public IDispatch, public IDropTarget, public IDataObject,
                 public IOleCommandTarget, public IOleInPlaceActiveObject {
public:
    explicit UIObject(UIObjectState* state = nullptr) : state_(state) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** value) override {
        if (!value) return E_POINTER; *value = nullptr;
        if (iid == IID_IUnknown || iid == IID_IDispatch) *value = static_cast<IDispatch*>(this);
        else if (iid == IID_IDropTarget) *value = static_cast<IDropTarget*>(this);
        else if (iid == IID_IDataObject) *value = static_cast<IDataObject*>(this);
        else if (iid == IID_IOleCommandTarget) *value = static_cast<IOleCommandTarget*>(this);
        else if (iid == IID_IOleInPlaceActiveObject) *value = static_cast<IOleInPlaceActiveObject*>(this);
        else return E_NOINTERFACE;
        AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { ++refs_; if (state_) state_->refs = refs_; return refs_; }
    ULONG STDMETHODCALLTYPE Release() override { const auto n = --refs_; if (state_) state_->refs = n; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT* count) override { if (!count) return E_POINTER; *count = 0; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT, LCID, ITypeInfo**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID iid, LPOLESTR* names, UINT count, LCID, DISPID* ids) override {
        if (iid != IID_NULL) return DISP_E_UNKNOWNINTERFACE;
        if (!names || !ids) return E_POINTER;
        if (count != 1 || !names[0] || ::CompareStringOrdinal(names[0], -1, L"OpenWTLHostToken", -1, TRUE) != CSTR_EQUAL) return DISP_E_UNKNOWNNAME;
        ids[0] = 1234; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Invoke(DISPID id, REFIID iid, LCID, WORD flags, DISPPARAMS* args, VARIANT* result, EXCEPINFO*, UINT*) override {
        if (iid != IID_NULL) return DISP_E_UNKNOWNINTERFACE;
        if (id != 1234 || flags != DISPATCH_PROPERTYGET) return DISP_E_MEMBERNOTFOUND;
        if (!args || !result) return E_POINTER;
        if (args->cArgs || args->cNamedArgs) return DISP_E_BADPARAMCOUNT;
        ::VariantInit(result); result->vt = VT_I4; result->lVal = 0x4f57544c; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject*, DWORD, POINTL, DWORD*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE DragOver(DWORD, POINTL, DWORD*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE DragLeave() override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Drop(IDataObject*, DWORD, POINTL, DWORD*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetData(FORMATETC*, STGMEDIUM*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC*, STGMEDIUM*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC*, FORMATETC*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetData(FORMATETC*, STGMEDIUM*, BOOL) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD, IEnumFORMATETC**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE QueryStatus(const GUID*, ULONG, OLECMD[], OLECMDTEXT*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Exec(const GUID*, DWORD, DWORD, VARIANT*, VARIANT*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetWindow(HWND* value) override { if (!value) return E_POINTER; *value = nullptr; return E_FAIL; }
    HRESULT STDMETHODCALLTYPE ContextSensitiveHelp(BOOL) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE TranslateAccelerator(LPMSG) override { return S_FALSE; }
    HRESULT STDMETHODCALLTYPE OnFrameWindowActivate(BOOL) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDocWindowActivate(BOOL) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE ResizeBorder(LPCRECT, IOleInPlaceUIWindow*, BOOL) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE EnableModeless(BOOL) override { return S_OK; }
private:
    ~UIObject() { if (state_) ++state_->destroyed; }
    ULONG refs_ = 1;
    UIObjectState* state_;
};
struct UIState {
    int calls[15]{}, destroyed = 0, action = 0;
    bool good = true, inside = false, diedInside = false, omitResult = false, nullResult = false;
    bool wrongInterface = false, nullGroup = false, browser = false, destroyOnRetain = false;
    HRESULT transport = S_OK, result = S_FALSE;
    IAxWinHostWindow* host = nullptr;
    IDocHostUIHandlerDispatch* replacement = nullptr;
    UIObject* object = nullptr;
    IUnknown* frame = nullptr;
    HWND window = nullptr;
    MSG message{};
};
class UIHandler : public IDocHostUIHandlerDispatch {
public:
    explicit UIHandler(UIState& state) : state_(state) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** value) override {
        if (!value) return E_POINTER; *value = nullptr;
        if (iid != IID_IUnknown && iid != IID_IDispatch && iid != __uuidof(IDocHostUIHandlerDispatch)) return E_NOINTERFACE;
        *value = static_cast<IDocHostUIHandlerDispatch*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {
        ++refs_;
        if (state_.destroyOnRetain) { state_.destroyOnRetain = false; ::DestroyWindow(state_.window); }
        return refs_;
    }
    ULONG STDMETHODCALLTYPE Release() override { const auto n = --refs_; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT* count) override { if (!count) return E_POINTER; *count = 0; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT, LCID, ITypeInfo**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID, LPOLESTR*, UINT, LCID, DISPID*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Invoke(DISPID, REFIID, LCID, WORD, DISPPARAMS*, VARIANT*, EXCEPINFO*, UINT*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE ShowContextMenu(DWORD id, DWORD x, DWORD y, IUnknown* command, IDispatch* reserved, HRESULT* result) override {
        ++state_.calls[0];
        state_.good = state_.browser || (id == CONTEXT_MENU_TEXTSELECT && x == static_cast<DWORD>(-19) && y == 27 &&
            command == static_cast<IDispatch*>(state_.object) && reserved == static_cast<IDispatch*>(state_.object) && state_.good);
        return Result(result);
    }
    HRESULT STDMETHODCALLTYPE GetHostInfo(DWORD* flags, DWORD* click) override {
        ++state_.calls[1];
        if (!state_.browser) {
            state_.good = state_.good && *flags == DOCHOSTUIFLAG_NO3DBORDER && *click == DOCHOSTUIDBLCLK_DEFAULT;
            *flags |= docHostUIFlagDIALOG; *click = docHostUIDblClkSHOWCODE;
        }
        return state_.transport;
    }
    HRESULT STDMETHODCALLTYPE ShowUI(DWORD id, IUnknown* active, IUnknown* target, IUnknown* frame, IUnknown* document, HRESULT* result) override {
        ++state_.calls[2];
        if (!state_.browser) state_.good = state_.good && id == 77 &&
            active == static_cast<IOleInPlaceActiveObject*>(state_.object) && target == static_cast<IOleCommandTarget*>(state_.object) &&
            frame == state_.frame && document == state_.frame;
        return Result(result);
    }
    HRESULT STDMETHODCALLTYPE HideUI() override {
        auto& state = state_; ++state.calls[3]; state.inside = true;
        const int action = state.action; state.action = 0;
        if (action == 1 || action == 2) state.good = state.host->SetExternalUIHandler(action == 1 ? nullptr : state.replacement) == S_OK && state.good;
        else if (action == 3) state.good = ::DestroyWindow(state.window) && state.good;
        state.inside = false; return state.transport;
    }
    HRESULT STDMETHODCALLTYPE UpdateUI() override { ++state_.calls[4]; return state_.transport; }
    HRESULT STDMETHODCALLTYPE EnableModeless(VARIANT_BOOL enable) override { ++state_.calls[5]; state_.good = state_.good && (state_.browser || enable == VARIANT_TRUE); return state_.transport; }
    HRESULT STDMETHODCALLTYPE OnDocWindowActivate(VARIANT_BOOL active) override { ++state_.calls[6]; state_.good = state_.good && (state_.browser || active == VARIANT_TRUE); return state_.transport; }
    HRESULT STDMETHODCALLTYPE OnFrameWindowActivate(VARIANT_BOOL active) override { ++state_.calls[7]; state_.good = state_.good && (state_.browser || active == VARIANT_FALSE); return state_.transport; }
    HRESULT STDMETHODCALLTYPE ResizeBorder(long left, long top, long right, long bottom, IUnknown* window, VARIANT_BOOL frame) override {
        ++state_.calls[8]; state_.good = state_.good && (state_.browser ||
            (left == -2 && top == 3 && right == 19 && bottom == 29 && window == state_.frame && frame == VARIANT_TRUE));
        return state_.transport;
    }
    HRESULT STDMETHODCALLTYPE TranslateAccelerator(DWORD_PTR window, DWORD message, DWORD_PTR w, DWORD_PTR l, BSTR group, DWORD id, HRESULT* result) override {
        ++state_.calls[9];
        if (!state_.browser) state_.good = state_.good && window == reinterpret_cast<DWORD_PTR>(state_.message.hwnd) &&
            message == state_.message.message && w == state_.message.wParam && l == static_cast<DWORD_PTR>(state_.message.lParam) && id == 99 &&
            (state_.nullGroup ? !group : group && !::wcscmp(group, L"{00020400-0000-0000-C000-000000000046}"));
        return Result(result);
    }
    HRESULT STDMETHODCALLTYPE GetOptionKeyPath(BSTR* value, DWORD flags) override {
        ++state_.calls[10]; if (!state_.browser) state_.good = state_.good && flags == 123;
        *value = state_.nullResult || state_.browser ? nullptr : ::SysAllocStringLen(L"key\0tail", 8);
        return state_.browser ? S_FALSE : state_.transport;
    }
    HRESULT STDMETHODCALLTYPE GetDropTarget(IUnknown* original, IUnknown** output) override {
        ++state_.calls[11]; if (!state_.browser) state_.good = state_.good && original == static_cast<IDropTarget*>(state_.object);
        return Object(output);
    }
    HRESULT STDMETHODCALLTYPE GetExternal(IDispatch** output) override {
        ++state_.calls[12]; *output = state_.nullResult ? nullptr : static_cast<IDispatch*>(state_.object);
        if (*output) (*output)->AddRef(); return state_.transport;
    }
    HRESULT STDMETHODCALLTYPE TranslateUrl(DWORD flags, BSTR input, BSTR* output) override {
        ++state_.calls[13]; if (!state_.browser) state_.good = state_.good && flags == 17 && input && !::wcscmp(input, L"about:blank#\u6771");
        *output = state_.nullResult || state_.browser ? nullptr : ::SysAllocString(L"about:blank#\u4eac");
        return state_.browser ? S_FALSE : state_.transport;
    }
    HRESULT STDMETHODCALLTYPE FilterDataObject(IUnknown* original, IUnknown** output) override {
        ++state_.calls[14]; if (!state_.browser) state_.good = state_.good && original == static_cast<IDataObject*>(state_.object);
        return Object(output);
    }
private:
    HRESULT Result(HRESULT* result) { if (!state_.omitResult) *result = state_.result; return state_.transport; }
    HRESULT Object(IUnknown** output) {
        *output = state_.nullResult || state_.browser ? nullptr : state_.wrongInterface ?
            static_cast<IDocHostUIHandlerDispatch*>(this) : static_cast<IDispatch*>(state_.object);
        if (*output) (*output)->AddRef(); return state_.browser ? E_NOTIMPL : state_.transport;
    }
    ~UIHandler() { ++state_.destroyed; state_.diedInside = state_.inside; }
    ULONG refs_ = 1;
    UIState& state_;
};
bool uiHandlers() {
    ATL::CAtlModule module;
    {
        UIState state; UIObjectState objects;
        OwnedWindow<ATL::CAxWindow> window;
        RECT rect{0,0,100,100}; REQUIRE(window.Create(nullptr, &rect, nullptr, WS_POPUP));
        ATL::CComPtr<IAxWinHostWindow> host; REQUIRE(window.QueryHost(&host) == S_OK);
        ATL::CComPtr<IDocHostUIHandler> ui; REQUIRE(window.QueryHost(&ui) == S_OK);
        ATL::CComPtr<IAxWinAmbientDispatch> ambient; REQUIRE(window.QueryHost(&ambient) == S_OK);
        ATL::CComPtr<IOleInPlaceFrame> frame; REQUIRE(window.QueryHost(&frame) == S_OK);
        state.frame = frame; state.host = host; state.window = window;
        ATL::CComPtr<UIObject> object; object.Attach(new UIObject(&objects)); state.object = object;
        REQUIRE(window.SetExternalDispatch(object) == S_OK && window.SetExternalDispatch(object) == S_OK && objects.refs == 2);
        ATL::CComPtr<IDispatch> external;
        REQUIRE(ui->GetExternal(&external) == S_OK && external.p == static_cast<IDispatch*>(object.p)); external.Release();
        ATL::CComPtr<IDocHostUIHandlerDispatch> handler; handler.Attach(new UIHandler(state));
        ATL::CComPtr<IDocHostUIHandler> unsupported;
        REQUIRE(handler->QueryInterface(IID_IDocHostUIHandler, reinterpret_cast<void**>(&unsupported)) == E_NOINTERFACE && !unsupported);
        REQUIRE(window.SetExternalUIHandler(handler) == S_OK);
        REQUIRE(ambient->put_DocHostFlags(DOCHOSTUIFLAG_NO3DBORDER) == S_OK);
        DOCHOSTUIINFO info{}; REQUIRE(ui->GetHostInfo(&info) == S_OK && info.cbSize == sizeof(info) &&
            info.dwFlags == (DOCHOSTUIFLAG_NO3DBORDER | DOCHOSTUIFLAG_DIALOG) && info.dwDoubleClick == DOCHOSTUIDBLCLK_SHOWCODE && !info.pchHostCss && !info.pchHostNS);
        POINT point{-19,27}; REQUIRE(ui->ShowContextMenu(CONTEXT_MENU_TEXTSELECT, &point, static_cast<IDispatch*>(object.p), object) == S_FALSE);
        state.result = S_OK; REQUIRE(ui->ShowContextMenu(CONTEXT_MENU_TEXTSELECT, &point, static_cast<IDispatch*>(object.p), object) == S_OK);
        REQUIRE(ui->ShowUI(77, object, object, frame, frame) == S_OK);
        REQUIRE(ui->HideUI() == S_OK && ui->UpdateUI() == S_OK && ui->EnableModeless(42) == S_OK &&
            ui->OnDocWindowActivate(2) == S_OK && ui->OnFrameWindowActivate(FALSE) == S_OK);
        RECT border{-2,3,19,29}; REQUIRE(ui->ResizeBorder(&border, frame, 2) == S_OK);
        state.message.hwnd = reinterpret_cast<HWND>(static_cast<ULONG_PTR>(0x12345678abcdef12ULL));
        state.message.message = WM_KEYDOWN; state.message.wParam = static_cast<WPARAM>(0xfedcba9876543210ULL);
        state.message.lParam = static_cast<LPARAM>(-123456789);
        REQUIRE(ui->TranslateAccelerator(&state.message, &IID_IDispatch, 99) == S_OK);
        state.nullGroup = true; state.omitResult = true;
        REQUIRE(ui->TranslateAccelerator(&state.message, nullptr, 99) == S_FALSE);
        REQUIRE(ui->ShowContextMenu(CONTEXT_MENU_TEXTSELECT, &point, static_cast<IDispatch*>(object.p), object) == S_FALSE);
        REQUIRE(ui->ShowUI(77, object, object, frame, frame) == S_FALSE); state.omitResult = false;
        LPOLESTR text = nullptr;
        REQUIRE(ui->GetOptionKeyPath(&text, 123) == S_OK && text && !std::memcmp(text, L"key\0tail", 9 * sizeof(WCHAR))); ::CoTaskMemFree(text);
        WCHAR url[] = L"about:blank#\u6771";
        REQUIRE(ui->TranslateUrl(17, url, &text) == S_OK && text && !::wcscmp(text, L"about:blank#\u4eac")); ::CoTaskMemFree(text);
        ATL::CComPtr<IDropTarget> target; REQUIRE(ui->GetDropTarget(object, &target) == S_OK && target.p == static_cast<IDropTarget*>(object.p)); target.Release();
        ATL::CComPtr<IDataObject> data; REQUIRE(ui->FilterDataObject(object, &data) == S_OK && data.p == static_cast<IDataObject*>(object.p)); data.Release();
        REQUIRE(ui->GetExternal(&external) == S_OK && external.p == static_cast<IDispatch*>(object.p)); external.Release();
        for (int calls : state.calls) REQUIRE(calls > 0);
        REQUIRE(objects.refs == 2 && state.good);
        state.wrongInterface = true;
        REQUIRE(ui->GetDropTarget(object, &target) == E_NOINTERFACE && !target);
        REQUIRE(ui->FilterDataObject(object, &data) == E_NOINTERFACE && !data); state.wrongInterface = false;
        state.transport = E_ACCESSDENIED;
        REQUIRE(ui->ShowContextMenu(CONTEXT_MENU_TEXTSELECT, &point, static_cast<IDispatch*>(object.p), object) == E_ACCESSDENIED);
        REQUIRE(ui->ShowUI(77, object, object, frame, frame) == E_ACCESSDENIED &&
            ui->TranslateAccelerator(&state.message, nullptr, 99) == E_ACCESSDENIED);
        REQUIRE(ui->GetOptionKeyPath(&text, 123) == E_ACCESSDENIED && !text);
        REQUIRE(ui->TranslateUrl(17, url, &text) == E_ACCESSDENIED && !text);
        REQUIRE(ui->GetDropTarget(object, &target) == E_ACCESSDENIED && !target);
        REQUIRE(ui->FilterDataObject(object, &data) == E_ACCESSDENIED && !data);
        REQUIRE(ui->GetExternal(&external) == E_ACCESSDENIED && !external && objects.refs == 2);
        state.transport = S_FALSE; state.nullResult = true;
        REQUIRE(ui->GetOptionKeyPath(&text, 123) == S_FALSE && !text && ui->TranslateUrl(17, url, &text) == S_FALSE && !text);
        REQUIRE(ui->GetDropTarget(object, &target) == S_FALSE && !target && ui->FilterDataObject(object, &data) == S_FALSE && !data);
        REQUIRE(ui->GetExternal(&external) == S_FALSE && !external);
        REQUIRE(ui->GetHostInfo(nullptr) == E_POINTER && ui->ShowContextMenu(0, nullptr, nullptr, nullptr) == E_POINTER &&
            ui->ResizeBorder(nullptr, nullptr, FALSE) == E_POINTER && ui->TranslateAccelerator(nullptr, nullptr, 0) == E_POINTER);
        REQUIRE(ui->GetOptionKeyPath(nullptr, 0) == E_POINTER && ui->TranslateUrl(0, nullptr, nullptr) == E_POINTER &&
            ui->GetDropTarget(nullptr, nullptr) == E_POINTER && ui->FilterDataObject(nullptr, nullptr) == E_POINTER && ui->GetExternal(nullptr) == E_POINTER);
        HRESULT foreignUI = S_OK, foreignObject = S_OK;
        std::thread foreign([&] { foreignUI = host->SetExternalUIHandler(handler); foreignObject = host->SetExternalDispatch(object); }); foreign.join();
        REQUIRE(foreignUI == RPC_E_WRONG_THREAD && foreignObject == RPC_E_WRONG_THREAD);
        REQUIRE(window.SetExternalUIHandler(nullptr) == S_OK);
        REQUIRE(ui->GetExternal(&external) == S_OK && external.p == static_cast<IDispatch*>(object.p)); external.Release();
        REQUIRE(window.SetExternalDispatch(nullptr) == S_OK);
        handler.Release(); REQUIRE(state.destroyed == 1 && state.good && !state.diedInside && objects.refs == 1);
        object.Release(); REQUIRE(objects.destroyed == 1 && objects.refs == 0);
    }
    REQUIRE(module.GetLockCount() == 0);
    for (int action = 1; action <= 4; ++action) {
        UIState state, replacement;
        OwnedWindow<ATL::CAxWindow> window;
        RECT rect{0,0,100,100}; REQUIRE(window.Create(nullptr, &rect, nullptr, WS_POPUP));
        ATL::CComPtr<IAxWinHostWindow> host; REQUIRE(window.QueryHost(&host) == S_OK);
        ATL::CComPtr<IDocHostUIHandler> ui; REQUIRE(window.QueryHost(&ui) == S_OK);
        state.host = host; state.window = window; state.action = action;
        ATL::CComPtr<IDocHostUIHandlerDispatch> next; next.Attach(new UIHandler(replacement)); state.replacement = next;
        auto handler = new UIHandler(state);
        if (action == 4) state.destroyOnRetain = true;
        REQUIRE(window.SetExternalUIHandler(handler) == (action == 4 ? CO_E_OBJNOTCONNECTED : S_OK)); handler->Release();
        if (action != 4) REQUIRE(ui->HideUI() == S_OK);
        if (action == 3) REQUIRE(state.destroyed == 0 && host->SetExternalUIHandler(next) == CO_E_OBJNOTCONNECTED);
        REQUIRE(host->SetExternalUIHandler(nullptr) == S_OK);
        REQUIRE(state.good && state.destroyed == 1 && !state.diedInside);
        next.Release(); REQUIRE(replacement.destroyed == 1);
        if (action >= 3) { REQUIRE(!::IsWindow(window)); window.m_hWnd = nullptr; }
    }
    REQUIRE(module.GetLockCount() == 0);
    return true;
}
CLSID MappedNativePageClass{}; // Assigned before registering the native page factory.
struct PropertyState {
    CLSID pageClass{};
    IOleControlSite* site = nullptr;
    IUnknown* identity = nullptr;
    HWND owner = nullptr;
    LCID locale = 0x040c; // Explicit French LCID, rather than the runner's usual default.
    HRESULT pagesResult = S_OK, activationResult = S_OK;
    bool empty = false, malformed = false, destroyOnQuery = false, destroyOnPages = false;
    bool cancel = false, captionFailure = false, good = true, reenter = true;
    bool useMap = false;
    int queried = 0, captions = 0, created = 0, destroyed = 0, sites = 0, clearedSites = 0;
    int bound = 0, unbound = 0, activated = 0, deactivated = 0, shown = 0, applied = 0, nested = 0;
    LONG value = 9;
};
struct ControlState {
    CLSID classId{};
    bool siteFirst = true, initInterface = true, failInit = false, destroyDuringInit = false;
    bool siteOrder = true, licenseSeen = false;
    bool storageInterface = true, storageReadOnly = true, rejectLicense = false, destroyDuringStorageQuery = false;
    bool discoverWindow = false, destroyDialogDuringInit = false, sendInitDuringLoad = false;
    ULONG streamBytes = 4;
    int failInitializationAt = 0;
    HWND window = nullptr;
    int initialized = 0, loaded = 0, activated = 0, detached = 0, closed = 0, destroyed = 0;
    int storageLoaded = 0, storageReleased = 0, lateStorageReads = 0;
    PropertyState* properties = nullptr;
};
class Control : public IOleObject, public IPersistStreamInit, public IPersistStream, public IPersistStorage,
                public IRunnableObject, public ATL::ISpecifyPropertyPagesImpl<Control>,
                public IDispatch, public ATL::IPerPropertyBrowsingImpl<Control> {
public:
    BEGIN_PROP_MAP(Control)
        PROP_ENTRY_TYPE("Extent", 1, MappedNativePageClass, VT_I4)
        PROP_ENTRY_TYPE_EX("ExtentAlias", 2, MappedNativePageClass, IID_IDispatch, VT_I4)
        PROP_PAGE(MappedNativePageClass)
        PROP_ENTRY_TYPE("Unpaged", 3, CLSID_NULL, VT_I4)
    END_PROP_MAP()
    explicit Control(ControlState& state) : state_(state) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** value) override {
        if (!value) return E_POINTER; *value = nullptr;
        if (iid == IID_IUnknown || iid == IID_IOleObject) *value = static_cast<IOleObject*>(this);
        else if (iid == IID_IPersistStreamInit && state_.initInterface) *value = static_cast<IPersistStreamInit*>(this);
        else if (iid == IID_IPersist || iid == IID_IPersistStream) *value = static_cast<IPersistStream*>(this);
        else if (iid == IID_IPersistStorage && state_.storageInterface) {
            *value = static_cast<IPersistStorage*>(this);
            if (state_.destroyDuringStorageQuery) ::DestroyWindow(state_.window);
        }
        else if (iid == IID_IRunnableObject) *value = static_cast<IRunnableObject*>(this);
        else if (iid == IID_ISpecifyPropertyPages && state_.properties) {
            *value = static_cast<ISpecifyPropertyPages*>(this);
            if (state_.properties->destroyOnQuery) ::DestroyWindow(state_.window);
        }
        else if (iid == IID_IDispatch && state_.properties && state_.properties->useMap)
            *value = static_cast<IDispatch*>(this);
        else if (iid == IID_IPerPropertyBrowsing && state_.properties && state_.properties->useMap)
            *value = static_cast<IPerPropertyBrowsing*>(this);
        else return E_NOINTERFACE;
        AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { const auto n = --refs_; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT* count) override { if (!count) return E_POINTER; *count = 0; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT, LCID, ITypeInfo**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID, LPOLESTR*, UINT, LCID, DISPID*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Invoke(DISPID member, REFIID iid, LCID, WORD flags, DISPPARAMS* args,
        VARIANT* result, EXCEPINFO*, UINT*) override {
        if (iid != IID_NULL) return DISP_E_UNKNOWNINTERFACE;
        if (!state_.properties || member < 1 || member > 3 || flags != DISPATCH_PROPERTYGET) return DISP_E_MEMBERNOTFOUND;
        if (!args || !result) return E_POINTER;
        if (args->cArgs || args->cNamedArgs) return DISP_E_BADPARAMCOUNT;
        ::VariantInit(result); result->vt = VT_I4; result->lVal = state_.properties->value; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetClientSite(IOleClientSite* site) override {
        if (!site && site_) ++state_.detached;
        if (site && state_.discoverWindow) {
            ATL::CComPtr<IOleWindow> window;
            HRESULT hr = site->QueryInterface(IID_IOleWindow, reinterpret_cast<void**>(&window));
            if (FAILED(hr)) return hr;
            hr = window->GetWindow(&state_.window);
            if (FAILED(hr)) return hr;
        }
        site_ = site; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetClientSite(IOleClientSite** site) override { return site_.CopyTo(site); }
    HRESULT STDMETHODCALLTYPE SetHostNames(LPCOLESTR, LPCOLESTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Close(DWORD) override {
        ++state_.closed;
        if (storage_) { storage_.Release(); ++state_.storageReleased; }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetMoniker(DWORD, IMoniker*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetMoniker(DWORD, DWORD, IMoniker**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE InitFromData(IDataObject*, BOOL, DWORD) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetClipboardData(DWORD, IDataObject**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE DoVerb(LONG, LPMSG, IOleClientSite* site, LONG, HWND parent, LPCRECT) override {
        if (parent != state_.window || site != site_.p) return E_FAIL;
        if (state_.properties) state_.properties->activationResult = state_.properties->site->ShowPropertyFrame();
        ++state_.activated; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE EnumVerbs(IEnumOLEVERB**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Update() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE IsUpToDate() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetUserClassID(CLSID* id) override { return GetClassID(id); }
    HRESULT STDMETHODCALLTYPE GetUserType(DWORD type, LPOLESTR* output) override {
        if (!state_.properties) return E_NOTIMPL;
        if (!output) return E_POINTER; *output = nullptr;
        auto& properties = *state_.properties; ++properties.captions;
        properties.good = properties.good && type == USERCLASSTYPE_SHORT;
        const WCHAR caption[] = L"OpenWTL control";
        *output = static_cast<LPOLESTR>(::CoTaskMemAlloc(sizeof(caption)));
        if (!*output) return E_OUTOFMEMORY;
        std::memcpy(*output, caption, sizeof(caption));
        return properties.captionFailure ? E_FAIL : S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetExtent(DWORD aspect, SIZEL* value) override {
        if (state_.properties && aspect == DVASPECT_CONTENT && value) state_.properties->value = value->cx;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPages(CAUUID* pages) override {
        if (!pages) return E_POINTER;
        auto& properties = *state_.properties; ++properties.queried;
        HRESULT result = properties.pagesResult;
        *pages = {};
        if (properties.useMap) {
            result = ATL::ISpecifyPropertyPagesImpl<Control>::GetPages(pages);
            if (FAILED(result)) return result;
        } else if (!properties.malformed) {
            pages->cElems = properties.empty ? 0 : 1;
            pages->pElems = static_cast<CLSID*>(::CoTaskMemAlloc(sizeof(CLSID)));
            if (!pages->pElems) return E_OUTOFMEMORY;
            pages->pElems[0] = properties.pageClass;
        } else pages->cElems = 1;
        if (properties.reenter) {
            ++properties.nested;
            properties.good = properties.site->ShowPropertyFrame() == E_UNEXPECTED && properties.good;
        }
        if (properties.destroyOnPages) ::DestroyWindow(state_.window);
        return result;
    }
    HRESULT STDMETHODCALLTYPE GetExtent(DWORD, SIZEL* size) override {
        if (!storage_) return E_NOTIMPL;
        if (!size) return E_POINTER;
        DWORD marker = 0;
        HRESULT hr = ReadStorage(storage_, marker);
        if (SUCCEEDED(hr)) { size->cx = static_cast<LONG>(marker); size->cy = 4; ++state_.lateStorageReads; }
        return hr;
    }
    HRESULT STDMETHODCALLTYPE Advise(IAdviseSink*, DWORD*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Unadvise(DWORD) override { return OLE_E_NOCONNECTION; }
    HRESULT STDMETHODCALLTYPE EnumAdvise(IEnumSTATDATA**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetMiscStatus(DWORD, DWORD* misc) override { if (!misc) return E_POINTER; *misc = state_.siteFirst ? OLEMISC_SETCLIENTSITEFIRST : 0; return S_OK; }
    HRESULT STDMETHODCALLTYPE SetColorScheme(LOGPALETTE*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetClassID(CLSID* id) override { if (!id) return E_POINTER; *id = state_.classId; return S_OK; }
    HRESULT STDMETHODCALLTYPE IsDirty() override { return S_FALSE; }
    HRESULT STDMETHODCALLTYPE Load(IStream* stream) override {
        ULONG read = 0; DWORD marker = 0;
        if (!stream || stream->Read(&marker, sizeof(marker), &read) != S_OK || read != sizeof(marker) || marker != 42) return E_FAIL;
        STATSTG stat{};
        if (stream->Stat(&stat, STATFLAG_NONAME) != S_OK || stat.cbSize.QuadPart != state_.streamBytes) return E_FAIL;
        if (state_.streamBytes == 5) {
            BYTE tail = 0;
            if (stream->Read(&tail, 1, &read) != S_OK || read != 1 || tail != 0xA5) return E_FAIL;
        }
        BYTE end = 0;
        // Native HGLOBAL streams return S_OK with zero bytes at EOF.
        if (FAILED(stream->Read(&end, 1, &read)) || read) return E_FAIL;
        ++state_.loaded; return Initialize();
    }
    HRESULT STDMETHODCALLTYPE Save(IStream*, BOOL) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetSizeMax(ULARGE_INTEGER* size) override { if (!size) return E_POINTER; size->QuadPart = 4; return S_OK; }
    HRESULT STDMETHODCALLTYPE InitNew() override { ++state_.initialized; return Initialize(); }
    HRESULT STDMETHODCALLTYPE Load(IStorage* storage) override {
        if (!storage) return E_POINTER;
        if (storage_) return CO_E_ALREADYINITIALIZED;
        DWORD marker = 0;
        HRESULT hr = ReadStorage(storage, marker);
        if (FAILED(hr)) return hr;
        STATSTG stat{};
        if (storage->Stat(&stat, STATFLAG_NONAME) != S_OK || (stat.grfMode & 3) != STGM_READ) return E_FAIL;
        ATL::CComPtr<IStream> forbidden;
        state_.storageReadOnly = state_.storageReadOnly && FAILED(storage->CreateStream(L"Forbidden", STGM_WRITE | STGM_SHARE_EXCLUSIVE, 0, 0, &forbidden));
        if (!state_.storageReadOnly || forbidden) return E_FAIL;
        storage_ = storage; ++state_.storageLoaded;
        return Initialize();
    }
    HRESULT STDMETHODCALLTYPE InitNew(IStorage*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Save(IStorage*, BOOL) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SaveCompleted(IStorage*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE HandsOffStorage() override {
        if (storage_) { storage_.Release(); ++state_.storageReleased; }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetRunningClass(CLSID* id) override { return GetClassID(id); }
    HRESULT STDMETHODCALLTYPE Run(IBindCtx*) override { return S_OK; }
    BOOL STDMETHODCALLTYPE IsRunning() override { return TRUE; }
    HRESULT STDMETHODCALLTYPE LockRunning(BOOL, BOOL) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SetContainedObject(BOOL) override { return S_OK; }
private:
    static HRESULT ReadStorage(IStorage* storage, DWORD& marker) {
        ATL::CComPtr<IStream> stream;
        HRESULT hr = storage->OpenStream(L"Marker", nullptr, STGM_READ | STGM_SHARE_EXCLUSIVE, 0, &stream);
        if (FAILED(hr)) return hr;
        ULONG read = 0;
        hr = stream->Read(&marker, sizeof(marker), &read);
        return SUCCEEDED(hr) && read == sizeof(marker) && marker == 42 ? S_OK : E_FAIL;
    }
    HRESULT Initialize() {
        state_.siteOrder = state_.siteOrder && (static_cast<bool>(site_.p) == state_.siteFirst);
        if (state_.destroyDuringInit) ::DestroyWindow(state_.window);
        if (state_.destroyDialogDuringInit) ::DestroyWindow(::GetParent(state_.window));
        if (state_.sendInitDuringLoad) ::SendMessageW(::GetParent(state_.window), WM_INITDIALOG, 0, 0);
        return state_.failInit || !state_.siteOrder || (state_.failInitializationAt &&
            state_.initialized + state_.loaded + state_.storageLoaded == state_.failInitializationAt) ? E_FAIL : S_OK;
    }
    ~Control() { ++state_.destroyed; }
    ULONG refs_ = 1;
    ControlState& state_;
    ATL::CComPtr<IOleClientSite> site_;
    ATL::CComPtr<IStorage> storage_;
};
class Factory : public IClassFactory2 {
public:
    explicit Factory(ControlState& state) : state_(state) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** value) override {
        if (!value) return E_POINTER; *value = nullptr;
        if (iid != IID_IUnknown && iid != IID_IClassFactory && iid != IID_IClassFactory2) return E_NOINTERFACE;
        *value = static_cast<IClassFactory2*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { const auto n = --refs_; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID iid, void** value) override {
        if (!value) return E_POINTER; *value = nullptr;
        if (outer) return CLASS_E_NOAGGREGATION;
        auto object = new (std::nothrow) Control(state_); if (!object) return E_OUTOFMEMORY;
        HRESULT hr = object->QueryInterface(iid, value); object->Release(); return hr;
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetLicInfo(LICINFO* info) override {
        if (!info) return E_POINTER; *info = {}; info->cbLicInfo = sizeof(*info); info->fRuntimeKeyAvail = TRUE; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE RequestLicKey(DWORD, BSTR* key) override {
        if (!key) return E_POINTER; *key = ::SysAllocString(L"OpenATL-test-license"); return *key ? S_OK : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE CreateInstanceLic(IUnknown* outer, IUnknown*, REFIID iid, BSTR key, void** value) override {
        if (!value) return E_POINTER; *value = nullptr;
        if (state_.rejectLicense || !key || ::SysStringLen(key) != 20 || std::memcmp(key, L"OpenATL-test-license", 20 * sizeof(WCHAR))) return CLASS_E_NOTLICENSED;
        state_.licenseSeen = true; return CreateInstance(outer, iid, value);
    }
private:
    ~Factory() = default;
    ULONG refs_ = 1;
    ControlState& state_;
};
struct Registration {
    DWORD cookie = 0;
    ~Registration() { if (cookie) ::CoRevokeClassObject(cookie); }
};
// Windows owns the property frame and its modal loop. The registered page is
// repository-owned test UI; native SetObjects/Apply perform an actual COM edit.
class PropertyPage : public ATL::CDialogImpl<PropertyPage>, public ATL::IPropertyPage2Impl<PropertyPage> {
    using Base = ATL::IPropertyPage2Impl<PropertyPage>;
public:
    enum { IDD = 602 };
    explicit PropertyPage(PropertyState& state) : state_(state) {
        ++state_.created;
        m_size = {220,80};
        m_dwTitle = 6901; m_dwDocString = 6902; m_dwHelpFile = 6903;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** output) override {
        if (!output) return E_POINTER; *output = nullptr;
        if (iid != IID_IUnknown && iid != IID_IPropertyPage && iid != IID_IPropertyPage2) return E_NOINTERFACE;
        *output = static_cast<IPropertyPage2*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { const auto count = --refs_; if (!count) delete this; return count; }
    HRESULT STDMETHODCALLTYPE SetPageSite(IPropertyPageSite* site) override {
        ATL::CComPtr<IPropertyPage> protect(this);
        const bool hadSite = m_pPageSite != nullptr;
        const HRESULT hr = Base::SetPageSite(site);
        if (hr == S_OK) {
            if (site) ++state_.sites;
            else if (hadSite) ++state_.clearedSites;
        }
        return hr;
    }
    HRESULT STDMETHODCALLTYPE Activate(HWND parent, LPCRECT rect, BOOL modal) override {
        ATL::CComPtr<IPropertyPage> protect(this);
        state_.good = state_.good && modal && ::IsWindow(parent);
        HRESULT hr = Base::Activate(parent,rect,modal);
        if (FAILED(hr)) return hr;
        ::SetWindowLongPtrW(m_hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        const HWND edit = ::CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"9", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER,
            8, 8, 100, 24, m_hWnd, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(771)), ATL::_AtlBaseModule.GetModuleInstance(), nullptr);
        if (!edit) { (void)Base::Deactivate(); return E_OUTOFMEMORY; }
        ++state_.activated; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Deactivate() override {
        ATL::CComPtr<IPropertyPage> protect(this);
        const HRESULT hr = Base::Deactivate();
        if (hr == S_OK) ++state_.deactivated;
        return hr;
    }
    HRESULT STDMETHODCALLTYPE SetObjects(ULONG count, IUnknown** objects) override {
        ATL::CComPtr<IPropertyPage> protect(this);
        if (!count) {
            const HRESULT hr = Base::SetObjects(0,nullptr);
            if (hr == S_OK && object_) { object_.Release(); ++state_.unbound; }
            return hr;
        }
        if (count != 1 || !objects || !objects[0] || object_) return E_INVALIDARG;
        ATL::CComPtr<IUnknown> identity;
        HRESULT hr = objects[0]->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&identity));
        if (FAILED(hr)) return hr;
        state_.good = state_.good && identity.p == state_.identity;
        ATL::CComPtr<IOleObject> staged;
        hr = objects[0]->QueryInterface(IID_IOleObject, reinterpret_cast<void**>(&staged));
        if (FAILED(hr)) return hr;
        hr = Base::SetObjects(count,objects);
        if (hr == S_OK) { object_.Attach(staged.Detach()); ++state_.bound; }
        return hr;
    }
    HRESULT STDMETHODCALLTYPE Show(UINT command) override {
        ATL::CComPtr<IPropertyPage> protect(this);
        const HRESULT hr = Base::Show(command);
        if (FAILED(hr)) return hr;
        if (command != SW_HIDE && !posted_) {
            posted_ = true; ++state_.shown;
            // Editing starts after the native modal frame has completed setup.
            if (!::SetTimer(m_hWnd, 1, 10, EditTimer)) {
                state_.good = false; ::PostMessageW(::GetAncestor(m_hWnd, GA_ROOT), WM_CLOSE, 0, 0);
            }
        }
        return hr;
    }
    HRESULT STDMETHODCALLTYPE Apply() override {
        ATL::CComPtr<IPropertyPage> protect(this);
        if (!::IsWindow(m_hWnd) || !object_) return E_UNEXPECTED;
        BOOL valid = FALSE;
        SIZEL value{static_cast<LONG>(::GetDlgItemInt(m_hWnd, 771, &valid, FALSE)), 1};
        if (!valid) return E_FAIL;
        const HRESULT hr = object_->SetExtent(DVASPECT_CONTENT, &value);
        if (SUCCEEDED(hr)) { SetDirty(FALSE); ++state_.applied; }
        return hr;
    }
    BEGIN_MSG_MAP(PropertyPage)
    END_MSG_MAP()
private:
    static void CALLBACK EditTimer(HWND window, UINT, UINT_PTR id, DWORD) {
        ::KillTimer(window, id);
        auto page = reinterpret_cast<PropertyPage*>(::GetWindowLongPtrW(window, GWLP_USERDATA));
        if (page) page->EditAndFinish();
    }
    void EditAndFinish() {
        ATL::CComPtr<IPropertyPage> protect(this);
        LCID locale = 0;
        state_.good = m_pPageSite && m_pPageSite->GetLocaleID(&locale) == S_OK && locale == state_.locale && state_.good;
        state_.good = ::SetDlgItemInt(m_hWnd, 771, 314, FALSE) && state_.good;
        SetDirty(TRUE); state_.good = IsPageDirty() == S_OK && state_.good;
        if (state_.reenter) {
            ++state_.nested;
            state_.good = state_.site->ShowPropertyFrame() == E_UNEXPECTED && state_.good;
        }
        const HWND frame = ::GetAncestor(m_hWnd, GA_ROOT);
        state_.good = ::GetAncestor(frame, GA_ROOTOWNER) == state_.owner && state_.good;
        WCHAR caption[128]{};
        state_.good = ::GetWindowTextW(frame, caption, 128) > 0 && state_.good;
        state_.good = state_.good && (state_.captionFailure ?
            !std::wcsstr(caption, L"OpenWTL control") && std::wcsstr(caption, L"Properties") :
            std::wcsstr(caption, L"OpenWTL control") != nullptr);
        ::SetActiveWindow(frame); // BM_CLICK requires an active dialog.
        const HWND button = ::GetDlgItem(frame, state_.cancel ? IDCANCEL : IDOK);
        if (!button || !::PostMessageW(button, BM_CLICK, 0, 0)) {
            state_.good = false; ::PostMessageW(frame, WM_CLOSE, 0, 0);
        }
    }
    ~PropertyPage() {
        // The native frame must retire its page HWND before the final Release.
        state_.good = !::IsWindow(m_hWnd) && state_.good;
        if (object_) { object_.Release(); ++state_.unbound; }
        if (m_pPageSite) ++state_.clearedSites; // Base releases the site below.
        if (m_nObjects) {
            // Base owns its independent SetObjects references and releases them.
            state_.good = m_nObjects == 1 && state_.good;
        }
        ++state_.destroyed;
    }
    ULONG refs_ = 1;
    PropertyState& state_;
    ATL::CComPtr<IOleObject> object_;
    bool posted_ = false;
};
class PageFactory : public IClassFactory {
public:
    explicit PageFactory(PropertyState& state) : state_(state) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** output) override {
        if (!output) return E_POINTER; *output = nullptr;
        if (iid != IID_IUnknown && iid != IID_IClassFactory) return E_NOINTERFACE;
        *output = static_cast<IClassFactory*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { const auto count = --refs_; if (!count) delete this; return count; }
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID iid, void** output) override {
        if (!output) return E_POINTER; *output = nullptr;
        if (outer) return CLASS_E_NOAGGREGATION;
        auto page = new (std::nothrow) PropertyPage(state_);
        if (!page) return E_OUTOFMEMORY;
        const HRESULT hr = page->QueryInterface(iid, output); page->Release(); return hr;
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL) override { return S_OK; }
private:
    ULONG refs_ = 1;
    PropertyState& state_;
};
bool propertyFrames() {
    ATL::CAtlModule module;
    PropertyState properties;
    CLSID pageClass{}; REQUIRE(::CoCreateGuid(&pageClass) == S_OK);
    MappedNativePageClass = pageClass;
    ATL::CComPtr<IClassFactory> factory; factory.Attach(new PageFactory(properties));
    Registration registration;
    REQUIRE(::CoRegisterClassObject(pageClass, factory, CLSCTX_LOCAL_SERVER, REGCLS_MULTIPLEUSE, &registration.cookie) == S_OK);
    for (int scenario = 0; scenario < 12; ++scenario) {
        properties = PropertyState{}; properties.pageClass = pageClass;
        properties.empty = scenario == 1; properties.pagesResult = scenario == 2 ? E_ACCESSDENIED : scenario == 6 ? S_FALSE : S_OK;
        properties.malformed = scenario == 3; properties.destroyOnQuery = scenario == 4; properties.destroyOnPages = scenario == 5;
        properties.cancel = scenario == 8 || scenario == 11; properties.captionFailure = scenario == 9;
        properties.useMap = scenario >= 10;
        ControlState state; state.properties = scenario == 0 ? nullptr : &properties;
        OwnedWindow<ATL::CAxWindow> window;
        RECT rect{0,0,100,100}; REQUIRE(window.Create(nullptr, &rect, nullptr, WS_POPUP));
        state.window = properties.owner = window;
        ATL::CComPtr<IOleControlSite> site; REQUIRE(window.QueryHost(&site) == S_OK); properties.site = site;
        REQUIRE(site->ShowPropertyFrame() == E_NOINTERFACE);
        ATL::CComPtr<IAxWinAmbientDispatch> ambient; REQUIRE(window.QueryHost(&ambient) == S_OK);
        REQUIRE(ambient->put_LocaleID(properties.locale) == S_OK);
        auto control = new Control(state); properties.identity = static_cast<IOleObject*>(control);
        REQUIRE(window.AttachControl(static_cast<IOleObject*>(control)) == S_OK); control->Release();
        if (properties.useMap) {
            ATL::CComPtr<IPerPropertyBrowsing> browser;
            REQUIRE(properties.identity->QueryInterface(IID_IPerPropertyBrowsing, reinterpret_cast<void**>(&browser)) == S_OK);
            CLSID mapped{}; REQUIRE(browser->MapPropertyToPage(1, &mapped) == S_OK && mapped == pageClass);
            BSTR display = nullptr;
            REQUIRE(browser->GetDisplayString(1, &display) == S_OK && display && !::wcscmp(display, L"9"));
            ::SysFreeString(display);
        }
        if (state.properties) REQUIRE(properties.activationResult == E_UNEXPECTED);
        HRESULT foreign = S_OK;
        std::thread thread([&] { foreign = site->ShowPropertyFrame(); }); thread.join();
        REQUIRE(foreign == RPC_E_WRONG_THREAD && properties.queried == 0);
        if (scenario == 7) {
            properties.pagesResult = E_ACCESSDENIED;
            REQUIRE(site->ShowPropertyFrame() == E_ACCESSDENIED && properties.good && properties.queried == 1 && properties.created == 0);
            properties.pagesResult = S_OK; properties.queried = 0; properties.nested = 0;
        }
        if (properties.cancel) ::EnableWindow(window, FALSE);
        const BOOL enabled = ::IsWindowEnabled(window);
        const HRESULT result = site->ShowPropertyFrame();
        const HRESULT expected = scenario == 0 ? E_NOINTERFACE : scenario == 1 ? E_NOTIMPL : scenario == 2 ? E_ACCESSDENIED :
            scenario == 3 || scenario == 6 ? E_UNEXPECTED : scenario == 4 || scenario == 5 ? E_ABORT : S_OK;
        REQUIRE(result == expected && properties.good);
        if (properties.useMap) {
            ATL::CComPtr<IPerPropertyBrowsing> browser;
            REQUIRE(properties.identity->QueryInterface(IID_IPerPropertyBrowsing, reinterpret_cast<void**>(&browser)) == S_OK);
            BSTR display = nullptr;
            REQUIRE(browser->GetDisplayString(2, &display) == S_OK && display &&
                !::wcscmp(display, properties.cancel ? L"9" : L"314"));
            ::SysFreeString(display);
        }
        REQUIRE(properties.queried == (scenario == 0 || scenario == 4 ? 0 : 1));
        if (scenario >= 7) {
            const bool complete = properties.created > 0 && properties.created == properties.destroyed && properties.activated > 0 &&
                properties.activated == properties.deactivated && properties.bound > 0 && properties.bound == properties.unbound &&
                properties.sites == properties.clearedSites && properties.shown == 1 && properties.nested == 2 && properties.captions == 1;
            if (!complete) std::fprintf(stderr, "PROPERTY_FRAME_COUNTS scenario=%d created=%d destroyed=%d active=%d deactive=%d bound=%d unbound=%d sites=%d clear=%d shown=%d nested=%d captions=%d applied=%d value=%ld\n",
                scenario, properties.created, properties.destroyed, properties.activated, properties.deactivated, properties.bound,
                properties.unbound, properties.sites, properties.clearedSites, properties.shown, properties.nested, properties.captions, properties.applied, properties.value);
            REQUIRE(complete);
            const bool committed = properties.cancel ? properties.applied == 0 && properties.value == 9 : properties.applied > 0 && properties.value == 314;
            if (!committed) std::fprintf(stderr, "PROPERTY_FRAME_APPLY scenario=%d cancel=%d applied=%d value=%ld\n", scenario, properties.cancel, properties.applied, properties.value);
            REQUIRE(committed);
            REQUIRE(::IsWindowEnabled(window) == enabled);
            // A second native modal frame proves that the operation guard was released.
            const int prior = properties.created;
            REQUIRE(site->ShowPropertyFrame() == S_OK && properties.good && properties.created > prior && properties.created == properties.destroyed);
        } else REQUIRE(properties.created == 0 && properties.applied == 0);
        if (scenario == 4 || scenario == 5) {
            REQUIRE(state.destroyed == 1 && !::IsWindow(window) && site->ShowPropertyFrame() == CO_E_OBJNOTCONNECTED);
            window.m_hWnd = nullptr;
        } else {
            REQUIRE(state.destroyed == 0 && window.DestroyWindow() && state.destroyed == 1);
            window.m_hWnd = nullptr;
        }
    }
    REQUIRE(module.GetLockCount() == 0);
    return true;
}
inline constexpr CLSID StorageClass = {0x772f940f,0xd3ca,0x49d8,{0x97,0xab,0x25,0x19,0x0f,0x8c,0x69,0x11}};
struct StorageState {
    std::vector<BYTE> payload{1,2,3,4,5,6,7,8};
    bool dirty = true, initialized = false, valid = true, reenter = true;
    int initializedCalls = 0, loads = 0, saves = 0, dirtyCalls = 0, failure = 0, queryFailure = 0, destroyed = 0;
    BOOL clearDirty = FALSE;
    IPersistStorage* owner = nullptr;
    IPersistStorage* dropOwner = nullptr;
    HRESULT Enter() {
        if (reenter) {
            CLSID id{};
            valid = owner && owner->GetClassID(&id) == S_OK && id == StorageClass &&
                owner->HandsOffStorage() == S_OK && owner->SaveCompleted(nullptr) == S_OK && valid;
        }
        if (dropOwner) { auto release = dropOwner; dropOwner = nullptr; release->Release(); }
        if (failure == 1) return E_ACCESSDENIED;
        if (failure == 2) throw ATL::CAtlException(E_ABORT);
        if (failure == 3) throw std::bad_alloc();
        if (failure == 4) throw std::runtime_error("storage codec failure");
        return S_OK;
    }
};
template<class T> class StorageCodec : public IPersistStreamInit {
public:
    HRESULT STDMETHODCALLTYPE GetClassID(CLSID* id) override { if (!id) return E_POINTER; *id = StorageClass; return S_OK; }
    HRESULT STDMETHODCALLTYPE IsDirty() override {
        auto& state = *static_cast<T*>(this)->state; ++state.dirtyCalls;
        const HRESULT hr = state.Enter(); return hr == S_OK ? (state.dirty ? S_OK : S_FALSE) : hr;
    }
    HRESULT STDMETHODCALLTYPE InitNew() override {
        auto& state = *static_cast<T*>(this)->state; ++state.initializedCalls;
        const HRESULT hr = state.Enter(); if (hr != S_OK) return hr;
        if (state.initialized) return CO_E_ALREADYINITIALIZED;
        state.initialized = true; state.dirty = true; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetSizeMax(ULARGE_INTEGER* size) override {
        if (!size) return E_POINTER; size->QuadPart = static_cast<T*>(this)->state->payload.size(); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Load(IStream* stream) override {
        auto& state = *static_cast<T*>(this)->state; ++state.loads;
        HRESULT hr = state.Enter(); if (hr != S_OK) return hr;
        if (!stream) return E_POINTER;
        LARGE_INTEGER zero{}; ULARGE_INTEGER position{}; STATSTG info{};
        if (stream->Seek(zero, STREAM_SEEK_CUR, &position) != S_OK || position.QuadPart ||
            stream->Stat(&info, STATFLAG_NONAME) != S_OK || info.cbSize.QuadPart > 1024) return E_FAIL;
        std::vector<BYTE> data(static_cast<size_t>(info.cbSize.QuadPart)); ULONG read = 0;
        hr = stream->Read(data.data(), static_cast<ULONG>(data.size()), &read);
        if (FAILED(hr)) return hr; if (read != data.size()) return STG_E_READFAULT;
        state.payload = std::move(data); state.dirty = false; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Save(IStream* stream, BOOL clear) override {
        auto& state = *static_cast<T*>(this)->state; ++state.saves; state.clearDirty = clear;
        HRESULT hr = state.Enter(); if (hr != S_OK) return hr;
        if (!stream) return E_POINTER;
        LARGE_INTEGER zero{}; ULARGE_INTEGER position{}; STATSTG info{};
        if (stream->Seek(zero, STREAM_SEEK_CUR, &position) != S_OK || position.QuadPart ||
            stream->Stat(&info, STATFLAG_NONAME) != S_OK || info.cbSize.QuadPart) return E_FAIL;
        ULONG written = 0; hr = stream->Write(state.payload.data(), static_cast<ULONG>(state.payload.size()), &written);
        if (FAILED(hr)) return hr; if (written != state.payload.size()) return STG_E_WRITEFAULT;
        if (clear) state.dirty = false; return S_OK;
    }
};
struct StoragePrefix { virtual ~StoragePrefix() = default; ULONG padding[3]{}; };
class StorageObject : public StoragePrefix, public ATL::CComObjectRootEx<ATL::CComMultiThreadModel>,
                      public StorageCodec<StorageObject>, public ATL::IPersistStorageImpl<StorageObject> {
public:
    using Codec = StorageCodec<StorageObject>;
    StorageState* state = nullptr;
    ~StorageObject() { if (state) ++state->destroyed; }
    static REFCLSID GetObjectCLSID() noexcept { return StorageClass; }
    BEGIN_COM_MAP(StorageObject)
        if (iid == IID_IPersistStreamInit && state && state->queryFailure)
            return state->queryFailure == 1 ? E_NOINTERFACE : S_FALSE;
        COM_INTERFACE_ENTRY(IPersistStreamInit)
        COM_INTERFACE_ENTRY2(IPersist, Codec)
        COM_INTERFACE_ENTRY(IPersistStorage)
    END_COM_MAP()
};
bool CreateStoredObject(StorageState& state, ATL::CComPtr<IPersistStorage>& output) {
    ATL::CComObject<StorageObject>* object = nullptr;
    REQUIRE(ATL::CComObject<StorageObject>::CreateInstance(&object) == S_OK && object);
    object->state = &state; object->AddRef();
    REQUIRE(object->QueryInterface(IID_IPersistStorage, reinterpret_cast<void**>(&output)) == S_OK);
    object->Release(); state.owner = output; return true;
}
// Observe COM lifetime only; every storage operation still executes in Windows.
class StorageMonitor : public ATL::CComObjectRootEx<ATL::CComMultiThreadModel>, public IStorage {
public:
    ATL::CComPtr<IStorage> native;
    int* destroyed = nullptr;
    ~StorageMonitor() { if (destroyed) ++*destroyed; }
    BEGIN_COM_MAP(StorageMonitor)
        COM_INTERFACE_ENTRY(IStorage)
    END_COM_MAP()
    HRESULT STDMETHODCALLTYPE CreateStream(const OLECHAR* name, DWORD mode, DWORD a, DWORD b, IStream** out) override { return native->CreateStream(name,mode,a,b,out); }
    HRESULT STDMETHODCALLTYPE OpenStream(const OLECHAR* name, void* a, DWORD mode, DWORD b, IStream** out) override { return native->OpenStream(name,a,mode,b,out); }
    HRESULT STDMETHODCALLTYPE CreateStorage(const OLECHAR* name, DWORD mode, DWORD a, DWORD b, IStorage** out) override { return native->CreateStorage(name,mode,a,b,out); }
    HRESULT STDMETHODCALLTYPE OpenStorage(const OLECHAR* name, IStorage* priority, DWORD mode, SNB exclude, DWORD reserved, IStorage** out) override { return native->OpenStorage(name,priority,mode,exclude,reserved,out); }
    HRESULT STDMETHODCALLTYPE CopyTo(DWORD count, const IID* ids, SNB exclude, IStorage* out) override { return native->CopyTo(count,ids,exclude,out); }
    HRESULT STDMETHODCALLTYPE MoveElementTo(const OLECHAR* name, IStorage* target, const OLECHAR* to, DWORD flags) override { return native->MoveElementTo(name,target,to,flags); }
    HRESULT STDMETHODCALLTYPE Commit(DWORD flags) override { return native->Commit(flags); }
    HRESULT STDMETHODCALLTYPE Revert() override { return native->Revert(); }
    HRESULT STDMETHODCALLTYPE EnumElements(DWORD a, void* b, DWORD c, IEnumSTATSTG** out) override { return native->EnumElements(a,b,c,out); }
    HRESULT STDMETHODCALLTYPE DestroyElement(const OLECHAR* name) override { return native->DestroyElement(name); }
    HRESULT STDMETHODCALLTYPE RenameElement(const OLECHAR* name, const OLECHAR* to) override { return native->RenameElement(name,to); }
    HRESULT STDMETHODCALLTYPE SetElementTimes(const OLECHAR* name, const FILETIME* a, const FILETIME* b, const FILETIME* c) override { return native->SetElementTimes(name,a,b,c); }
    HRESULT STDMETHODCALLTYPE SetClass(REFCLSID id) override { return native->SetClass(id); }
    HRESULT STDMETHODCALLTYPE SetStateBits(DWORD bits, DWORD mask) override { return native->SetStateBits(bits,mask); }
    HRESULT STDMETHODCALLTYPE Stat(STATSTG* info, DWORD flags) override { return native->Stat(info,flags); }
};
bool storagePersistence() {
    ATL::CAtlModule module;
    int mediumDestroyed = 0;
    StorageState source, target;
    ATL::CComPtr<IPersistStorage> save, load;
    REQUIRE(CreateStoredObject(source, save) && CreateStoredObject(target, load));
    ATL::CComPtr<IUnknown> identity, fromStream;
    ATL::CComPtr<IPersistStreamInit> streamFace;
    REQUIRE(save->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&identity)) == S_OK &&
        save->QueryInterface(IID_IPersistStreamInit, reinterpret_cast<void**>(&streamFace)) == S_OK &&
        streamFace->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&fromStream)) == S_OK && identity.p == fromStream.p);
    CLSID id{}; REQUIRE(save->GetClassID(&id) == S_OK && id == StorageClass && save->GetClassID(nullptr) == E_POINTER);
    REQUIRE(save->IsDirty() == S_OK && save->InitNew(nullptr) == E_POINTER &&
        save->Load(nullptr) == E_POINTER && save->Save(nullptr, TRUE) == E_POINTER);
    ATL::CComPtr<ILockBytes> bytes;
    REQUIRE(::CreateILockBytesOnHGlobal(nullptr, TRUE, &bytes) == S_OK);
    ATL::CComPtr<IStorage> storage;
    REQUIRE(::StgCreateDocfileOnILockBytes(bytes, STGM_CREATE | STGM_READWRITE | STGM_SHARE_EXCLUSIVE | STGM_TRANSACTED, 0, &storage) == S_OK);
    ATL::CComObject<StorageMonitor>* monitor = nullptr;
    REQUIRE(ATL::CComObject<StorageMonitor>::CreateInstance(&monitor) == S_OK && monitor);
    monitor->native.Attach(storage.Detach()); monitor->destroyed = &mediumDestroyed;
    monitor->AddRef(); storage.Attach(monitor);
    REQUIRE(save->InitNew(storage) == S_OK && save->InitNew(storage) == CO_E_ALREADYINITIALIZED && source.initializedCalls == 2);
    ATL::CComPtr<IStream> sibling;
    REQUIRE(storage->CreateStream(L"Sibling", STGM_CREATE | STGM_READWRITE | STGM_SHARE_EXCLUSIVE, 0, 0, &sibling) == S_OK);
    const BYTE marker = 0x5A; ULONG transferred = 0;
    REQUIRE(sibling->Write(&marker, 1, &transferred) == S_OK && transferred == 1); sibling.Release();
    REQUIRE(::WriteClassStg(storage, CLSID_StdFont) == S_OK && storage->Commit(STGC_DEFAULT) == S_OK);
    const int beforeMissing = target.loads;
    REQUIRE(FAILED(load->Load(storage)) && target.loads == beforeMissing);
    REQUIRE(save->Save(storage, FALSE) == S_OK && source.clearDirty == FALSE && save->IsDirty() == S_OK);
    REQUIRE(::ReadClassStg(storage, &id) == S_OK && id == CLSID_StdFont); // Save does not write the CLSID.
    REQUIRE(load->Load(storage) == S_OK && target.payload == source.payload && load->IsDirty() == S_FALSE);
    REQUIRE(storage->Revert() == S_OK && FAILED(load->Load(storage))); // Save did not commit the root.
    REQUIRE(save->Save(storage, TRUE) == S_OK && source.clearDirty == TRUE && save->IsDirty() == S_FALSE);
    REQUIRE(save->SaveCompleted(nullptr) == S_OK && save->HandsOffStorage() == S_OK);
    source.payload = {9,8,7}; source.dirty = true;
    REQUIRE(save->Save(storage, TRUE) == S_OK && load->Load(storage) == S_OK && target.payload == source.payload);
    REQUIRE(storage->OpenStream(L"Contents", nullptr, STGM_READ | STGM_SHARE_EXCLUSIVE, 0, &sibling) == S_OK);
    STATSTG info{}; REQUIRE(sibling->Stat(&info, STATFLAG_NONAME) == S_OK && info.cbSize.QuadPart == 3); sibling.Release();
    REQUIRE(storage->OpenStream(L"Sibling", nullptr, STGM_READ | STGM_SHARE_EXCLUSIVE, 0, &sibling) == S_OK);
    BYTE actual = 0; REQUIRE(sibling->Read(&actual, 1, &transferred) == S_OK && transferred == 1 && actual == marker); sibling.Release();
    source.dirty = true;
    REQUIRE(::OleSave(save, storage, TRUE) == S_OK && ::ReadClassStg(storage, &id) == S_OK && id == StorageClass);
    REQUIRE(save->SaveCompleted(storage) == S_OK && storage->Commit(STGC_DEFAULT) == S_OK);
    for (int mode = 1; mode <= 2; ++mode) {
        source.queryFailure = mode;
        const HRESULT expected = mode == 1 ? E_NOINTERFACE : E_UNEXPECTED;
        REQUIRE(save->IsDirty() == expected && save->InitNew(storage) == expected &&
            save->Load(storage) == expected && save->Save(storage, TRUE) == expected);
        REQUIRE(save->GetClassID(&id) == S_OK && id == StorageClass && save->SaveCompleted(nullptr) == S_OK && save->HandsOffStorage() == S_OK);
    }
    source.queryFailure = 0;
    REQUIRE(load->Load(storage) == S_OK && target.payload == source.payload); // Failed QI did not truncate Contents.
    for (int mode = 1; mode <= 4; ++mode) {
        source.failure = mode; source.dirty = true;
        const HRESULT expected = mode == 1 ? E_ACCESSDENIED : mode == 2 ? E_ABORT : mode == 3 ? E_OUTOFMEMORY : E_UNEXPECTED;
        REQUIRE(save->IsDirty() == expected && save->InitNew(storage) == expected &&
            save->Load(storage) == expected && save->Save(storage, TRUE) == expected && source.dirty);
        REQUIRE(save->SaveCompleted(storage) == S_OK && save->HandsOffStorage() == S_OK);
    }
    source.failure = 0;
    REQUIRE(save->Save(storage, TRUE) == S_OK && load->Load(storage) == S_OK && target.payload == source.payload);
    REQUIRE(storage->Commit(STGC_DEFAULT) == S_OK); storage.Release();
    REQUIRE(mediumDestroyed == 1); // Both objects are alive; no adapter holds this supplied medium.
    // HGLOBAL does not guarantee cross-instance region locks; this is a byte round trip.
    ATL::CComPtr<IStorage> readOnly;
    REQUIRE(::StgOpenStorageOnILockBytes(bytes, nullptr, STGM_READ | STGM_SHARE_EXCLUSIVE, nullptr, 0, &readOnly) == S_OK);
    REQUIRE(load->Load(readOnly) == S_OK && target.payload == source.payload);
    const int saves = source.saves; source.dirty = true;
    REQUIRE(FAILED(save->Save(readOnly, TRUE)) && source.saves == saves && source.dirty);
    REQUIRE(source.valid && target.valid);
    identity.Release(); fromStream.Release(); streamFace.Release(); save.Release(); load.Release();
    REQUIRE(source.destroyed == 1 && target.destroyed == 1 && module.GetLockCount() == 0);
    readOnly.Release();
    REQUIRE(::StgOpenStorageOnILockBytes(bytes, nullptr, STGM_READWRITE | STGM_SHARE_EXCLUSIVE, nullptr, 0, &storage) == S_OK);
    StorageState dropped; ATL::CComPtr<IPersistStorage> last;
    REQUIRE(CreateStoredObject(dropped, last)); dropped.reenter = false; dropped.dirty = true;
    dropped.dropOwner = last.Detach();
    REQUIRE(dropped.dropOwner->Save(storage, TRUE) == S_OK && !dropped.dropOwner && !dropped.dirty && dropped.destroyed == 1);
    REQUIRE(module.GetLockCount() == 0);
    return true;
}
bool creation() {
    ControlState state;
    CLSID classId{}; REQUIRE(::CoCreateGuid(&classId) == S_OK);
    wchar_t className[40]{}; REQUIRE(::StringFromGUID2(classId, className, 40) > 0);
    state.classId = classId;
    ATL::CComPtr<IClassFactory2> factory; factory.Attach(new Factory(state));
    Registration registration;
    REQUIRE(::CoRegisterClassObject(classId, factory, CLSCTX_LOCAL_SERVER, REGCLS_MULTIPLEUSE, &registration.cookie) == S_OK);
    for (int mode = 0; mode < 6; ++mode) {
        state = ControlState{};
        state.classId = classId;
        state.siteFirst = mode != 1;
        state.failInit = mode == 2;
        state.destroyDuringInit = mode == 3;
        state.initInterface = mode != 5;
        OwnedWindow<ATL::CAxWindow2> window;
        RECT rect{0,0,100,100};
        REQUIRE(window.Create(nullptr, &rect, nullptr, WS_POPUP));
        state.window = window;
        ATL::CComPtr<IStream> stream;
        if (mode == 4 || mode == 5) {
            REQUIRE(::CreateStreamOnHGlobal(nullptr, TRUE, &stream) == S_OK);
            DWORD marker = 42; ULONG written = 0;
            REQUIRE(stream->Write(&marker, sizeof(marker), &written) == S_OK && written == sizeof(marker));
            LARGE_INTEGER start{}; REQUIRE(stream->Seek(start, STREAM_SEEK_SET, nullptr) == S_OK);
        }
        ATL::CComBSTR license(mode == 4 ? L"OpenATL-test-license" : nullptr);
        HRESULT hr = window.CreateControlLic(className, stream, nullptr, license);
        if (mode == 2 || mode == 3) {
            REQUIRE(FAILED(hr) && state.activated == 0);
            REQUIRE(state.detached == 1 && state.closed == 1 && state.destroyed == 1);
        } else {
            REQUIRE(hr == S_OK && state.siteOrder && state.activated == 1);
            REQUIRE((mode >= 4 ? state.loaded : state.initialized) == 1);
            if (mode == 4) REQUIRE(state.licenseSeen);
        }
        if (::IsWindow(window)) REQUIRE(window.DestroyWindow());
        window.m_hWnd = nullptr;
        REQUIRE(state.destroyed == 1);
    }
    return true;
}
class ResourceDialog : public ATL::CAxDialogImpl<ResourceDialog> {
public:
    enum { IDD = 610 };
    WORD resource = IDD;
    ControlState& state;
    bool modal = false, closeInInit = false, good = true, attached = false;
    int initialized = 0, finalized = 0;
    int expectedNew = 0, expectedLoads = 1, expectedActivations = 1, expectedStorages = 0;
    explicit ResourceDialog(ControlState& value) : state(value) {}
    DLGPROC GetDialogProc() override { return Trace; }
    static INT_PTR CALLBACK Trace(HWND context, UINT message, WPARAM w, LPARAM l) {
        auto self = static_cast<ResourceDialog*>(reinterpret_cast<ATL::CDialogImplBaseT<ATL::CWindow>*>(context));
        self->attached = true;
        return ATL::CAxDialogImpl<ResourceDialog>::DialogProc(context, message, w, l);
    }
    int GetIDD() const { return resource; }
    LRESULT Init(UINT, WPARAM, LPARAM parameter, BOOL&) {
        ++initialized;
        ATL::CComPtr<IOleObject> control;
        good = parameter == 1773 && state.loaded == expectedLoads && state.initialized == expectedNew && state.activated == expectedActivations &&
            GetDlgControl(711, IID_IOleObject, reinterpret_cast<void**>(&control)) == S_OK && ::GetDlgItem(m_hWnd, 714);
        good = good && state.storageLoaded == expectedStorages;
        if (expectedStorages) {
            SIZEL extent{};
            good = good && control->GetExtent(DVASPECT_CONTENT, &extent) == S_OK && extent.cx == 42 && extent.cy == 4;
        }
        if (resource == 648) {
            WCHAR text[16]{};
            const HWND list = ::GetDlgItem(m_hWnd, 715), combo = ::GetDlgItem(m_hWnd, 716);
            good = good && ::SendMessageW(list, LB_GETCOUNT, 0, 0) == 1 && ::SendMessageW(combo, CB_GETCOUNT, 0, 0) == 1;
            good = good && ::SendMessageW(list, LB_GETTEXT, 0, reinterpret_cast<LPARAM>(text)) == 3 && !::wcscmp(text, L"mix");
            good = good && ::SendMessageW(combo, CB_GETLBTEXT, 0, reinterpret_cast<LPARAM>(text)) == 3 && !::wcscmp(text, L"mix");
        }
        WCHAR hostClass[32]{};
        good = ::GetClassNameW(::GetDlgItem(m_hWnd, 711), hostClass, 32) &&
            !::wcscmp(hostClass, resource == 611 || resource == 621 || resource == 632 ? L"AtlAxWinLic140" : L"AtlAxWin140") && good;
        if (modal) good = EndDialog(93) && good;
        else if (closeInInit) good = DestroyWindow() && good;
        return TRUE;
    }
    void OnFinalMessage(HWND) override { ++finalized; }
    BEGIN_MSG_MAP(ResourceDialog)
        MESSAGE_HANDLER(WM_INITDIALOG, Init)
    END_MSG_MAP()
};
bool resourceInitialization() {
    ControlState state;
    CLSID classId{};
    REQUIRE(::CLSIDFromString(L"{2D71A87E-15C3-491D-9F8B-8319472CEA04}", &classId) == S_OK);
    ATL::CComPtr<IClassFactory2> factory; factory.Attach(new Factory(state));
    Registration registration;
    REQUIRE(::CoRegisterClassObject(classId, factory, CLSCTX_LOCAL_SERVER, REGCLS_MULTIPLEUSE, &registration.cookie) == S_OK);
    ATL::CAtlModule module;
    for (WORD resource : {WORD(610), WORD(611), WORD(612), WORD(619), WORD(620), WORD(621), WORD(622), WORD(631), WORD(632), WORD(633), WORD(648)}) for (int mode = 0; mode < 5; ++mode) {
        state = ControlState{}; state.classId = classId; state.discoverWindow = true;
        state.siteFirst = mode != 1; state.initInterface = mode != 2;
        state.sendInitDuringLoad = mode == 4;
        state.streamBytes = resource == 619 ? 5 : 4;
        ResourceDialog dialog(state); dialog.resource = resource; dialog.modal = mode == 3;
        if (resource == 648) {
            dialog.expectedNew = state.initInterface ? 1 : 0;
            dialog.expectedLoads = 0;
        } else if (resource >= 631) {
            dialog.expectedStorages = 1;
            dialog.expectedNew = resource == 633 && state.initInterface ? 1 : 0;
            dialog.expectedLoads = resource == 633 ? 1 : 0;
            dialog.expectedActivations = resource == 633 ? 3 : 1;
        } else if (resource >= 620) {
            dialog.expectedNew = state.initInterface ? 1 : 0;
            dialog.expectedLoads = resource == 622 ? 1 : 0;
            dialog.expectedActivations = resource == 622 ? 2 : 1;
        }
        if (dialog.modal) REQUIRE(dialog.DoModal(nullptr, 1773) == 93);
        else {
            REQUIRE(dialog.Create(nullptr, 1773));
            REQUIRE(module.GetLockCount() == dialog.expectedActivations);
            REQUIRE(dialog.DestroyWindow());
        }
        REQUIRE(dialog.good && dialog.initialized == 1 && dialog.finalized == 1 && !dialog.m_hWnd);
        REQUIRE(state.siteOrder && state.loaded == dialog.expectedLoads && state.initialized == dialog.expectedNew && state.activated == dialog.expectedActivations);
        REQUIRE(state.licenseSeen == (resource == 612 || resource == 619 || resource == 632));
        REQUIRE(state.storageReadOnly && state.storageReleased == dialog.expectedStorages);
        if (dialog.expectedStorages) REQUIRE(state.lateStorageReads == 1);
        REQUIRE(state.detached == dialog.expectedActivations && state.closed == dialog.expectedActivations &&
            state.destroyed == dialog.expectedActivations && module.GetLockCount() == 0);
    }
    // Failure must finish before application initialization, clean all earlier
    // children and leave the C++ owner reusable without pending creation data.
    for (int mode = 0; mode < 7; ++mode) for (bool modal : {false, true}) {
        state = ControlState{}; state.classId = classId; state.discoverWindow = true;
        state.failInit = mode == 2;
        state.destroyDuringInit = mode == 3;
        state.destroyDialogDuringInit = mode == 4;
        ResourceDialog dialog(state); dialog.modal = modal;
        dialog.resource = mode == 0 ? 613 : mode == 1 ? 614 : 610;
        dialog.closeInInit = mode == 5;
        if (mode == 6) state.initInterface = false; // Stream fallback failure, not InitNew.
        if (mode == 6) state.failInit = true;
        if (mode == 5 && modal) {
            REQUIRE(dialog.DoModal(nullptr, 1773) == 93);
        } else if (modal) REQUIRE(dialog.DoModal(nullptr, 1773) == -1);
        else REQUIRE(!dialog.Create(nullptr, 1773));
        REQUIRE(!dialog.m_hWnd && module.GetLockCount() == 0);
        REQUIRE(dialog.initialized == (mode == 5 ? 1 : 0));
        REQUIRE(dialog.finalized == (dialog.attached ? 1 : 0));
        REQUIRE(state.destroyed == (mode == 0 ? 0 : mode == 1 ? 2 : 1));
        state = ControlState{}; state.classId = classId; state.discoverWindow = true;
        dialog.resource = 610; dialog.modal = false; dialog.closeInInit = false;
        REQUIRE(dialog.Create(nullptr, 1773) && dialog.good);
        REQUIRE(dialog.DestroyWindow() && module.GetLockCount() == 0);
    }
    for (int mode = 0; mode < 6; ++mode) for (bool modal : {false, true}) {
        state = ControlState{}; state.classId = classId; state.discoverWindow = true;
        state.failInit = mode == 0;
        state.destroyDuringInit = mode == 1;
        state.destroyDialogDuringInit = mode == 2;
        state.failInitializationAt = mode == 3 ? 2 : 0;
        ResourceDialog dialog(state); dialog.modal = modal;
        dialog.resource = mode == 3 ? 627 : mode == 5 ? 625 : 620;
        dialog.expectedNew = 1; dialog.expectedLoads = 0;
        dialog.closeInInit = mode == 4;
        if (mode == 4 && modal) REQUIRE(dialog.DoModal(nullptr, 1773) == 93);
        else if (modal) REQUIRE(dialog.DoModal(nullptr, 1773) == -1);
        else REQUIRE(!dialog.Create(nullptr, 1773));
        REQUIRE(!dialog.m_hWnd && module.GetLockCount() == 0);
        REQUIRE(dialog.initialized == (mode == 4 ? 1 : 0) && dialog.finalized == (dialog.attached ? 1 : 0));
        REQUIRE(state.initialized == (mode == 3 ? 2 : mode == 5 ? 0 : 1) && state.loaded == 0);
        REQUIRE(state.activated == (mode == 3 || mode == 4 ? 1 : 0));
        REQUIRE(state.destroyed == (mode == 3 ? 2 : 1));
        state = ControlState{}; state.classId = classId; state.discoverWindow = true;
        dialog.resource = 620; dialog.modal = false; dialog.closeInInit = false;
        REQUIRE(dialog.Create(nullptr, 1773) && dialog.good);
        REQUIRE(dialog.DestroyWindow() && module.GetLockCount() == 0);
    }
    for (int mode = 0; mode < 9; ++mode) for (bool modal : {false, true}) {
        state = ControlState{}; state.classId = classId; state.discoverWindow = true;
        state.storageInterface = mode != 1;
        state.failInit = mode == 2;
        state.destroyDuringInit = mode == 3;
        state.destroyDialogDuringInit = mode == 4;
        state.failInitializationAt = mode == 5 ? 2 : 0;
        state.rejectLicense = mode == 7;
        state.destroyDuringStorageQuery = mode == 8;
        ResourceDialog dialog(state); dialog.modal = modal;
        dialog.resource = mode == 0 ? 634 : mode == 5 ? 636 : mode == 7 ? 632 : 631;
        dialog.expectedLoads = 0; dialog.expectedStorages = 1; dialog.closeInInit = mode == 6;
        if (mode == 6 && modal) REQUIRE(dialog.DoModal(nullptr, 1773) == 93);
        else if (modal) REQUIRE(dialog.DoModal(nullptr, 1773) == -1);
        else REQUIRE(!dialog.Create(nullptr, 1773));
        REQUIRE(!dialog.m_hWnd && module.GetLockCount() == 0 && !state.initialized && !state.loaded);
        REQUIRE(dialog.initialized == (mode == 6 ? 1 : 0) && dialog.finalized == (dialog.attached ? 1 : 0));
        REQUIRE(state.storageLoaded == (mode == 0 || mode == 1 || mode == 7 || mode == 8 ? 0 : mode == 5 ? 2 : 1));
        REQUIRE(state.storageReleased == state.storageLoaded && state.storageReadOnly);
        REQUIRE(state.activated == (mode == 5 || mode == 6 ? 1 : 0));
        REQUIRE(state.destroyed == (mode == 0 || mode == 7 ? 0 : mode == 5 ? 2 : 1));
        state = ControlState{}; state.classId = classId; state.discoverWindow = true;
        dialog.resource = 631; dialog.modal = false; dialog.closeInInit = false;
        REQUIRE(dialog.Create(nullptr, 1773) && dialog.good);
        REQUIRE(dialog.DestroyWindow() && state.storageReleased == 1 && module.GetLockCount() == 0);
    }
    state = ControlState{}; state.classId = classId; state.discoverWindow = true;
    ResourceDialog retainedDialog(state); retainedDialog.resource = 631;
    retainedDialog.expectedLoads = 0; retainedDialog.expectedStorages = 1;
    REQUIRE(retainedDialog.Create(nullptr, 1773) && retainedDialog.good && state.lateStorageReads == 1);
    ATL::CComPtr<IOleObject> retained;
    REQUIRE(retainedDialog.GetDlgControl(711, IID_IOleObject, reinterpret_cast<void**>(&retained)) == S_OK);
    REQUIRE(retainedDialog.DestroyWindow() && !state.destroyed && state.storageReleased == 1 && module.GetLockCount() == 0);
    SIZEL closedExtent{}; REQUIRE(retained->GetExtent(DVASPECT_CONTENT, &closedExtent) == E_NOTIMPL);
    retained.Release(); REQUIRE(state.destroyed == 1);
    return true;
}
struct ActiveState { bool inside = false, diedInside = false; int destroyed = 0, calls = 0; };
class Active : public IOleInPlaceActiveObject {
public:
    Active(ActiveState& state, IOleInPlaceFrame* frame, HWND window, bool destroy) : state_(state), frame_(frame), window_(window), destroy_(destroy) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** value) override {
        if (!value) return E_POINTER; *value = nullptr;
        if (iid != IID_IUnknown && iid != IID_IOleWindow && iid != IID_IOleInPlaceActiveObject) return E_NOINTERFACE;
        *value = static_cast<IOleInPlaceActiveObject*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { const auto n = --refs_; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE GetWindow(HWND* window) override { if (!window) return E_POINTER; *window = window_; return S_OK; }
    HRESULT STDMETHODCALLTYPE ContextSensitiveHelp(BOOL) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE TranslateAccelerator(LPMSG) override {
        ActiveState& state = state_; // remains valid even if the old host deletes this
        state.inside = true; ++state.calls;
        if (destroy_) ::DestroyWindow(window_); else frame_->SetActiveObject(nullptr, nullptr);
        state.inside = false; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnFrameWindowActivate(BOOL) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDocWindowActivate(BOOL) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE ResizeBorder(LPCRECT, IOleInPlaceUIWindow*, BOOL) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE EnableModeless(BOOL) override { return S_OK; }
private:
    ~Active() { state_.diedInside = state_.inside; ++state_.destroyed; }
    ULONG refs_ = 1;
    ActiveState& state_;
    IOleInPlaceFrame* frame_; // test keeps its frame reference through the callback
    HWND window_;
    bool destroy_;
};
bool accelerators() {
    for (int mode = 0; mode < 4; ++mode) {
        ActiveState state;
        OwnedWindow<ATL::CAxWindow> window;
        RECT rect{0,0,100,100}; REQUIRE(window.Create(nullptr, &rect, nullptr, WS_POPUP));
        ATL::CComPtr<IOleInPlaceFrame> frame; REQUIRE(window.QueryHost(&frame) == S_OK);
        ATL::CComPtr<IOleControlSite> site; REQUIRE(window.QueryHost(&site) == S_OK);
        auto active = new Active(state, frame, window, mode >= 2);
        REQUIRE(frame->SetActiveObject(active, nullptr) == S_OK); active->Release();
        MSG message{}; message.hwnd = window; message.message = WM_KEYDOWN; message.wParam = VK_TAB;
        if (mode % 2) REQUIRE(site->TranslateAccelerator(&message, 0) == S_OK);
        else REQUIRE(::SendMessageW(window, WM_FORWARDMSG, 0, reinterpret_cast<LPARAM>(&message)) == TRUE);
        REQUIRE(state.calls == 1 && state.destroyed == 1 && !state.diedInside);
        if (mode >= 2) {
            ActiveState lateState;
            auto late = new Active(lateState, frame, window, false);
            HRESULT lateResult = frame->SetActiveObject(late, nullptr);
            late->Release();
            frame->SetActiveObject(nullptr, nullptr);
            REQUIRE(lateResult == CO_E_OBJNOTCONNECTED && lateState.destroyed == 1);
        }
        if (::IsWindow(window)) REQUIRE(window.DestroyWindow());
        window.m_hWnd = nullptr;
    }
    return true;
}
}
bool RunHostLifecycleTests() {
    const bool created = creation();
    const bool active = accelerators();
    const bool resources = resourceInitialization();
    const bool ui = uiHandlers();
    const bool properties = propertyFrames();
    const bool storage = storagePersistence();
    return created && active && resources && ui && properties && storage;
}
bool RunBrowserUIHandlerTest(ATL::CAxWindow& window, IHTMLDocument2* document) {
    UIState state; state.browser = true;
    ATL::CComPtr<UIObject> object; object.Attach(new UIObject); state.object = object;
    ATL::CComPtr<IDocHostUIHandlerDispatch> handler; handler.Attach(new UIHandler(state));
    ATL::CComPtr<IAxWinHostWindow> host; REQUIRE(window.QueryHost(&host) == S_OK);
    struct Reset {
        IAxWinHostWindow* host;
        ~Reset() { host->SetExternalUIHandler(nullptr); }
    } reset{host}; // Clear before the borrowed fixture state leaves scope, including failures.
    REQUIRE(window.SetExternalUIHandler(handler) == S_OK);
    ATL::CComPtr<IHTMLWindow2> script; REQUIRE(document->get_parentWindow(&script) == S_OK && script);
    ATL::CComPtr<IDispatch> external; REQUIRE(script->get_external(&external) == S_OK);
    REQUIRE(state.calls[12] > 0 && external);
    LPOLESTR name = const_cast<LPOLESTR>(L"OpenWTLHostToken"); DISPID id = DISPID_UNKNOWN;
    REQUIRE(external->GetIDsOfNames(IID_NULL, &name, 1, LOCALE_USER_DEFAULT, &id) == S_OK);
    DISPPARAMS args{}; ATL::CComVariant value;
    REQUIRE(external->Invoke(id, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_PROPERTYGET, &args, &value, nullptr, nullptr) == S_OK &&
        value.vt == VT_I4 && value.lVal == 0x4f57544c);
    REQUIRE(window.SetExternalUIHandler(nullptr) == S_OK);
    handler.Release(); REQUIRE(state.destroyed == 1 && !state.diedInside);
    return true;
}
