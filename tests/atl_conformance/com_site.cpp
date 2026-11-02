// Native COM site helper acceptance. The original WTL PrintDlgEx inherits
// IObjectWithSiteImpl<T>; this tests its COM ownership without invoking UI.
#define _ATL_NO_AUTOMATIC_NAMESPACE
template<class T> struct CComPtr { enum { application_owned = 1 }; };
#include <atlcomcli.h>
static_assert(CComPtr<IUnknown>::application_owned == 1,
              "Standalone atlcomcli.h must honor the namespace opt-out");
using QualifiedComPointer = ATL::CComPtr<IUnknown>;
#include <atlcom.h>
#include <ocidl.h>
#include <cstdio>
static_assert(CComPtr<IUnknown>::application_owned == 1,
              "Later ATL headers must preserve the namespace opt-out");

class RecordingSite final : public IUnknown {
public:
    ULONG references = 1;
    ULONG added = 0;
    ULONG released = 0;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** output) noexcept override {
        if (!output) return E_POINTER;
        *output = nullptr;
        if (!::IsEqualIID(iid, IID_IUnknown)) return E_NOINTERFACE;
        *output = static_cast<IUnknown*>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() noexcept override {
        ++added;
        return ++references;
    }
    ULONG STDMETHODCALLTYPE Release() noexcept override {
        ++released;
        if (!references) std::terminate();
        return --references;
    }
};

class SiteHolder final : public ATL::IObjectWithSiteImpl<SiteHolder> {
public:
    ULONG references = 1;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** output) noexcept override {
        if (!output) return E_POINTER;
        *output = nullptr;
        if (!::IsEqualIID(iid, IID_IUnknown) && !::IsEqualIID(iid, IID_IObjectWithSite))
            return E_NOINTERFACE;
        *output = static_cast<IObjectWithSite*>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() noexcept override { return ++references; }
    ULONG STDMETHODCALLTYPE Release() noexcept override {
        if (!references) std::terminate();
        return --references;
    }
};

int main() {
    APTTYPE apartment = APTTYPE_CURRENT;
    APTTYPEQUALIFIER qualifier = APTTYPEQUALIFIER_NONE;
    if (::CoGetApartmentType(&apartment, &qualifier) != CO_E_NOTINITIALIZED)
        return 1;
    RecordingSite site;
    SiteHolder holder;
    if (holder.GetSite(IID_IUnknown, nullptr) != E_POINTER) return 2;
    void* result = reinterpret_cast<void*>(1);
    if (holder.GetSite(IID_IUnknown, &result) != E_FAIL || result != nullptr)
        return 3;
    if (holder.SetSite(&site) != S_OK || site.references != 2) return 4;
    result = nullptr;
    if (holder.GetSite(IID_IUnknown, &result) != S_OK ||
        result != static_cast<IUnknown*>(&site) || site.references != 3)
        return 5;
    static_cast<IUnknown*>(result)->Release();
    result = reinterpret_cast<void*>(1);
    if (holder.GetSite(IID_IClassFactory, &result) != E_NOINTERFACE || result != nullptr)
        return 6;
    if (holder.SetSite(&site) != S_OK || site.references != 2) return 7;
    IObjectWithSite* siteInterface = nullptr;
    if (holder.QueryInterface(IID_IObjectWithSite,
            reinterpret_cast<void**>(&siteInterface)) != S_OK ||
        siteInterface != static_cast<IObjectWithSite*>(&holder) ||
        holder.references != 2)
        return 8;
    siteInterface->Release();
    if (holder.SetSite(nullptr) != S_OK || site.references != 1 ||
        site.added != 2 || site.released != 2)
        return 9;
    result = reinterpret_cast<void*>(1);
    if (holder.GetSite(IID_IUnknown, &result) != E_FAIL || result != nullptr)
        return 10;
    if (holder.references != 1 ||
        ::CoGetApartmentType(&apartment, &qualifier) != CO_E_NOTINITIALIZED)
        return 11;
    std::puts("OPENATL_SITE_OK set=1 get=1 errors=1 self=1 release=1 no_com=1");
    return 0;
}
