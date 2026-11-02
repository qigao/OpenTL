// OpenWTL repository-owned COM property-page authoring subset.
#ifndef OPENATL_PROPERTY_PAGE_H
#define OPENATL_PROPERTY_PAGE_H
#pragma once
#include "../atlbase.h"
#include "../atlcomcli.h"
#include "../atlwin.h"
#include <ocidl.h>
#include <new>
#include <limits>
#include <memory>
#include <cstring>

namespace ATL {
namespace detail {
inline HRESULT PropertyPageException() noexcept {
    try { throw; }
    catch (const CAtlException& error) { return FAILED(error.m_hr) ? error.m_hr : E_UNEXPECTED; }
    catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
    catch (...) { return E_UNEXPECTED; }
}
}

// T supplies its normal native dialog Create/DestroyWindow/m_hWnd operations.
// Before activation, set m_size explicitly in pixels. This initial subset does
// not infer font metrics from dialog-template bytes. Containers must Deactivate
// before releasing their final page reference, as required by the page protocol.
// IPropertyPage2 is the single interface lineage; applications choose the COM
// map entries they expose. EditProperty defaults to E_NOTIMPL.
template<class T>
class ATL_NO_VTABLE IPropertyPageImpl : public IPropertyPage2 {
public:
    BOOL m_bDirty = FALSE;
    UINT m_dwTitle = 0, m_dwDocString = 0, m_dwHelpFile = 0;
    DWORD m_dwHelpContext = 0;
    ULONG m_nObjects = 0;
    IUnknown** m_ppUnk = nullptr;
    IPropertyPageSite* m_pPageSite = nullptr;
    SIZE m_size{};

    IPropertyPageImpl() noexcept = default;
    IPropertyPageImpl(const IPropertyPageImpl&) = delete;
    IPropertyPageImpl& operator=(const IPropertyPageImpl&) = delete;
    ~IPropertyPageImpl() {
        ATLASSERT(!window_);
        ReleaseObjects();
        auto site = m_pPageSite; m_pPageSite = nullptr;
        if (site) site->Release();
    }

    HRESULT STDMETHODCALLTYPE SetPageSite(IPropertyPageSite* site) override {
        if (changing_ || (site && m_pPageSite) || (!site && !m_pPageSite)) return E_UNEXPECTED;
        try {
            CComPtr<IPropertyPage> self(this);
            Change transition(changing_);
            if (site) site->AddRef();
            auto old = m_pPageSite; m_pPageSite = site;
            if (old) old->Release();
            return S_OK;
        } catch (...) { return detail::PropertyPageException(); }
    }
    HRESULT STDMETHODCALLTYPE SetObjects(ULONG count, IUnknown** objects) override {
        if (changing_) return E_UNEXPECTED;
        if (count && !objects) return E_POINTER;
        if (count > (std::numeric_limits<SIZE_T>::max)() / sizeof(IUnknown*)) return E_OUTOFMEMORY;
        for (ULONG i=0;i<count;++i) if (!objects[i]) return E_POINTER;
        try {
            CComPtr<IPropertyPage> self(this);
            Change transition(changing_);
            std::unique_ptr<IUnknown*[]> staged(count ? new (std::nothrow) IUnknown*[count] : nullptr);
            if (count && !staged) return E_OUTOFMEMORY;
            ULONG held=0;
            try {
                for (;held<count;++held) { staged[held]=objects[held]; staged[held]->AddRef(); }
            } catch (...) {
                for (ULONG i=0;i<held;++i) staged[i]->Release();
                throw;
            }
            auto old = m_ppUnk; const ULONG oldCount = m_nObjects;
            m_ppUnk=staged.release(); m_nObjects=count;
            for (ULONG i=0;i<oldCount;++i) old[i]->Release();
            delete[] old;
            return S_OK;
        } catch (...) { return detail::PropertyPageException(); }
    }
    HRESULT STDMETHODCALLTYPE Activate(HWND parent, LPCRECT rect, BOOL /*modal*/) override {
        if (!rect) return E_POINTER;
        if (changing_ || window_ || !m_pPageSite || !m_nObjects) return E_UNEXPECTED;
        int width=0,height=0;
        if (!::IsWindow(parent) || !RectSize(*rect,width,height)) return E_INVALIDARG;
        try {
            CComPtr<IPropertyPage> self(this);
            Change transition(changing_);
            auto object = static_cast<T*>(this);
            const HWND window = object->Create(parent);
            if (!window) {
                const DWORD error=::GetLastError();
                return error ? HRESULT_FROM_WIN32(error) : E_FAIL;
            }
            if (!::IsWindow(window) || object->m_hWnd != window) return E_UNEXPECTED;
            window_=window;
            // A page is embedded in the native property-frame client area.
            ::SetLastError(ERROR_SUCCESS);
            const LONG_PTR oldStyle = ::SetWindowLongPtrW(window,GWL_STYLE,
                (::GetWindowLongPtrW(window,GWL_STYLE) & ~(WS_POPUP|WS_CAPTION)) | WS_CHILD);
            DWORD setupError = oldStyle ? ERROR_SUCCESS : ::GetLastError();
            if (!setupError) {
                ::SetLastError(ERROR_SUCCESS);
                const HWND oldParent = ::SetParent(window,parent);
                if (!oldParent) setupError = ::GetLastError();
            }
            if (setupError) {
                if (::IsWindow(window) && object->m_hWnd == window) object->DestroyWindow();
                window_=nullptr;
                return HRESULT_FROM_WIN32(setupError);
            }
            if (!::SetWindowPos(window,nullptr,rect->left,rect->top,
                width,height,
                SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED)) {
                const DWORD error=::GetLastError();
                if (::IsWindow(window) && object->m_hWnd == window) object->DestroyWindow();
                window_=nullptr;
                return error ? HRESULT_FROM_WIN32(error) : E_FAIL;
            }
            if (!::IsWindow(window) || object->m_hWnd != window) {
                window_=nullptr;
                return E_UNEXPECTED;
            }
            RECT client{};
            if (::GetClientRect(window,&client)) m_size={client.right,client.bottom};
            return S_OK;
        } catch (...) { return detail::PropertyPageException(); }
    }
    HRESULT STDMETHODCALLTYPE Deactivate() override {
        if (changing_ || !window_) return E_UNEXPECTED;
        try {
            CComPtr<IPropertyPage> self(this);
            Change transition(changing_);
            if (!OwnsWindow()) { window_=nullptr; return E_UNEXPECTED; }
            if (!static_cast<T*>(this)->DestroyWindow()) return E_FAIL;
            window_=nullptr;
            return S_OK;
        } catch (...) { return detail::PropertyPageException(); }
    }
    HRESULT STDMETHODCALLTYPE GetPageInfo(PROPPAGEINFO* output) override {
        if (!output) return E_POINTER;
        *output={};
        if (m_size.cx <= 0 || m_size.cy <= 0) return E_UNEXPECTED;
        PROPPAGEINFO staged{}; staged.cb=sizeof(staged); staged.size=m_size;
        staged.dwHelpContext=m_dwHelpContext;
        HRESULT hr=LoadText(m_dwTitle,&staged.pszTitle,true);
        if (SUCCEEDED(hr)) hr=LoadText(m_dwDocString,&staged.pszDocString,false);
        if (SUCCEEDED(hr)) hr=LoadText(m_dwHelpFile,&staged.pszHelpFile,false);
        if (FAILED(hr)) {
            ::CoTaskMemFree(staged.pszTitle); ::CoTaskMemFree(staged.pszDocString);
            ::CoTaskMemFree(staged.pszHelpFile); return hr;
        }
        *output=staged; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Show(UINT command) override {
        if (!OwnsWindow()) return E_UNEXPECTED;
        if (command!=SW_SHOW && command!=SW_SHOWNORMAL && command!=SW_HIDE) return E_INVALIDARG;
        CComPtr<IPropertyPage> self(this);
        ::ShowWindow(window_,command); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Move(LPCRECT rect) override {
        if (!rect) return E_POINTER;
        if (!OwnsWindow()) return E_UNEXPECTED;
        int width=0,height=0;
        if (!RectSize(*rect,width,height)) return E_INVALIDARG;
        CComPtr<IPropertyPage> self(this);
        return ::MoveWindow(window_,rect->left,rect->top,
            width,height,TRUE) ? S_OK : E_FAIL;
    }
    HRESULT STDMETHODCALLTYPE IsPageDirty() override { return m_bDirty ? S_OK : S_FALSE; }
    void SetDirty(BOOL dirty) {
        CComPtr<IPropertyPage> self(this);
        m_bDirty=dirty ? TRUE : FALSE;
        CComPtr<IPropertyPageSite> site(m_pPageSite);
        if (site && dirty) site->OnStatusChange(PROPPAGESTATUS_DIRTY);
    }
    // ATL's default does not apply application properties. T overrides Apply
    // to validate/update its objects and call SetDirty(FALSE) on success.
    HRESULT STDMETHODCALLTYPE Apply() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Help(LPCOLESTR) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE TranslateAccelerator(MSG* message) override {
        if (!message) return E_POINTER;
        if (!OwnsWindow()) return E_UNEXPECTED;
        CComPtr<IPropertyPage> self(this);
        return ::IsDialogMessageW(window_,message) ? S_OK : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE EditProperty(DISPID) override { return E_NOTIMPL; }
private:
    struct Change {
        bool& state;
        explicit Change(bool& value) : state(value) { state=true; }
        ~Change() { state=false; }
    };
    bool changing_=false;
    HWND window_=nullptr;
    bool OwnsWindow() const noexcept {
        return window_ && ::IsWindow(window_) && static_cast<const T*>(this)->m_hWnd == window_;
    }
    static bool RectSize(const RECT& rect,int& width,int& height) noexcept {
        const LONGLONG w=static_cast<LONGLONG>(rect.right)-rect.left;
        const LONGLONG h=static_cast<LONGLONG>(rect.bottom)-rect.top;
        if (w<0 || h<0 || w>(std::numeric_limits<int>::max)() || h>(std::numeric_limits<int>::max)()) return false;
        width=static_cast<int>(w); height=static_cast<int>(h); return true;
    }
    void ReleaseObjects() noexcept {
        auto objects=m_ppUnk; const ULONG count=m_nObjects;
        m_ppUnk=nullptr; m_nObjects=0;
        for (ULONG i=0;i<count;++i) objects[i]->Release();
        delete[] objects;
    }
    static HRESULT LoadText(UINT id, LPOLESTR* output, bool required) noexcept {
        *output=nullptr;
        LPCWSTR resource=L"";
        int count=0;
        if (id) {
            count=::LoadStringW(_AtlBaseModule.GetResourceInstance(),id,
                reinterpret_cast<LPWSTR>(&resource),0);
            if (count<=0) return HRESULT_FROM_WIN32(ERROR_RESOURCE_NAME_NOT_FOUND);
        } else if (!required) return S_OK;
        const SIZE_T bytes=(static_cast<SIZE_T>(count)+1)*sizeof(WCHAR);
        auto text=static_cast<LPOLESTR>(::CoTaskMemAlloc(bytes));
        if (!text) return E_OUTOFMEMORY;
        if (count) std::memcpy(text,resource,static_cast<SIZE_T>(count)*sizeof(WCHAR));
        text[count]=0; *output=text; return S_OK;
    }
};

template<class T>
class ATL_NO_VTABLE IPropertyPage2Impl : public IPropertyPageImpl<T> {};
}
#endif
