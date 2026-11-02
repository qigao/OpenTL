#include <atlhost.h>
#include <atlctl.h>
#include <openatl/property_page_metrics.h>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <thread>
#include <algorithm>
bool RunHostLifecycleTests();
bool RunBrowserUIHandlerTest(ATL::CAxWindow&, IHTMLDocument2*);
#if defined(OPENATL_ORIGINAL_COMMAP_CLASSES)
bool RunMicrosoftCommapClassTests();
bool RunMicrosoftSafeArrayTests();
#endif

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "ACTIVEX_FAIL line=%d: %s hr=%08lx\n", __LINE__, #x, static_cast<unsigned long>(::GetLastError())); return false; } } while (0)
namespace {
inline constexpr IID EventIID = {0x56b852d1,0xe47c,0x48a6,{0x94,0x1b,0xde,0xae,0x8c,0x6d,0x08,0xfb}};
inline constexpr CLSID MapPageA = {0xaa4d4e73,0x7789,0x4c87,{0x8f,0x31,0x0a,0x9b,0x29,0x9d,0x22,0x61}};
inline constexpr CLSID MapPageB = {0xe7f10fc4,0x2d23,0x4d9b,{0x93,0x61,0xfe,0x88,0x44,0x52,0x0f,0x91}};
CLSID MapPageAlias = MapPageA; // Equal value at a distinct writable address.
template<class T> class PageMapObject : public ATL::CComObjectRootEx<ATL::CComMultiThreadModel>,
                                      public ATL::ISpecifyPropertyPagesImpl<T> {
public:
    BEGIN_COM_MAP(PageMapObject<T>)
        COM_INTERFACE_ENTRY(ISpecifyPropertyPages)
    END_COM_MAP()
};
struct MapPrefix { virtual ~MapPrefix() = default; ULONG padding[3]{}; };
class MappedPages : public MapPrefix, public PageMapObject<MappedPages> {
public:
    BEGIN_PROP_MAP(MappedPages)
        PROP_PAGE(MapPageB)
        PROP_ENTRY_TYPE("Width", 11, MapPageA, VT_UI4)
        PROP_ENTRY_TYPE_EX("Caption", 12, MapPageAlias, IID_IHTMLDocument2, VT_BSTR)
        PROP_ENTRY("Legacy", 13, MapPageB)
        PROP_ENTRY_EX("LegacyEx", 14, MapPageA, IID_IDispatch)
        PROP_ENTRY_TYPE("Unpaged", 15, CLSID_NULL, VT_BOOL)
        PROP_PAGE(CLSID_NULL)
    END_PROP_MAP()
};
struct DataMap {
    DWORD prefix = 99;
    SIZEL extent{123,456};
    BEGIN_PROP_MAP(DataMap)
        PROP_DATA_ENTRY("_cx", extent.cx, VT_I4)
        PROP_DATA_ENTRY("_cy", extent.cy, VT_I4)
    END_PROP_MAP()
};
class DataOnlyPages : public PageMapObject<DataOnlyPages> {
public:
    static ATL::_ATL_PROP_ENTRY* GetPropertyMap() noexcept { return DataMap::GetPropertyMap(); }
};
class EmptyPages : public PageMapObject<EmptyPages> {
public:
    BEGIN_PROP_MAP(EmptyPages)
    END_PROP_MAP()
};
class OnlyPages : public PageMapObject<OnlyPages> {
public:
    BEGIN_PROP_MAP(OnlyPages)
        PROP_PAGE(MapPageA)
        PROP_PAGE(MapPageB)
    END_PROP_MAP()
};
class FailingPages : public PageMapObject<FailingPages> {
public:
    inline static int failure = 0;
    static ATL::_ATL_PROP_ENTRY* GetPropertyMap() {
        if (failure == 1) throw ATL::CAtlException(E_ACCESSDENIED);
        if (failure == 2) throw std::bad_alloc();
        if (failure == 3) throw std::runtime_error("property-map lookup failure");
        return failure == 4 ? MappedPages::GetPropertyMap() : nullptr;
    }
};
template<class T> bool CreatePageMap(ATL::CComPtr<ISpecifyPropertyPages>& output) {
    ATL::CComObject<T>* object = nullptr;
    CHECK(ATL::CComObject<T>::CreateInstance(&object) == S_OK && object);
    object->AddRef();
    const HRESULT hr = object->QueryInterface(IID_ISpecifyPropertyPages, reinterpret_cast<void**>(&output));
    object->Release(); CHECK(hr == S_OK && output); return true;
}
bool propertyMaps() {
    CHECK(&MapPageAlias != &MapPageA);
    ATL::CComPtr<ISpecifyPropertyPages> mapped, empty, dataOnly, failing, only;
    CHECK(CreatePageMap<MappedPages>(mapped) && CreatePageMap<EmptyPages>(empty) &&
        CreatePageMap<DataOnlyPages>(dataOnly) && CreatePageMap<FailingPages>(failing) && CreatePageMap<OnlyPages>(only));
    CHECK(mapped->GetPages(nullptr) == E_POINTER);
    CAUUID first{}, second{};
    CHECK(mapped->GetPages(&first) == S_OK && first.cElems == 2 && first.pElems &&
        first.pElems[0] == MapPageB && first.pElems[1] == MapPageA);
    CHECK(mapped->GetPages(&second) == S_OK && second.cElems == 2 && second.pElems != first.pElems);
    first.pElems[0] = CLSID_NULL;
    CHECK(second.pElems[0] == MapPageB && *MappedPages::GetPropertyMap()[0].pclsidPropPage == MapPageB);
    ::CoTaskMemFree(first.pElems); ::CoTaskMemFree(second.pElems);
    CHECK(only->GetPages(&first) == S_OK && first.cElems == 2 && first.pElems[0] == MapPageA && first.pElems[1] == MapPageB);
    ::CoTaskMemFree(first.pElems);
    for (auto object : {empty.p, dataOnly.p}) {
        CAUUID pages{99, reinterpret_cast<CLSID*>(static_cast<UINT_PTR>(1))};
        CHECK(object->GetPages(&pages) == S_OK && !pages.cElems && !pages.pElems);
    }
    DataMap record;
    const auto entries = DataMap::GetPropertyMap();
    LONG width = 0, height = 0;
    CHECK(entries[0].dwSizeData == sizeof(width) && entries[1].dwSizeData == sizeof(height));
    std::memcpy(&width, reinterpret_cast<const BYTE*>(&record) + entries[0].dwOffsetData, sizeof(width));
    std::memcpy(&height, reinterpret_cast<const BYTE*>(&record) + entries[1].dwOffsetData, sizeof(height));
    CHECK(width == record.extent.cx && height == record.extent.cy && entries[0].vt == VT_I4 &&
        !::wcscmp(entries[0].szDesc, L"_cx") && !entries[0].pclsidPropPage && !entries[0].piidDispatch);
    const auto map = MappedPages::GetPropertyMap();
    CHECK(map[1].dispid == 11 && map[1].vt == VT_UI4 && *map[1].piidDispatch == IID_IDispatch);
    CHECK(map[2].dispid == 12 && map[2].vt == VT_BSTR && *map[2].piidDispatch == IID_IHTMLDocument2);
    CHECK(map[3].vt == VT_EMPTY && map[4].vt == VT_EMPTY);
    for (int mode = 0; mode < 4; ++mode) {
        FailingPages::failure = mode;
        CAUUID pages{99, reinterpret_cast<CLSID*>(static_cast<UINT_PTR>(1))};
        const HRESULT expected = mode == 1 ? E_ACCESSDENIED : mode == 2 ? E_OUTOFMEMORY : E_UNEXPECTED;
        CHECK(failing->GetPages(&pages) == expected && !pages.cElems && !pages.pElems);
    }
    FailingPages::failure = 4;
    CHECK(failing->GetPages(&first) == S_OK && first.cElems == 2 && first.pElems[0] == MapPageB);
    ::CoTaskMemFree(first.pElems);
    bool correct[2]{true, true};
    const auto collect = [&](int index) {
        for (int iteration = 0; iteration < 16; ++iteration) {
            CAUUID pages{};
            correct[index] = mapped->GetPages(&pages) == S_OK && pages.cElems == 2 && pages.pElems &&
                pages.pElems[0] == MapPageB && pages.pElems[1] == MapPageA && correct[index];
            ::CoTaskMemFree(pages.pElems);
        }
    };
    std::thread a(collect, 0), b(collect, 1); a.join(); b.join();
    CHECK(correct[0] && correct[1]);
    return true;
}
inline constexpr IID BrowseAlternateIID = {0xca8dc074,0x99d7,0x43d0,{0xa6,0x72,0x08,0xf5,0x3f,0x96,0x22,0xc1}};
inline constexpr IID BrowseMissingIID = {0x6fcfdc61,0x5160,0x4db2,{0xad,0x26,0xf9,0x39,0x84,0xce,0x30,0x18}};
struct BrowsingMap {
    LONG data = 0;
    BEGIN_PROP_MAP(BrowsingMap)
        PROP_PAGE(MapPageB)
        PROP_DATA_ENTRY("Internal", data, VT_I4)
        PROP_ENTRY_TYPE("Default", DISPID_VALUE, MapPageA, VT_I4)
        PROP_ENTRY_TYPE("Number", 11, MapPageA, VT_I4)
        PROP_ENTRY_TYPE_EX("Text", 12, MapPageB, BrowseAlternateIID, VT_BSTR)
        PROP_ENTRY_EX("Missing interface", 13, MapPageA, BrowseMissingIID)
        PROP_ENTRY_TYPE("Unpaged", 14, CLSID_NULL, VT_BOOL)
        PROP_ENTRY_TYPE_EX("First", 15, MapPageA, BrowseAlternateIID, VT_BSTR)
        PROP_ENTRY("Second", 15, MapPageB)
        PROP_ENTRY("Negative", -77, MapPageB)
    END_PROP_MAP()
};
struct BrowsingState {
    int calls[2]{}, failure = 0, destroyed = 0;
    LONG number = 314;
    bool valid = true, reenter = false;
    IPerPropertyBrowsing* browser = nullptr; // Borrowed for the synchronous call.
    IUnknown* probe = nullptr;
};
template<class T, int Which> class BrowsingDispatch : public IDispatch {
public:
    HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT* count) override { if (!count) return E_POINTER; *count = 0; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT, LCID, ITypeInfo**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID, LPOLESTR*, UINT, LCID, DISPID*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Invoke(DISPID member, REFIID iid, LCID locale, WORD flags,
        DISPPARAMS* args, VARIANT* result, EXCEPINFO*, UINT*) override {
        auto& state = static_cast<T*>(this)->state;
        ++state->calls[Which];
        state->valid = state->valid && iid == IID_NULL && locale == LOCALE_USER_DEFAULT &&
            flags == DISPATCH_PROPERTYGET && args && !args->cArgs && !args->cNamedArgs &&
            !args->rgvarg && !args->rgdispidNamedArgs && result && result->vt == VT_EMPTY;
        if (!result) return E_POINTER;
        if (state->reenter) {
            CLSID page{};
            state->valid = state->valid && state->browser->MapPropertyToPage(12, &page) == S_OK && page == MapPageB;
        }
        if (state->failure) {
            result->vt = VT_UNKNOWN; result->punkVal = state->probe; state->probe->AddRef();
            if (state->failure == 1) return E_ACCESSDENIED;
            if (state->failure == 2) return S_FALSE;
            if (state->failure == 3) throw ATL::CAtlException(E_ABORT);
            if (state->failure == 4) throw std::bad_alloc();
            if (state->failure == 5) throw std::runtime_error("browsing callback failure");
            return S_OK; // Native conversion of this non-Automation object fails.
        }
        if (member == 12 || member == 15) {
            const WCHAR text[] = {L'A', 0, L'B'};
            result->vt = VT_BSTR; result->bstrVal = ::SysAllocStringLen(text, 3);
            return result->bstrVal ? S_OK : E_OUTOFMEMORY;
        }
        if (member == 14) { result->vt = VT_BOOL; result->boolVal = VARIANT_TRUE; return S_OK; }
        if (member == -77) { result->vt = VT_I4 | VT_BYREF; result->plVal = &state->number; return S_OK; }
        if (member == DISPID_VALUE || member == 11 || member == 99) {
            result->vt = VT_I4; result->lVal = state->number; return S_OK;
        }
        return DISP_E_MEMBERNOTFOUND;
    }
};
class BrowsingObject : public MapPrefix, public ATL::CComObjectRootEx<ATL::CComMultiThreadModel>,
                       public BrowsingDispatch<BrowsingObject, 0>, public BrowsingDispatch<BrowsingObject, 1>,
                       public ATL::IPerPropertyBrowsingImpl<BrowsingObject> {
public:
    using DefaultDispatch = BrowsingDispatch<BrowsingObject, 0>;
    using AlternateDispatch = BrowsingDispatch<BrowsingObject, 1>;
    inline static int mapFailure = 0;
    BrowsingState* state = nullptr;
    ~BrowsingObject() { if (state) ++state->destroyed; }
    static ATL::_ATL_PROP_ENTRY* GetPropertyMap() {
        if (mapFailure == 1) throw ATL::CAtlException(E_ACCESSDENIED);
        if (mapFailure == 2) throw std::bad_alloc();
        if (mapFailure == 3) throw std::runtime_error("browsing map failure");
        return mapFailure == 4 ? nullptr : BrowsingMap::GetPropertyMap();
    }
    BEGIN_COM_MAP(BrowsingObject)
        COM_INTERFACE_ENTRY(IPerPropertyBrowsing)
        COM_INTERFACE_ENTRY2(IDispatch, DefaultDispatch)
        COM_INTERFACE_ENTRY2_IID(BrowseAlternateIID, IDispatch, AlternateDispatch)
    END_COM_MAP()
};
bool propertyBrowsing() {
    BrowsingState state;
    ATL::CComObject<BrowsingObject>* object = nullptr;
    CHECK(ATL::CComObject<BrowsingObject>::CreateInstance(&object) == S_OK && object);
    object->state = &state; object->AddRef();
    ATL::CComPtr<IPerPropertyBrowsing> browser;
    CHECK(object->QueryInterface(IID_IPerPropertyBrowsing, reinterpret_cast<void**>(&browser)) == S_OK);
    object->Release(); state.browser = browser;
    ATL::CComPtr<ISpecifyPropertyPages> probe;
    CHECK(CreatePageMap<EmptyPages>(probe)); state.probe = probe;
    ATL::CComPtr<IUnknown> identity, defaultIdentity, alternateIdentity;
    ATL::CComPtr<IDispatch> primary, alternate;
    CHECK(browser->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&identity)) == S_OK);
    CHECK(browser->QueryInterface(IID_IDispatch, reinterpret_cast<void**>(&primary)) == S_OK &&
        browser->QueryInterface(BrowseAlternateIID, reinterpret_cast<void**>(&alternate)) == S_OK && primary.p != alternate.p);
    CHECK(primary->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&defaultIdentity)) == S_OK &&
        alternate->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&alternateIdentity)) == S_OK &&
        identity.p == defaultIdentity.p && identity.p == alternateIdentity.p);
    CLSID page{};
    CHECK(browser->MapPropertyToPage(11, nullptr) == E_POINTER);
    for (DISPID member : {DISPID(DISPID_VALUE), DISPID(11), DISPID(13), DISPID(15)})
        CHECK(browser->MapPropertyToPage(member, &page) == S_OK && page == MapPageA);
    CHECK(browser->MapPropertyToPage(12, &page) == S_OK && page == MapPageB);
    CHECK(browser->MapPropertyToPage(-77, &page) == S_OK && page == MapPageB);
    CHECK(browser->MapPropertyToPage(14, &page) == S_OK && page == CLSID_NULL);
    page = MapPageA;
    CHECK(browser->MapPropertyToPage(99, &page) == E_INVALIDARG && page == CLSID_NULL);
    CHECK(browser->GetDisplayString(11, nullptr) == E_POINTER);
    BSTR text = nullptr, second = nullptr;
    for (DISPID member : {DISPID(DISPID_VALUE), DISPID(11), DISPID(-77), DISPID(99)}) {
        CHECK(browser->GetDisplayString(member, &text) == S_OK && text && !::wcscmp(text, L"314"));
        ::SysFreeString(text);
    }
    for (DISPID member : {DISPID(12), DISPID(15)}) {
        CHECK(browser->GetDisplayString(member, &text) == S_OK && text && ::SysStringLen(text) == 3 &&
            text[0] == L'A' && !text[1] && text[2] == L'B');
        CHECK(browser->GetDisplayString(member, &second) == S_OK && second != text);
        text[0] = L'Z'; CHECK(second[0] == L'A');
        ::SysFreeString(text); ::SysFreeString(second);
    }
    CHECK(state.calls[0] == 4 && state.calls[1] == 4);
    ATL::CComVariant boolean, expected;
    boolean.vt = VT_BOOL; boolean.boolVal = VARIANT_TRUE;
    CHECK(::VariantChangeType(&expected, &boolean, 0, VT_BSTR) == S_OK);
    CHECK(browser->GetDisplayString(14, &text) == S_OK && !::wcscmp(text, expected.bstrVal));
    ::SysFreeString(text);
    for (DISPID member : {DISPID(13), DISPID(1000)}) {
        text = reinterpret_cast<BSTR>(static_cast<UINT_PTR>(1));
        CHECK(browser->GetDisplayString(member, &text) == (member == 13 ? E_NOINTERFACE : DISP_E_MEMBERNOTFOUND) && !text);
    }
    const HRESULT failures[] = {E_ACCESSDENIED, E_UNEXPECTED, E_ABORT, E_OUTOFMEMORY, E_UNEXPECTED};
    for (int mode = 1; mode <= 6; ++mode) {
        state.failure = mode; text = reinterpret_cast<BSTR>(static_cast<UINT_PTR>(1));
        const HRESULT hr = browser->GetDisplayString(11, &text);
        CHECK((mode <= 5 ? hr == failures[mode - 1] : FAILED(hr)) && !text);
        CHECK(probe.p->AddRef() == 2 && probe.p->Release() == 1); // Result VARIANT releases even on failure/throw.
    }
    state.failure = 0; state.reenter = true; state.number = 271;
    CHECK(browser->GetDisplayString(11, &text) == S_OK && !::wcscmp(text, L"271"));
    ::SysFreeString(text); CHECK(state.valid);
    for (int mode = 1; mode <= 4; ++mode) {
        BrowsingObject::mapFailure = mode; page = MapPageA; text = reinterpret_cast<BSTR>(static_cast<UINT_PTR>(1));
        const HRESULT hr = mode == 1 ? E_ACCESSDENIED : mode == 2 ? E_OUTOFMEMORY : E_UNEXPECTED;
        CHECK(browser->MapPropertyToPage(11, &page) == hr && page == CLSID_NULL);
        CHECK(browser->GetDisplayString(11, &text) == hr && !text);
    }
    BrowsingObject::mapFailure = 0;
    CALPOLESTR strings{99, reinterpret_cast<LPOLESTR*>(static_cast<UINT_PTR>(1))};
    CADWORD cookies{99, reinterpret_cast<DWORD*>(static_cast<UINT_PTR>(1))};
    CHECK(browser->GetPredefinedStrings(11, &strings, &cookies) == S_OK && !strings.cElems && !strings.pElems && !cookies.cElems && !cookies.pElems);
    cookies.cElems = 99;
    CHECK(browser->GetPredefinedStrings(11, nullptr, &cookies) == E_POINTER && !cookies.cElems && !cookies.pElems);
    strings.cElems = 99;
    CHECK(browser->GetPredefinedStrings(11, &strings, nullptr) == E_POINTER && !strings.cElems && !strings.pElems);
    CHECK(browser->GetPredefinedStrings(11, nullptr, nullptr) == E_POINTER);
    VARIANT value; std::memset(&value, 0xCD, sizeof(value));
    CHECK(browser->GetPredefinedValue(11, 1, &value) == E_NOTIMPL && value.vt == VT_EMPTY);
    CHECK(browser->GetPredefinedValue(11, 1, nullptr) == E_POINTER);
    primary.Release(); alternate.Release(); identity.Release(); defaultIdentity.Release(); alternateIdentity.Release(); browser.Release();
    CHECK(state.destroyed == 1);
    return true;
}
struct PersistenceState {
    LONG number = 314;
    ATL::CComBSTR text;
    ATL::CComPtr<IUnknown> reference;
    BOOL flag = TRUE;
    BOOL dirtyAtDestruction = TRUE;
    int destroyed = 0, getFailure = 0, putFailure = 0, puts[2]{};
    bool valid = true;
};
template<class T, int Which> class PersistenceDispatch : public IDispatch {
public:
    HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT* count) override { if (!count) return E_POINTER; *count = 0; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT, LCID, ITypeInfo**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID, LPOLESTR*, UINT, LCID, DISPID*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Invoke(DISPID member, REFIID iid, LCID locale, WORD flags,
        DISPPARAMS* args, VARIANT* result, EXCEPINFO*, UINT*) override {
        auto object = static_cast<T*>(this); auto& state = *object->state;
        state.valid = state.valid && iid == IID_NULL && locale == LOCALE_USER_DEFAULT && args &&
            (Which == (member == 12 ? 1 : 0));
        if (!args) return E_POINTER;
        if (flags == DISPATCH_PROPERTYGET) {
            state.valid = state.valid && !args->cArgs && !args->cNamedArgs && !args->rgvarg && !args->rgdispidNamedArgs;
            if (!result) return E_POINTER;
            if (state.getFailure) {
                result->vt = VT_UNKNOWN; result->punkVal = state.reference; result->punkVal->AddRef();
                if (state.getFailure == 2) throw ATL::CAtlException(E_ABORT);
                if (state.getFailure == 3) throw std::bad_alloc();
                return E_ACCESSDENIED;
            }
            if (member == 11) { result->vt = VT_I4 | VT_BYREF; result->plVal = &state.number; }
            else if (member == 12) { result->vt = VT_BSTR; result->bstrVal = state.text.Copy(); if (!result->bstrVal) return E_OUTOFMEMORY; }
            else if (member == 13) { result->vt = VT_BOOL; result->boolVal = state.flag ? VARIANT_TRUE : VARIANT_FALSE; }
            else if (member == 14) { result->vt = VT_UNKNOWN; result->punkVal = state.reference; if (result->punkVal) result->punkVal->AddRef(); }
            else return DISP_E_MEMBERNOTFOUND;
            return S_OK;
        }
        state.valid = state.valid && args->cArgs == 1 && args->cNamedArgs == 1 && args->rgvarg &&
            args->rgdispidNamedArgs && args->rgdispidNamedArgs[0] == DISPID_PROPERTYPUT &&
            flags == (member == 14 ? DISPATCH_PROPERTYPUTREF : DISPATCH_PROPERTYPUT) && !result;
        if (state.putFailure) return E_ACCESSDENIED;
        if (!args->rgvarg) return E_POINTER;
        const auto& value = args->rgvarg[0]; ++state.puts[Which];
        if (member == 11 && value.vt == VT_I4) state.number = value.lVal;
        else if (member == 12 && value.vt == VT_BSTR) {
            BSTR copy = ::SysAllocStringLen(value.bstrVal, ::SysStringLen(value.bstrVal));
            if (!copy) return E_OUTOFMEMORY; state.text.Attach(copy);
        } else if (member == 13 && value.vt == VT_BOOL) state.flag = value.boolVal != VARIANT_FALSE;
        else if (member == 14 && value.vt == VT_UNKNOWN) state.reference = value.punkVal;
        else return DISP_E_TYPEMISMATCH;
        object->m_bRequiresSave = TRUE;
        return S_OK;
    }
};
class PersistentObject : public MapPrefix, public ATL::CComObjectRootEx<ATL::CComMultiThreadModel>,
                         public PersistenceDispatch<PersistentObject, 0>, public PersistenceDispatch<PersistentObject, 1>,
                         public ATL::IPersistPropertyBagImpl<PersistentObject> {
public:
    using Primary = PersistenceDispatch<PersistentObject, 0>;
    using Secondary = PersistenceDispatch<PersistentObject, 1>;
    using _PropMapClass = PersistentObject;
    inline static int mapFailure = 0;
    PersistenceState* state = nullptr;
    ULONG guard1 = 0x12345678;
    SIZEL extent{123,456};
    ULONG guard2 = 0x98765432;
    BOOL m_bRequiresSave = TRUE;
    ~PersistentObject() { if (state) { ++state->destroyed; state->dirtyAtDestruction = m_bRequiresSave; } }
    static REFCLSID GetObjectCLSID() noexcept { return MapPageA; }
    static ATL::_ATL_PROP_ENTRY* GetPropertyMap() {
        static ATL::_ATL_PROP_ENTRY entries[] = {
            PROP_PAGE(MapPageA)
            PROP_DATA_ENTRY("_cx", extent.cx, VT_I4)
            PROP_DATA_ENTRY("_cy", extent.cy, VT_I4)
            PROP_ENTRY_TYPE("Number", 11, CLSID_NULL, VT_I4)
            PROP_ENTRY_TYPE_EX("Text", 12, MapPageA, BrowseAlternateIID, VT_BSTR)
            PROP_ENTRY("Flag", 13, CLSID_NULL)
            PROP_ENTRY_TYPE("Reference", 14, CLSID_NULL, VT_UNKNOWN)
            {}
        };
        static ATL::_ATL_PROP_ENTRY empty[] = { PROP_PAGE(MapPageA) {} };
        static ATL::_ATL_PROP_ENTRY bad[] = { {L"Bad",0,nullptr,nullptr,0,4,VT_I4}, {} };
        if (mapFailure == 1) return nullptr;
        if (mapFailure == 2) throw std::bad_alloc();
        if (mapFailure == 3) { bad[0] = {L"Bad",0,nullptr,nullptr,MAXDWORD,4,VT_I4}; return bad; }
        if (mapFailure == 4) { bad[0] = {L"Bad",0,nullptr,nullptr,0,4,VT_BSTR}; return bad; }
        if (mapFailure == 5) { bad[0] = {L"Bad",0,nullptr,nullptr,0,1,VT_I4}; return bad; }
        if (mapFailure == 7) { bad[0] = {L"Number",11,&MapPageA,&BrowseMissingIID,0,0,VT_I4}; return bad; }
        return mapFailure == 6 ? empty : entries;
    }
    BEGIN_COM_MAP(PersistentObject)
        COM_INTERFACE_ENTRY(IPersistPropertyBag)
        COM_INTERFACE_ENTRY(IPersist)
        COM_INTERFACE_ENTRY2(IDispatch, Primary)
        COM_INTERFACE_ENTRY2_IID(BrowseAlternateIID, IDispatch, Secondary)
    END_COM_MAP()
};
class CallerPropertyBag : public ATL::CComObjectRootEx<ATL::CComMultiThreadModel>,
                          public IPropertyBag, public IErrorLog {
public:
    LPCOLESTR names[6] = {L"_cx",L"_cy",L"Number",L"Text",L"Flag",L"Reference"};
    ATL::CComVariant values[6];
    int reads = 0, writes = 0, errors = 0, failRead = 0, failWrite = 0, throwRead = 0;
    DWORD missing = 0;
    HRESULT lastError = S_OK;
    LPCOLESTR lastName = nullptr;
    bool valid = true, reenter = false, positiveRead = false, positiveWrite = false;
    IUnknown* probe = nullptr;
    IPersistPropertyBag* owner = nullptr;
    IPersistPropertyBag* dropOwner = nullptr;
    BEGIN_COM_MAP(CallerPropertyBag)
        COM_INTERFACE_ENTRY(IPropertyBag)
        COM_INTERFACE_ENTRY(IErrorLog)
    END_COM_MAP()
    int Find(LPCOLESTR name) const { for (int i = 0; i < 6; ++i) if (!::wcscmp(name, names[i])) return i; return -1; }
    void Reenter() {
        if (reenter) { CLSID id{}; valid = owner && owner->GetClassID(&id) == S_OK && id == MapPageA && valid; }
        if (dropOwner) { auto release = dropOwner; dropOwner = nullptr; release->Release(); }
    }
    HRESULT STDMETHODCALLTYPE Read(LPCOLESTR name, VARIANT* value, IErrorLog* log) override {
        ++reads; Reenter(); const int index = Find(name);
        if (index < 0 || (missing & (1UL << index))) return E_INVALIDARG;
        const VARTYPE types[] = {VT_I4,VT_I4,VT_I4,VT_BSTR,VT_EMPTY,VT_UNKNOWN};
        valid = value && value->vt == types[index] && (!log || log == static_cast<IErrorLog*>(this)) && valid;
        if (!value) return E_POINTER;
        ::VariantClear(value);
        if (throwRead) {
            value->vt = VT_UNKNOWN; value->punkVal = probe; probe->AddRef();
            if (throwRead == 1) throw ATL::CAtlException(E_ABORT);
            if (throwRead == 2) throw std::bad_alloc();
            throw std::runtime_error("property bag read failure");
        }
        if (failRead == reads) {
            EXCEPINFO error{}; error.scode = STG_E_READFAULT; if (log) log->AddError(name, &error);
            return STG_E_READFAULT;
        }
        const HRESULT hr = ::VariantCopy(value, &values[index]); // Deliberately leaves coercion to the consumer.
        return SUCCEEDED(hr) && positiveRead ? S_FALSE : hr;
    }
    HRESULT STDMETHODCALLTYPE Write(LPCOLESTR name, VARIANT* value) override {
        ++writes; Reenter(); const int index = Find(name);
        if (index < 0) return E_INVALIDARG;
        if (failWrite == writes) return STG_E_WRITEFAULT;
        const HRESULT hr = values[index].Copy(value);
        return SUCCEEDED(hr) && positiveWrite ? S_FALSE : hr;
    }
    HRESULT STDMETHODCALLTYPE AddError(LPCOLESTR name, EXCEPINFO* info) override {
        ++errors; lastName = name; lastError = info->scode; return E_ACCESSDENIED;
    }
};
bool persistentProperties() {
    PersistenceState sourceState, targetState;
    const WCHAR text[] = {L'A',0,L'B'};
    sourceState.text.Attach(::SysAllocStringLen(text, 3)); targetState.text = L"default";
    ATL::CComPtr<ISpecifyPropertyPages> probe; CHECK(CreatePageMap<EmptyPages>(probe));
    sourceState.reference = probe; targetState.reference = probe;
    ATL::CComObject<PersistentObject>* source = nullptr; ATL::CComObject<PersistentObject>* target = nullptr;
    CHECK(ATL::CComObject<PersistentObject>::CreateInstance(&source) == S_OK && ATL::CComObject<PersistentObject>::CreateInstance(&target) == S_OK);
    source->state = &sourceState; target->state = &targetState; source->AddRef(); target->AddRef();
    ATL::CComPtr<IPersistPropertyBag> save, load; save.Attach(source); load.Attach(target);
    ATL::CComPtr<IPersist> persist;
    CHECK(save->QueryInterface(IID_IPersist, reinterpret_cast<void**>(&persist)) == S_OK);
    CLSID id{}; CHECK(persist->GetClassID(&id) == S_OK && id == MapPageA && save->GetClassID(nullptr) == E_POINTER);
    CHECK(save->InitNew() == S_OK && source->m_bRequiresSave);
    CHECK(save->Save(nullptr, TRUE, TRUE) == E_POINTER && load->Load(nullptr, nullptr) == E_POINTER);
    ATL::CComObject<CallerPropertyBag>* medium = nullptr;
    CHECK(ATL::CComObject<CallerPropertyBag>::CreateInstance(&medium) == S_OK); medium->AddRef();
    ATL::CComPtr<IPropertyBag> bag; bag.Attach(medium);
    auto log = static_cast<IErrorLog*>(medium);
    medium->owner = save; medium->probe = probe; medium->reenter = true;
    CHECK(save->Save(bag, FALSE, FALSE) == S_OK && source->m_bRequiresSave && medium->writes == 6);
    CHECK(medium->values[0].vt == VT_I4 && medium->values[0].lVal == 123 && medium->values[1].lVal == 456);
    CHECK(medium->values[2].vt == VT_I4 && medium->values[2].lVal == 314 && medium->values[3].vt == VT_BSTR &&
        ::SysStringLen(medium->values[3].bstrVal) == 3 && medium->values[3].bstrVal[2] == L'B');
    medium->values[2] = L"271"; target->extent = {1,2}; targetState.number = 9; targetState.flag = FALSE;
    CHECK(load->Load(bag, log) == S_OK && !target->m_bRequiresSave && targetState.number == 271 && targetState.flag &&
        targetState.puts[0] == 3 && targetState.puts[1] == 1 && targetState.valid && medium->valid);
    CHECK(target->extent.cx == 123 && target->extent.cy == 456 && target->guard1 == 0x12345678 && target->guard2 == 0x98765432);
    CHECK(::SysStringLen(targetState.text) == 3 && targetState.text.m_str[0] == L'A' && !targetState.text.m_str[1] && targetState.text.m_str[2] == L'B');
    sourceState.text.m_str[0] = L'Z'; CHECK(targetState.text.m_str[0] == L'A' && medium->values[3].bstrVal[0] == L'A');
    CHECK(save->Save(bag, TRUE, FALSE) == S_OK && !source->m_bRequiresSave && medium->writes == 12);
    source->m_bRequiresSave = TRUE; medium->failWrite = medium->writes + 2;
    CHECK(save->Save(bag, TRUE, TRUE) == STG_E_WRITEFAULT && source->m_bRequiresSave);
    medium->failWrite = 0; medium->missing = 1UL << 2; targetState.number = 777; target->m_bRequiresSave = TRUE;
    CHECK(load->Load(bag, log) == S_OK && targetState.number == 777 && !target->m_bRequiresSave);
    medium->missing = 0; medium->values[2] = L"not a number"; target->m_bRequiresSave = TRUE;
    CHECK(load->Load(bag, log) == DISP_E_TYPEMISMATCH && targetState.number == 777 && target->m_bRequiresSave &&
        medium->errors == 1 && medium->lastError == DISP_E_TYPEMISMATCH && !::wcscmp(medium->lastName, L"Number"));
    medium->values[2] = LONG(314); medium->failRead = medium->reads + 1;
    CHECK(load->Load(bag, log) == STG_E_READFAULT && medium->errors == 2 && target->m_bRequiresSave);
    medium->failRead = 0; targetState.putFailure = 1;
    CHECK(load->Load(bag, log) == E_ACCESSDENIED && medium->errors == 3 && target->m_bRequiresSave);
    targetState.putFailure = 0;
    const ULONG baseline = probe.p->AddRef() - 1; probe.p->Release();
    for (int mode = 1; mode <= 3; ++mode) {
        sourceState.getFailure = mode; source->m_bRequiresSave = TRUE;
        const HRESULT expected = mode == 1 ? E_ACCESSDENIED : mode == 2 ? E_ABORT : E_OUTOFMEMORY;
        CHECK(save->Save(bag, TRUE, TRUE) == expected && source->m_bRequiresSave);
        CHECK(probe.p->AddRef() == baseline + 1 && probe.p->Release() == baseline);
        medium->throwRead = mode;
        CHECK(load->Load(bag, log) == (mode == 1 ? E_ABORT : mode == 2 ? E_OUTOFMEMORY : E_UNEXPECTED));
        CHECK(probe.p->AddRef() == baseline + 1 && probe.p->Release() == baseline);
        medium->throwRead = 0;
    }
    sourceState.getFailure = 0;
    medium->positiveWrite = true;
    CHECK(save->Save(bag, TRUE, TRUE) == E_UNEXPECTED && source->m_bRequiresSave);
    medium->positiveWrite = false; medium->positiveRead = true;
    CHECK(load->Load(bag, log) == E_UNEXPECTED && target->m_bRequiresSave);
    medium->positiveRead = false;
    for (int mode = 1; mode <= 5; ++mode) {
        PersistentObject::mapFailure = mode;
        const int reads = medium->reads, writes = medium->writes;
        const HRESULT expected = mode == 1 ? E_UNEXPECTED : mode == 2 ? E_OUTOFMEMORY : E_INVALIDARG;
        CHECK(save->Save(bag, TRUE, TRUE) == expected && load->Load(bag, log) == expected);
        CHECK(medium->reads == reads && medium->writes == writes && source->m_bRequiresSave && target->m_bRequiresSave);
    }
    PersistentObject::mapFailure = 7;
    CHECK(save->Save(bag, TRUE, TRUE) == E_NOINTERFACE && load->Load(bag, log) == E_NOINTERFACE);
    PersistentObject::mapFailure = 6;
    const int reads = medium->reads, writes = medium->writes;
    CHECK(save->Save(bag, FALSE, TRUE) == S_OK && load->Load(bag, log) == S_OK && medium->reads == reads && medium->writes == writes);
    PersistentObject::mapFailure = 0;
    CHECK(save->Save(bag, TRUE, TRUE) == S_OK && !source->m_bRequiresSave && load->Load(bag, nullptr) == S_OK && !target->m_bRequiresSave);
    CHECK(sourceState.valid && targetState.valid && medium->valid);
    CHECK(medium->AddRef() == 2 && medium->Release() == 1);
    persist.Release(); medium->reenter = false; source->m_bRequiresSave = TRUE;
    medium->dropOwner = save.Detach(); // Callback drops the last outside reference during Save.
    CHECK(medium->dropOwner->Save(bag, TRUE, TRUE) == S_OK && sourceState.destroyed == 1 &&
        !sourceState.dirtyAtDestruction && !medium->dropOwner);
    load.Release(); CHECK(targetState.destroyed == 1);
    return true;
}
struct AuthoredPageState { int destroyed=0, finalized=0, applies=0, notifications=0; bool valid=true; };
class AuthoredPage : public ATL::CComObjectRootEx<ATL::CComMultiThreadModel>,
                     public ATL::CDialogImpl<AuthoredPage>, public ATL::IPropertyPage2Impl<AuthoredPage> {
public:
    enum { IDD=602 };
    AuthoredPageState* state=nullptr;
    LONG selected=918;
    HRESULT applyFailure=S_OK;
    bool destroyOnPosition=false;
    AuthoredPage() { m_size={180,90}; }
    ~AuthoredPage() { if (state) ++state->destroyed; }
    void OnFinalMessage(HWND) override { if (state) ++state->finalized; }
    LRESULT OnPosition(UINT,WPARAM,LPARAM,BOOL& handled) {
        handled=FALSE;
        if (destroyOnPosition) { destroyOnPosition=false; DestroyWindow(); }
        return 0;
    }
    HRESULT STDMETHODCALLTYPE Apply() override {
        ATL::CComPtr<IPropertyPage> self(this);
        if (FAILED(applyFailure)) return applyFailure;
        if (!m_nObjects) return E_UNEXPECTED;
        for (ULONG i=0;i<m_nObjects;++i) {
            ATL::CComPtr<IDispatch> object;
            HRESULT hr=m_ppUnk[i]->QueryInterface(IID_IDispatch,reinterpret_cast<void**>(&object.p));
            if (FAILED(hr)) return hr;
            ATL::CComVariant value(selected); DISPID named=DISPID_PROPERTYPUT;
            DISPPARAMS args{&value,&named,1,1};
            hr=object->Invoke(11,IID_NULL,LOCALE_USER_DEFAULT,DISPATCH_PROPERTYPUT,&args,nullptr,nullptr,nullptr);
            if (FAILED(hr)) return hr;
        }
        SetDirty(FALSE); if (state) ++state->applies; return S_OK;
    }
    BEGIN_COM_MAP(AuthoredPage)
        COM_INTERFACE_ENTRY(IPropertyPage2)
        COM_INTERFACE_ENTRY(IPropertyPage)
    END_COM_MAP()
    BEGIN_MSG_MAP(AuthoredPage)
        MESSAGE_HANDLER(WM_WINDOWPOSCHANGED, OnPosition)
    END_MSG_MAP()
};
class AuthoredPageSite : public ATL::CComObjectRootEx<ATL::CComMultiThreadModel>, public IPropertyPageSite {
public:
    AuthoredPageState* state=nullptr;
    IPropertyPage* page=nullptr;
    IPropertyPage* drop=nullptr;
    HRESULT STDMETHODCALLTYPE OnStatusChange(DWORD flags) override {
        ++state->notifications;
        state->valid=flags==PROPPAGESTATUS_DIRTY && page->IsPageDirty()==S_OK && state->valid;
        if (drop) { auto release=drop; drop=nullptr; release->Release(); }
        return E_ACCESSDENIED; // Notification failure does not undo dirty state.
    }
    HRESULT STDMETHODCALLTYPE GetLocaleID(LCID* locale) override { if (!locale) return E_POINTER; *locale=0x409; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetPageContainer(IUnknown** output) override { if (!output) return E_POINTER; *output=nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE TranslateAccelerator(MSG*) override { return E_NOTIMPL; }
    BEGIN_COM_MAP(AuthoredPageSite)
        COM_INTERFACE_ENTRY(IPropertyPageSite)
    END_COM_MAP()
};
bool authoredPropertyPages() {
    AuthoredPageState state; PersistenceState objectState;
    ATL::CComObject<PersistentObject>* object=nullptr;
    CHECK(ATL::CComObject<PersistentObject>::CreateInstance(&object)==S_OK); object->state=&objectState; object->AddRef();
    ATL::CComPtr<IPersistPropertyBag> bound; bound.Attach(object);
    ATL::CComObject<AuthoredPage>* authored=nullptr;
    CHECK(ATL::CComObject<AuthoredPage>::CreateInstance(&authored)==S_OK); authored->state=&state; authored->AddRef();
    ATL::CComPtr<IPropertyPage2> page; page.Attach(authored);
    ATL::CComObject<AuthoredPageSite>* frameSite=nullptr;
    CHECK(ATL::CComObject<AuthoredPageSite>::CreateInstance(&frameSite)==S_OK); frameSite->state=&state; frameSite->page=page; frameSite->AddRef();
    ATL::CComPtr<IPropertyPageSite> site; site.Attach(frameSite);
    RECT rect{0,0,180,90}; MSG message{}; PROPPAGEINFO info{},again{};
    // Default page metadata is derived before activation without executing
    // the page's dialog procedure or changing its public size override.
    authored->m_size={}; SIZE inferred{};
    CHECK(ATL::detail::PropertyPageResourceSize(ATL::_AtlBaseModule.GetResourceInstance(),
        MAKEINTRESOURCEW(AuthoredPage::IDD),inferred)==S_OK);
    CHECK(page->GetPageInfo(&info)==S_OK && info.size.cx==inferred.cx && info.size.cy==inferred.cy &&
        !authored->m_hWnd && state.finalized==0 && !authored->m_size.cx && !authored->m_size.cy);
    ::CoTaskMemFree(info.pszTitle); info={};
    authored->m_size={0,90};
    CHECK(page->GetPageInfo(&info)==E_UNEXPECTED && !info.cb && !info.pszTitle);
    authored->m_size={180,90};
    CHECK(page->Activate(::GetDesktopWindow(),&rect,FALSE)==E_UNEXPECTED && page->SetPageSite(nullptr)==E_UNEXPECTED);
    CHECK(page->SetPageSite(site)==S_OK && page->SetPageSite(site)==E_UNEXPECTED);
    IUnknown* objects[]={bound.p}; IUnknown* bad[]={nullptr};
    CHECK(page->SetObjects(1,nullptr)==E_POINTER && page->SetObjects(1,bad)==E_POINTER);
    CHECK(page->SetObjects(1,objects)==S_OK && object->AddRef()==3 && object->Release()==2);
    CHECK(page->SetObjects(1,authored->m_ppUnk)==S_OK && object->AddRef()==3 && object->Release()==2);
    CHECK(page->GetPageInfo(nullptr)==E_POINTER && page->GetPageInfo(&info)==S_OK &&
        page->GetPageInfo(&again)==S_OK && info.cb==sizeof(info) && info.pszTitle &&
        !info.pszTitle[0] && info.pszTitle!=again.pszTitle && info.size.cx==180 && info.size.cy==90);
    ::CoTaskMemFree(info.pszTitle); ::CoTaskMemFree(again.pszTitle);
    authored->m_dwTitle=6901; authored->m_dwDocString=6902;
    authored->m_dwHelpFile=6903; authored->m_dwHelpContext=12345;
    info={}; again={};
    CHECK(page->GetPageInfo(&info)==S_OK && page->GetPageInfo(&again)==S_OK &&
        info.dwHelpContext==12345 && info.pszTitle && info.pszDocString && info.pszHelpFile &&
        !::wcscmp(info.pszTitle,L"OpenWTL authored page") &&
        !::wcscmp(info.pszDocString,L"Native property metadata") &&
        !::wcscmp(info.pszHelpFile,L"openwtl-help.chm") &&
        info.pszTitle!=again.pszTitle && info.pszDocString!=again.pszDocString &&
        info.pszHelpFile!=again.pszHelpFile);
    info.pszTitle[0]=L'X'; info.pszDocString[0]=L'X'; info.pszHelpFile[0]=L'X';
    CHECK(again.pszTitle[0]==L'O' && again.pszDocString[0]==L'N' && again.pszHelpFile[0]==L'o');
    ::CoTaskMemFree(info.pszTitle); ::CoTaskMemFree(info.pszDocString); ::CoTaskMemFree(info.pszHelpFile);
    ::CoTaskMemFree(again.pszTitle); ::CoTaskMemFree(again.pszDocString); ::CoTaskMemFree(again.pszHelpFile);
    authored->m_dwDocString=65535; info={};
    CHECK(page->GetPageInfo(&info)==HRESULT_FROM_WIN32(ERROR_RESOURCE_NAME_NOT_FOUND) &&
        !info.pszTitle && !info.pszDocString && !info.pszHelpFile && !info.cb);
    authored->m_dwDocString=0; authored->m_dwHelpFile=65535; info={};
    CHECK(page->GetPageInfo(&info)==HRESULT_FROM_WIN32(ERROR_RESOURCE_NAME_NOT_FOUND) &&
        !info.pszTitle && !info.pszDocString && !info.pszHelpFile && !info.cb);
    authored->m_dwTitle=0; authored->m_dwDocString=0; authored->m_dwHelpFile=0; authored->m_dwHelpContext=0;
    CHECK(page->Move(&rect)==E_UNEXPECTED && page->Deactivate()==E_UNEXPECTED && page->IsPageDirty()==S_FALSE);
    for (int cycle=0;cycle<2;++cycle) {
        const RECT overflow{(std::numeric_limits<LONG>::min)(),0,(std::numeric_limits<LONG>::max)(),90};
        CHECK(page->Activate(::GetDesktopWindow(),&overflow,FALSE)==E_INVALIDARG);
        CHECK(page->Activate(::GetDesktopWindow(),nullptr,FALSE)==E_POINTER);
        CHECK(page->Activate(::GetDesktopWindow(),&rect,FALSE)==S_OK && ::IsWindow(authored->m_hWnd));
        HWND window=authored->m_hWnd;
        CHECK(page->Activate(::GetDesktopWindow(),&rect,TRUE)==E_UNEXPECTED);
        CHECK(page->Move(nullptr)==E_POINTER && page->Move(&overflow)==E_INVALIDARG && page->Move(&rect)==S_OK && page->Show(SW_SHOW)==S_OK &&
            page->Show(SW_HIDE)==S_OK && page->Show(SW_MINIMIZE)==E_INVALIDARG);
        CHECK(page->TranslateAccelerator(nullptr)==E_POINTER && page->TranslateAccelerator(&message)==S_FALSE &&
            page->EditProperty(11)==E_NOTIMPL && page->Help(nullptr)==E_NOTIMPL);
        authored->SetDirty(TRUE); CHECK(page->IsPageDirty()==S_OK);
        authored->applyFailure=E_ACCESSDENIED;
        CHECK(page->Apply()==E_ACCESSDENIED && page->IsPageDirty()==S_OK && objectState.number!=918);
        authored->applyFailure=S_OK;
        CHECK(page->Apply()==S_OK && page->IsPageDirty()==S_FALSE && objectState.number==918);
        objectState.number=314;
        CHECK(page->Deactivate()==S_OK && !::IsWindow(window) && !authored->m_hWnd);
    }
    CHECK(state.finalized==2 && state.applies==2 && state.notifications==2 && state.valid);
    authored->destroyOnPosition=true;
    CHECK(FAILED(page->Activate(::GetDesktopWindow(),&rect,FALSE)) && !authored->m_hWnd);
    HWND parent=::CreateWindowExW(0,L"STATIC",L"property-page parent",WS_POPUP,
        0,0,200,120,nullptr,nullptr,::GetModuleHandleW(nullptr),nullptr);
    CHECK(parent && page->Activate(parent,&rect,FALSE)==S_OK);
    CHECK(::DestroyWindow(parent) && !authored->m_hWnd);
    CHECK(page->Show(SW_SHOW)==E_UNEXPECTED && page->Move(&rect)==E_UNEXPECTED &&
        page->TranslateAccelerator(&message)==E_UNEXPECTED && page->Deactivate()==E_UNEXPECTED);
    CHECK(state.finalized==4);
    CHECK(page->SetObjects(0,nullptr)==S_OK && object->AddRef()==2 && object->Release()==1);
    CHECK(page->SetPageSite(nullptr)==S_OK && frameSite->AddRef()==2 && frameSite->Release()==1);
    // Callback retires the last outside page owner while SetDirty holds self.
    CHECK(page->SetPageSite(site)==S_OK);
    frameSite->drop=page.Detach(); authored->SetDirty(TRUE);
    CHECK(state.destroyed==1 && !frameSite->drop && state.valid);
    frameSite->page=nullptr; bound.Release(); CHECK(objectState.destroyed==1);
    return true;
}
int MetricsDialogCallbacks=0;
INT_PTR CALLBACK MetricsDialogProcedure(HWND,UINT,WPARAM,LPARAM) {
    ++MetricsDialogCallbacks;
    return FALSE;
}
struct MetricsDialogWindow {
    HWND window=nullptr;
    ~MetricsDialogWindow() { if (::IsWindow(window)) ::DestroyWindow(window); }
};
bool propertyPageMetrics() {
    using ATL::detail::PropertyPageTemplateMetrics;
    using ATL::detail::ReadPropertyPageTemplateMetrics;
    using ATL::detail::PropertyPageDialogSize;
    using ATL::detail::PropertyPageResourceSize;
    const HINSTANCE instance=ATL::_AtlBaseModule.GetResourceInstance();
    const auto compareNative=[&](const std::vector<BYTE>& record)->bool {
        PropertyPageTemplateMetrics metadata;
        SIZE measured{};
        CHECK(ReadPropertyPageTemplateMetrics(record.data(),record.size(),metadata)==ERROR_SUCCESS);
        CHECK(PropertyPageDialogSize(record.data(),record.size(),measured)==S_OK);
        std::vector<DWORD> header((metadata.headerBytes+3)/4,0);
        std::memcpy(header.data(),record.data(),metadata.headerBytes);
        const WORD noControls=0;
        std::memcpy(reinterpret_cast<BYTE*>(header.data())+(metadata.extended?16:8),&noControls,sizeof(noControls));
        MetricsDialogWindow native;
        native.window=::CreateDialogIndirectParamW(instance,reinterpret_cast<const DLGTEMPLATE*>(header.data()),
            ::GetDesktopWindow(),MetricsDialogProcedure,0);
        CHECK(native.window);
        RECT oracle{0,0,metadata.cx,metadata.cy};
        CHECK(::MapDialogRect(native.window,&oracle));
        if (measured.cx!=oracle.right || measured.cy!=oracle.bottom)
            std::fprintf(stderr,"PROPERTY_PAGE_FONT_MISMATCH style=%08lx extended=%d measured=%ld,%ld native=%ld,%ld\n",
                static_cast<unsigned long>(metadata.style),metadata.extended,measured.cx,measured.cy,oracle.right,oracle.bottom);
        CHECK(measured.cx==oracle.right && measured.cy==oracle.bottom);
        return true;
    };
    SIZE output{123,456};
    CHECK(PropertyPageDialogSize(nullptr,0,output)==HRESULT_FROM_WIN32(ERROR_INVALID_DATA) && !output.cx && !output.cy);
    CHECK(PropertyPageResourceSize(instance,nullptr,output)==E_POINTER && !output.cx && !output.cy);
    CHECK(FAILED(PropertyPageResourceSize(instance,MAKEINTRESOURCEW(65534),output)) && !output.cx && !output.cy);
    for (WORD id : {WORD(602),WORD(607),WORD(606)}) {
        HRSRC resource=::FindResourceW(instance,MAKEINTRESOURCEW(id),MAKEINTRESOURCEW(5));
        CHECK(resource);
        const DWORD size=::SizeofResource(instance,resource);
        const auto bytes=static_cast<const BYTE*>(::LockResource(::LoadResource(instance,resource)));
        CHECK(bytes && size);
        const std::vector<BYTE> snapshot(bytes,bytes+size);
        PropertyPageTemplateMetrics parsed;
        CHECK(ReadPropertyPageTemplateMetrics(bytes,size,parsed)==ERROR_SUCCESS);
        CHECK(parsed.cx==(id==602?80:160) && parsed.cy==(id==602?40:100) &&
            parsed.extended==(id==606) && parsed.hasFont==(id!=602));
        const int callbacksBefore=MetricsDialogCallbacks;
        SIZE measured{},resourceMeasured{};
        CHECK(PropertyPageDialogSize(bytes,size,measured)==S_OK &&
            PropertyPageResourceSize(instance,MAKEINTRESOURCEW(id),resourceMeasured)==S_OK &&
            measured.cx==resourceMeasured.cx && measured.cy==resourceMeasured.cy &&
            MetricsDialogCallbacks==callbacksBefore && std::memcmp(bytes,snapshot.data(),size)==0);
        // A native dialog is an independent font-mapping oracle. Only its
        // original header/font bytes are retained: no CLSID child controls,
        // application initialization, hosting conversion or COM activation.
        std::vector<DWORD> header((parsed.headerBytes+3)/4,0);
        std::memcpy(header.data(),bytes,parsed.headerBytes);
        const WORD noControls=0;
        std::memcpy(reinterpret_cast<BYTE*>(header.data())+(parsed.extended?16:8),&noControls,sizeof(noControls));
        MetricsDialogWindow native;
        native.window=::CreateDialogIndirectParamW(instance,reinterpret_cast<const DLGTEMPLATE*>(header.data()),
            ::GetDesktopWindow(),MetricsDialogProcedure,0);
        CHECK(native.window && MetricsDialogCallbacks>callbacksBefore);
        RECT oracle{0,0,parsed.cx,parsed.cy};
        CHECK(::MapDialogRect(native.window,&oracle) && measured.cx==oracle.right && measured.cy==oracle.bottom);
        // Measuring while a native dialog exists must not dispatch its proc.
        const int callbacksDuring=MetricsDialogCallbacks;
        CHECK(PropertyPageDialogSize(bytes,size,output)==S_OK && MetricsDialogCallbacks==callbacksDuring);
        for (SIZE_T cut=0;cut<parsed.headerBytes;++cut) {
            output={123,456};
            CHECK(PropertyPageDialogSize(bytes,cut,output)==HRESULT_FROM_WIN32(ERROR_INVALID_DATA) && !output.cx && !output.cy);
        }
        const SIZE_T cxOffset=parsed.extended?22:14,cyOffset=parsed.extended?24:16;
        for (SHORT invalid : {SHORT(0),SHORT(-1)}) {
            for (SIZE_T offset : {cxOffset,cyOffset}) {
                auto bad=snapshot;
                std::memcpy(bad.data()+offset,&invalid,sizeof(invalid));
                CHECK(PropertyPageDialogSize(bad.data(),bad.size(),output)==HRESULT_FROM_WIN32(ERROR_INVALID_DATA) && !output.cx && !output.cy);
            }
        }
        if (parsed.extended) {
            auto bad=snapshot; const WORD invalidVersion=2;
            std::memcpy(bad.data(),&invalidVersion,sizeof(invalidVersion));
            CHECK(PropertyPageDialogSize(bad.data(),bad.size(),output)==HRESULT_FROM_WIN32(ERROR_INVALID_DATA));
        }
        {
            auto bad=snapshot;
            const DWORD fixedStyle=parsed.style|DS_FIXEDSYS;
            std::memcpy(bad.data()+(parsed.extended?12:0),&fixedStyle,sizeof(fixedStyle));
            CHECK(PropertyPageDialogSize(bad.data(),bad.size(),output)==S_OK && compareNative(bad));
            // Restore the caller's DC selections on every repeated stock-font
            // measurement; a borrowed SYSTEM_FIXED_FONT must not be deleted.
            for (int repeat=0;repeat<4;++repeat) {
                SIZE again{};
                CHECK(PropertyPageDialogSize(bad.data(),bad.size(),again)==S_OK &&
                    again.cx==output.cx && again.cy==output.cy);
            }
            if (parsed.hasFont) {
                const SIZE_T faceBytes=(std::wcslen(parsed.face)+1)*sizeof(WCHAR);
                const SIZE_T faceOffset=parsed.headerBytes-faceBytes;
                const WCHAR notShell=L'X';
                std::memcpy(bad.data()+faceOffset,&notShell,sizeof(notShell));
                CHECK(PropertyPageDialogSize(bad.data(),bad.size(),output)==HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED));
            }
        }
        if (parsed.hasFont) {
            const SIZE_T faceBytes=(std::wcslen(parsed.face)+1)*sizeof(WCHAR);
            const SIZE_T faceOffset=parsed.headerBytes-faceBytes;
            const SIZE_T pointOffset=faceOffset-(parsed.extended?6:2);
            auto bad=snapshot; const WORD specialSize=0x7fff;
            std::memcpy(bad.data()+pointOffset,&specialSize,sizeof(specialSize));
            CHECK(PropertyPageDialogSize(bad.data(),bad.size(),output)==HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED));
            bad=snapshot; const WORD zero=0;
            std::memcpy(bad.data()+pointOffset,&zero,sizeof(zero));
            CHECK(PropertyPageDialogSize(bad.data(),bad.size(),output)==HRESULT_FROM_WIN32(ERROR_INVALID_DATA));
            bad=snapshot; std::memcpy(bad.data()+faceOffset,&zero,sizeof(zero));
            CHECK(PropertyPageDialogSize(bad.data(),bad.size(),output)==HRESULT_FROM_WIN32(ERROR_INVALID_DATA));
            if (parsed.extended) {
                bad=snapshot; const WORD unqualifiedWeight=FW_BOLD;
                std::memcpy(bad.data()+pointOffset+sizeof(WORD),&unqualifiedWeight,sizeof(unqualifiedWeight));
                CHECK(PropertyPageDialogSize(bad.data(),bad.size(),output)==HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED));
            }
        }
    }
    return true;
}
size_t DialogBytesFind(const std::vector<BYTE>& bytes, const void* value, size_t size) {
    const auto first = static_cast<const BYTE*>(value);
    return static_cast<size_t>(std::search(bytes.begin(), bytes.end(), first, first + size) - bytes.begin());
}
struct GlobalSplitTemplate {
    HGLOBAL memory = nullptr;
    ~GlobalSplitTemplate() { if (memory) ::GlobalFree(memory); }
    bool Free() {
        if (!memory) return true;
        if (::GlobalFree(memory)) return false;
        memory = nullptr; return true;
    }
};
bool splitDialogResources() {
    namespace Split = ATL::_DialogSplitHelper;
    const HINSTANCE instance = ATL::_AtlBaseModule.GetResourceInstance();
    const auto inspect = [&](LPCWSTR name, WORD language, bool extended, WORD axId, WORD labelId)->bool {
        const HRSRC resource = ::FindResourceExW(instance, MAKEINTRESOURCEW(5), name, language);
        CHECK(resource);
        const auto bytes = static_cast<const BYTE*>(::LockResource(::LoadResource(instance, resource)));
        const DWORD size = ::SizeofResource(instance, resource);
        CHECK(bytes && size);
        const auto original = reinterpret_cast<const DLGTEMPLATE*>(bytes);
        const std::vector<BYTE> snapshot(bytes, bytes + size);
        const int callbacks = MetricsDialogCallbacks;
        CHECK(Split::IsDialogEx(original) == extended && ::GetLastError() == ERROR_SUCCESS);
        CHECK(Split::DlgTemplateItemCount(original) == (axId ? 2 : 1) && ::GetLastError() == ERROR_SUCCESS);
        DLGITEMTEMPLATE* item = Split::FindFirstDlgItem(original);
        CHECK(item && ::GetLastError() == ERROR_SUCCESS);
        CHECK(Split::IsActiveXControl(item, !extended) == FALSE && ::GetLastError() == ERROR_INVALID_DATA);
        CHECK(!Split::FindNextDlgItem(reinterpret_cast<DLGITEMTEMPLATE*>(reinterpret_cast<BYTE*>(item) + 2), extended) &&
            ::GetLastError() == ERROR_INVALID_DATA);
        for (WORD n = 0; n < (axId ? 2 : 1); ++n) {
            CHECK(item && reinterpret_cast<UINT_PTR>(item) % 4 == 0);
            DWORD id = 0;
            if (extended) id = reinterpret_cast<Split::DLGITEMTEMPLATEEX*>(item)->id;
            else id = item->id;
            CHECK(id == (axId && !n ? axId : labelId));
            CHECK(Split::IsActiveXControl(item, extended) == (axId && !n) && ::GetLastError() == ERROR_SUCCESS);
            item = Split::FindNextDlgItem(item, extended);
            CHECK(::GetLastError() == ERROR_SUCCESS);
        }
        CHECK(!item && MetricsDialogCallbacks == callbacks);
        CHECK(!Split::FindFirstDlgItem(reinterpret_cast<const DLGTEMPLATE*>(bytes + 2)) &&
            ::GetLastError() == ERROR_INVALID_DATA);
        GlobalSplitTemplate owned;
        const DLGTEMPLATE* split = Split::SplitDialogTemplate(original);
        if (split && split != original) owned.memory = reinterpret_cast<HGLOBAL>(const_cast<DLGTEMPLATE*>(split));
        CHECK(split && ::GetLastError() == ERROR_SUCCESS && (split != original) == (axId != 0));
        ATL::detail::AxDialogTemplate expected;
        CHECK(ATL::detail::SplitAxDialogTemplate(bytes, size, expected) == ERROR_SUCCESS &&
            !std::memcmp(split, expected.Get(), expected.size));
        if (owned.memory) CHECK(::GlobalHandle(split) == owned.memory && ::GlobalSize(owned.memory) >= expected.size);
        CHECK(MetricsDialogCallbacks == callbacks && !std::memcmp(bytes, snapshot.data(), size));
        MetricsDialogWindow native;
        native.window = ::CreateDialogIndirectParamW(instance, split, ::GetDesktopWindow(), MetricsDialogProcedure, 0);
        CHECK(native.window && (!axId || !::GetDlgItem(native.window, axId)) && ::GetDlgItem(native.window, labelId));
        CHECK(::DestroyWindow(native.window)); native.window = nullptr;
        CHECK(owned.Free());
        // Pointer-only legacy helpers do not infer an allocation length. Heap
        // copies remain usable through the shared size-bearing syntax cursor.
        std::vector<DWORD> copied((size + 3) / 4);
        std::memcpy(copied.data(), bytes, size);
        CHECK(!Split::FindFirstDlgItem(reinterpret_cast<const DLGTEMPLATE*>(copied.data())) &&
            ::GetLastError() == ERROR_RESOURCE_DATA_NOT_FOUND);
        ATL::detail::DialogTemplateCursor cursor(copied.data(), size);
        CHECK(cursor.ReadHeader() == ERROR_SUCCESS && cursor.extended == extended && cursor.count == (axId ? 2 : 1));
        return true;
    };
    CHECK(inspect(MAKEINTRESOURCEW(602), MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL), false, 0, 702));
    CHECK(inspect(L"OPENATL_PLAIN", MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL), false, 0, 702));
    CHECK(inspect(MAKEINTRESOURCEW(606), MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL), true, 701, 704));
    CHECK(inspect(MAKEINTRESOURCEW(607), MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL), false, 701, 704));
    CHECK(inspect(MAKEINTRESOURCEW(652), MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), false, 781, 782));
    CHECK(inspect(MAKEINTRESOURCEW(652), MAKELANGID(LANG_FRENCH, SUBLANG_FRENCH), true, 781, 782));
    CHECK(!Split::SplitDialogTemplate(nullptr) && ::GetLastError() == ERROR_INVALID_PARAMETER);
    for (UINT_PTR address : {UINT_PTR(1), UINT_PTR(-1)}) {
        CHECK(!Split::FindFirstDlgItem(reinterpret_cast<const DLGTEMPLATE*>(address)) &&
            ::GetLastError() == ERROR_RESOURCE_DATA_NOT_FOUND);
        CHECK(!Split::FindNextDlgItem(reinterpret_cast<const DLGITEMTEMPLATE*>(address), FALSE) &&
            ::GetLastError() == ERROR_RESOURCE_DATA_NOT_FOUND);
    }
    for (WORD id : {WORD(610), WORD(611), WORD(612), WORD(615), WORD(616), WORD(617), WORD(618), WORD(620), WORD(631)}) {
        const HRSRC resource = ::FindResourceW(instance, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(5));
        const HRSRC initialization = ::FindResourceW(instance, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(240));
        CHECK(resource && initialization);
        const auto dialog = static_cast<const DLGTEMPLATE*>(::LockResource(::LoadResource(instance, resource)));
        const auto init = static_cast<const BYTE*>(::LockResource(::LoadResource(instance, initialization)));
        GlobalSplitTemplate owned;
        const auto split = Split::SplitDialogTemplate(dialog, init);
        if (split && split != dialog) owned.memory = reinterpret_cast<HGLOBAL>(const_cast<DLGTEMPLATE*>(split));
        if (id == 610 || id == 612 || id == 620) {
            CHECK(split && owned.memory && ::GetLastError() == ERROR_SUCCESS && ::GlobalHandle(split) == owned.memory);
        } else {
            const DWORD expected = id == 611 || id == 617 || id == 631 ? ERROR_NOT_SUPPORTED : ERROR_INVALID_DATA;
            CHECK(!split && ::GetLastError() == expected);
        }
        CHECK(!Split::SplitDialogTemplate(dialog, init + 1) && ::GetLastError() == ERROR_INVALID_DATA);
    }
    const HRSRC resource = ::FindResourceW(instance, MAKEINTRESOURCEW(602), MAKEINTRESOURCEW(5));
    const auto original = static_cast<const DLGTEMPLATE*>(::LockResource(::LoadResource(instance, resource)));
    struct ResourceModule {
        HINSTANCE original;
        ~ResourceModule() { ATL::_AtlBaseModule.SetResourceInstance(original); }
    } changed{instance};
    const HMODULE other = ::GetModuleHandleW(L"kernel32.dll");
    CHECK(other);
    ATL::_AtlBaseModule.SetResourceInstance(other);
    CHECK(!Split::FindFirstDlgItem(original) && ::GetLastError() == ERROR_RESOURCE_DATA_NOT_FOUND);
    ATL::_AtlBaseModule.SetResourceInstance(instance);
    CHECK(Split::FindFirstDlgItem(original) && ::GetLastError() == ERROR_SUCCESS);
    return true;
}
bool dialogTemplates() {
    using ATL::detail::AxDialogTemplate;
    using ATL::detail::BuildAxDialogTemplate;
    using ATL::detail::SplitAxDialogTemplate;
    const HINSTANCE instance = ATL::_AtlBaseModule.GetResourceInstance();
    const WCHAR clsid[] = L"{8856F961-340A-11D0-A96B-00C04FD705A2}";
    const WCHAR sibling[] = L"resource sibling";
    const WORD staticClass[] = {0xFFFF, 0x82};
    const auto sameInitialization = [](const AxDialogTemplate& left, const AxDialogTemplate& right)->bool {
        CHECK(left.initialization.size() == right.initialization.size());
        for (size_t n = 0; n < left.initialization.size(); ++n) {
            const auto& a = left.initialization[n];
            const auto& b = right.initialization[n];
            CHECK(a.id == b.id && a.kind == b.kind && a.name == b.name &&
                a.license == b.license && a.stream == b.stream);
        }
        return true;
    };
    for (WORD id : {WORD(602), WORD(606), WORD(607)}) {
        HRSRC resource = ::FindResourceW(instance, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(5));
        CHECK(resource);
        const DWORD size = ::SizeofResource(instance, resource);
        auto original = static_cast<const BYTE*>(::LockResource(::LoadResource(instance, resource)));
        CHECK(original && size > 8);
        const std::vector<BYTE> snapshot(original, original + size);
        AxDialogTemplate built;
        CHECK(BuildAxDialogTemplate(original, size, built) == ERROR_SUCCESS && built.converted == (id != 602));
        CHECK(std::memcmp(original, snapshot.data(), size) == 0 && reinterpret_cast<UINT_PTR>(built.Get()) % 4 == 0);
        AxDialogTemplate split;
        const int callbacksBefore = MetricsDialogCallbacks;
        CHECK(SplitAxDialogTemplate(original, size, split) == ERROR_SUCCESS &&
            split.converted == (id != 602) && split.initialization.empty() &&
            MetricsDialogCallbacks == callbacksBefore && reinterpret_cast<UINT_PTR>(split.Get()) % 4 == 0);
        WORD remaining = 0;
        std::memcpy(&remaining, reinterpret_cast<const BYTE*>(split.Get()) + (id == 606 ? 16 : 8), sizeof(remaining));
        CHECK(remaining == 1 && std::memcmp(original, snapshot.data(), size) == 0);
        SIZE originalSize{}, splitSize{};
        CHECK(ATL::detail::PropertyPageDialogSize(original, size, originalSize) == S_OK &&
            ATL::detail::PropertyPageDialogSize(split.Get(), split.size, splitSize) == S_OK &&
            originalSize.cx == splitSize.cx && originalSize.cy == splitSize.cy);
        MetricsDialogWindow native;
        native.window = ::CreateDialogIndirectParamW(instance, split.Get(), ::GetDesktopWindow(), MetricsDialogProcedure, 0);
        CHECK(native.window && !::GetDlgItem(native.window, 701));
        WCHAR nativeLabel[32]{};
        CHECK(::GetDlgItemTextW(native.window, id == 602 ? 702 : 704, nativeLabel, 32) &&
            std::wcscmp(nativeLabel, id == 602 ? L"native" : sibling) == 0);
        AxDialogTemplate again;
        CHECK(SplitAxDialogTemplate(split.Get(), split.size, again) == ERROR_SUCCESS && !again.converted &&
            again.size == split.size && std::memcmp(again.Get(), split.Get(), split.size) == 0);
        CHECK(BuildAxDialogTemplate(built.storage.data(), built.size, again) == ERROR_SUCCESS && !again.converted);
        CHECK(again.size == built.size && std::memcmp(again.storage.data(), built.storage.data(), built.size) == 0);
        if (id == 602) {
            CHECK(built.size == size && std::memcmp(built.storage.data(), original, size) == 0);
            continue;
        }
        const auto converted = reinterpret_cast<const BYTE*>(built.storage.data());
        std::vector<BYTE> bytes(converted, converted + built.size);
        const size_t cls = DialogBytesFind(snapshot, clsid, sizeof(clsid));
        const size_t oldStatic = DialogBytesFind(snapshot, staticClass, sizeof(staticClass));
        const size_t newStatic = DialogBytesFind(bytes, staticClass, sizeof(staticClass));
        const size_t fixed = id == 606 ? 24 : 18;
        CHECK(cls < size && oldStatic >= fixed && oldStatic < size && newStatic >= fixed && newStatic < bytes.size());
        CHECK(std::memcmp(original, converted, cls) == 0); // Header/font and first fixed record.
        CHECK(size - oldStatic == bytes.size() - newStatic);
        CHECK(std::memcmp(original + oldStatic - fixed, converted + newStatic - fixed, size - oldStatic + fixed) == 0);
        const auto splitBytes = reinterpret_cast<const BYTE*>(split.Get());
        const std::vector<BYTE> splitSnapshot(splitBytes, splitBytes + split.size);
        const size_t splitStatic = DialogBytesFind(splitSnapshot, staticClass, sizeof(staticClass));
        CHECK(splitStatic >= fixed && splitStatic < split.size && size - oldStatic == split.size - splitStatic);
        CHECK(std::memcmp(original + oldStatic - fixed, splitBytes + splitStatic - fixed, size - oldStatic + fixed) == 0);
        for (size_t cut = 0; cut <= size - 8; ++cut) {
            CHECK(BuildAxDialogTemplate(original, cut, again) == ERROR_INVALID_DATA && again.storage.empty() && !again.converted && !again.size);
            CHECK(SplitAxDialogTemplate(original, cut, again) == ERROR_INVALID_DATA && again.storage.empty() && !again.converted && !again.size);
        }
        std::vector<BYTE> bad = snapshot;
        const WORD tooMany = 3;
        std::memcpy(bad.data() + (id == 606 ? 16 : 8), &tooMany, sizeof(tooMany));
        CHECK(BuildAxDialogTemplate(bad.data(), bad.size(), again) == ERROR_INVALID_DATA);
        bad = snapshot; const WCHAR invalidHex = L'G';
        std::memcpy(bad.data() + cls + sizeof(WCHAR), &invalidHex, sizeof(invalidHex));
        CHECK(BuildAxDialogTemplate(bad.data(), bad.size(), again) == ERROR_INVALID_DATA);
        CHECK(SplitAxDialogTemplate(bad.data(), bad.size(), again) == ERROR_INVALID_DATA && again.storage.empty());
        bad = snapshot; const WORD inlineData = 4;
        std::memcpy(bad.data() + cls + sizeof(clsid) + sizeof(WORD), &inlineData, sizeof(inlineData));
        CHECK(BuildAxDialogTemplate(bad.data(), bad.size(), again) == ERROR_NOT_SUPPORTED);
        // Creation data for ordinary controls remains opaque. Standard counts
        // include their size word; extended counts exclude it (Win32 contract).
        const size_t label = DialogBytesFind(snapshot, sibling, sizeof(sibling));
        CHECK(label < size);
        const size_t extra = label + sizeof(sibling);
        bad.assign(snapshot.begin(), snapshot.begin() + extra + sizeof(WORD));
        const WORD payloadSize = id == 606 ? 3 : 5;
        std::memcpy(bad.data() + extra, &payloadSize, sizeof(payloadSize));
        const BYTE payload[] = {0xA1, 0xB2, 0xC3};
        bad.insert(bad.end(), payload, payload + sizeof(payload));
        CHECK(BuildAxDialogTemplate(bad.data(), bad.size(), again) == ERROR_SUCCESS && again.converted);
        CHECK(std::memcmp(reinterpret_cast<const BYTE*>(again.storage.data()) + again.size - sizeof(payload), payload, sizeof(payload)) == 0);
        CHECK(SplitAxDialogTemplate(bad.data(), bad.size(), again) == ERROR_SUCCESS && again.converted);
        CHECK(std::memcmp(reinterpret_cast<const BYTE*>(again.Get()) + again.size - sizeof(payload), payload, sizeof(payload)) == 0);
        // All controls may be removed, for either native template format.
        bad.assign(snapshot.begin(), snapshot.begin() + oldStatic - fixed);
        const WORD oneControl = 1;
        std::memcpy(bad.data() + (id == 606 ? 16 : 8), &oneControl, sizeof(oneControl));
        CHECK(SplitAxDialogTemplate(bad.data(), bad.size(), again) == ERROR_SUCCESS && again.converted);
        std::memcpy(&remaining, reinterpret_cast<const BYTE*>(again.Get()) + (id == 606 ? 16 : 8), sizeof(remaining));
        CHECK(remaining == 0);
        MetricsDialogWindow emptyNative;
        emptyNative.window = ::CreateDialogIndirectParamW(instance, again.Get(), ::GetDesktopWindow(), MetricsDialogProcedure, 0);
        CHECK(emptyNative.window && !::GetWindow(emptyNative.window, GW_CHILD));
        if (id == 606) {
            bad = snapshot; const WORD unsupportedVersion = 2;
            std::memcpy(bad.data(), &unsupportedVersion, sizeof(unsupportedVersion));
            CHECK(BuildAxDialogTemplate(bad.data(), bad.size(), again) == ERROR_INVALID_DATA);
        }
    }
    for (WORD id : {WORD(610), WORD(611), WORD(612), WORD(614), WORD(619)}) {
        const HRSRC dialog = ::FindResourceW(instance, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(5));
        const HRSRC init = ::FindResourceW(instance, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(240));
        CHECK(dialog && init);
        const auto bytes = static_cast<const BYTE*>(::LockResource(::LoadResource(instance, dialog)));
        const auto initBytes = static_cast<const BYTE*>(::LockResource(::LoadResource(instance, init)));
        const DWORD size = ::SizeofResource(instance, dialog), initSize = ::SizeofResource(instance, init);
        const std::vector<BYTE> snapshot(initBytes, initBytes + initSize);
        AxDialogTemplate built;
        CHECK(BuildAxDialogTemplate(bytes, size, built, initBytes, initSize) == ERROR_SUCCESS && built.converted);
        CHECK(built.initialization.size() == (id == 614 ? 2U : 1U));
        CHECK(built.initialization[0].id == 711 && built.initialization[0].stream.size() == (id == 619 ? 5U : 4U));
        CHECK(built.initialization[0].license == ((id == 612 || id == 619) ? L"OpenATL-test-license" : L""));
        CHECK(std::memcmp(initBytes, snapshot.data(), initSize) == 0);
        AxDialogTemplate split;
        if (id == 611) {
            CHECK(SplitAxDialogTemplate(bytes, size, split, initBytes, initSize) == ERROR_NOT_SUPPORTED &&
                split.storage.empty() && split.initialization.empty());
        } else {
            CHECK(SplitAxDialogTemplate(bytes, size, split, initBytes, initSize) == ERROR_SUCCESS && split.converted &&
                sameInitialization(split, built));
            MetricsDialogWindow native;
            native.window = ::CreateDialogIndirectParamW(instance, split.Get(), ::GetDesktopWindow(), MetricsDialogProcedure, 0);
            CHECK(native.window && !::GetDlgItem(native.window, 711) && !::GetDlgItem(native.window, 712) &&
                ::GetDlgItem(native.window, 714));
        }
        for (size_t cut = 0; cut + 3 < initSize; ++cut) {
            CHECK(BuildAxDialogTemplate(bytes, size, built, initBytes, cut) == ERROR_INVALID_DATA && built.storage.empty() && built.initialization.empty());
        }
        std::vector<BYTE> bad = snapshot;
        const DWORD oversized = MAXDWORD;
        std::memcpy(bad.data() + 4, &oversized, sizeof(oversized));
        CHECK(BuildAxDialogTemplate(bytes, size, built, bad.data(), bad.size()) == ERROR_INVALID_DATA);
        CHECK(SplitAxDialogTemplate(bytes, size, split, bad.data(), bad.size()) == ERROR_INVALID_DATA &&
            split.storage.empty() && split.initialization.empty());
        bad = snapshot; std::memcpy(bad.data() + 8, &oversized, sizeof(oversized));
        CHECK(BuildAxDialogTemplate(bytes, size, built, bad.data(), bad.size()) == ERROR_INVALID_DATA);
        bad = snapshot; const WORD unmatched = 799;
        std::memcpy(bad.data(), &unmatched, sizeof(unmatched));
        CHECK(BuildAxDialogTemplate(bytes, size, built, bad.data(), bad.size()) == ERROR_INVALID_DATA);
        bad = snapshot; const WORD binding = 0x37A;
        std::memcpy(bad.data() + 2, &binding, sizeof(binding));
        CHECK(BuildAxDialogTemplate(bytes, size, built, bad.data(), bad.size()) == ERROR_NOT_SUPPORTED);
        bad = snapshot; bad.push_back(1);
        CHECK(BuildAxDialogTemplate(bytes, size, built, bad.data(), bad.size()) == ERROR_INVALID_DATA);
        if (id == 614) {
            // Resource order may differ from dialog order; initialization follows
            // the dialog's child order, including for reentrant controls.
            std::swap_ranges(bad.begin(), bad.begin() + 16, bad.begin() + 16);
            bad.resize(snapshot.size());
            CHECK(BuildAxDialogTemplate(bytes, size, built, bad.data(), bad.size()) == ERROR_SUCCESS);
            CHECK(built.initialization[0].id == 711 && built.initialization[1].id == 712);
        }
    }
    for (WORD id : {WORD(620), WORD(621), WORD(622), WORD(625), WORD(627)}) {
        const HRSRC dialog = ::FindResourceW(instance, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(5));
        const HRSRC init = ::FindResourceW(instance, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(240));
        CHECK(dialog && init);
        const auto bytes = static_cast<const BYTE*>(::LockResource(::LoadResource(instance, dialog)));
        const auto initBytes = static_cast<const BYTE*>(::LockResource(::LoadResource(instance, init)));
        const DWORD size = ::SizeofResource(instance, dialog), initSize = ::SizeofResource(instance, init);
        const std::vector<BYTE> snapshot(initBytes, initBytes + initSize);
        const size_t minimum = id == 622 ? 26 : id == 627 ? 18 : id == 625 ? 14 : 10;
        CHECK(initSize >= minimum && initSize - minimum <= 3);
        AxDialogTemplate built;
        CHECK(BuildAxDialogTemplate(bytes, size, built, initBytes, initSize) == ERROR_SUCCESS && built.converted);
        CHECK(built.initialization.size() == ((id == 622 || id == 627) ? 2U : 1U));
        const auto& first = built.initialization[0];
        CHECK(first.id == 711 && first.stream.empty() && first.license.empty());
        CHECK(first.kind == (id == 625 ? ATL::detail::AxDialogInit::Kind::Stream : ATL::detail::AxDialogInit::Kind::New));
        if (id == 622) {
            CHECK(built.initialization[1].id == 712 && built.initialization[1].kind == ATL::detail::AxDialogInit::Kind::Stream);
            CHECK(built.initialization[1].stream.size() == 4 && built.initialization[1].stream[0] == 42);
        }
        CHECK(std::memcmp(initBytes, snapshot.data(), initSize) == 0);
        AxDialogTemplate split;
        if (id == 621) {
            CHECK(SplitAxDialogTemplate(bytes, size, split, initBytes, initSize) == ERROR_NOT_SUPPORTED);
        } else {
            CHECK(SplitAxDialogTemplate(bytes, size, split, initBytes, initSize) == ERROR_SUCCESS &&
                split.converted && sameInitialization(split, built));
        }
        for (size_t cut = 0; cut < minimum; ++cut) {
            CHECK(BuildAxDialogTemplate(bytes, size, built, initBytes, cut) == ERROR_INVALID_DATA && built.storage.empty() && built.initialization.empty());
        }
        std::vector<BYTE> bad = snapshot;
        const DWORD oversized = MAXDWORD;
        std::memcpy(bad.data() + 4, &oversized, sizeof(oversized));
        CHECK(BuildAxDialogTemplate(bytes, size, built, bad.data(), bad.size()) == ERROR_INVALID_DATA);
        bad = snapshot; const WORD ordinary = 714;
        std::memcpy(bad.data(), &ordinary, sizeof(ordinary));
        CHECK(BuildAxDialogTemplate(bytes, size, built, bad.data(), bad.size()) == ERROR_INVALID_DATA);
        if (id == 620) {
            bad = snapshot; const WORD stream = 0x376;
            std::memcpy(bad.data() + 2, &stream, sizeof(stream));
            CHECK(BuildAxDialogTemplate(bytes, size, built, bad.data(), bad.size()) == ERROR_INVALID_DATA);
        }
    }
    for (WORD id : {WORD(631), WORD(632), WORD(633), WORD(636)}) {
        const HRSRC dialog = ::FindResourceW(instance, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(5));
        const HRSRC init = ::FindResourceW(instance, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(240));
        CHECK(dialog && init);
        const auto bytes = static_cast<const BYTE*>(::LockResource(::LoadResource(instance, dialog)));
        const auto initBytes = static_cast<const BYTE*>(::LockResource(::LoadResource(instance, init)));
        const DWORD size = ::SizeofResource(instance, dialog), initSize = ::SizeofResource(instance, init);
        const std::vector<BYTE> snapshot(initBytes, initBytes + initSize);
        const size_t minimum = id == 632 ? 2614 : id == 633 ? 2598 : id == 636 ? 5146 : 2574;
        CHECK(initSize >= minimum && initSize - minimum <= 3);
        AxDialogTemplate built;
        CHECK(BuildAxDialogTemplate(bytes, size, built, initBytes, initSize) == ERROR_SUCCESS && built.converted);
        CHECK(built.initialization.size() == (id == 633 ? 3U : id == 636 ? 2U : 1U));
        CHECK(built.initialization[0].id == 711 && built.initialization[0].kind == ATL::detail::AxDialogInit::Kind::Storage);
        CHECK(built.initialization[0].stream.size() == 2560);
        CHECK(built.initialization[0].license == (id == 632 ? L"OpenATL-test-license" : L""));
        if (id == 633) {
            CHECK(built.initialization[1].id == 712 && built.initialization[1].kind == ATL::detail::AxDialogInit::Kind::New);
            CHECK(built.initialization[2].id == 713 && built.initialization[2].kind == ATL::detail::AxDialogInit::Kind::Stream);
        }
        CHECK(std::memcmp(initBytes, snapshot.data(), initSize) == 0);
        AxDialogTemplate split;
        if (id == 632) {
            CHECK(SplitAxDialogTemplate(bytes, size, split, initBytes, initSize) == ERROR_NOT_SUPPORTED);
        } else {
            CHECK(SplitAxDialogTemplate(bytes, size, split, initBytes, initSize) == ERROR_SUCCESS &&
                split.converted && sameInitialization(split, built));
        }
        for (size_t cut = 0; cut < minimum; ++cut) {
            CHECK(BuildAxDialogTemplate(bytes, size, built, initBytes, cut) == ERROR_INVALID_DATA && built.storage.empty() && built.initialization.empty());
        }
        std::vector<BYTE> bad = snapshot; const DWORD oversized = MAXDWORD;
        std::memcpy(bad.data() + 4, &oversized, sizeof(oversized));
        CHECK(BuildAxDialogTemplate(bytes, size, built, bad.data(), bad.size()) == ERROR_INVALID_DATA);
        bad = snapshot; std::memcpy(bad.data() + 8, &oversized, sizeof(oversized));
        CHECK(BuildAxDialogTemplate(bytes, size, built, bad.data(), bad.size()) == ERROR_INVALID_DATA);
    }
    for (WORD id : {WORD(637), WORD(638)}) {
        const HRSRC dialog = ::FindResourceW(instance, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(5));
        const HRSRC init = ::FindResourceW(instance, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(240));
        CHECK(dialog && init);
        const auto bytes = static_cast<const BYTE*>(::LockResource(::LoadResource(instance, dialog)));
        const auto initBytes = static_cast<const BYTE*>(::LockResource(::LoadResource(instance, init)));
        const DWORD size = ::SizeofResource(instance, dialog), initSize = ::SizeofResource(instance, init);
        const std::vector<BYTE> snapshot(initBytes, initBytes + initSize);
        AxDialogTemplate built;
        CHECK(BuildAxDialogTemplate(bytes, size, built, initBytes, initSize) == ERROR_SUCCESS && !built.converted);
        CHECK(built.size == size && !std::memcmp(bytes, built.storage.data(), size) && built.initialization.size() == 6);
        CHECK(built.initialization[0].id == 711 && built.initialization[0].kind == ATL::detail::AxDialogInit::Kind::ListString);
        CHECK(built.initialization[1].id == 711 && !std::strcmp(reinterpret_cast<const char*>(built.initialization[1].stream.data()), "alpha"));
        CHECK(built.initialization[2].id == 712 && built.initialization[5].stream.size() == 1);
        AxDialogTemplate split;
        CHECK(SplitAxDialogTemplate(bytes, size, split, initBytes, initSize) == ERROR_SUCCESS && !split.converted &&
            split.size == size && !std::memcmp(bytes, split.Get(), size) && sameInitialization(split, built));
        const size_t minimum = 74; // Six headers, their counted bytes and the terminator.
        CHECK(initSize >= minimum && initSize - minimum <= 3);
        for (size_t cut = 0; cut < minimum; ++cut) {
            CHECK(BuildAxDialogTemplate(bytes, size, built, initBytes, cut) == ERROR_INVALID_DATA && built.storage.empty() && built.initialization.empty());
        }
        CHECK(!std::memcmp(initBytes, snapshot.data(), initSize));
        auto bad = snapshot; const WORD unknown = 799;
        std::memcpy(bad.data(), &unknown, sizeof(unknown));
        CHECK(BuildAxDialogTemplate(bytes, size, built, bad.data(), bad.size()) == ERROR_INVALID_DATA);
        bad = snapshot; const WORD list = 0x401;
        std::memcpy(bad.data() + 2, &list, sizeof(list)); // Conflicting record kinds for one ID.
        CHECK(BuildAxDialogTemplate(bytes, size, built, bad.data(), bad.size()) == ERROR_INVALID_DATA);
    }
    return true;
}
bool DialogStringContents(HWND window) {
    const HWND list = ::GetDlgItem(window, 711), combo = ::GetDlgItem(window, 712);
    CHECK(list && combo && ::SendMessageW(list, LB_GETCOUNT, 0, 0) == 2 && ::SendMessageW(combo, CB_GETCOUNT, 0, 0) == 4);
    WCHAR text[32]{};
    CHECK(::SendMessageW(list, LB_GETTEXT, 0, reinterpret_cast<LPARAM>(text)) == 5 && !::wcscmp(text, L"alpha"));
    CHECK(::SendMessageW(list, LB_GETTEXT, 1, reinterpret_cast<LPARAM>(text)) == 4 && !::wcscmp(text, L"beta"));
    const bool upper = (::GetWindowLongPtrW(combo, GWL_STYLE) & CBS_UPPERCASE) != 0;
    CHECK(::SendMessageW(combo, CB_GETLBTEXT, 0, reinterpret_cast<LPARAM>(text)) == 3 && !::wcscmp(text, upper ? L"LOW" : L"low"));
    CHECK(::SendMessageW(combo, CB_GETLBTEXT, 1, reinterpret_cast<LPARAM>(text)) == 4 && !::wcscmp(text, upper ? L"HIGH" : L"high"));
    const char encoded[] = {static_cast<char>(0xC3), static_cast<char>(0xA9), 0};
    WCHAR expected[16]{};
    const int count = ::MultiByteToWideChar(CP_ACP, 0, encoded, 2, expected, 15);
    CHECK(count > 0);
    if (upper) CHECK(::CharUpperBuffW(expected, count) == static_cast<DWORD>(count));
    CHECK(::SendMessageW(combo, CB_GETLBTEXT, 2, reinterpret_cast<LPARAM>(text)) == count && !::wcscmp(text, expected));
    CHECK(::SendMessageW(combo, CB_GETLBTEXT, 3, reinterpret_cast<LPARAM>(text)) == 0 && !text[0]);
    CHECK(::SendMessageW(combo, CB_GETCURSEL, 0, 0) == CB_ERR);
    return true;
}
struct FreeDialogState { bool modal = false, checkStrings = false, good = true; int initialized = 0; HWND window = nullptr; };
INT_PTR CALLBACK FreeDialogProc(HWND window, UINT message, WPARAM, LPARAM parameter) {
    if (message != WM_INITDIALOG) return FALSE;
    auto state = reinterpret_cast<FreeDialogState*>(parameter);
    ++state->initialized; state->window = window;
    if (state->checkStrings) state->good = DialogStringContents(window);
    if (state->modal) ::EndDialog(window, 83);
    return TRUE;
}
class PlainDialog : public ATL::CAxDialogImpl<PlainDialog> {
public:
    enum { IDD = 602 };
    int resource = IDD;
    int GetIDD() const { return resource; }
    bool modal = false, modeRejected = false;
    bool good = true;
    int initialized = 0, finalized = 0;
    LPARAM parameter = 0;
    LRESULT Init(UINT, WPARAM, LPARAM value, BOOL&) {
        ++initialized; parameter = value;
        if (resource == 637 || resource == 638) good = DialogStringContents(m_hWnd);
        if (modal) { modeRejected = !DestroyWindow(); EndDialog(73); }
        return TRUE;
    }
    void OnFinalMessage(HWND) override { ++finalized; }
    BEGIN_MSG_MAP(PlainDialog)
        MESSAGE_HANDLER(WM_INITDIALOG, Init)
    END_MSG_MAP()
};
class InitFailureDialog : public ATL::CAxDialogImpl<InitFailureDialog> {
public:
    enum { IDD = 602 };
    int initialized = 0, finalized = 0;
    HRESULT AdviseSinkMap(bool advise) noexcept { return advise ? E_ACCESSDENIED : S_OK; }
    LRESULT Init(UINT, WPARAM, LPARAM, BOOL&) { ++initialized; return TRUE; }
    void OnFinalMessage(HWND) override { ++finalized; }
    BEGIN_MSG_MAP(InitFailureDialog)
        MESSAGE_HANDLER(WM_INITDIALOG, Init)
    END_MSG_MAP()
};
struct DialogRetirement { int initialized = 0, finalized = 0, destroyed = 0; bool good = true; };
class CloseFailureDialog : public ATL::CAxDialogImpl<CloseFailureDialog> {
public:
    enum { IDD = 602 };
    bool fail = false, preserved = false;
    HRESULT AdviseSinkMap(bool advise) noexcept { return !advise && fail ? E_ACCESSDENIED : S_OK; }
    LRESULT Init(UINT, WPARAM, LPARAM, BOOL&) {
        if (m_bModal) {
            fail = true; preserved = !EndDialog(1) && ::IsWindow(this->m_hWnd);
            fail = false; EndDialog(82);
        }
        return TRUE;
    }
    BEGIN_MSG_MAP(CloseFailureDialog)
        MESSAGE_HANDLER(WM_INITDIALOG, Init)
    END_MSG_MAP()
};
class DeletingDialog : public ATL::CAxDialogImpl<DeletingDialog> {
    DialogRetirement& state_;
    bool modal_;
    int resource_;
public:
    enum { IDD = 602 };
    DeletingDialog(DialogRetirement& state, bool modal, int resource = IDD) : state_(state), modal_(modal), resource_(resource) {}
    int GetIDD() const { return resource_; }
    ~DeletingDialog() { ++state_.destroyed; }
    LRESULT Init(UINT, WPARAM, LPARAM, BOOL&) {
        ++state_.initialized;
        if (resource_ != IDD) state_.good = DialogStringContents(m_hWnd);
        if (modal_) EndDialog(91); else DestroyWindow();
        return TRUE;
    }
    void OnFinalMessage(HWND) override { ++state_.finalized; delete this; }
    BEGIN_MSG_MAP(DeletingDialog)
        MESSAGE_HANDLER(WM_INITDIALOG, Init)
    END_MSG_MAP()
};
struct OwnerDrawState {
    int initialized = 0, measured = 0, listMeasured = 0, beforeInit = 0, drawn = 0, finalized = 0, deleted = 0;
    bool good = true, modal = false, nested = false, cancel = false, deleteOnFinal = false, recursiveInit = false;
    int resource = 649;
};
class OwnerDrawDialog : public ATL::CAxDialogImpl<OwnerDrawDialog> {
    OwnerDrawState& state_;
    bool nested_ = false;
public:
    enum { IDD = 649 };
    explicit OwnerDrawDialog(OwnerDrawState& state) : state_(state) {}
    ~OwnerDrawDialog() { ++state_.deleted; }
    int GetIDD() const { return state_.resource; }
    LRESULT Measure(UINT, WPARAM w, LPARAM l, BOOL&) {
        auto item = reinterpret_cast<MEASUREITEMSTRUCT*>(l);
        state_.good = state_.good && item && item->CtlID == w && (item->CtlID == 711 || item->CtlID == 712);
        if (!item) return FALSE;
        ++state_.measured;
        if (item->CtlID == 711) ++state_.listMeasured;
        // Fixed-height measurement can run while the child is still being
        // created. Query its contents only after dialog initialization.
        if (!state_.initialized) ++state_.beforeInit;
        item->itemWidth = 80; item->itemHeight = 21;
        if (state_.nested && !nested_) {
            nested_ = true;
            const auto creation = ATL::detail::AxDialogCreation::Current();
            PlainDialog nested; nested.resource = 637; nested.modal = true;
            state_.good = nested.DoModal(nullptr, 2112) == 73 && nested.good && nested.parameter == 2112 &&
                ATL::detail::AxDialogCreation::Current() == creation && state_.good;
        }
        if (state_.recursiveInit && state_.resource == 651 && item->CtlID == 711 && state_.listMeasured == 2 && !state_.initialized)
            ::SendMessageW(m_hWnd, WM_INITDIALOG, 0, 0);
        return TRUE;
    }
    LRESULT Draw(UINT, WPARAM, LPARAM l, BOOL&) {
        auto item = reinterpret_cast<DRAWITEMSTRUCT*>(l);
        if (!item || (item->CtlID != 711 && item->CtlID != 712)) return FALSE;
        ++state_.drawn;
        ::FillRect(item->hDC, &item->rcItem, ::GetSysColorBrush(COLOR_WINDOW));
        if (item->itemID != static_cast<UINT>(-1)) {
            WCHAR text[32]{};
            const LRESULT count = ::SendMessageW(item->hwndItem, item->CtlID == 711 ? LB_GETTEXT : CB_GETLBTEXT,
                item->itemID, reinterpret_cast<LPARAM>(text));
            state_.good = state_.good && count >= 0 && count < 32;
            if (count >= 0 && count < 32) ::TextOutW(item->hDC, item->rcItem.left + 2, item->rcItem.top, text, static_cast<int>(count));
        }
        return TRUE;
    }
    bool Paint() {
        const int before = state_.drawn;
        HDC dc = ::CreateCompatibleDC(nullptr);
        BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = 320; info.bmiHeader.biHeight = -240;
        info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
        void* pixels = nullptr;
        HBITMAP bitmap = dc ? ::CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0) : nullptr;
        if (!bitmap) { if (dc) ::DeleteDC(dc); return false; }
        HGDIOBJ previous = ::SelectObject(dc, bitmap);
        ::SendMessageW(::GetDlgItem(m_hWnd, 711), WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc), PRF_CLIENT);
        ::SelectObject(dc, previous); ::DeleteObject(bitmap); ::DeleteDC(dc);
        return state_.drawn > before;
    }
    LRESULT Init(UINT, WPARAM, LPARAM parameter, BOOL&) {
        ++state_.initialized;
        const HWND list = ::GetDlgItem(m_hWnd, 711), combo = ::GetDlgItem(m_hWnd, 712);
        WCHAR text[32]{};
        state_.good = state_.good && parameter == 1773 && state_.measured > 0 &&
            ::SendMessageW(list, LB_GETCOUNT, 0, 0) == 2 && ::SendMessageW(combo, CB_GETCOUNT, 0, 0) == 2;
        state_.good = state_.good && ::SendMessageW(list, LB_GETTEXT, 0, reinterpret_cast<LPARAM>(text)) == 5 && !::wcscmp(text, L"alpha");
        state_.good = state_.good && ::SendMessageW(combo, CB_GETLBTEXT, 0, reinterpret_cast<LPARAM>(text)) == 3 && !::wcscmp(text, L"low");
        state_.good = state_.good && ::SendMessageW(list, LB_GETITEMHEIGHT, 0, 0) == 21 && ::SendMessageW(combo, CB_GETITEMHEIGHT, 0, 0) == 21;
        state_.good = Paint() && state_.good;
        if (state_.modal) EndDialog(93);
        else if (state_.cancel) DestroyWindow();
        return TRUE;
    }
    void OnFinalMessage(HWND) override {
        ++state_.finalized;
        if (state_.deleteOnFinal) delete this;
    }
    BEGIN_MSG_MAP(OwnerDrawDialog)
        MESSAGE_HANDLER(WM_MEASUREITEM, Measure)
        MESSAGE_HANDLER(WM_DRAWITEM, Draw)
        MESSAGE_HANDLER(WM_INITDIALOG, Init)
    END_MSG_MAP()
};
bool ownerDrawDialogs() {
    for (int resource : {649, 650, 651}) for (bool modal : {false, true}) {
        OwnerDrawState state; state.resource = resource; state.modal = modal;
        state.nested = true; state.recursiveInit = resource == 651;
        OwnerDrawDialog dialog(state);
        if (modal) CHECK(dialog.DoModal(nullptr, 1773) == 93);
        else {
            CHECK(dialog.Create(nullptr, 1773));
            CHECK(dialog.Paint()); // The creation context must already have detached.
            CHECK(dialog.DestroyWindow());
        }
        CHECK(state.good && state.initialized == 1 && state.measured >= 2 && state.drawn > 0 && state.finalized == 1);
        CHECK(state.beforeInit >= 1);
        CHECK(!ATL::detail::AxDialogCreation::Current());
    }
    for (bool modal : {false, true}) {
        OwnerDrawState state; state.modal = modal; state.cancel = true;
        OwnerDrawDialog dialog(state);
        if (modal) CHECK(dialog.DoModal(nullptr, 1773) == 93);
        else CHECK(!dialog.Create(nullptr, 1773));
        CHECK(state.good && state.measured >= 2 && state.initialized == 1 && state.finalized == 1 && !dialog.m_hWnd);
        if (!modal) CHECK(::GetLastError() == ERROR_CANCELLED);
        CHECK(!ATL::detail::AxDialogCreation::Current());
        state.cancel = false; state.modal = false;
        CHECK(dialog.Create(nullptr, 1773) && state.good && dialog.DestroyWindow() && state.finalized == 2);
        OwnerDrawState deleted; deleted.modal = modal; deleted.cancel = true; deleted.deleteOnFinal = true;
        auto owner = new OwnerDrawDialog(deleted);
        if (modal) CHECK(owner->DoModal(nullptr, 1773) == 93);
        else CHECK(!owner->Create(nullptr, 1773));
        CHECK(deleted.good && deleted.initialized == 1 && deleted.finalized == 1 && deleted.deleted == 1 && !ATL::detail::AxDialogCreation::Current());
    }
    return true;
}
bool plainDialogs() {
    const HINSTANCE instance = ATL::_AtlBaseModule.GetResourceInstance();
    FreeDialogState free;
    HWND window = ATL::AtlAxCreateDialog(instance, MAKEINTRESOURCEW(602), nullptr, FreeDialogProc, reinterpret_cast<LPARAM>(&free));
    CHECK(window && free.window == window && free.initialized == 1 && ::GetDlgItem(window, 702));
    CHECK(::DestroyWindow(window));
    free.modal = true;
    CHECK(ATL::AtlAxDialogBox(instance, L"OPENATL_PLAIN", nullptr, FreeDialogProc, reinterpret_cast<LPARAM>(&free)) == 83 && free.initialized == 2);
    CHECK(!ATL::AtlAxCreateDialog(instance, nullptr, nullptr, FreeDialogProc, 0) && ::GetLastError() == ERROR_INVALID_PARAMETER);
    CHECK(ATL::AtlAxDialogBox(instance, MAKEINTRESOURCEW(999), nullptr, FreeDialogProc, 0) == -1 && ::GetLastError() != ERROR_SUCCESS);
    free = {};
    window = ATL::AtlAxCreateDialog(instance, MAKEINTRESOURCEW(605), nullptr, FreeDialogProc, reinterpret_cast<LPARAM>(&free));
    CHECK(window && free.initialized == 1 && ::DestroyWindow(window));
    free.modal = true;
    CHECK(ATL::AtlAxDialogBox(instance, MAKEINTRESOURCEW(605), nullptr, FreeDialogProc, reinterpret_cast<LPARAM>(&free)) == 83 && free.initialized == 2);
    for (WORD id : {WORD(604), WORD(609)}) {
        CHECK(!ATL::AtlAxCreateDialog(instance, MAKEINTRESOURCEW(id), nullptr, FreeDialogProc, 0) && ::GetLastError() == ERROR_NOT_SUPPORTED);
        CHECK(ATL::AtlAxDialogBox(instance, MAKEINTRESOURCEW(id), nullptr, FreeDialogProc, 0) == -1 && ::GetLastError() == ERROR_NOT_SUPPORTED);
    }
    for (WORD id : {WORD(615), WORD(616), WORD(617), WORD(618)}) {
        const DWORD error = id == 617 ? ERROR_NOT_SUPPORTED : ERROR_INVALID_DATA;
        CHECK(!ATL::AtlAxCreateDialog(instance, MAKEINTRESOURCEW(id), nullptr, FreeDialogProc, 0) && ::GetLastError() == error);
        CHECK(ATL::AtlAxDialogBox(instance, MAKEINTRESOURCEW(id), nullptr, FreeDialogProc, 0) == -1 && ::GetLastError() == error);
    }
    for (WORD id : {WORD(623), WORD(624), WORD(626)}) {
        const DWORD error = id == 623 ? ERROR_NOT_SUPPORTED : ERROR_INVALID_DATA;
        CHECK(!ATL::AtlAxCreateDialog(instance, MAKEINTRESOURCEW(id), nullptr, FreeDialogProc, 0) && ::GetLastError() == error);
        CHECK(ATL::AtlAxDialogBox(instance, MAKEINTRESOURCEW(id), nullptr, FreeDialogProc, 0) == -1 && ::GetLastError() == error);
    }
    CHECK(!ATL::AtlAxCreateDialog(instance, MAKEINTRESOURCEW(635), nullptr, FreeDialogProc, 0) && ::GetLastError() == ERROR_INVALID_DATA);
    CHECK(ATL::AtlAxDialogBox(instance, MAKEINTRESOURCEW(635), nullptr, FreeDialogProc, 0) == -1 && ::GetLastError() == ERROR_INVALID_DATA);
    PlainDialog plain; plain.modal = true;
    CHECK(plain.GetIDD() == 602 && plain.GetDialogProc());
    CHECK(plain.DoModal(nullptr, 1234) == 73 && plain.initialized == 1 && plain.finalized == 1 && plain.parameter == 1234 && plain.modeRejected && !plain.m_hWnd);
    plain.modal = false; RECT ignored{};
    CHECK(plain.Create(nullptr, ignored, 5678) && plain.parameter == 5678 && plain.initialized == 2);
    CHECK(!plain.Create(nullptr) && ::GetLastError() == ERROR_ALREADY_EXISTS);
    CHECK(!plain.EndDialog(0) && ::IsWindow(plain.m_hWnd));
    CHECK(plain.DestroyWindow() && plain.finalized == 2 && !plain.m_hWnd);
    InitFailureDialog failed;
    CHECK(!failed.Create(nullptr) && ::GetLastError() == ERROR_ACCESS_DENIED && !failed.initialized && failed.finalized == 1 && !failed.m_hWnd);
    CHECK(failed.DoModal(nullptr) == -1 && ::GetLastError() == ERROR_ACCESS_DENIED && !failed.initialized && failed.finalized == 2 && !failed.m_hWnd);
    CloseFailureDialog retry;
    CHECK(retry.Create(nullptr)); retry.fail = true;
    CHECK(!retry.DestroyWindow() && ::GetLastError() == ERROR_ACCESS_DENIED && ::IsWindow(retry.m_hWnd));
    retry.fail = false; CHECK(retry.DestroyWindow() && !retry.m_hWnd);
    CHECK(retry.DoModal(nullptr) == 82 && retry.preserved && !retry.m_hWnd);
    DialogRetirement modeless, modal;
    CHECK(!(new DeletingDialog(modeless, false))->Create(nullptr));
    CHECK(modeless.initialized == 1 && modeless.finalized == 1 && modeless.destroyed == 1);
    CHECK((new DeletingDialog(modal, true))->DoModal(nullptr) == 91);
    CHECK(modal.initialized == 1 && modal.finalized == 1 && modal.destroyed == 1);
    for (WORD id : {WORD(637), WORD(638)}) {
        PlainDialog strings; strings.resource = id;
        CHECK(strings.Create(nullptr, 1773) && strings.good && strings.parameter == 1773 && strings.initialized == 1);
        CHECK(strings.DestroyWindow() && strings.finalized == 1);
        strings.modal = true;
        CHECK(strings.DoModal(nullptr, 1774) == 73 && strings.good && strings.parameter == 1774 && strings.initialized == 2 && strings.finalized == 2);
        strings.modal = false; strings.resource = 639;
        CHECK(!strings.Create(nullptr) && ::GetLastError() == ERROR_INVALID_DATA && strings.initialized == 2);
        strings.resource = id;
        CHECK(strings.Create(nullptr) && strings.good && strings.DestroyWindow() && strings.initialized == 3);
    }
    for (WORD id : {WORD(639), WORD(640), WORD(641), WORD(642), WORD(643), WORD(644), WORD(645), WORD(646)}) {
        const DWORD error = id == 641 || id == 645 || id == 646 ? ERROR_NOT_SUPPORTED : ERROR_INVALID_DATA;
        CHECK(!ATL::AtlAxCreateDialog(instance, MAKEINTRESOURCEW(id), nullptr, FreeDialogProc, 0) && ::GetLastError() == error);
        CHECK(ATL::AtlAxDialogBox(instance, MAKEINTRESOURCEW(id), nullptr, FreeDialogProc, 0) == -1 && ::GetLastError() == error);
    }
    free = {}; free.checkStrings = true;
    window = ATL::AtlAxCreateDialog(instance, L"OPENATL_STRINGS", nullptr, FreeDialogProc, reinterpret_cast<LPARAM>(&free));
    CHECK(window && free.good && free.initialized == 1 && ::DestroyWindow(window));
    free.modal = true;
    CHECK(ATL::AtlAxDialogBox(instance, L"OPENATL_STRINGS", nullptr, FreeDialogProc, reinterpret_cast<LPARAM>(&free)) == 83 && free.good && free.initialized == 2);
    DialogRetirement stringModeless, stringModal;
    CHECK(!(new DeletingDialog(stringModeless, false, 637))->Create(nullptr));
    CHECK(stringModeless.good && stringModeless.initialized == 1 && stringModeless.finalized == 1 && stringModeless.destroyed == 1);
    CHECK((new DeletingDialog(stringModal, true, 638))->DoModal(nullptr) == 91);
    CHECK(stringModal.good && stringModal.initialized == 1 && stringModal.finalized == 1 && stringModal.destroyed == 1);
    return true;
}
class Source : public IConnectionPointContainer, public IConnectionPoint {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** value) override {
        if (!value) return E_POINTER; *value = nullptr;
        if (iid == IID_IUnknown || iid == IID_IConnectionPointContainer) *value = static_cast<IConnectionPointContainer*>(this);
        else if (iid == IID_IConnectionPoint) *value = static_cast<IConnectionPoint*>(this);
        else return E_NOINTERFACE;
        AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { const auto n = --refs; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE EnumConnectionPoints(IEnumConnectionPoints**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE FindConnectionPoint(REFIID iid, IConnectionPoint** value) override {
        if (!value) return E_POINTER; *value = nullptr;
        return iid == EventIID ? QueryInterface(IID_IConnectionPoint, reinterpret_cast<void**>(value)) : CONNECT_E_NOCONNECTION;
    }
    HRESULT STDMETHODCALLTYPE GetConnectionInterface(IID* value) override { if (!value) return E_POINTER; *value = EventIID; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetConnectionPointContainer(IConnectionPointContainer** value) override {
        return QueryInterface(IID_IConnectionPointContainer, reinterpret_cast<void**>(value));
    }
    HRESULT STDMETHODCALLTYPE Advise(IUnknown* value, DWORD* cookie) override {
        if (!value || !cookie) return E_POINTER;
        if (sink) return CONNECT_E_ADVISELIMIT;
        HRESULT hr = value->QueryInterface(EventIID, reinterpret_cast<void**>(&sink));
        if (SUCCEEDED(hr)) { *cookie = 42; ++advises; }
        return hr;
    }
    HRESULT STDMETHODCALLTYPE Unadvise(DWORD cookie) override {
        if (cookie != 42 || !sink) return CONNECT_E_NOCONNECTION;
        sink.Release(); ++unadvises; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE EnumConnections(IEnumConnections**) override { return E_NOTIMPL; }
    HRESULT Fire(DISPID id, DISPPARAMS& params, UINT* argument = nullptr) {
        ATL::CComPtr<IDispatch> retained = sink;
        return retained ? retained->Invoke(id, IID_NULL, 0, DISPATCH_METHOD, &params, nullptr, nullptr, argument) : CONNECT_E_NOCONNECTION;
    }
    ULONG refs = 1;
    int advises = 0, unadvises = 0;
private:
    ATL::CComPtr<IDispatch> sink;
};
// Keep the original WTL presence guard: MSVC inspects the member, while the
// existing MinGW compatibility macro includes the guarded body unconditionally.
template<class T> HRESULT WtlAdviseSinkMap(T* object, bool advise) {
    HRESULT result = E_NOTIMPL;
    __if_exists(T::_GetSinkMapFinder) {
        result = ATL::AtlAdviseSinkMap(object, advise);
    }
    return result;
}
class Sink : public ATL::IDispEventSimpleImpl<7, Sink, &EventIID> {
public:
    IUnknown* mappedSource = nullptr; // Borrowed for the map test's scope.
    HRESULT GetDlgControl(int id, REFIID iid, void** output) {
        if (!output) return E_POINTER;
        *output = nullptr;
        return id == 7 && mappedSource ? mappedSource->QueryInterface(iid, output) : E_FAIL;
    }
    inline static ATL::_ATL_FUNC_INFO scalar = {CC_STDCALL, VT_EMPTY, 3, {VT_I4, VT_BOOL, VT_BSTR}};
    inline static ATL::_ATL_FUNC_INFO ref = {CC_STDCALL, VT_EMPTY, 1, {VT_VARIANT | VT_BYREF}};
    inline static ATL::_ATL_FUNC_INFO throwing = {CC_STDCALL, VT_EMPTY, 0, {}};
    inline static ATL::_ATL_FUNC_INFO doubleRef = {CC_STDCALL, VT_EMPTY, 1, {VT_BYREF | VT_R4}};
    BEGIN_SINK_MAP(Sink)
        SINK_ENTRY_INFO(7, EventIID, 1, Scalar, &scalar)
        SINK_ENTRY_INFO(7, EventIID, 2, Ref, &ref)
        SINK_ENTRY_INFO(7, EventIID, 3, Throw, &throwing)
        SINK_ENTRY_INFO(7, EventIID, 4, Double, &doubleRef)
    END_SINK_MAP()
    void __stdcall Scalar(long number, VARIANT_BOOL enabled, BSTR text) {
        good = number == 123 && enabled == VARIANT_TRUE && text && std::wcscmp(text, L"event\u6771\u4eac") == 0;
        ++calls;
    }
    void __stdcall Ref(VARIANT* value) { value->lVal += 1; }
    void __stdcall Throw() { throw std::runtime_error("handler failure"); }
    void __stdcall Double(double*) { ++doubleCalls; }
    int doubleCalls = 0;
    int calls = 0;
    bool good = false;
};
bool events() {
    struct SinklessOwner {} sinkless;
    CHECK(ATL::AtlAdviseSinkMap(&sinkless, true) == S_OK);
    CHECK(ATL::AtlAdviseSinkMap(&sinkless, false) == S_OK);
    CHECK(ATL::AtlAdviseSinkMap<SinklessOwner>(nullptr, true) == E_POINTER);
#ifdef _OPENATL_DEFINED_MSC_VER
    CHECK(WtlAdviseSinkMap(&sinkless, true) == S_OK);
#else
    CHECK(WtlAdviseSinkMap(&sinkless, true) == E_NOTIMPL);
#endif
    CHECK(Sink::_GetSinkMapFinder() == Sink::_GetSinkMap());
    CHECK(WtlAdviseSinkMap<Sink>(nullptr, true) == E_POINTER);
    ATL::CComPtr<Source> source; source.Attach(new Source);
    ATL::CComPtr<Source> other; other.Attach(new Source);
    auto unknown = static_cast<IConnectionPointContainer*>(source.p);
    {
        ATL::CComPtr<IConnectionPoint> secondary;
        CHECK(source->FindConnectionPoint(EventIID, &secondary) == S_OK);
        ATL::CComQIPtr<IUnknown> identity(secondary.p);
        CHECK(identity.p == static_cast<IUnknown*>(unknown));
        {
            ATL::CComQIPtr<IUnknown, &IID_IConnectionPoint> explicitlySelected(unknown);
            CHECK(static_cast<void*>(explicitlySelected.p) == static_cast<void*>(secondary.p));
        }
        const ULONG references = source->refs;
        identity = secondary.p;
        CHECK(identity.p == static_cast<IUnknown*>(unknown) && source->refs == references);
        ATL::CComQIPtr<IUnknown> fromSmartPointer = secondary;
        CHECK(fromSmartPointer.p == identity.p);
        fromSmartPointer = secondary;
        CHECK(fromSmartPointer.p == identity.p && source->refs == references + 1);
    }
    CHECK(source->refs == 1);
    {
        Sink sink;
        CHECK(sink.DispEventAdvise(unknown) == S_OK);
        CHECK(sink.DispEventAdvise(unknown) == CONNECT_E_ADVISELIMIT);
        ATL::CComVariant args[3] = {ATL::CComVariant(L"event\u6771\u4eac"), ATL::CComVariant(VARIANT_TRUE), ATL::CComVariant(123L)};
        args[1].vt = VT_BOOL; args[1].boolVal = VARIANT_TRUE;
        DISPPARAMS params = {args, nullptr, 3, 0};
        CHECK(source->Fire(1, params) == S_OK && sink.good && sink.calls == 1);
        Sink::scalar.pVarTypes[2] = VT_I4;
        CHECK(source->Fire(1, params) == E_INVALIDARG && sink.calls == 1);
        Sink::scalar.pVarTypes[2] = VT_BSTR;
        params.cNamedArgs = 1;
        CHECK(source->Fire(1, params) == DISP_E_NONAMEDARGS);
        params.cNamedArgs = 0;
        CHECK(source->Fire(99, params) == DISP_E_MEMBERNOTFOUND);
        params.cArgs = 2; CHECK(source->Fire(1, params) == DISP_E_BADPARAMCOUNT); params.cArgs = 3;
        args[2] = L"not a number";
        UINT argument = 99;
        CHECK(source->Fire(1, params, &argument) == DISP_E_TYPEMISMATCH && argument == 2 && sink.calls == 1);
        ATL::CComVariant value(40L);
        VARIANTARG reference{}; reference.vt = VT_VARIANT | VT_BYREF; reference.pvarVal = &value;
        DISPPARAMS byref = {&reference, nullptr, 1, 0};
        CHECK(source->Fire(2, byref) == S_OK && value.lVal == 41);
        reference.pvarVal = nullptr;
        CHECK(source->Fire(2, byref) == DISP_E_TYPEMISMATCH);
        DISPPARAMS empty{};
        CHECK(source->Fire(3, empty) == DISP_E_EXCEPTION);
        alignas(double) float number = 2.0f;
        VARIANTARG floating{}; floating.vt = VT_BYREF | VT_R4; floating.pfltVal = &number;
        DISPPARAMS floatingParams = {&floating, nullptr, 1, 0};
        CHECK(source->Fire(4, floatingParams) == E_INVALIDARG && sink.doubleCalls == 0);
        double wide = 2.0;
        Sink::doubleRef.pVarTypes[0] = VT_BYREF | VT_R8;
        floating.vt = VT_BYREF | VT_R8; floating.pdblVal = &wide;
        CHECK(source->Fire(4, floatingParams) == S_OK && sink.doubleCalls == 1);
        CHECK(sink.DispEventUnadvise(static_cast<IConnectionPointContainer*>(other.p)) == CONNECT_E_NOCONNECTION);
        CHECK(sink.DispEventUnadvise(unknown) == S_OK && source->unadvises == 1);
        CHECK(source->Fire(1, params) == CONNECT_E_NOCONNECTION);
        CHECK(sink.DispEventAdvise(unknown) == S_OK);
    }
    CHECK(source->advises == 2 && source->unadvises == 2 && source->refs == 1);
    {
        Sink sink;
        sink.mappedSource = unknown;
        CHECK(WtlAdviseSinkMap(&sink, true) == S_OK);
        CHECK(WtlAdviseSinkMap(&sink, true) == CONNECT_E_ADVISELIMIT);
        ATL::CComVariant value(51L);
        VARIANTARG reference{}; reference.vt = VT_VARIANT | VT_BYREF; reference.pvarVal = &value;
        DISPPARAMS params = {&reference, nullptr, 1, 0};
        CHECK(source->Fire(2, params) == S_OK && value.lVal == 52);
        sink.mappedSource = nullptr; // Unadvise must use the retained point.
        CHECK(WtlAdviseSinkMap(&sink, false) == S_OK);
        CHECK(source->Fire(2, params) == CONNECT_E_NOCONNECTION);
        CHECK(WtlAdviseSinkMap(&sink, false) == S_OK);
    }
    CHECK(source->advises == 3 && source->unadvises == 3 && source->refs == 1);
    return true;
}
class BrowserSink : public ATL::CWindow, public ATL::IDispEventImpl<1, BrowserSink, &DIID_DWebBrowserEvents2> {
public:
    explicit BrowserSink(HWND parent) : ATL::CWindow(parent) {}
    BEGIN_SINK_MAP(BrowserSink)
        SINK_ENTRY_EX(1, DIID_DWebBrowserEvents2, 259, &BrowserSink::Document)
    END_SINK_MAP()
    void __stdcall Document(IDispatch*, VARIANT* value) { if (value && value->vt == VT_BSTR) ++documents; }
    int documents = 0;
};
class ControlDialog : public ATL::CAxDialogImpl<ControlDialog>,
                      public ATL::IDispEventImpl<701, ControlDialog, &DIID_DWebBrowserEvents2> {
public:
    enum { IDD = 601 };
    int resource = IDD;
    int GetIDD() noexcept { return resource; }
    bool modal = false, good = true;
    int initialized = 0, finalized = 0, documents = 0;
    LPARAM parameter = 0;
    ATL::CComPtr<IWebBrowser2> control;
    BEGIN_SINK_MAP(ControlDialog)
        SINK_ENTRY_EX(701, DIID_DWebBrowserEvents2, 259, &ControlDialog::Document)
    END_SINK_MAP()
    LRESULT Init(UINT, WPARAM, LPARAM value, BOOL&) {
        ++initialized; parameter = value;
        good = GetDlgControl(701, IID_IWebBrowser2, reinterpret_cast<void**>(&control.p)) == S_OK;
        if (resource == 606 || resource == 607 || resource == 630) {
            WCHAR name[32]{}, text[32]{};
            const HWND host = ::GetDlgItem(this->m_hWnd, 701);
            good = good && ::GetClassNameW(host, name, 32) && std::wcscmp(name, L"AtlAxWin140") == 0;
            good = good && (::GetWindowLongPtrW(host, GWL_EXSTYLE) & WS_EX_CLIENTEDGE) != 0;
            good = good && ::GetDlgCtrlID(host) == 701 && ::GetDlgItemTextW(this->m_hWnd, 704, text, 32) && std::wcscmp(text, L"resource sibling") == 0;
            if (resource != 607) good = good && ::GetWindowContextHelpId(host) == 273;
        }
        good = good && ::SetTimer(this->m_hWnd, 1, 4500, nullptr) != 0;
        ::PostMessageW(this->m_hWnd, WM_APP + 81, 0, 0);
        return TRUE;
    }
    void Finish(int result) {
        if (modal) { if (!EndDialog(result)) { good = false; ::EndDialog(this->m_hWnd, -2); } }
        else if (!DestroyWindow()) { good = false; ::DestroyWindow(this->m_hWnd); }
    }
    LRESULT Start(UINT, WPARAM, LPARAM, BOOL&) {
        ATL::CComVariant url(L"about:blank#openatl-dialog"), empty;
        if (!good || control->Navigate2(&url, &empty, &empty, &empty, &empty) != S_OK) { good = false; Finish(-2); }
        return 0;
    }
    LRESULT Timeout(UINT, WPARAM, LPARAM, BOOL&) { good = false; Finish(-2); return 0; }
    LRESULT Destroy(UINT, WPARAM, LPARAM, BOOL&) { control.Release(); return 0; }
    void Document(IDispatch*, VARIANT* value) {
        if (!value || value->vt != VT_BSTR) return;
        ++documents; ::KillTimer(this->m_hWnd, 1);
        if (modal) Finish(73);
    }
    void OnFinalMessage(HWND) override { ++finalized; }
    BEGIN_MSG_MAP(ControlDialog)
        MESSAGE_HANDLER(WM_INITDIALOG, Init)
        MESSAGE_HANDLER(WM_APP + 81, Start)
        MESSAGE_HANDLER(WM_TIMER, Timeout)
        MESSAGE_HANDLER(WM_DESTROY, Destroy)
    END_MSG_MAP()
};
class MissingControlDialog : public ATL::CAxDialogImpl<MissingControlDialog> {
public:
    enum { IDD = 603 };
    int resource = IDD;
    int GetIDD() noexcept { return resource; }
    int initialized = 0;
    LRESULT Init(UINT, WPARAM, LPARAM, BOOL&) { ++initialized; return TRUE; }
    BEGIN_MSG_MAP(MissingControlDialog)
        MESSAGE_HANDLER(WM_INITDIALOG, Init)
    END_MSG_MAP()
};
bool controlDialogs(ATL::CAtlModule& module) {
    for (WORD resource : {WORD(601), WORD(606), WORD(607), WORD(630)}) {
        ControlDialog modeless;
        modeless.resource = resource;
        CHECK(modeless.Create(nullptr, 312) && modeless.initialized == 1 && modeless.parameter == 312);
        CHECK(modeless.AdviseSinkMap(true) == CONNECT_E_ADVISELIMIT);
        const ULONGLONG deadline = ::GetTickCount64() + 6000;
        while (!modeless.documents && modeless.m_hWnd && ::GetTickCount64() < deadline) {
            MSG message{};
            while (::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                if (!modeless.IsDialogMessage(&message)) { ::TranslateMessage(&message); ::DispatchMessageW(&message); }
            }
            ::MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_ALLINPUT);
        }
        CHECK(modeless.good && modeless.documents && modeless.DestroyWindow() && modeless.finalized == 1 && !modeless.m_hWnd);
        CHECK(modeless.AdviseSinkMap(false) == S_OK && module.GetLockCount() == 0);
        ControlDialog modal; modal.modal = true; modal.resource = resource;
        CHECK(modal.DoModal(nullptr, 728) == 73 && modal.good && modal.documents && modal.initialized == 1 && modal.parameter == 728 && modal.finalized == 1 && !modal.m_hWnd);
        CHECK(module.GetLockCount() == 0);
    }
    for (WORD resource : {WORD(603), WORD(608)}) {
        MissingControlDialog missing;
        missing.resource = resource;
        CHECK(!missing.Create(nullptr) && !missing.initialized && !missing.m_hWnd && module.GetLockCount() == 0);
        CHECK(missing.DoModal(nullptr) == -1 && !missing.initialized && !missing.m_hWnd && module.GetLockCount() == 0);
    }
    FreeDialogState named;
    const HWND window = ATL::AtlAxCreateDialog(ATL::_AtlBaseModule.GetResourceInstance(), L"OPENATL_CLSID", nullptr, FreeDialogProc, reinterpret_cast<LPARAM>(&named));
    CHECK(window && named.initialized == 1);
    ATL::CWindow container(window); ATL::CComPtr<IWebBrowser2> control;
    CHECK(container.GetDlgControl(701, IID_IWebBrowser2, reinterpret_cast<void**>(&control.p)) == S_OK);
    control.Release(); CHECK(::DestroyWindow(window) && module.GetLockCount() == 0);
    return true;
}
void BrowserPhase(int cycle, const char* phase) noexcept {
    // Keep real IWebBrowser2 admission and its original 25s CTest budget.
    // If a hosted Windows runner stalls inside a synchronous OLE call, log
    // the exact phase before it rather than replacing the real control.
    std::fprintf(stderr, "ACTIVEX_BROWSER cycle=%d phase=%s ticks=%llu\n",
                 cycle, phase, static_cast<unsigned long long>(::GetTickCount64()));
    std::fflush(stderr);
}
bool browser() {
    BrowserPhase(-1, "start");
    ATL::CAtlModule module;
    for (int cycle = 0; cycle < 3; ++cycle) {
        BrowserPhase(cycle, "begin");
        CHECK(module.GetLockCount() == 0);
        struct Parent {
            HWND window = ::CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 320, 200,
                nullptr, nullptr, ATL::_AtlBaseModule.GetModuleInstance(), nullptr);
            ~Parent() { if (window) ::DestroyWindow(window); }
        } parent;
        CHECK(parent.window);
        ATL::CWindow container(parent.window);
        ATL::CAxWindow2 window;
        RECT rect{0, 0, 320, 200};
        CHECK(window.Create(parent.window, &rect, nullptr, WS_CHILD, 0, 1U) != nullptr);
        BrowserPhase(cycle, "window-created");
        ATL::CComPtr<IUnknown> raw;
        CHECK(ATL::AtlAxGetControl(window, &raw) == E_NOINTERFACE && !raw);
        BrowserPhase(cycle, "creating-webbrowser-control");
        CHECK(window.CreateControlLic(L"about:blank") == S_OK);
        BrowserPhase(cycle, "webbrowser-created");
        CHECK(module.GetLockCount() == 1);
        ATL::CComPtr<IWebBrowser2> browser;
        CHECK(container.GetDlgControl(1, IID_IWebBrowser2, reinterpret_cast<void**>(&browser.p)) == S_OK);
        void* missing = reinterpret_cast<void*>(1);
        CHECK(container.GetDlgControl(99, IID_IUnknown, &missing) == E_FAIL && !missing);
        CHECK(container.GetDlgControl(1, EventIID, &missing) == E_NOINTERFACE && !missing);
        CHECK(container.GetDlgControl(1, IID_IUnknown, nullptr) == E_POINTER);
        ATL::CComPtr<IAxWinAmbientDispatch> ambient;
        CHECK(window.QueryHost(&ambient) == S_OK);
        CHECK(ambient->put_DocHostFlags(DOCHOSTUIFLAG_NO3DBORDER | DOCHOSTUIFLAG_THEME) == S_OK);
        ATL::CComPtr<IDocHostUIHandler> ui;
        CHECK(window.QueryHost(&ui) == S_OK);
        DOCHOSTUIINFO info{}; info.cbSize = sizeof(info);
        CHECK(ui->GetHostInfo(&info) == S_OK && (info.dwFlags & DOCHOSTUIFLAG_THEME));
        BrowserSink sink(parent.window);
        CHECK(WtlAdviseSinkMap(&sink, true) == S_OK);
        ATL::CComVariant url(L"about:blank#openatl"), empty;
        BrowserPhase(cycle, "navigating");
        CHECK(browser->Navigate2(&url, &empty, &empty, &empty, &empty) == S_OK);
        BrowserPhase(cycle, "awaiting-document-complete");
        const ULONGLONG deadline = ::GetTickCount64() + 6000;
        while (!sink.documents && ::GetTickCount64() < deadline) {
            MSG msg{};
            while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { ::TranslateMessage(&msg); ::DispatchMessageW(&msg); }
            ::MsgWaitForMultipleObjects(0, nullptr, FALSE, 20, QS_ALLINPUT);
        }
        CHECK(sink.documents > 0);
        BrowserPhase(cycle, "document-complete");
        ATL::CComPtr<IDispatch> document;
        CHECK(browser->get_Document(&document) == S_OK);
        ATL::CComQIPtr<IHTMLDocument2> html = document;
        CHECK(html.p != nullptr);
        CHECK(RunBrowserUIHandlerTest(window, html));
        html.Release(); html = document;
        CHECK(html.p != nullptr);
        CHECK(WtlAdviseSinkMap(&sink, false) == S_OK);
        ATL::CComPtr<IOleObject> object;
        CHECK(browser->QueryInterface(IID_IOleObject, reinterpret_cast<void**>(&object)) == S_OK);
        ATL::CComPtr<IAxWinHostWindow> host;
        CHECK(window.QueryHost(&host) == S_OK);
        HRESULT foreign = S_OK;
        HWND handle = window;
        std::thread reader([&] { ATL::CComPtr<IUnknown> value; foreign = ATL::AtlAxGetControl(handle, &value); });
        reader.join(); CHECK(foreign == RPC_E_WRONG_THREAD);
        BrowserPhase(cycle, "destroying-window");
        CHECK(window.DestroyWindow()); window.m_hWnd = nullptr;
        missing = reinterpret_cast<void*>(1);
        CHECK(container.GetDlgControl(1, IID_IUnknown, &missing) == E_FAIL && !missing);
        BrowserPhase(cycle, "window-destroyed");
        ATL::CComPtr<IOleClientSite> site;
        CHECK(object->GetClientSite(&site) == S_OK && !site);
        raw.Release();
        CHECK(host->QueryControl(IID_IUnknown, reinterpret_cast<void**>(&raw)) == CO_E_OBJNOTCONNECTED && !raw);
        CHECK(module.GetLockCount() == 1);
        BrowserPhase(cycle, "end");
    }
    CHECK(module.GetLockCount() == 0);
    BrowserPhase(-1, "complete");
    return controlDialogs(module);
}
}
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    bool (*test)() = nullptr;
    if (std::strcmp(argv[1], "events") == 0) test = events;
    else if (std::strcmp(argv[1], "browser") == 0) test = browser;
    else if (std::strcmp(argv[1], "lifecycle") == 0) test = RunHostLifecycleTests;
    else return 2;
    ULONG_PTR token = 0;
    if (::CoGetContextToken(&token) != CO_E_NOTINITIALIZED || !ATL::AtlAxWinInit() ||
        ::CoGetContextToken(&token) != CO_E_NOTINITIALIZED) return 1;
    if (!propertyMaps() || !propertyBrowsing() || !persistentProperties() || !authoredPropertyPages() || !propertyPageMetrics() || !splitDialogResources() || !dialogTemplates() || !plainDialogs() || !ownerDrawDialogs() || ::CoGetContextToken(&token) != CO_E_NOTINITIALIZED) return 1;
    HRESULT hr = ::OleInitialize(nullptr); if (FAILED(hr)) return 1;
    bool ok = true;
#if defined(OPENATL_ORIGINAL_COMMAP_CLASSES)
    if (test == events) ok = RunMicrosoftCommapClassTests() && RunMicrosoftSafeArrayTests();
#endif
    if (ok) ok = test();
    ::OleUninitialize();
    ok = ok && ::CoGetContextToken(&token) == CO_E_NOTINITIALIZED;
    if (ok) std::puts("ACTIVEX_OK");
    return ok ? 0 : 1;
}
