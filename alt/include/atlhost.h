// OpenATL windowed OLE containment. The application owns COM initialization.
#ifndef OPENATL_ATLHOST_H
#define OPENATL_ATLHOST_H
#pragma once
#include "atlcom.h"
#include "atlwin.h"
#include "atliface.h"
#include <mshtmhst.h>
#include <mshtml.h>
#include <exdisp.h>
#include <new>
#include <cstring>
#include "openatl/atl_dialog_template.h"
#include "openatl/atl_dialog_split.h"

namespace ATL {
namespace detail {
inline constexpr wchar_t AxHostProperty[] = L"OpenATL.AxHost.1.B6EA2050";
inline HRESULT RegisteredClassId(LPCOLESTR name, CLSID* id) noexcept {
    if (!name || !id) return E_POINTER;
    if (name[0] == L'{') return ::CLSIDFromString(name, id);
    // CLSIDFromProgID can register a new OLE1 class for an unknown string.
    // Containment only reads registered ProgIDs; URLs never register classes.
    CRegKey program, clsid;
    LSTATUS status = program.Open(HKEY_CLASSES_ROOT, name, KEY_READ);
    if (status == ERROR_SUCCESS) status = clsid.Open(program, L"CLSID", KEY_READ);
    wchar_t value[39]{}; ULONG length = 39;
    if (status == ERROR_SUCCESS) status = clsid.QueryStringValue(nullptr, value, &length);
    return status == ERROR_SUCCESS ? ::CLSIDFromString(value, id) : HRESULT_FROM_WIN32(status);
}
// Adapt the public ATL UI dispatch contract to the native MSHTML site ABI.
// The host retains this adapter across each call, including reentrant removal.
class AxUIHandler : private AtlObjectModulePin, public IDocHostUIHandler {
public:
    explicit AxUIHandler(IDocHostUIHandlerDispatch* handler) noexcept : handler_(handler) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** output) override {
        if (!output) return E_POINTER;
        *output = nullptr;
        if (iid != IID_IUnknown && iid != IID_IDocHostUIHandler) return E_NOINTERFACE;
        *output = static_cast<IDocHostUIHandler*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ::InterlockedIncrement(&refs_); }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG count = ::InterlockedDecrement(&refs_);
        if (!count) delete this;
        return count;
    }
    HRESULT STDMETHODCALLTYPE ShowContextMenu(DWORD id, POINT* point, IUnknown* command, IDispatch* reserved) override {
        if (!point) return E_POINTER;
        HRESULT result = S_FALSE;
        const HRESULT hr = handler_->ShowContextMenu(id, static_cast<DWORD>(point->x), static_cast<DWORD>(point->y), command, reserved, &result);
        return FAILED(hr) ? hr : result;
    }
    HRESULT STDMETHODCALLTYPE GetHostInfo(DOCHOSTUIINFO* info) override {
        if (!info) return E_POINTER;
        return handler_->GetHostInfo(&info->dwFlags, &info->dwDoubleClick);
    }
    HRESULT STDMETHODCALLTYPE ShowUI(DWORD id, IOleInPlaceActiveObject* active, IOleCommandTarget* target,
                                     IOleInPlaceFrame* frame, IOleInPlaceUIWindow* document) override {
        HRESULT result = S_FALSE;
        const HRESULT hr = handler_->ShowUI(id, active, target, frame, document, &result);
        return FAILED(hr) ? hr : result;
    }
    HRESULT STDMETHODCALLTYPE HideUI() override { return handler_->HideUI(); }
    HRESULT STDMETHODCALLTYPE UpdateUI() override { return handler_->UpdateUI(); }
    HRESULT STDMETHODCALLTYPE EnableModeless(BOOL enable) override { return handler_->EnableModeless(enable ? VARIANT_TRUE : VARIANT_FALSE); }
    HRESULT STDMETHODCALLTYPE OnDocWindowActivate(BOOL active) override { return handler_->OnDocWindowActivate(active ? VARIANT_TRUE : VARIANT_FALSE); }
    HRESULT STDMETHODCALLTYPE OnFrameWindowActivate(BOOL active) override { return handler_->OnFrameWindowActivate(active ? VARIANT_TRUE : VARIANT_FALSE); }
    HRESULT STDMETHODCALLTYPE ResizeBorder(LPCRECT rect, IOleInPlaceUIWindow* window, BOOL frame) override {
        if (!rect) return E_POINTER;
        return handler_->ResizeBorder(rect->left, rect->top, rect->right, rect->bottom, window, frame ? VARIANT_TRUE : VARIANT_FALSE);
    }
    HRESULT STDMETHODCALLTYPE TranslateAccelerator(LPMSG message, const GUID* group, DWORD id) override {
        if (!message) return E_POINTER;
        CComBSTR command;
        if (group) {
            WCHAR text[39]{};
            if (!::StringFromGUID2(*group, text, 39)) return E_INVALIDARG;
            command.Attach(::SysAllocStringLen(text, 38));
            if (!command.m_str) return E_OUTOFMEMORY;
        }
        HRESULT result = S_FALSE;
        const HRESULT hr = handler_->TranslateAccelerator(reinterpret_cast<DWORD_PTR>(message->hwnd), message->message,
            static_cast<DWORD_PTR>(message->wParam), static_cast<DWORD_PTR>(message->lParam), command, id, &result);
        return FAILED(hr) ? hr : result;
    }
    HRESULT STDMETHODCALLTYPE GetOptionKeyPath(LPOLESTR* output, DWORD flags) override {
        if (!output) return E_POINTER;
        *output = nullptr;
        CComBSTR value;
        const HRESULT hr = handler_->GetOptionKeyPath(&value.m_str, flags);
        return FAILED(hr) ? hr : CopyString(value, output, hr);
    }
    HRESULT STDMETHODCALLTYPE GetDropTarget(IDropTarget* original, IDropTarget** output) override {
        if (!output) return E_POINTER;
        *output = nullptr;
        CComPtr<IUnknown> value;
        const HRESULT hr = handler_->GetDropTarget(original, &value);
        if (FAILED(hr) || !value) return hr;
        const HRESULT query = value->QueryInterface(IID_IDropTarget, reinterpret_cast<void**>(output));
        return FAILED(query) ? query : hr;
    }
    HRESULT STDMETHODCALLTYPE GetExternal(IDispatch** output) override {
        if (!output) return E_POINTER;
        *output = nullptr;
        CComPtr<IDispatch> value;
        const HRESULT hr = handler_->GetExternal(&value);
        if (SUCCEEDED(hr)) *output = value.Detach();
        return hr;
    }
    HRESULT STDMETHODCALLTYPE TranslateUrl(DWORD flags, OLECHAR* input, OLECHAR** output) override {
        if (!output) return E_POINTER;
        *output = nullptr;
        CComBSTR source(input), value;
        if (input && !source.m_str) return E_OUTOFMEMORY;
        const HRESULT hr = handler_->TranslateUrl(flags, source, &value.m_str);
        return FAILED(hr) ? hr : CopyString(value, output, hr);
    }
    HRESULT STDMETHODCALLTYPE FilterDataObject(IDataObject* original, IDataObject** output) override {
        if (!output) return E_POINTER;
        *output = nullptr;
        CComPtr<IUnknown> value;
        const HRESULT hr = handler_->FilterDataObject(original, &value);
        if (FAILED(hr) || !value) return hr;
        const HRESULT query = value->QueryInterface(IID_IDataObject, reinterpret_cast<void**>(output));
        return FAILED(query) ? query : hr;
    }
private:
    static HRESULT CopyString(BSTR value, LPOLESTR* output, HRESULT result) noexcept {
        if (!value) return result;
        const SIZE_T length = ::SysStringLen(value);
        if (length > SIZE_T(-1) / sizeof(WCHAR) - 1) return E_OUTOFMEMORY;
        const SIZE_T bytes = (length + 1) * sizeof(WCHAR);
        *output = static_cast<LPOLESTR>(::CoTaskMemAlloc(bytes));
        if (!*output) return E_OUTOFMEMORY;
        std::memcpy(*output, value, bytes); return result;
    }
    LONG refs_ = 1;
    CComPtr<IDocHostUIHandlerDispatch> handler_;
};
class AxHost : private AtlObjectModulePin, public IOleClientSite, public IOleInPlaceSite,
               public IOleInPlaceFrame, public IOleControlSite,
               public IAxWinAmbientDispatch, public IAxWinHostWindow,
               public IDocHostUIHandler {
public:
    explicit AxHost(HWND window) noexcept : window_(window), thread_(::GetCurrentThreadId()) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** output) override {
        if (!output) return E_POINTER;
        *output = nullptr;
        if (iid == IID_IUnknown || iid == IID_IOleClientSite) *output = static_cast<IOleClientSite*>(this);
        else if (iid == IID_IOleWindow || iid == IID_IOleInPlaceSite) *output = static_cast<IOleInPlaceSite*>(this);
        else if (iid == IID_IOleInPlaceUIWindow || iid == IID_IOleInPlaceFrame) *output = static_cast<IOleInPlaceFrame*>(this);
        else if (iid == IID_IOleControlSite) *output = static_cast<IOleControlSite*>(this);
        else if (iid == IID_IDispatch || iid == IID_IAxWinAmbientDispatch) *output = static_cast<IAxWinAmbientDispatch*>(this);
        else if (iid == IID_IAxWinHostWindow) *output = static_cast<IAxWinHostWindow*>(this);
        else if (iid == IID_IDocHostUIHandler) *output = static_cast<IDocHostUIHandler*>(this);
        else return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ::InterlockedIncrement(&refs_); }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG count = ::InterlockedDecrement(&refs_);
        if (!count) delete this;
        return count;
    }
    HRESULT CheckWindow() const noexcept {
        if (thread_ != ::GetCurrentThreadId()) return RPC_E_WRONG_THREAD;
        return window_ && ::IsWindow(window_) ? S_OK : CO_E_OBJNOTCONNECTED;
    }
    void Close() noexcept {
        if (closing_) return;
        closing_ = true;
        // Break the control/client-site cycle while the window still owns us.
        CComPtr<IOleObject> closing;
        closing.Attach(control_.Detach());
        active_.Release();
        if (closing) {
            if (eventCookie_) AtlUnadvise(closing, eventIID_, eventCookie_);
            eventCookie_ = 0;
            closing->Close(OLECLOSE_NOSAVE);
            closing->SetClientSite(nullptr);
            ::OleSetContainedObject(closing, FALSE);
        }
        active_.Release();
        closing_ = false;
    }
    static LRESULT CALLBACK Subclass(HWND window, UINT msg, WPARAM w, LPARAM l,
                                      UINT_PTR id, DWORD_PTR data) {
        auto self = reinterpret_cast<AxHost*>(data);
        CComPtr<IUnknown> keep(static_cast<IOleClientSite*>(self));
        if (msg == WM_SIZE && self->control_) {
            CComPtr<IOleInPlaceObject> object;
            if (SUCCEEDED(self->control_->QueryInterface(IID_IOleInPlaceObject, reinterpret_cast<void**>(&object)))) {
                RECT rect{}; ::GetClientRect(window, &rect);
                object->SetObjectRects(&rect, &rect);
            }
        } else if (msg == WM_SETFOCUS && self->control_) {
            CComPtr<IOleWindow> object;
            HWND child = nullptr;
            if (SUCCEEDED(self->control_->QueryInterface(IID_IOleWindow, reinterpret_cast<void**>(&object))) &&
                SUCCEEDED(object->GetWindow(&child)) && child && child != window) ::SetFocus(child);
        } else if (msg == WM_FORWARDMSG && self->active_ && l) {
            CComPtr<IOleInPlaceActiveObject> active = self->active_;
            return active->TranslateAccelerator(reinterpret_cast<MSG*>(l)) == S_OK;
        } else if (msg == WM_NCDESTROY) {
            ::RemovePropW(window, AxHostProperty);
            ::RemoveWindowSubclass(window, Subclass, id);
            self->window_ = nullptr;
            self->Close();
            self->Release(); // release the window's reference, keep holds the callback
        }
        return ::DefSubclassProc(window, msg, w, l);
    }

    HRESULT STDMETHODCALLTYPE CreateControl(LPCOLESTR name, HWND window, IStream* stream) override {
        return CreateControlEx(name, window, stream, nullptr, IID_NULL, nullptr);
    }
    HRESULT STDMETHODCALLTYPE CreateControlEx(LPCOLESTR name, HWND window, IStream* stream,
                                              IUnknown** output, REFIID iid, IUnknown* sink) override {
        return CreateNamedControl(name, window, stream, output, iid, sink, nullptr);
    }
    HRESULT CreateNamedControl(LPCOLESTR name, HWND window, IStream* stream, IUnknown** output,
                               REFIID iid, IUnknown* sink, BSTR license, IStorage* storage = nullptr) noexcept {
        if (output) *output = nullptr;
        HRESULT hr = CheckWindow();
        if (FAILED(hr)) return hr;
        if (window != window_ || !name || (stream && storage)) return E_INVALIDARG;
        if (control_ || creating_ || closing_) return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);
        CComPtr<IUnknown> keep(static_cast<IOleClientSite*>(this));
        struct Creating { bool& value; Creating(bool& v) : value(v) { v = true; } ~Creating() { value = false; } } creating(creating_);
        CLSID clsid{};
        bool navigate = false;
        hr = RegisteredClassId(name, &clsid);
        if (FAILED(hr) && (license || storage || name[0] == L'{')) return hr;
        if (FAILED(hr)) { clsid = CLSID_WebBrowser; navigate = true; }
        CComPtr<IOleObject> object;
        if (license) {
            CComPtr<IClassFactory2> factory;
            hr = ::CoGetClassObject(clsid, CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER, nullptr,
                                    IID_IClassFactory2, reinterpret_cast<void**>(&factory));
            if (SUCCEEDED(hr)) hr = factory->CreateInstanceLic(nullptr, nullptr, IID_IOleObject, license,
                                                               reinterpret_cast<void**>(&object));
        } else {
            hr = ::CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER,
                                    IID_IOleObject, reinterpret_cast<void**>(&object));
        }
        if (FAILED(hr)) return hr;
        hr = InitializeAndAttach(object, window, stream, true, storage);
        if (SUCCEEDED(hr) && sink) hr = Connect(sink, iid);
        if (SUCCEEDED(hr) && navigate) {
            CComPtr<IWebBrowser2> browser;
            hr = object->QueryInterface(IID_IWebBrowser2, reinterpret_cast<void**>(&browser));
            if (SUCCEEDED(hr)) {
                CComVariant url(name), empty;
                hr = url.bstrVal ? browser->Navigate2(&url, &empty, &empty, &empty, &empty) : E_OUTOFMEMORY;
            }
        }
        if (FAILED(hr)) { Close(); return hr; }
        if (FAILED(CheckWindow()) || !control_) { Close(); return E_ABORT; }
        return output ? object->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(output)) : S_OK;
    }
    HRESULT Connect(IUnknown* sink, REFIID iid) noexcept {
        if (!control_) return CO_E_OBJNOTCONNECTED;
        if (eventCookie_) return CONNECT_E_ADVISELIMIT;
        CComPtr<IOleObject> object = control_;
        DWORD cookie = 0;
        HRESULT hr = AtlAdvise(object, sink, iid, &cookie);
        if (FAILED(hr)) return hr;
        if (FAILED(CheckWindow()) || control_.p != object.p) {
            AtlUnadvise(object, iid, cookie); return E_ABORT;
        }
        eventIID_ = iid; eventCookie_ = cookie; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE AttachControl(IUnknown* object, HWND window) override {
        if (creating_) return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);
        CComPtr<IUnknown> keep(static_cast<IOleClientSite*>(this));
        struct Creating { bool& value; Creating(bool& v) : value(v) { v = true; } ~Creating() { value = false; } } creating(creating_);
        return InitializeAndAttach(object, window, nullptr, false);
    }
    HRESULT InitializeAndAttach(IUnknown* object, HWND window, IStream* stream, bool initialize,
                                IStorage* storage = nullptr) noexcept {
        HRESULT hr = CheckWindow();
        if (FAILED(hr)) return hr;
        if (!object) return E_POINTER;
        if (window != window_ || (storage && (stream || !initialize))) return E_INVALIDARG;
        if (control_ || closing_) return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);
        CComPtr<IOleObject> ole;
        hr = object->QueryInterface(IID_IOleObject, reinterpret_cast<void**>(&ole));
        if (FAILED(hr)) return hr;
        control_ = ole;
        DWORD misc = 0;
        if (initialize && FAILED(ole->GetMiscStatus(DVASPECT_CONTENT, &misc))) misc = 0;
        hr = FinishStep(ole, S_OK);
        if (FAILED(hr)) return hr;
        const bool siteFirst = (misc & OLEMISC_SETCLIENTSITEFIRST) != 0;
        if (siteFirst) {
            hr = FinishStep(ole, ole->SetClientSite(static_cast<IOleClientSite*>(this)));
            if (FAILED(hr)) return hr;
        }
        if (initialize && storage) {
            CComPtr<IPersistStorage> persist;
            hr = ole->QueryInterface(IID_IPersistStorage, reinterpret_cast<void**>(&persist));
            const HRESULT alive = FinishStep(ole, hr);
            if (FAILED(alive)) return alive;
            hr = FinishStep(ole, persist ? persist->Load(storage) : E_UNEXPECTED);
            if (FAILED(hr)) return hr;
        } else if (initialize) {
            CComPtr<IPersistStreamInit> persist;
            hr = ole->QueryInterface(IID_IPersistStreamInit, reinterpret_cast<void**>(&persist));
            HRESULT alive = FinishStep(ole, hr == E_NOINTERFACE ? S_OK : hr);
            if (FAILED(alive)) return alive;
            if (SUCCEEDED(hr)) hr = persist ? (stream ? persist->Load(stream) : persist->InitNew()) : E_UNEXPECTED;
            else if (hr == E_NOINTERFACE && stream) {
                CComPtr<IPersistStream> fallback;
                hr = ole->QueryInterface(IID_IPersistStream, reinterpret_cast<void**>(&fallback));
                alive = FinishStep(ole, hr);
                if (FAILED(alive)) return alive;
                if (SUCCEEDED(hr)) hr = fallback ? fallback->Load(stream) : E_UNEXPECTED;
            } else if (hr == E_NOINTERFACE) hr = S_OK;
            hr = FinishStep(ole, hr);
            if (FAILED(hr)) return hr;
        }
        if (!siteFirst) {
            hr = FinishStep(ole, ole->SetClientSite(static_cast<IOleClientSite*>(this)));
            if (FAILED(hr)) return hr;
        }
        hr = FinishStep(ole, ole->SetHostNames(L"OpenATL", nullptr));
        if (FAILED(hr)) return hr;
        hr = FinishStep(ole, ::OleSetContainedObject(ole, TRUE));
        if (FAILED(hr)) return hr;
        RECT rect{}; ::GetClientRect(window, &rect);
        return FinishStep(ole, ole->DoVerb(OLEIVERB_SHOW, nullptr, static_cast<IOleClientSite*>(this), 0, window, &rect));
    }
    HRESULT STDMETHODCALLTYPE QueryControl(REFIID iid, void** output) override {
        if (!output) return E_POINTER;
        *output = nullptr;
        HRESULT hr = CheckWindow();
        if (FAILED(hr)) return hr;
        return control_ ? control_->QueryInterface(iid, output) : E_NOINTERFACE;
    }
    HRESULT STDMETHODCALLTYPE SetExternalDispatch(IDispatch* dispatch) override {
        if (thread_ != ::GetCurrentThreadId()) return RPC_E_WRONG_THREAD;
        if (dispatch) { const HRESULT hr = CheckWindow(); if (FAILED(hr)) return hr; }
        CComPtr<IUnknown> keep(static_cast<IOleClientSite*>(this));
        CComPtr<IDispatch> replacement = dispatch;
        if (dispatch) { const HRESULT hr = CheckWindow(); if (FAILED(hr)) return hr; }
        external_ = replacement; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetExternalUIHandler(IDocHostUIHandlerDispatch* handler) override {
        if (thread_ != ::GetCurrentThreadId()) return RPC_E_WRONG_THREAD;
        if (handler) { const HRESULT hr = CheckWindow(); if (FAILED(hr)) return hr; }
        CComPtr<IUnknown> keep(static_cast<IOleClientSite*>(this));
        CComPtr<IDocHostUIHandler> replacement;
        if (handler) {
            replacement.Attach(new (std::nothrow) AxUIHandler(handler));
            if (!replacement) return E_OUTOFMEMORY;
            const HRESULT hr = CheckWindow(); if (FAILED(hr)) return hr;
        }
        ui_ = replacement; return S_OK;
    }

    // IOleClientSite / IOleInPlaceSite / IOleInPlaceFrame
    HRESULT STDMETHODCALLTYPE SaveObject() override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetMoniker(DWORD, DWORD, IMoniker** value) override { if (value) *value = nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetContainer(IOleContainer** value) override { if (value) *value = nullptr; return E_NOINTERFACE; }
    HRESULT STDMETHODCALLTYPE ShowObject() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnShowWindow(BOOL) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE RequestNewObjectLayout() override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetWindow(HWND* output) override { if (!output) return E_POINTER; *output = window_; return CheckWindow(); }
    HRESULT STDMETHODCALLTYPE ContextSensitiveHelp(BOOL) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CanInPlaceActivate() override { return CheckWindow(); }
    HRESULT STDMETHODCALLTYPE OnInPlaceActivate() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnUIActivate() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetWindowContext(IOleInPlaceFrame** frame, IOleInPlaceUIWindow** document,
                                               LPRECT position, LPRECT clip, LPOLEINPLACEFRAMEINFO info) override {
        if (!frame || !document || !position || !clip || !info) return E_POINTER;
        *frame = nullptr; *document = nullptr;
        HRESULT hr = CheckWindow(); if (FAILED(hr)) return hr;
        ::GetClientRect(window_, position); *clip = *position;
        *info = {}; info->cb = sizeof(*info); info->hwndFrame = ::GetAncestor(window_, GA_ROOT);
        *frame = static_cast<IOleInPlaceFrame*>(this); AddRef(); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Scroll(SIZE) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE OnUIDeactivate(BOOL) override { active_.Release(); return S_OK; }
    HRESULT STDMETHODCALLTYPE OnInPlaceDeactivate() override { active_.Release(); return S_OK; }
    HRESULT STDMETHODCALLTYPE DiscardUndoState() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE DeactivateAndUndo() override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE OnPosRectChange(LPCRECT rect) override {
        if (!rect) return E_POINTER;
        if (!control_) return CO_E_OBJNOTCONNECTED;
        CComPtr<IOleInPlaceObject> object;
        HRESULT hr = control_->QueryInterface(IID_IOleInPlaceObject, reinterpret_cast<void**>(&object));
        return FAILED(hr) ? hr : object->SetObjectRects(rect, rect);
    }
    HRESULT STDMETHODCALLTYPE GetBorder(LPRECT rect) override { return rect && ::GetClientRect(window_, rect) ? S_OK : E_FAIL; }
    HRESULT STDMETHODCALLTYPE RequestBorderSpace(LPCBORDERWIDTHS) override { return INPLACE_E_NOTOOLSPACE; }
    HRESULT STDMETHODCALLTYPE SetBorderSpace(LPCBORDERWIDTHS) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SetActiveObject(IOleInPlaceActiveObject* object, LPCOLESTR) override {
        if (thread_ != ::GetCurrentThreadId()) return RPC_E_WRONG_THREAD;
        if (object) {
            HRESULT hr = CheckWindow();
            if (FAILED(hr)) return hr;
            if (closing_) return CO_E_OBJNOTCONNECTED;
        }
        active_ = object; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE InsertMenus(HMENU, LPOLEMENUGROUPWIDTHS) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetMenu(HMENU, HOLEMENU, HWND) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE RemoveMenus(HMENU) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SetStatusText(LPCOLESTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE EnableModeless(BOOL enable) override {
        CComPtr<IDocHostUIHandler> handler = ui_; return handler ? handler->EnableModeless(enable) : S_OK;
    }
    HRESULT STDMETHODCALLTYPE TranslateAccelerator(LPMSG, WORD) override { return S_FALSE; }
    // IOleControlSite
    HRESULT STDMETHODCALLTYPE OnControlInfoChanged() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE LockInPlaceActive(BOOL) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetExtendedControl(IDispatch** value) override { return QueryInterface(IID_IDispatch, reinterpret_cast<void**>(value)); }
    HRESULT STDMETHODCALLTYPE TransformCoords(POINTL* metric, POINTF* pixels, DWORD flags) override {
        if (!metric || !pixels) return E_POINTER;
        HDC dc = ::GetDC(window_); if (!dc) return E_FAIL;
        const int x = ::GetDeviceCaps(dc, LOGPIXELSX), y = ::GetDeviceCaps(dc, LOGPIXELSY);
        ::ReleaseDC(window_, dc);
        if (flags & XFORMCOORDS_HIMETRICTOCONTAINER) { pixels->x = metric->x * x / 2540.0f; pixels->y = metric->y * y / 2540.0f; }
        else if (flags & XFORMCOORDS_CONTAINERTOHIMETRIC) { metric->x = static_cast<LONG>(pixels->x * 2540.0f / x); metric->y = static_cast<LONG>(pixels->y * 2540.0f / y); }
        else return E_INVALIDARG;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE TranslateAccelerator(LPMSG msg, DWORD) override {
        CComPtr<IOleInPlaceActiveObject> active = active_;
        return active ? active->TranslateAccelerator(msg) : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE OnFocus(BOOL) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE ShowPropertyFrame() override {
        HRESULT hr = CheckWindow(); if (FAILED(hr)) return hr;
        if (creating_ || closing_ || propertyFrame_) return E_UNEXPECTED;
        if (!control_) return E_NOINTERFACE;
        CComPtr<IUnknown> keep(static_cast<IOleClientSite*>(this));
        struct Showing {
            bool& value;
            explicit Showing(bool& flag) : value(flag) { value = true; }
            ~Showing() { value = false; }
        } showing(propertyFrame_);
        const HWND owner = window_;
        const LCID locale = locale_;
        CComPtr<IOleObject> control = control_;
        const auto current = [&] { return SUCCEEDED(CheckWindow()) && control_.p == control.p; };
        CComPtr<ISpecifyPropertyPages> provider;
        hr = control->QueryInterface(IID_ISpecifyPropertyPages, reinterpret_cast<void**>(&provider));
        if (FAILED(hr)) return hr;
        if (!current()) return E_ABORT;
        if (!provider) return E_UNEXPECTED;
        struct Metadata {
            CAUUID pages{};
            LPOLESTR caption = nullptr;
            ~Metadata() { ::CoTaskMemFree(caption); ::CoTaskMemFree(pages.pElems); }
        } metadata;
        hr = provider->GetPages(&metadata.pages);
        if (hr != S_OK) return FAILED(hr) ? hr : E_UNEXPECTED;
        if (!current()) return E_ABORT;
        if (!metadata.pages.cElems) return E_NOTIMPL;
        if (!metadata.pages.pElems) return E_UNEXPECTED;
        // The short class name is optional display metadata. Its allocator is
        // still owned even if the control reports failure after filling it.
        const HRESULT captionResult = control->GetUserType(USERCLASSTYPE_SHORT, &metadata.caption);
        if (!current()) return E_ABORT;
        IUnknown* object = control.p;
        const LPCOLESTR caption = SUCCEEDED(captionResult) && metadata.caption ? metadata.caption : L"Properties";
        hr = ::OleCreatePropertyFrame(owner, 0, 0, caption,
            1, &object, metadata.pages.cElems, metadata.pages.pElems, locale, 0, nullptr);
        return SUCCEEDED(hr) && !current() ? E_ABORT : hr;
    }

    HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT* count) override { if (!count) return E_POINTER; *count = 0; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT, LCID, ITypeInfo** info) override { if (info) *info = nullptr; return DISP_E_BADINDEX; }
    HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID, LPOLESTR*, UINT, LCID, DISPID*) override { return DISP_E_UNKNOWNNAME; }
    HRESULT STDMETHODCALLTYPE Invoke(DISPID member, REFIID iid, LCID, WORD flags, DISPPARAMS*, VARIANT* result, EXCEPINFO*, UINT*) override {
        if (iid != IID_NULL) return DISP_E_UNKNOWNINTERFACE;
        if (!(flags & DISPATCH_PROPERTYGET)) return DISP_E_MEMBERNOTFOUND;
        if (!result) return E_POINTER;
        ::VariantInit(result);
        switch (member) {
        case DISPID_AMBIENT_USERMODE: result->vt = VT_BOOL; result->boolVal = userMode_; break;
        case DISPID_AMBIENT_BACKCOLOR: result->vt = VT_I4; result->lVal = back_; break;
        case DISPID_AMBIENT_FORECOLOR: result->vt = VT_I4; result->lVal = fore_; break;
        case DISPID_AMBIENT_LOCALEID: result->vt = VT_I4; result->lVal = locale_; break;
        case DISPID_AMBIENT_DISPLAYASDEFAULT: result->vt = VT_BOOL; result->boolVal = default_; break;
        case DISPID_AMBIENT_MESSAGEREFLECT: result->vt = VT_BOOL; result->boolVal = reflect_; break;
        case DISPID_AMBIENT_FONT: result->vt = VT_DISPATCH; result->pdispVal = font_; if (font_) font_.p->AddRef(); break;
        case DISPID_AMBIENT_SHOWGRABHANDLES: case DISPID_AMBIENT_SHOWHATCHING:
            result->vt = VT_BOOL; result->boolVal = VARIANT_FALSE; break;
        case DISPID_AMBIENT_SUPPORTSMNEMONICS: result->vt = VT_BOOL; result->boolVal = VARIANT_TRUE; break;
        default: return DISP_E_MEMBERNOTFOUND;
        }
        return S_OK;
    }
#define OPENATL_AMBIENT_PROPERTY(name, type, field, dispid) \
    HRESULT STDMETHODCALLTYPE put_##name(type value) override { field = value; Notify(dispid); return S_OK; } \
    HRESULT STDMETHODCALLTYPE get_##name(type* value) override { if (!value) return E_POINTER; *value = field; return S_OK; }
    // Windowless activation is deliberately declined through the OLE site QI;
    // never advertise it as available and then fake drawing/input handling.
    HRESULT STDMETHODCALLTYPE put_AllowWindowlessActivation(VARIANT_BOOL value) override { return value ? E_NOTIMPL : S_OK; }
    HRESULT STDMETHODCALLTYPE get_AllowWindowlessActivation(VARIANT_BOOL* value) override { if (!value) return E_POINTER; *value = VARIANT_FALSE; return S_OK; }
    OPENATL_AMBIENT_PROPERTY(BackColor, OLE_COLOR, back_, DISPID_AMBIENT_BACKCOLOR)
    OPENATL_AMBIENT_PROPERTY(ForeColor, OLE_COLOR, fore_, DISPID_AMBIENT_FORECOLOR)
    OPENATL_AMBIENT_PROPERTY(LocaleID, LCID, locale_, DISPID_AMBIENT_LOCALEID)
    OPENATL_AMBIENT_PROPERTY(UserMode, VARIANT_BOOL, userMode_, DISPID_AMBIENT_USERMODE)
    OPENATL_AMBIENT_PROPERTY(DisplayAsDefault, VARIANT_BOOL, default_, DISPID_AMBIENT_DISPLAYASDEFAULT)
    OPENATL_AMBIENT_PROPERTY(MessageReflect, VARIANT_BOOL, reflect_, DISPID_AMBIENT_MESSAGEREFLECT)
    OPENATL_AMBIENT_PROPERTY(DocHostFlags, DWORD, docFlags_, DISPID_UNKNOWN)
    OPENATL_AMBIENT_PROPERTY(DocHostDoubleClickFlags, DWORD, doubleClick_, DISPID_UNKNOWN)
    OPENATL_AMBIENT_PROPERTY(AllowContextMenu, VARIANT_BOOL, contextMenu_, DISPID_UNKNOWN)
    OPENATL_AMBIENT_PROPERTY(AllowShowUI, VARIANT_BOOL, showUI_, DISPID_UNKNOWN)
#undef OPENATL_AMBIENT_PROPERTY
    HRESULT STDMETHODCALLTYPE put_Font(IFontDisp* value) override { font_ = value; Notify(DISPID_AMBIENT_FONT); return S_OK; }
    HRESULT STDMETHODCALLTYPE get_Font(IFontDisp** value) override { return font_.CopyTo(value); }
    HRESULT STDMETHODCALLTYPE get_ShowGrabHandles(VARIANT_BOOL* value) override { if (!value) return E_POINTER; *value = VARIANT_FALSE; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_ShowHatching(VARIANT_BOOL* value) override { return get_ShowGrabHandles(value); }
    HRESULT STDMETHODCALLTYPE put_OptionKeyPath(BSTR value) override {
        BSTR copy = value ? ::SysAllocStringLen(value, ::SysStringLen(value)) : nullptr;
        if (value && !copy) return E_OUTOFMEMORY;
        option_.Attach(copy); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_OptionKeyPath(BSTR* value) override { return option_.CopyTo(value); }
    // IDocHostUIHandler: return S_FALSE to request the engine's standard UI.
    HRESULT STDMETHODCALLTYPE ShowContextMenu(DWORD id, POINT* point, IUnknown* command, IDispatch* reserved) override {
        CComPtr<IDocHostUIHandler> handler = ui_; return handler ? handler->ShowContextMenu(id, point, command, reserved) : (contextMenu_ ? S_FALSE : S_OK);
    }
    HRESULT STDMETHODCALLTYPE GetHostInfo(DOCHOSTUIINFO* info) override {
        if (!info) return E_POINTER;
        info->cbSize = sizeof(*info); info->dwFlags = docFlags_; info->dwDoubleClick = doubleClick_;
        info->pchHostCss = nullptr; info->pchHostNS = nullptr;
        CComPtr<IDocHostUIHandler> handler = ui_; return handler ? handler->GetHostInfo(info) : S_OK;
    }
    HRESULT STDMETHODCALLTYPE ShowUI(DWORD id, IOleInPlaceActiveObject* active, IOleCommandTarget* target,
                                     IOleInPlaceFrame* frame, IOleInPlaceUIWindow* document) override {
        CComPtr<IDocHostUIHandler> handler = ui_; return handler ? handler->ShowUI(id, active, target, frame, document) : (showUI_ ? S_FALSE : S_OK);
    }
    HRESULT STDMETHODCALLTYPE HideUI() override { CComPtr<IDocHostUIHandler> handler = ui_; return handler ? handler->HideUI() : S_OK; }
    HRESULT STDMETHODCALLTYPE UpdateUI() override { CComPtr<IDocHostUIHandler> handler = ui_; return handler ? handler->UpdateUI() : S_OK; }
    HRESULT STDMETHODCALLTYPE OnDocWindowActivate(BOOL active) override { CComPtr<IDocHostUIHandler> handler = ui_; return handler ? handler->OnDocWindowActivate(active) : S_OK; }
    HRESULT STDMETHODCALLTYPE OnFrameWindowActivate(BOOL active) override { CComPtr<IDocHostUIHandler> handler = ui_; return handler ? handler->OnFrameWindowActivate(active) : S_OK; }
    HRESULT STDMETHODCALLTYPE ResizeBorder(LPCRECT rect, IOleInPlaceUIWindow* window, BOOL frame) override { CComPtr<IDocHostUIHandler> handler = ui_; return handler ? handler->ResizeBorder(rect, window, frame) : S_OK; }
    HRESULT STDMETHODCALLTYPE TranslateAccelerator(LPMSG msg, const GUID* group, DWORD id) override { CComPtr<IDocHostUIHandler> handler = ui_; return handler ? handler->TranslateAccelerator(msg, group, id) : S_FALSE; }
    HRESULT STDMETHODCALLTYPE GetOptionKeyPath(LPOLESTR* value, DWORD flags) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        CComPtr<IDocHostUIHandler> handler = ui_; if (handler) return handler->GetOptionKeyPath(value, flags);
        if (!option_.m_str) return S_FALSE;
        const SIZE_T bytes = (::SysStringLen(option_.m_str) + 1) * sizeof(wchar_t);
        *value = static_cast<LPOLESTR>(::CoTaskMemAlloc(bytes));
        if (!*value) return E_OUTOFMEMORY;
        std::memcpy(*value, option_.m_str, bytes); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDropTarget(IDropTarget* original, IDropTarget** value) override {
        if (!value) return E_POINTER; *value = nullptr;
        CComPtr<IDocHostUIHandler> handler = ui_; return handler ? handler->GetDropTarget(original, value) : E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE GetExternal(IDispatch** value) override { CComPtr<IDocHostUIHandler> handler = ui_; return handler ? handler->GetExternal(value) : external_.CopyTo(value); }
    HRESULT STDMETHODCALLTYPE TranslateUrl(DWORD flags, OLECHAR* input, OLECHAR** output) override {
        if (!output) return E_POINTER; *output = nullptr;
        CComPtr<IDocHostUIHandler> handler = ui_; return handler ? handler->TranslateUrl(flags, input, output) : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE FilterDataObject(IDataObject* original, IDataObject** output) override {
        if (!output) return E_POINTER; *output = nullptr;
        CComPtr<IDocHostUIHandler> handler = ui_; return handler ? handler->FilterDataObject(original, output) : S_FALSE;
    }
private:
    HRESULT FinishStep(IOleObject* expected, HRESULT hr) noexcept {
        if (SUCCEEDED(hr) && SUCCEEDED(CheckWindow()) && control_.p == expected) return S_OK;
        if (control_.p == expected) Close();
        return FAILED(hr) ? hr : E_ABORT;
    }
    ~AxHost() { Close(); }
    void Notify(DISPID member) noexcept {
        CComPtr<IOleControl> control;
        if (control_ && SUCCEEDED(control_->QueryInterface(IID_IOleControl, reinterpret_cast<void**>(&control))))
            control->OnAmbientPropertyChange(member);
    }
    LONG refs_ = 1;
    HWND window_;
    DWORD thread_;
    bool creating_ = false;
    bool closing_ = false;
    bool propertyFrame_ = false;
    CComPtr<IOleObject> control_;
    CComPtr<IOleInPlaceActiveObject> active_;
    CComPtr<IDispatch> external_;
    CComPtr<IDocHostUIHandler> ui_;
    CComPtr<IFontDisp> font_;
    CComBSTR option_;
    OLE_COLOR back_ = 0x80000005, fore_ = 0x80000008;
    LCID locale_ = ::GetUserDefaultLCID();
    VARIANT_BOOL userMode_ = VARIANT_TRUE, default_ = VARIANT_FALSE, reflect_ = VARIANT_TRUE;
    VARIANT_BOOL contextMenu_ = VARIANT_TRUE, showUI_ = VARIANT_FALSE;
    DWORD docFlags_ = DOCHOSTUIFLAG_NO3DBORDER, doubleClick_ = DOCHOSTUIDBLCLK_DEFAULT;
    IID eventIID_{};
    DWORD eventCookie_ = 0;
};

inline HRESULT GetAxHost(HWND window, AxHost** output, bool create) noexcept {
    if (!output) return E_POINTER;
    *output = nullptr;
    if (!::IsWindow(window)) return HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE);
    if (::GetWindowThreadProcessId(window, nullptr) != ::GetCurrentThreadId()) return RPC_E_WRONG_THREAD;
    auto host = static_cast<AxHost*>(::GetPropW(window, AxHostProperty));
    if (!host && create) {
        host = new (std::nothrow) AxHost(window);
        if (!host) return E_OUTOFMEMORY;
        if (!::SetPropW(window, AxHostProperty, host)) { host->Release(); return HRESULT_FROM_WIN32(::GetLastError()); }
        if (!::SetWindowSubclass(window, AxHost::Subclass, reinterpret_cast<UINT_PTR>(host), reinterpret_cast<DWORD_PTR>(host))) {
            ::RemovePropW(window, AxHostProperty); host->Release(); return E_FAIL;
        }
    }
    if (!host) return E_FAIL;
    host->AddRef(); *output = host; return S_OK;
}
inline LRESULT CALLBACK AxWindowProc(HWND window, UINT msg, WPARAM w, LPARAM l) {
    if (msg == WM_CREATE) {
        CComPtr<AxHost> host;
        HRESULT hr = GetAxHost(window, &host, true);
        if (SUCCEEDED(hr)) {
            const auto params = reinterpret_cast<CREATESTRUCTW*>(l);
            if (params->lpszName && params->lpszName[0]) hr = host->CreateControl(params->lpszName, window, nullptr);
        }
        if (FAILED(hr)) { ::SetLastError(HRESULT_CODE(hr)); return -1; }
    }
    return ::DefWindowProcW(window, msg, w, l);
}
} // namespace detail

inline BOOL WINAPI AtlAxWinInit() noexcept {
    WNDCLASSEXW cls{}; cls.cbSize = sizeof(cls); cls.lpfnWndProc = detail::AxWindowProc;
    cls.hInstance = _AtlBaseModule.GetModuleInstance(); cls.hCursor = ::LoadCursor(nullptr, IDC_ARROW);
    for (const auto name : {L"AtlAxWin140", L"AtlAxWinLic140"}) {
        cls.lpszClassName = name;
        if (!::RegisterClassExW(&cls)) {
            WNDCLASSEXW existing{}; existing.cbSize = sizeof(existing);
            if (!::GetClassInfoExW(cls.hInstance, name, &existing) || existing.lpfnWndProc != cls.lpfnWndProc) return FALSE;
        }
    }
    return TRUE;
}
inline BOOL WINAPI AtlAxWinTerm() noexcept {
    BOOL regular = ::UnregisterClassW(L"AtlAxWin140", _AtlBaseModule.GetModuleInstance());
    BOOL licensed = ::UnregisterClassW(L"AtlAxWinLic140", _AtlBaseModule.GetModuleInstance());
    return regular && licensed;
}
namespace detail {
inline BOOL PrepareAxDialog(HINSTANCE instance, LPCWSTR name, AxDialogTemplate& prepared) noexcept {
    if (!name) { ::SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    const HRSRC resource = ::FindResourceW(instance, name, MAKEINTRESOURCEW(5));
    if (!resource) return FALSE;
    const DWORD size = ::SizeofResource(instance, resource);
    const HGLOBAL loaded = ::LoadResource(instance, resource);
    const auto bytes = static_cast<const BYTE*>(::LockResource(loaded));
    const HRSRC initResource = ::FindResourceW(instance, name, MAKEINTRESOURCEW(240));
    const DWORD initSize = initResource ? ::SizeofResource(instance, initResource) : 0;
    const void* init = initResource ? ::LockResource(::LoadResource(instance, initResource)) : nullptr;
    if (initResource && !init) { ::SetLastError(ERROR_INVALID_DATA); return FALSE; }
    const DWORD error = BuildAxDialogTemplate(bytes, size, prepared, init, initSize);
    if (error) { ::SetLastError(error); return FALSE; }
    return AtlAxWinInit();
}
inline DWORD AxDialogError(HRESULT hr) noexcept {
    if (HRESULT_FACILITY(hr) == FACILITY_WIN32) return HRESULT_CODE(hr);
    if (hr == E_OUTOFMEMORY) return ERROR_OUTOFMEMORY;
    if (hr == E_POINTER || hr == E_INVALIDARG) return ERROR_INVALID_PARAMETER;
    return ERROR_GEN_FAILURE;
}
struct AxDialogCreation {
    AxDialogTemplate& prepared;
    DLGPROC procedure;
    LPARAM parameter;
    bool modal;
    DWORD error = ERROR_SUCCESS;
    HWND window = nullptr;
    bool subclassed = false, initializing = false, initialized = false, retired = false;
    static AxDialogCreation*& Current() noexcept {
        static thread_local AxDialogCreation* current = nullptr;
        return current;
    }
    UINT_PTR Key() const noexcept { return reinterpret_cast<UINT_PTR>(this); }
    void Detach() noexcept {
        if (subclassed && window && ::IsWindow(window)) {
            if (!::RemoveWindowSubclass(window, Subclass, Key())) std::terminate();
        }
        subclassed = false;
    }
    struct Scope {
        AxDialogCreation& creation;
        AxDialogCreation* previous;
        explicit Scope(AxDialogCreation& value) noexcept : creation(value), previous(Current()) { Current() = &value; }
        ~Scope() {
            const DWORD error = ::GetLastError();
            creation.Detach(); Current() = previous; ::SetLastError(error);
        }
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;
    };
    void Abort(HRESULT hr) noexcept {
        if (!error) error = AxDialogError(hr);
        if (!retired && window && ::IsWindow(window)) {
            if (modal) ::EndDialog(window, -1); else ::DestroyWindow(window);
        }
    }
    HRESULT Bind(HWND target) noexcept {
        if (window || retired) return E_UNEXPECTED;
        window = target;
        ::SetLastError(ERROR_SUCCESS);
        if (!::SetWindowSubclass(target, Subclass, Key(), reinterpret_cast<DWORD_PTR>(this))) {
            const DWORD failure = ::GetLastError();
            return failure ? HRESULT_FROM_WIN32(failure) : E_FAIL;
        }
        subclassed = true;
        ::SetLastError(ERROR_SUCCESS);
        const LONG_PTR previous = ::SetWindowLongPtrW(target, DWLP_DLGPROC, reinterpret_cast<LONG_PTR>(procedure));
        return !previous && ::GetLastError() ? HRESULT_FROM_WIN32(::GetLastError()) : S_OK;
    }
    HRESULT Initialize(HWND dialog) noexcept {
        for (const auto& record : prepared.initialization) {
            if (retired) return E_ABORT;
            const HWND child = ::GetDlgItem(dialog, record.id);
            if (!child) return HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE);
            if (record.IsString()) {
                // Use the native ANSI message entry point for legacy DLGINIT
                // bytes; Windows performs the control's code-page translation.
                const UINT message = record.kind == AxDialogInit::Kind::ListString ? LB_ADDSTRING : CB_ADDSTRING;
                const LRESULT added = ::SendMessageA(child, message, 0, reinterpret_cast<LPARAM>(record.stream.data()));
                if (added == LB_ERRSPACE) return E_OUTOFMEMORY;
                if (added == LB_ERR) return E_FAIL;
                if (!::IsWindow(dialog) || !::IsWindow(child) || ::GetParent(child) != dialog) return E_ABORT;
                continue;
            }
            CComPtr<IStream> stream;
            CComPtr<IStorage> storage;
            HRESULT hr = S_OK;
            if (record.kind == AxDialogInit::Kind::Stream) {
                hr = ::CreateStreamOnHGlobal(nullptr, TRUE, &stream);
                if (FAILED(hr)) return hr;
                ULONG written = 0;
                if (!record.stream.empty()) {
                    hr = stream->Write(record.stream.data(), static_cast<ULONG>(record.stream.size()), &written);
                    if (FAILED(hr)) return hr;
                    if (written != record.stream.size()) return STG_E_WRITEFAULT;
                }
                LARGE_INTEGER start{};
                hr = stream->Seek(start, STREAM_SEEK_SET, nullptr);
                if (FAILED(hr)) return hr;
            } else if (record.kind == AxDialogInit::Kind::Storage) {
                // Own a private byte array; the native structured-storage
                // implementation validates and opens the compound image.
                CComPtr<ILockBytes> bytes;
                hr = ::CreateILockBytesOnHGlobal(nullptr, TRUE, &bytes);
                if (FAILED(hr)) return hr;
                ULARGE_INTEGER size{}; size.QuadPart = record.stream.size();
                hr = bytes->SetSize(size);
                if (FAILED(hr)) return hr;
                if (!record.stream.empty()) {
                    ULARGE_INTEGER offset{}; ULONG written = 0;
                    hr = bytes->WriteAt(offset, record.stream.data(), static_cast<ULONG>(record.stream.size()), &written);
                    if (FAILED(hr)) return hr;
                    if (written != record.stream.size()) return STG_E_WRITEFAULT;
                }
                hr = ::StgOpenStorageOnILockBytes(bytes, nullptr, STGM_READ | STGM_SHARE_EXCLUSIVE, nullptr, 0, &storage);
                if (FAILED(hr)) return hr;
            }
            CComBSTR license;
            if (!record.license.empty()) {
                license.Attach(::SysAllocStringLen(record.license.data(), static_cast<UINT>(record.license.size())));
                if (!license.m_str) return E_OUTOFMEMORY;
            }
            CComPtr<AxHost> host;
            hr = GetAxHost(child, &host, false);
            if (SUCCEEDED(hr)) hr = host->CreateNamedControl(record.name.c_str(), child, stream, nullptr, IID_NULL, nullptr, license, storage);
            if (FAILED(hr)) return hr;
            if (!::IsWindow(dialog) || !::IsWindow(child) || ::GetParent(child) != dialog) return E_ABORT;
        }
        return S_OK;
    }
    bool BeginInitialization() noexcept {
        if (initializing || initialized || retired || error) return false;
        initializing = true;
        const HRESULT hr = Initialize(window);
        initializing = false;
        if (FAILED(hr) || retired) { Abort(FAILED(hr) ? hr : E_ABORT); return false; }
        initialized = true;
        Detach(); // No stack reference remains on a returned modeless window.
        return true;
    }
    static LRESULT CALLBACK Subclass(HWND target, UINT message, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) noexcept {
        auto& creation = *reinterpret_cast<AxDialogCreation*>(data);
        if (message == WM_NCDESTROY) {
            creation.Detach(); creation.retired = true; creation.window = nullptr;
        } else if (message == WM_INITDIALOG && !creation.BeginInitialization()) return FALSE;
        // This reaches the actual current dialog procedure, including a thunk
        // installed by an early StartDialogProc callback.
        return ::DefSubclassProc(target, message, w, l);
    }
    static INT_PTR CALLBACK DialogProc(HWND target, UINT message, WPARAM w, LPARAM l) noexcept {
        auto creation = Current();
        if (!creation || creation->retired || (creation->window && creation->window != target)) return FALSE;
        const HRESULT hr = creation->Bind(target);
        if (FAILED(hr)) { creation->Abort(hr); return FALSE; }
        // The first callback can itself be WM_INITDIALOG: installing a window
        // subclass now cannot intercept the message already being dispatched.
        if (message == WM_INITDIALOG && !creation->BeginInitialization()) return FALSE;
        if (creation->retired || !::IsWindow(target)) return FALSE;
        const auto application = reinterpret_cast<DLGPROC>(::GetWindowLongPtrW(target, DWLP_DLGPROC));
        // DWLP_DLGPROC can be an opaque handle; invoke it through User32.
        return application && application != DialogProc ?
            ::CallWindowProcW(reinterpret_cast<WNDPROC>(application), target, message, w, l) : FALSE;
    }
};
}
inline HWND WINAPI AtlAxCreateDialog(HINSTANCE instance, LPCWSTR name, HWND parent,
    DLGPROC procedure, LPARAM parameter) noexcept {
    detail::AxDialogTemplate prepared;
    if (!detail::PrepareAxDialog(instance, name, prepared)) return nullptr;
    if (!prepared.initialization.empty()) {
        detail::AxDialogCreation creation{prepared, procedure, parameter, false};
        detail::AxDialogCreation::Scope scope(creation);
        ::SetLastError(ERROR_SUCCESS);
        const HWND window = ::CreateDialogIndirectParamW(instance, prepared.Get(), parent,
            detail::AxDialogCreation::DialogProc, parameter);
        const DWORD error = ::GetLastError();
        if (creation.error) { ::SetLastError(creation.error); return nullptr; }
        if (!creation.initialized || creation.retired) { ::SetLastError(creation.retired || !error ? ERROR_CANCELLED : error); return nullptr; }
        return window;
    }
    if (prepared.converted) return ::CreateDialogIndirectParamW(instance, prepared.Get(), parent, procedure, parameter);
    return ::CreateDialogParamW(instance, name, parent, procedure, parameter);
}
inline int WINAPI AtlAxDialogBox(HINSTANCE instance, LPCWSTR name, HWND parent,
    DLGPROC procedure, LPARAM parameter) noexcept {
    detail::AxDialogTemplate prepared;
    if (!detail::PrepareAxDialog(instance, name, prepared)) return -1;
    if (!prepared.initialization.empty()) {
        detail::AxDialogCreation creation{prepared, procedure, parameter, true};
        detail::AxDialogCreation::Scope scope(creation);
        ::SetLastError(ERROR_SUCCESS);
        const int result = static_cast<int>(::DialogBoxIndirectParamW(instance, prepared.Get(), parent,
            detail::AxDialogCreation::DialogProc, parameter));
        const DWORD error = ::GetLastError();
        if (creation.error) { ::SetLastError(creation.error); return -1; }
        if (!creation.initialized || creation.retired) { ::SetLastError(creation.retired || !error ? ERROR_CANCELLED : error); return -1; }
        return result;
    }
    if (prepared.converted) return static_cast<int>(::DialogBoxIndirectParamW(instance, prepared.Get(), parent, procedure, parameter));
    return static_cast<int>(::DialogBoxParamW(instance, name, parent, procedure, parameter));
}
inline HRESULT WINAPI AtlAxGetHost(HWND window, IUnknown** output) noexcept {
    if (!output) return E_POINTER; *output = nullptr;
    CComPtr<detail::AxHost> host;
    HRESULT hr = detail::GetAxHost(window, &host, false);
    return FAILED(hr) ? hr : host->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(output));
}
inline HRESULT WINAPI AtlAxGetControl(HWND window, IUnknown** output) noexcept {
    if (!output) return E_POINTER; *output = nullptr;
    CComPtr<detail::AxHost> host;
    HRESULT hr = detail::GetAxHost(window, &host, false);
    return FAILED(hr) ? hr : host->QueryControl(IID_IUnknown, reinterpret_cast<void**>(output));
}
inline HRESULT CWindow::GetDlgControl(int id, REFIID iid, void** output) noexcept {
    if (!output) return E_POINTER; *output = nullptr;
    if (!::IsWindow(m_hWnd)) return E_FAIL;
    const HWND child = ::GetDlgItem(m_hWnd, id);
    if (!child) return E_FAIL;
    CComPtr<IUnknown> control;
    const HRESULT hr = AtlAxGetControl(child, &control.p);
    return FAILED(hr) ? hr : control->QueryInterface(iid, output);
}
inline HRESULT WINAPI AtlAxAttachControl(IUnknown* control, HWND window, IUnknown** output) noexcept {
    if (output) *output = nullptr;
    CComPtr<detail::AxHost> host;
    HRESULT hr = detail::GetAxHost(window, &host, true);
    if (SUCCEEDED(hr)) hr = host->AttachControl(control, window);
    return FAILED(hr) || !output ? hr : host->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(output));
}
inline HRESULT WINAPI AtlAxCreateControlEx(LPCOLESTR name, HWND window, IStream* stream,
                                           IUnknown** hostOutput, IUnknown** controlOutput,
                                           REFIID iid = IID_NULL, IUnknown* sink = nullptr) noexcept {
    if (hostOutput) *hostOutput = nullptr;
    if (controlOutput) *controlOutput = nullptr;
    CComPtr<detail::AxHost> host;
    HRESULT hr = detail::GetAxHost(window, &host, true);
    if (SUCCEEDED(hr)) hr = host->CreateControlEx(name, window, stream, controlOutput, iid, sink);
    return FAILED(hr) || !hostOutput ? hr : host->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(hostOutput));
}
inline HRESULT WINAPI AtlAxCreateControl(LPCOLESTR name, HWND window, IStream* stream, IUnknown** host) noexcept {
    return AtlAxCreateControlEx(name, window, stream, host, nullptr);
}
inline HRESULT WINAPI AtlAxCreateControlLicEx(LPCOLESTR name, HWND window, IStream* stream,
                                              IUnknown** hostOutput, IUnknown** controlOutput,
                                              REFIID iid = IID_NULL, IUnknown* sink = nullptr,
                                              BSTR license = nullptr) noexcept {
    if (hostOutput) *hostOutput = nullptr;
    if (controlOutput) *controlOutput = nullptr;
    if (!license) return AtlAxCreateControlEx(name, window, stream, hostOutput, controlOutput, iid, sink);
    CComPtr<detail::AxHost> host;
    HRESULT hr = detail::GetAxHost(window, &host, true);
    if (SUCCEEDED(hr)) hr = host->CreateNamedControl(name, window, stream, controlOutput, iid, sink, license);
    return FAILED(hr) || !hostOutput ? hr : host->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(hostOutput));
}
inline HRESULT WINAPI AtlAxCreateControlLic(LPCOLESTR name, HWND window, IStream* stream,
                                            IUnknown** host, BSTR license = nullptr) noexcept {
    return AtlAxCreateControlLicEx(name, window, stream, host, nullptr, IID_NULL, nullptr, license);
}

template<class TBase = CWindow> class CAxWindowT : public TBase {
public:
    CAxWindowT(HWND window = nullptr) noexcept : TBase(window) {}
    CAxWindowT& operator=(HWND window) noexcept { this->m_hWnd = window; return *this; }
    static LPCTSTR GetWndClassName() noexcept { return _T("AtlAxWin140"); }
    HWND Create(HWND parent, _U_RECT rect = nullptr, LPCTSTR name = nullptr, DWORD style = 0,
                DWORD extended = 0, _U_MENUorID menu = 0U, LPVOID param = nullptr) noexcept {
        if (!AtlAxWinInit()) return nullptr;
        return TBase::Create(GetWndClassName(), parent, rect, name, style, extended, menu, param);
    }
    HRESULT QueryHost(REFIID iid, void** output) noexcept {
        if (!output) return E_POINTER; *output = nullptr;
        CComPtr<IUnknown> host;
        HRESULT hr = AtlAxGetHost(this->m_hWnd, &host);
        return FAILED(hr) ? hr : host->QueryInterface(iid, output);
    }
    template<class Q> HRESULT QueryHost(Q** output) noexcept { return QueryHost(__uuidof(Q), reinterpret_cast<void**>(output)); }
    HRESULT QueryControl(REFIID iid, void** output) noexcept {
        if (!output) return E_POINTER; *output = nullptr;
        CComPtr<IUnknown> control;
        HRESULT hr = AtlAxGetControl(this->m_hWnd, &control);
        return FAILED(hr) ? hr : control->QueryInterface(iid, output);
    }
    template<class Q> HRESULT QueryControl(Q** output) noexcept { return QueryControl(__uuidof(Q), reinterpret_cast<void**>(output)); }
    HRESULT CreateControl(LPCOLESTR name, IStream* stream = nullptr, IUnknown** host = nullptr) noexcept {
        return AtlAxCreateControl(name, this->m_hWnd, stream, host);
    }
    HRESULT CreateControlEx(LPCOLESTR name, IStream* stream, IUnknown** host, IUnknown** control,
                            REFIID iid = IID_NULL, IUnknown* sink = nullptr) noexcept {
        return AtlAxCreateControlEx(name, this->m_hWnd, stream, host, control, iid, sink);
    }
    HRESULT AttachControl(IUnknown* control, IUnknown** host = nullptr) noexcept { return AtlAxAttachControl(control, this->m_hWnd, host); }
    HRESULT SetExternalDispatch(IDispatch* dispatch) noexcept {
        CComPtr<IAxWinHostWindow> host;
        const HRESULT hr = QueryHost(&host);
        return FAILED(hr) ? hr : host->SetExternalDispatch(dispatch);
    }
    HRESULT SetExternalUIHandler(IDocHostUIHandlerDispatch* handler) noexcept {
        CComPtr<IAxWinHostWindow> host;
        const HRESULT hr = QueryHost(&host);
        return FAILED(hr) ? hr : host->SetExternalUIHandler(handler);
    }
};
using CAxWindow = CAxWindowT<CWindow>;

template<class T, class TBase>
class CAxDialogImpl : public CDialogImplBaseT<TBase> {
    using Base = CDialogImplBaseT<TBase>;
    struct Creation { DWORD error = ERROR_SUCCESS; bool initialized = false, destroyed = false; };
    Creation* creation_ = nullptr;
    bool BeginCreation(Creation& state, bool modal) noexcept {
        if (creation_ || this->m_hWnd) { ::SetLastError(ERROR_ALREADY_EXISTS); return false; }
        if (!this->m_thunk.Init(nullptr, nullptr)) return false;
        m_bModal = modal;
        creation_ = &state;
        _AtlWinModule.AddCreateWndData(&this->m_thunk.cd, static_cast<Base*>(this));
        return true;
    }
public:
    int GetIDD() noexcept { return static_cast<T*>(this)->IDD; }
    HRESULT AdviseSinkMap(bool advise) noexcept {
        return AtlAdviseSinkMap(static_cast<T*>(this), advise);
    }
    HWND Create(HWND parent, LPARAM parameter = 0) noexcept {
        Creation state;
        if (!BeginCreation(state, false)) return nullptr;
        const auto pending = &this->m_thunk.cd;
        const HWND window = AtlAxCreateDialog(_AtlBaseModule.GetResourceInstance(),
            MAKEINTRESOURCEW(static_cast<T*>(this)->GetIDD()), parent, T::StartDialogProc, parameter);
        const DWORD error = ::GetLastError();
        (void)_AtlWinModule.RemoveCreateWndData(pending);
        // WM_INITDIALOG/OnFinalMessage may already have deleted the owner.
        if (!state.destroyed) creation_ = nullptr;
        if (state.error || state.destroyed || !state.initialized || !window) {
            ::SetLastError(state.error ? state.error : state.destroyed && state.initialized ? ERROR_CANCELLED : error ? error : ERROR_GEN_FAILURE);
            return nullptr;
        }
        return window;
    }
    HWND Create(HWND parent, RECT&, LPARAM parameter = 0) noexcept { return Create(parent, parameter); }
    INT_PTR DoModal(HWND parent = ::GetActiveWindow(), LPARAM parameter = 0) noexcept {
        Creation state;
        if (!BeginCreation(state, true)) return -1;
        const auto pending = &this->m_thunk.cd;
        const INT_PTR result = AtlAxDialogBox(_AtlBaseModule.GetResourceInstance(),
            MAKEINTRESOURCEW(static_cast<T*>(this)->GetIDD()), parent, T::StartDialogProc, parameter);
        const DWORD error = ::GetLastError();
        (void)_AtlWinModule.RemoveCreateWndData(pending);
        if (!state.destroyed) creation_ = nullptr;
        if (state.error || !state.initialized || result == -1) {
            ::SetLastError(state.error ? state.error : error ? error : ERROR_GEN_FAILURE); return -1;
        }
        return result;
    }
    BOOL DestroyWindow() noexcept {
        if (m_bModal || !::IsWindow(this->m_hWnd)) { ::SetLastError(ERROR_INVALID_WINDOW_HANDLE); return FALSE; }
        const HRESULT hr = static_cast<T*>(this)->AdviseSinkMap(false);
        if (hr != S_OK) { ::SetLastError(detail::AxDialogError(FAILED(hr) ? hr : E_UNEXPECTED)); return FALSE; }
        return ::DestroyWindow(this->m_hWnd);
    }
    BOOL EndDialog(int result) noexcept {
        if (!m_bModal || !::IsWindow(this->m_hWnd)) { ::SetLastError(ERROR_INVALID_WINDOW_HANDLE); return FALSE; }
        const HRESULT hr = static_cast<T*>(this)->AdviseSinkMap(false);
        if (hr != S_OK) { ::SetLastError(detail::AxDialogError(FAILED(hr) ? hr : E_UNEXPECTED)); return FALSE; }
        return ::EndDialog(this->m_hWnd, result);
    }
    DLGPROC GetDialogProc() override { return DialogProc; }
    static INT_PTR CALLBACK DialogProc(HWND context, UINT message, WPARAM w, LPARAM l) {
        auto self = static_cast<CAxDialogImpl*>(reinterpret_cast<Base*>(context));
        if (message == WM_INITDIALOG) {
            if (self->creation_) self->creation_->initialized = true;
            const HRESULT hr = static_cast<T*>(self)->AdviseSinkMap(true);
            if (hr != S_OK) {
                const DWORD error = detail::AxDialogError(FAILED(hr) ? hr : E_UNEXPECTED);
                if (self->creation_) self->creation_->error = error;
                if (static_cast<T*>(self)->AdviseSinkMap(false) != S_OK) std::terminate();
                const HWND window = self->m_hWnd;
                if (self->m_bModal) ::EndDialog(window, -1); else ::DestroyWindow(window);
                ::SetLastError(error); return FALSE;
            }
        } else if (message == WM_DESTROY) {
            if (static_cast<T*>(self)->AdviseSinkMap(false) != S_OK) std::terminate();
        } else if (message == WM_NCDESTROY && self->creation_) {
            self->creation_->destroyed = true;
            self->creation_ = nullptr;
        }
        // The shared dispatcher owns deferred final notification. Do not
        // access self after it returns: OnFinalMessage can delete the owner.
        return Base::DialogProc(context, message, w, l);
    }
protected:
    BOOL m_bModal = FALSE;
    void OnDialogCreateFailure(HWND window, DWORD error) noexcept override {
        if (creation_) creation_->error = error;
        if (m_bModal) ::EndDialog(window, -1); else ::DestroyWindow(window);
        ::SetLastError(error);
    }
};

template<class TBase = CWindow> class CAxWindow2T : public CAxWindowT<TBase> {
public:
    CAxWindow2T(HWND window = nullptr) noexcept : CAxWindowT<TBase>(window) {}
    CAxWindow2T& operator=(HWND window) noexcept { this->m_hWnd = window; return *this; }
    static LPCTSTR GetWndClassName() noexcept { return _T("AtlAxWinLic140"); }
    HWND Create(HWND parent, _U_RECT rect = nullptr, LPCTSTR name = nullptr, DWORD style = 0,
                DWORD extended = 0, _U_MENUorID menu = 0U, LPVOID param = nullptr) noexcept {
        if (!AtlAxWinInit()) return nullptr;
        return TBase::Create(GetWndClassName(), parent, rect, name, style, extended, menu, param);
    }
    HRESULT CreateControlLic(LPCOLESTR name, IStream* stream = nullptr, IUnknown** host = nullptr,
                              BSTR license = nullptr) noexcept {
        return AtlAxCreateControlLic(name, this->m_hWnd, stream, host, license);
    }
    HRESULT CreateControlLicEx(LPCOLESTR name, IStream* stream, IUnknown** host, IUnknown** control,
                                REFIID iid = IID_NULL, IUnknown* sink = nullptr, BSTR license = nullptr) noexcept {
        return AtlAxCreateControlLicEx(name, this->m_hWnd, stream, host, control, iid, sink, license);
    }
};
} // namespace ATL
#endif
