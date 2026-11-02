// Native acceptance of unchanged original Microsoft class implementations.
// Original COMMap/aggreg module entry points also run in two existing DLLs.
#include "premap.h"
#include "COMMap.h"
#include "AtlSafeArray.h"
#include "Outer.h"
#include "aggreg/Agg.h"
#include "aggreg/AggBlind.h"
#include "aggreg/AutoAgg.h"
#include "aggreg/AutoAggB.h"
#include <cstdio>
#include <cwchar>
#include <string>
#include <utility>

#define ORIGINAL_CLASS_CHECK(x) do { if (!(x)) { \
    std::fprintf(stderr, "MICROSOFT_COMMAP_CLASS_FAIL line=%d: %s\n", __LINE__, #x); return false; } } while (0)
namespace {
class OriginalClassModule : public ATL::CAtlDllModuleT<OriginalClassModule> {
public:
    DECLARE_LIBID(LIBID_COMMAPLib)
};
struct ClassRegistrations {
    OriginalClassModule& module;
    DWORD cookies[6]{};
    ATL::CComPtr<IClassFactory> factories[6];
    size_t count = 0;
    explicit ClassRegistrations(OriginalClassModule& value) : module(value) {}
    HRESULT Add(REFCLSID clsid) {
        if (count == 6) return E_UNEXPECTED;
        ATL::CComPtr<IClassFactory> factory;
        HRESULT hr = module.DllGetClassObject(clsid, IID_IClassFactory,
            reinterpret_cast<void**>(&factory.p));
        if (FAILED(hr)) return hr;
        hr = ::CoRegisterClassObject(clsid, factory.p, CLSCTX_INPROC_SERVER,
            REGCLS_MULTIPLEUSE, &cookies[count]);
        if (SUCCEEDED(hr)) factories[count++].Attach(factory.Detach());
        return hr;
    }
    HRESULT Revoke() {
        for (size_t i = count; i > 0; --i) {
            if (cookies[i - 1]) {
                HRESULT hr = ::CoRevokeClassObject(cookies[i - 1]);
                if (FAILED(hr)) return hr;
                cookies[i - 1] = 0;
            }
            factories[i - 1].Release();
        }
        count = 0;
        return S_OK;
    }
    ~ClassRegistrations() { if (FAILED(Revoke())) std::terminate(); }
};
bool NameIs(BSTR text, const wchar_t* expected) {
    return text && std::wcscmp(text, expected) == 0;
}
std::wstring GuidPath(const wchar_t* prefix, REFGUID id) {
    wchar_t text[40]{};
    if (!::StringFromGUID2(id, text, 40)) return {};
    return std::wstring(prefix) + text;
}
bool RegistryAbsent(HKEY root, const std::wstring& path) {
    HKEY key = nullptr;
    const LSTATUS result = ::RegOpenKeyExW(root, path.c_str(), 0, KEY_READ, &key);
    if (key) ::RegCloseKey(key);
    return result == ERROR_FILE_NOT_FOUND || result == ERROR_PATH_NOT_FOUND;
}
bool RegistryText(HKEY root, const std::wstring& path, const wchar_t* value,
                   const std::wstring& expected) {
    wchar_t text[8193]{};
    DWORD bytes = sizeof(text);
    return ::RegGetValueW(root, path.c_str(), value, RRF_RT_REG_SZ, nullptr,
        text, &bytes) == ERROR_SUCCESS &&
        ::CompareStringOrdinal(text, -1, expected.c_str(), -1, TRUE) == CSTR_EQUAL;
}
struct OriginalRegistryRoot {
    HKEY key = nullptr;
    std::wstring path;
    bool Open() {
        GUID unique{};
        if (FAILED(::CoCreateGuid(&unique))) return false;
        path = GuidPath(L"Software\\OpenWTL\\Tests\\OriginalCommap-", unique);
        DWORD disposition = 0;
        return !path.empty() && ::RegCreateKeyExW(HKEY_CURRENT_USER, path.c_str(),
            0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &key, &disposition) == ERROR_SUCCESS &&
            disposition == REG_CREATED_NEW_KEY;
    }
    ~OriginalRegistryRoot() {
        if (key) {
            ::RegCloseKey(key);
            ::RegDeleteTreeW(HKEY_CURRENT_USER, path.c_str());
        }
    }
};
struct OriginalResourceImage {
    HMODULE module = nullptr;
    bool Open() {
        wchar_t path[8193]{};
        const DWORD length = ::GetModuleFileNameW(nullptr, path, 8193);
        if (!length || length >= 8193) return false;
        std::wstring file(path, length);
        const auto slash = file.find_last_of(L"\\/");
        if (slash == std::wstring::npos) return false;
        file.resize(slash + 1);
        file += L"openatl_com_server_fixture.dll";
        module = ::LoadLibraryW(file.c_str());
        return module != nullptr;
    }
    ~OriginalResourceImage() { if (module) ::FreeLibrary(module); }
};
struct OriginalRgsRow {
    const CLSID* clsid;
    const wchar_t* versioned;
    const wchar_t* independent;
    HMODULE image;
};
struct OriginalRgsInstall {
    OriginalClassModule& module;
    HKEY root;
    OriginalRgsRow* rows;
    bool installed[6]{};
    HRESULT Apply(size_t index, bool install) {
        OpenATL::COM::ScopedRgsRegistrationContext context(root, rows[index].image, rows[index].versioned);
        if (FAILED(context.Status())) return context.Status();
        return install ? module.RegisterServer(FALSE, rows[index].clsid)
                       : module.UnregisterServer(FALSE, rows[index].clsid);
    }
    HRESULT Undo() {
        for (size_t n = 6; n > 0; --n) if (installed[n - 1]) {
            const HRESULT hr = Apply(n - 1, false);
            if (FAILED(hr)) return hr;
            installed[n - 1] = false;
        }
        return S_OK;
    }
    ~OriginalRgsInstall() { (void)Undo(); }
};
bool OriginalRgsCallbacks(OriginalClassModule& module) {
    OriginalResourceImage aggregateImage;
    ORIGINAL_CLASS_CHECK(aggregateImage.Open());
    OriginalRegistryRoot root;
    ORIGINAL_CLASS_CHECK(root.Open());
    const HMODULE self = ATL::_AtlBaseModule.GetModuleInstance();
    OriginalRgsRow rows[] = {
        {&CLSID_COuter, L"COMMap.Outer.1", L"COMMap.Outer", self},
        {&CLSID_CChain, L"COMMap.Chain.1", L"COMMap.Chain", self},
        {&CLSID_CAgg, L"Aggreg.Agg.1", L"Aggreg.Agg", aggregateImage.module},
        {&CLSID_CAggBlind, L"Aggreg.AggBlind.1", L"Aggreg.AggBlind", aggregateImage.module},
        {&CLSID_CAutoAgg, L"Aggreg.AutoAgg.1", L"Aggreg.AutoAgg", aggregateImage.module},
        {&CLSID_CAutoAggB, L"Aggreg.AutoAggB.1", L"Aggreg.AutoAggB", aggregateImage.module}
    };
    OriginalRgsInstall installation{module, root.key, rows};
    for (size_t n = 0; n < 6; ++n) {
        ORIGINAL_CLASS_CHECK(installation.Apply(n, true) == S_OK);
        installation.installed[n] = true;
        const auto clsid = GuidPath(L"", *rows[n].clsid);
        const auto classPath = GuidPath(L"CLSID\\", *rows[n].clsid);
        wchar_t imagePath[8193]{};
        ORIGINAL_CLASS_CHECK(::GetModuleFileNameW(rows[n].image, imagePath, 8193) > 0);
        // The existing resource contract quotes %MODULE% for an EXE image;
        // the DLL image expands it to the unquoted module path.
        std::wstring expectedImage(imagePath);
        if (rows[n].image == ::GetModuleHandleW(nullptr))
            expectedImage = L"\"" + expectedImage + L"\"";
        ORIGINAL_CLASS_CHECK(RegistryText(root.key, classPath + L"\\InprocServer32", nullptr, expectedImage));
        ORIGINAL_CLASS_CHECK(RegistryText(root.key, classPath + L"\\InprocServer32", L"ThreadingModel", L"both"));
        ORIGINAL_CLASS_CHECK(RegistryText(root.key, classPath + L"\\ProgID", nullptr, rows[n].versioned));
        ORIGINAL_CLASS_CHECK(RegistryText(root.key, classPath + L"\\VersionIndependentProgID", nullptr, rows[n].independent));
        ORIGINAL_CLASS_CHECK(RegistryText(root.key, std::wstring(rows[n].versioned) + L"\\CLSID", nullptr, clsid));
        ORIGINAL_CLASS_CHECK(RegistryText(root.key, std::wstring(rows[n].independent) + L"\\CurVer", nullptr, rows[n].versioned));
    }
    ORIGINAL_CLASS_CHECK(installation.Apply(0, true) == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS));
    ORIGINAL_CLASS_CHECK(installation.Undo() == S_OK);
    for (const auto& row : rows) {
        ORIGINAL_CLASS_CHECK(RegistryAbsent(root.key, GuidPath(L"CLSID\\", *row.clsid)));
        ORIGINAL_CLASS_CHECK(RegistryAbsent(root.key, row.versioned));
        ORIGINAL_CLASS_CHECK(RegistryAbsent(root.key, row.independent));
    }
    return true;
}
bool OriginalTypeLibsAbsent() {
    for (const GUID* id : {&LIBID_COMMAPLib, &LIBID_AGGREGLib}) {
        ORIGINAL_CLASS_CHECK(RegistryAbsent(HKEY_CLASSES_ROOT, GuidPath(L"TypeLib\\", *id)));
        ATL::CComPtr<ITypeLib> library;
        ORIGINAL_CLASS_CHECK(::LoadRegTypeLib(*id, 1, 0, LOCALE_SYSTEM_DEFAULT, &library.p) == TYPE_E_LIBNOTREGISTERED && !library.p);
    }
    for (const IID* iid : {&IID_IOuter, &IID_IChain, &IID_ITearOff1, &IID_ITearOff2,
                           &IID_IAgg, &IID_IAggBlind, &IID_IAutoAgg, &IID_IAutoAggB})
        ORIGINAL_CLASS_CHECK(RegistryAbsent(HKEY_CLASSES_ROOT, GuidPath(L"Interface\\", *iid)));
    for (const CLSID* id : {&CLSID_COuter, &CLSID_CChain, &CLSID_CAgg,
                            &CLSID_CAggBlind, &CLSID_CAutoAgg, &CLSID_CAutoAggB})
        ORIGINAL_CLASS_CHECK(RegistryAbsent(HKEY_CLASSES_ROOT, GuidPath(L"CLSID\\", *id)));
    return true;
}
struct OriginalTypeLibRegistration {
    bool previous = false;
    bool active = false;
    bool installed[2]{};
    HINSTANCE image = ATL::_AtlBaseModule.GetModuleInstance();
    const wchar_t* indices[2] = {L"\\1", L"\\2"};
    bool Begin() {
        if (FAILED(ATL::AtlGetPerUserRegistration(&previous))) return false;
        active = true;
        return ATL::AtlSetPerUserRegistration(true) == S_OK;
    }
    HRESULT Add(size_t n) {
        const HRESULT hr = ATL::AtlRegisterTypeLib(image, indices[n]);
        if (SUCCEEDED(hr)) installed[n] = true;
        return hr;
    }
    HRESULT Remove(size_t n) {
        const HRESULT hr = ATL::AtlUnRegisterTypeLib(image, indices[n]);
        if (SUCCEEDED(hr)) installed[n] = false;
        return hr;
    }
    ~OriginalTypeLibRegistration() {
        if (!active) return;
        ATL::AtlSetPerUserRegistration(true);
        for (size_t n = 2; n > 0; --n)
            if (installed[n - 1] && FAILED(Remove(n - 1))) std::terminate();
        ATL::AtlSetPerUserRegistration(previous);
    }
};
bool OriginalRegisteredInfo(IDispatch* object, REFIID iid, REFGUID libid, ITypeInfo** retained = nullptr) {
    ATL::CComPtr<ITypeInfo> info;
    ORIGINAL_CLASS_CHECK(object->GetTypeInfo(0, LOCALE_SYSTEM_DEFAULT, &info.p) == S_OK && info.p);
    TYPEATTR* type = nullptr;
    ORIGINAL_CLASS_CHECK(info->GetTypeAttr(&type) == S_OK && type);
    const bool typeMatches = IsEqualGUID(type->guid, iid);
    info->ReleaseTypeAttr(type);
    ORIGINAL_CLASS_CHECK(typeMatches);
    ATL::CComPtr<ITypeLib> library;
    UINT index = 0;
    ORIGINAL_CLASS_CHECK(info->GetContainingTypeLib(&library.p, &index) == S_OK && library.p);
    TLIBATTR* attributes = nullptr;
    ORIGINAL_CLASS_CHECK(library->GetLibAttr(&attributes) == S_OK && attributes);
    const bool libraryMatches = IsEqualGUID(attributes->guid, libid) &&
        attributes->wMajorVerNum == 1 && attributes->wMinorVerNum == 0 && attributes->syskind == SYS_WIN64;
    library->ReleaseTLibAttr(attributes);
    ORIGINAL_CLASS_CHECK(libraryMatches);
    if (retained) *retained = info.Detach();
    return true;
}
bool OriginalRegisteredProperty(IDispatch* object, REFIID iid, REFGUID libid,
                                 const wchar_t* method, const wchar_t* expected) {
    ORIGINAL_CLASS_CHECK(OriginalRegisteredInfo(object, iid, libid));
    LPOLESTR name = const_cast<LPOLESTR>(method);
    DISPID id = DISPID_UNKNOWN;
    ORIGINAL_CLASS_CHECK(object->GetIDsOfNames(IID_NULL, &name, 1, LOCALE_SYSTEM_DEFAULT, &id) == S_OK && id == 0);
    DISPPARAMS parameters{};
    ATL::CComVariant result;
    ORIGINAL_CLASS_CHECK(object->Invoke(id, IID_NULL, LOCALE_SYSTEM_DEFAULT, DISPATCH_PROPERTYGET,
        &parameters, &result, nullptr, nullptr) == S_OK && result.vt == VT_BSTR && NameIs(result.bstrVal, expected));
    return true;
}
bool OriginalRegisteredDispatch(IOuter* outer) {
    ORIGINAL_CLASS_CHECK(OriginalTypeLibsAbsent());
    ATL::CComPtr<ITypeInfo> missing;
    ORIGINAL_CLASS_CHECK(outer->GetTypeInfo(0, LOCALE_SYSTEM_DEFAULT, &missing.p) == TYPE_E_LIBNOTREGISTERED && !missing.p);
    ATL::CComPtr<IAgg> aggregate;
    ORIGINAL_CLASS_CHECK(outer->QueryInterface(IID_IAgg, reinterpret_cast<void**>(&aggregate.p)) == S_OK);
    ORIGINAL_CLASS_CHECK(aggregate->GetTypeInfo(0, LOCALE_SYSTEM_DEFAULT, &missing.p) == TYPE_E_LIBNOTREGISTERED && !missing.p);
    OriginalTypeLibRegistration registration;
    ORIGINAL_CLASS_CHECK(registration.Begin());
    ORIGINAL_CLASS_CHECK(registration.Add(0) == S_OK);
    ATL::CComPtr<ITypeInfo> outerInfo, aggregateInfo;
    ORIGINAL_CLASS_CHECK(OriginalRegisteredInfo(outer, IID_IOuter, LIBID_COMMAPLib, &outerInfo.p));
    ORIGINAL_CLASS_CHECK(aggregate->GetTypeInfo(0, LOCALE_SYSTEM_DEFAULT, &missing.p) == TYPE_E_LIBNOTREGISTERED && !missing.p);
    ORIGINAL_CLASS_CHECK(registration.Add(1) == S_OK);
    ORIGINAL_CLASS_CHECK(OriginalRegisteredInfo(aggregate.p, IID_IAgg, LIBID_AGGREGLib, &aggregateInfo.p));
    LPOLESTR dispatchNames[] = {const_cast<LPOLESTR>(L"GetName"),
        const_cast<LPOLESTR>(L"n")};
    DISPID dispatchIds[] = {DISPID_UNKNOWN, DISPID_UNKNOWN};
    ORIGINAL_CLASS_CHECK(outer->GetIDsOfNames(IID_NULL, dispatchNames, 2,
        LOCALE_SYSTEM_DEFAULT, dispatchIds) == S_OK &&
        dispatchIds[0] != DISPID_UNKNOWN && dispatchIds[1] != DISPID_UNKNOWN);
    const DISPID member = dispatchIds[0];
    const wchar_t* names[] = {L"IOuter", L"ITearOff1", L"ITearOff2", L"IChain", L"IAgg", L"IAutoAgg", L"IAggBlind", L"IAutoAggB"};
    for (short n = 0; n < 8; ++n) {
        VARIANTARG number{}; number.vt = VT_I2; number.iVal = n;
        DISPPARAMS parameters{&number, nullptr, 1, 0};
        ATL::CComVariant result;
        ORIGINAL_CLASS_CHECK(outer->Invoke(member, IID_NULL, LOCALE_SYSTEM_DEFAULT, DISPATCH_METHOD,
            &parameters, &result, nullptr, nullptr) == S_OK && result.vt == VT_BSTR && NameIs(result.bstrVal, names[n]));
    }
    ATL::CComVariant namedNumber(static_cast<short>(3));
    DISPPARAMS namedParameters{&namedNumber, &dispatchIds[1], 1, 1};
    ATL::CComVariant namedResult;
    ORIGINAL_CLASS_CHECK(outer->Invoke(member, IID_NULL, LOCALE_SYSTEM_DEFAULT,
        DISPATCH_METHOD, &namedParameters, &namedResult, nullptr, nullptr) == S_OK &&
        namedResult.vt == VT_BSTR && NameIs(namedResult.bstrVal, L"IChain"));
    ATL::CComVariant convertedNumber(L"4");
    DISPPARAMS convertedParameters{&convertedNumber, nullptr, 1, 0};
    ATL::CComVariant convertedResult;
    ORIGINAL_CLASS_CHECK(outer->Invoke(member, IID_NULL, LOCALE_SYSTEM_DEFAULT,
        DISPATCH_METHOD, &convertedParameters, &convertedResult, nullptr, nullptr) == S_OK &&
        convertedResult.vt == VT_BSTR && NameIs(convertedResult.bstrVal, L"IAgg"));
    ATL::CComVariant wrongNumber(L"not-a-number");
    DISPPARAMS wrongParameters{&wrongNumber, nullptr, 1, 0};
    UINT wrongArgument = 99;
    ORIGINAL_CLASS_CHECK(outer->Invoke(member, IID_NULL, LOCALE_SYSTEM_DEFAULT,
        DISPATCH_METHOD, &wrongParameters, nullptr, nullptr, &wrongArgument) ==
        DISP_E_TYPEMISMATCH && wrongArgument == 0);
    ATL::CComVariant exceptionNumber(static_cast<short>(8));
    DISPPARAMS exceptionParameters{&exceptionNumber, nullptr, 1, 0};
    ATL::CComVariant exceptionResult;
    EXCEPINFO dispatchException{};
    ORIGINAL_CLASS_CHECK(outer->Invoke(member, IID_NULL, LOCALE_SYSTEM_DEFAULT,
        DISPATCH_METHOD, &exceptionParameters, &exceptionResult,
        &dispatchException, nullptr) == DISP_E_EXCEPTION);
    ATL::CComBSTR dispatchExceptionText, dispatchExceptionSource,
        dispatchExceptionHelp;
    dispatchExceptionText.Attach(dispatchException.bstrDescription);
    dispatchExceptionSource.Attach(dispatchException.bstrSource);
    dispatchExceptionHelp.Attach(dispatchException.bstrHelpFile);
    ORIGINAL_CLASS_CHECK(NameIs(dispatchExceptionText,
        L"Interface type out of range"));
    ATL::CComPtr<IChain> chain;
    ATL::CComPtr<IAggBlind> blind;
    ATL::CComPtr<IAutoAgg> automatic;
    ATL::CComPtr<IAutoAggB> automaticBlind;
    ORIGINAL_CLASS_CHECK(outer->QueryInterface(IID_IChain, reinterpret_cast<void**>(&chain.p)) == S_OK);
    ORIGINAL_CLASS_CHECK(outer->QueryInterface(IID_IAggBlind, reinterpret_cast<void**>(&blind.p)) == S_OK);
    ORIGINAL_CLASS_CHECK(outer->QueryInterface(IID_IAutoAgg, reinterpret_cast<void**>(&automatic.p)) == S_OK);
    ORIGINAL_CLASS_CHECK(outer->QueryInterface(IID_IAutoAggB, reinterpret_cast<void**>(&automaticBlind.p)) == S_OK);
    ORIGINAL_CLASS_CHECK(OriginalRegisteredProperty(chain.p, IID_IChain, LIBID_COMMAPLib, L"Name2", L"IChain"));
    ORIGINAL_CLASS_CHECK(OriginalRegisteredProperty(aggregate.p, IID_IAgg, LIBID_AGGREGLib, L"Name", L"IAgg"));
    ORIGINAL_CLASS_CHECK(OriginalRegisteredProperty(blind.p, IID_IAggBlind, LIBID_AGGREGLib, L"Name", L"IAggBlind"));
    ORIGINAL_CLASS_CHECK(OriginalRegisteredProperty(automatic.p, IID_IAutoAgg, LIBID_AGGREGLib, L"Name", L"IAutoAgg"));
    ORIGINAL_CLASS_CHECK(OriginalRegisteredProperty(automaticBlind.p, IID_IAutoAggB, LIBID_AGGREGLib, L"Name", L"IAutoAggB"));
    ATL::CComPtr<ITearOff1> tearOne;
    ATL::CComPtr<ITearOff2> tearTwo;
    ORIGINAL_CLASS_CHECK(outer->QueryInterface(IID_ITearOff1, reinterpret_cast<void**>(&tearOne.p)) == S_OK);
    ORIGINAL_CLASS_CHECK(outer->QueryInterface(IID_ITearOff2, reinterpret_cast<void**>(&tearTwo.p)) == S_OK);
    ORIGINAL_CLASS_CHECK(tearOne->GetTypeInfo(0, LOCALE_SYSTEM_DEFAULT, &missing.p) == TYPE_E_ELEMENTNOTFOUND && !missing.p);
    ORIGINAL_CLASS_CHECK(tearTwo->GetTypeInfo(0, LOCALE_SYSTEM_DEFAULT, &missing.p) == TYPE_E_ELEMENTNOTFOUND && !missing.p);
    ORIGINAL_CLASS_CHECK(registration.Remove(0) == S_OK);
    ORIGINAL_CLASS_CHECK(outer->GetTypeInfo(0, LOCALE_SYSTEM_DEFAULT, &missing.p) == TYPE_E_LIBNOTREGISTERED && !missing.p);
    ORIGINAL_CLASS_CHECK(OriginalRegisteredProperty(aggregate.p, IID_IAgg, LIBID_AGGREGLib, L"Name", L"IAgg"));
    ORIGINAL_CLASS_CHECK(registration.Remove(1) == S_OK);
    ORIGINAL_CLASS_CHECK(aggregate->GetTypeInfo(0, LOCALE_SYSTEM_DEFAULT, &missing.p) == TYPE_E_LIBNOTREGISTERED && !missing.p);
    ORIGINAL_CLASS_CHECK(OriginalTypeLibsAbsent());
    // Already owned native metadata stays usable after unregister; subsequent
    // object GetTypeInfo does not return that metadata as an implicit fallback.
    VARIANTARG zero{}; zero.vt = VT_I2; zero.iVal = 0;
    DISPPARAMS parameters{&zero, nullptr, 1, 0};
    ATL::CComVariant result;
    ORIGINAL_CLASS_CHECK(outerInfo->Invoke(outer, member, DISPATCH_METHOD,
        &parameters, &result, nullptr, nullptr) == S_OK && result.vt == VT_BSTR && NameIs(result.bstrVal, L"IOuter"));
    DISPPARAMS none{};
    ATL::CComVariant aggregateResult;
    ORIGINAL_CLASS_CHECK(aggregateInfo->Invoke(aggregate.p, 0, DISPATCH_PROPERTYGET,
        &none, &aggregateResult, nullptr, nullptr) == S_OK && aggregateResult.vt == VT_BSTR && NameIs(aggregateResult.bstrVal, L"IAgg"));
    return true;
}

bool OriginalClassBodies() {
    ORIGINAL_CLASS_CHECK(!ATL::_pAtlModule);
    const CLSID* ids[] = {&CLSID_COuter, &CLSID_CChain, &CLSID_CAgg,
        &CLSID_CAggBlind, &CLSID_CAutoAgg, &CLSID_CAutoAggB, &CLSID_ArrayManager};
    size_t count = 0;
    for (const auto* entry = ATL::detail::autoObjectEntries.load(std::memory_order_acquire);
         entry; entry = entry->next) {
        ++count;
        bool known = false;
        for (const auto* id : ids) if (IsEqualCLSID(entry->clsid, *id)) known = true;
        ORIGINAL_CLASS_CHECK(known);
    }
    // Original Chain.h occurs in Chain.cpp, Outer.cpp and this translation
    // unit; each aggregate header occurs twice. One logical entry per class.
    // ArrayManager adds one independently tested class to this existing image.
    ORIGINAL_CLASS_CHECK(count == 7);
    OriginalClassModule module;
    ClassRegistrations registrations(module);
    ATL::CComPtr<IClassFactory> outerFactory;
    ORIGINAL_CLASS_CHECK(module.DllGetClassObject(CLSID_COuter, IID_IClassFactory,
        reinterpret_cast<void**>(&outerFactory.p)) == S_OK);
    void* failed = reinterpret_cast<void*>(1);
    ORIGINAL_CLASS_CHECK(outerFactory->CreateInstance(nullptr, IID_IOuter, &failed) == REGDB_E_CLASSNOTREG && !failed);
    ORIGINAL_CLASS_CHECK(module.GetLockCount() == 0);
    // Let the first real aggregate succeed, then fail the second. Original
    // COuter's FinalRelease must retire the partially constructed aggregate.
    ORIGINAL_CLASS_CHECK(registrations.Add(CLSID_CAgg) == S_OK);
    ORIGINAL_CLASS_CHECK(outerFactory->CreateInstance(nullptr, IID_IOuter, &failed) == REGDB_E_CLASSNOTREG && !failed);
    ORIGINAL_CLASS_CHECK(module.GetLockCount() == 0);
    outerFactory.Release();
    for (const CLSID* id : {&CLSID_CAggBlind, &CLSID_CAutoAgg, &CLSID_CAutoAggB, &CLSID_COuter, &CLSID_CChain})
        ORIGINAL_CLASS_CHECK(registrations.Add(*id) == S_OK);
    {
        ATL::CComPtr<IChain> chain;
        ORIGINAL_CLASS_CHECK(::CoCreateInstance(CLSID_CChain, nullptr, CLSCTX_INPROC_SERVER,
            IID_IChain, reinterpret_cast<void**>(&chain.p)) == S_OK);
        ATL::CComBSTR text;
        ORIGINAL_CLASS_CHECK(chain->get_Name2(&text.m_str) == S_OK && NameIs(text, L"IChain"));
    }
    ATL::CComPtr<IOuter> outer;
    ORIGINAL_CLASS_CHECK(::CoCreateInstance(CLSID_COuter, nullptr, CLSCTX_INPROC_SERVER,
        IID_IOuter, reinterpret_cast<void**>(&outer.p)) == S_OK);
    auto* data = static_cast<COuter*>(outer.p);
    ORIGINAL_CLASS_CHECK(data->m_pUnkAgg.p && data->m_pUnkAggBlind.p &&
        !data->m_pUnkAutoAgg.p && !data->m_pUnkAutoAggB.p && !data->m_pUnkTearOff2.p);
    const wchar_t* names[] = {L"IOuter", L"ITearOff1", L"ITearOff2", L"IChain",
        L"IAgg", L"IAutoAgg", L"IAggBlind", L"IAutoAggB"};
    for (short n = 0; n < 8; ++n) {
        ATL::CComBSTR text;
        ORIGINAL_CLASS_CHECK(outer->GetName(n, &text.m_str) == S_OK && NameIs(text, names[n]));
    }
    ORIGINAL_CLASS_CHECK(data->m_pUnkAutoAgg.p && data->m_pUnkAutoAggB.p && data->m_pUnkTearOff2.p);
    ORIGINAL_CLASS_CHECK(OriginalRgsCallbacks(module));
    ORIGINAL_CLASS_CHECK(OriginalRegisteredDispatch(outer.p));
    ATL::CComPtr<IUnknown> canonical;
    ORIGINAL_CLASS_CHECK(outer->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&canonical.p)) == S_OK);
    ATL::CComPtr<ITearOff1> first, another;
    ATL::CComPtr<ITearOff2> cached, repeated;
    ORIGINAL_CLASS_CHECK(outer->QueryInterface(IID_ITearOff1, reinterpret_cast<void**>(&first.p)) == S_OK);
    ORIGINAL_CLASS_CHECK(outer->QueryInterface(IID_ITearOff1, reinterpret_cast<void**>(&another.p)) == S_OK && first.p != another.p);
    ORIGINAL_CLASS_CHECK(outer->QueryInterface(IID_ITearOff2, reinterpret_cast<void**>(&cached.p)) == S_OK);
    ORIGINAL_CLASS_CHECK(outer->QueryInterface(IID_ITearOff2, reinterpret_cast<void**>(&repeated.p)) == S_OK && cached.p == repeated.p);
    const IID* interfaces[] = {&IID_IOuter, &IID_IChain, &IID_ITearOff1, &IID_ITearOff2,
        &IID_IAgg, &IID_IAggBlind, &IID_IAutoAgg, &IID_IAutoAggB};
    for (const IID* iid : interfaces) {
        ATL::CComPtr<IUnknown> face, identity;
        ORIGINAL_CLASS_CHECK(outer->QueryInterface(*iid, reinterpret_cast<void**>(&face.p)) == S_OK);
        ORIGINAL_CLASS_CHECK(face->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&identity.p)) == S_OK && identity.p == canonical.p);
    }
    ATL::CComPtr<ISupportErrorInfo> support;
    ORIGINAL_CLASS_CHECK(outer->QueryInterface(IID_ISupportErrorInfo, reinterpret_cast<void**>(&support.p)) == S_OK);
    // The original map places the explicit blind aggregate before its local
    // ISupportErrorInfo entry. That aggregate's support view wins this QI.
    ORIGINAL_CLASS_CHECK(support->InterfaceSupportsErrorInfo(IID_IAggBlind) == S_OK &&
        support->InterfaceSupportsErrorInfo(IID_IOuter) == S_FALSE &&
        support->InterfaceSupportsErrorInfo(IID_IChain) == S_FALSE &&
        support->InterfaceSupportsErrorInfo(IID_IStream) == S_FALSE);
    ORIGINAL_CLASS_CHECK(data->COuter::InterfaceSupportsErrorInfo(IID_IOuter) == S_OK &&
        data->COuter::InterfaceSupportsErrorInfo(IID_IChain) == S_OK);
    support.Release();
    // Real original ITypeInfo drives the unmodified GetName vtable, without
    // replacing the object's registered-mode IDispatchImpl.
    ATL::CComTypeInfoHolder holder;
    ATL::CComPtr<ITypeInfo> info;
    ORIGINAL_CLASS_CHECK(holder.GetTI(&IID_IOuter, &LIBID_COMMAPLib, 0xffff, 0xffff,
        LOCALE_SYSTEM_DEFAULT, &info.p) == S_OK);
    LPOLESTR method = const_cast<LPOLESTR>(L"GetName");
    MEMBERID member = MEMBERID_NIL;
    ORIGINAL_CLASS_CHECK(info->GetIDsOfNames(&method, 1, &member) == S_OK && member != MEMBERID_NIL);
    for (short n = 0; n < 8; ++n) {
        VARIANTARG number{}; number.vt = VT_I2; number.iVal = n;
        DISPPARAMS parameters{&number, nullptr, 1, 0};
        ATL::CComVariant result;
        ORIGINAL_CLASS_CHECK(info->Invoke(outer.p, member, DISPATCH_METHOD,
            &parameters, &result, nullptr, nullptr) == S_OK && result.vt == VT_BSTR && NameIs(result.bstrVal, names[n]));
    }
    ATL::CComBSTR unused;
    ORIGINAL_CLASS_CHECK(outer->GetName(8, &unused.m_str) == DISP_E_EXCEPTION);
    ATL::CComPtr<IErrorInfo> error;
    ORIGINAL_CLASS_CHECK(::GetErrorInfo(0, &error.p) == S_OK && error.p);
    ATL::CComBSTR description;
    ORIGINAL_CLASS_CHECK(error->GetDescription(&description.m_str) == S_OK && NameIs(description, L"Interface type out of range"));
    VARIANTARG invalidNumber{}; invalidNumber.vt = VT_I2; invalidNumber.iVal = 8;
    DISPPARAMS invalidParameters{&invalidNumber, nullptr, 1, 0};
    ATL::CComVariant invalidResult;
    EXCEPINFO exception{};
    const HRESULT invokedError = info->Invoke(outer.p, member, DISPATCH_METHOD,
        &invalidParameters, &invalidResult, &exception, nullptr);
    ATL::CComBSTR exceptionText, exceptionSource, exceptionHelp;
    exceptionText.Attach(exception.bstrDescription);
    exceptionSource.Attach(exception.bstrSource);
    exceptionHelp.Attach(exception.bstrHelpFile);
    ORIGINAL_CLASS_CHECK(invokedError == DISP_E_EXCEPTION && NameIs(exceptionText, L"Interface type out of range"));
    ::SetErrorInfo(0, nullptr);
    failed = reinterpret_cast<void*>(1);
    ORIGINAL_CLASS_CHECK(outer->QueryInterface(IID_IStream, &failed) == E_NOINTERFACE && !failed);
    ORIGINAL_CLASS_CHECK(registrations.Revoke() == S_OK);
    first.Release(); another.Release(); repeated.Release(); canonical.Release(); outer.Release();
    ORIGINAL_CLASS_CHECK(module.DllCanUnloadNow() == S_FALSE);
    ATL::CComPtr<IOuter> retainedOwner;
    ORIGINAL_CLASS_CHECK(cached->QueryInterface(IID_IOuter, reinterpret_cast<void**>(&retainedOwner.p)) == S_OK);
    ATL::CComBSTR finalName;
    ORIGINAL_CLASS_CHECK(retainedOwner->GetName(0, &finalName.m_str) == S_OK && NameIs(finalName, L"IOuter"));
    retainedOwner.Release(); cached.Release();
    ORIGINAL_CLASS_CHECK(module.GetLockCount() == 0 && module.DllCanUnloadNow() == S_OK);
    // The native error object owns its copied description independently.
    ATL::CComBSTR survivingError;
    ORIGINAL_CLASS_CHECK(error->GetDescription(&survivingError.m_str) == S_OK && NameIs(survivingError, L"Interface type out of range"));
    return true;
}

struct OriginalDllImage {
    using ServerCall = HRESULT (WINAPI*)();
    using ClassCall = HRESULT (WINAPI*)(REFCLSID, REFIID, void**);
    HMODULE image = nullptr;
    std::wstring path;
    ServerCall install = nullptr, uninstall = nullptr, canUnload = nullptr;
    ClassCall getClass = nullptr;
    bool installed = false;
    bool Open(const wchar_t* filename) {
        wchar_t executable[8193]{};
        const DWORD length = ::GetModuleFileNameW(nullptr, executable, 8193);
        if (!length || length >= 8193) return false;
        path.assign(executable, length);
        const auto slash = path.find_last_of(L"\\/");
        if (slash == path.npos) return false;
        path.resize(slash + 1);
        path += filename;
        image = ::LoadLibraryW(path.c_str());
        if (!image) return false;
        install = reinterpret_cast<ServerCall>(::GetProcAddress(image, "DllRegisterServer"));
        uninstall = reinterpret_cast<ServerCall>(::GetProcAddress(image, "DllUnregisterServer"));
        canUnload = reinterpret_cast<ServerCall>(::GetProcAddress(image, "DllCanUnloadNow"));
        getClass = reinterpret_cast<ClassCall>(::GetProcAddress(image, "DllGetClassObject"));
        return install && uninstall && canUnload && getClass;
    }
    HRESULT Register() {
        const HRESULT result = install();
        if (SUCCEEDED(result)) installed = true;
        return result;
    }
    HRESULT Unregister() {
        const HRESULT result = uninstall();
        if (SUCCEEDED(result)) installed = false;
        return result;
    }
    bool Close() {
        if (installed || !image) return false;
        if (!::FreeLibrary(image)) return false;
        image = nullptr;
        return ::GetModuleHandleW(path.c_str()) == nullptr;
    }
    ~OriginalDllImage() {
        if (installed && FAILED(Unregister())) std::terminate();
        if (image) ::FreeLibrary(image);
    }
};
bool OriginalInterfaceImage(IUnknown* object, HMODULE expected) {
    MEMORY_BASIC_INFORMATION memory{};
    // The actual interface vtable must belong to its original DLL, rather
    // than the host's earlier process-local source-subset implementation.
    const void* table = *reinterpret_cast<void* const*>(object);
    return ::VirtualQuery(table, &memory, sizeof(memory)) == sizeof(memory) &&
        memory.AllocationBase == expected && memory.Type == MEM_IMAGE;
}
bool OriginalDllDeployment() {
    ORIGINAL_CLASS_CHECK(!ATL::_pAtlModule && OriginalTypeLibsAbsent());
    const wchar_t* progIds[] = {L"COMMap.Outer", L"COMMap.Outer.1", L"COMMap.Chain", L"COMMap.Chain.1",
        L"Aggreg.Agg", L"Aggreg.Agg.1", L"Aggreg.AggBlind", L"Aggreg.AggBlind.1",
        L"Aggreg.AutoAgg", L"Aggreg.AutoAgg.1", L"Aggreg.AutoAggB", L"Aggreg.AutoAggB.1"};
    for (const auto* progId : progIds)
        ORIGINAL_CLASS_CHECK(RegistryAbsent(HKEY_CLASSES_ROOT, progId));
    OriginalDllImage aggregates, commap;
    ORIGINAL_CLASS_CHECK(aggregates.Open(L"openatl_unload_probe.dll"));
    ORIGINAL_CLASS_CHECK(commap.Open(L"openatl_unload_fixture.dll"));
    ORIGINAL_CLASS_CHECK(aggregates.canUnload() == S_OK && commap.canUnload() == S_OK);
    void* wrong = reinterpret_cast<void*>(1);
    ORIGINAL_CLASS_CHECK(commap.getClass(CLSID_CAgg, IID_IClassFactory, &wrong) == CLASS_E_CLASSNOTAVAILABLE && !wrong);
    wrong = reinterpret_cast<void*>(1);
    ORIGINAL_CLASS_CHECK(aggregates.getClass(CLSID_COuter, IID_IClassFactory, &wrong) == CLASS_E_CLASSNOTAVAILABLE && !wrong);
    {
        ATL::CComPtr<IClassFactory> factory;
        ORIGINAL_CLASS_CHECK(commap.getClass(CLSID_COuter, IID_IClassFactory, reinterpret_cast<void**>(&factory.p)) == S_OK);
        ORIGINAL_CLASS_CHECK(commap.canUnload() == S_FALSE);
        wrong = reinterpret_cast<void*>(1);
        ORIGINAL_CLASS_CHECK(factory->CreateInstance(nullptr, IID_IOuter, &wrong) == REGDB_E_CLASSNOTREG && !wrong);
    }
    ORIGINAL_CLASS_CHECK(commap.canUnload() == S_OK);
    ORIGINAL_CLASS_CHECK(aggregates.Register() == S_OK && commap.Register() == S_OK);
    const CLSID* classes[] = {&CLSID_COuter, &CLSID_CChain, &CLSID_CAgg,
        &CLSID_CAggBlind, &CLSID_CAutoAgg, &CLSID_CAutoAggB};
    for (size_t n = 0; n < 6; ++n) {
        const auto key = GuidPath(L"Software\\Classes\\CLSID\\", *classes[n]) + L"\\InprocServer32";
        ORIGINAL_CLASS_CHECK(RegistryText(HKEY_CURRENT_USER, key, nullptr, n < 2 ? commap.path : aggregates.path));
        ORIGINAL_CLASS_CHECK(RegistryText(HKEY_CURRENT_USER, key, L"ThreadingModel", L"both"));
    }
    for (const auto& pair : {std::make_pair(&LIBID_COMMAPLib, &commap.path),
                             std::make_pair(&LIBID_AGGREGLib, &aggregates.path)}) {
        ATL::CComBSTR path;
        ORIGINAL_CLASS_CHECK(::QueryPathOfRegTypeLib(*pair.first, 1, 0, LOCALE_SYSTEM_DEFAULT, &path.m_str) == S_OK);
        ORIGINAL_CLASS_CHECK(path.m_str && ::CompareStringOrdinal(path.m_str, -1, pair.second->c_str(), -1, TRUE) == CSTR_EQUAL);
    }
    ATL::CComPtr<IOuter> outer;
    ORIGINAL_CLASS_CHECK(::CoCreateInstance(CLSID_COuter, nullptr, CLSCTX_INPROC_SERVER,
        IID_IOuter, reinterpret_cast<void**>(&outer.p)) == S_OK);
    ORIGINAL_CLASS_CHECK(OriginalInterfaceImage(outer.p, commap.image));
    ORIGINAL_CLASS_CHECK(OriginalRegisteredInfo(outer.p, IID_IOuter, LIBID_COMMAPLib));
    const wchar_t* names[] = {L"IOuter", L"ITearOff1", L"ITearOff2", L"IChain",
        L"IAgg", L"IAutoAgg", L"IAggBlind", L"IAutoAggB"};
    LPOLESTR method = const_cast<LPOLESTR>(L"GetName");
    DISPID member = DISPID_UNKNOWN;
    ORIGINAL_CLASS_CHECK(outer->GetIDsOfNames(IID_NULL, &method, 1, LOCALE_SYSTEM_DEFAULT, &member) == S_OK);
    for (short n = 0; n < 8; ++n) {
        ATL::CComBSTR direct;
        ORIGINAL_CLASS_CHECK(outer->GetName(n, &direct.m_str) == S_OK && NameIs(direct, names[n]));
        VARIANTARG number{}; number.vt = VT_I2; number.iVal = n;
        DISPPARAMS parameters{&number, nullptr, 1, 0};
        ATL::CComVariant result;
        ORIGINAL_CLASS_CHECK(outer->Invoke(member, IID_NULL, LOCALE_SYSTEM_DEFAULT, DISPATCH_METHOD,
            &parameters, &result, nullptr, nullptr) == S_OK && result.vt == VT_BSTR && NameIs(result.bstrVal, names[n]));
    }
    ATL::CComPtr<IUnknown> identity;
    ORIGINAL_CLASS_CHECK(outer->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&identity.p)) == S_OK);
    const IID* interfaces[] = {&IID_IOuter, &IID_ITearOff1, &IID_ITearOff2, &IID_IChain,
        &IID_IAgg, &IID_IAutoAgg, &IID_IAggBlind, &IID_IAutoAggB};
    for (size_t n = 0; n < 8; ++n) {
        ATL::CComPtr<IUnknown> face, canonical;
        ORIGINAL_CLASS_CHECK(outer->QueryInterface(*interfaces[n], reinterpret_cast<void**>(&face.p)) == S_OK);
        ORIGINAL_CLASS_CHECK(OriginalInterfaceImage(face.p, n < 4 ? commap.image : aggregates.image));
        ORIGINAL_CLASS_CHECK(face->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&canonical.p)) == S_OK && canonical.p == identity.p);
    }
    {
        ATL::CComPtr<IChain> chain;
        ATL::CComPtr<IAgg> aggregate;
        ATL::CComPtr<IAggBlind> blind;
        ATL::CComPtr<IAutoAgg> automatic;
        ATL::CComPtr<IAutoAggB> automaticBlind;
        ORIGINAL_CLASS_CHECK(outer->QueryInterface(IID_IChain, reinterpret_cast<void**>(&chain.p)) == S_OK);
        ORIGINAL_CLASS_CHECK(outer->QueryInterface(IID_IAgg, reinterpret_cast<void**>(&aggregate.p)) == S_OK);
        ORIGINAL_CLASS_CHECK(outer->QueryInterface(IID_IAggBlind, reinterpret_cast<void**>(&blind.p)) == S_OK);
        ORIGINAL_CLASS_CHECK(outer->QueryInterface(IID_IAutoAgg, reinterpret_cast<void**>(&automatic.p)) == S_OK);
        ORIGINAL_CLASS_CHECK(outer->QueryInterface(IID_IAutoAggB, reinterpret_cast<void**>(&automaticBlind.p)) == S_OK);
        ORIGINAL_CLASS_CHECK(OriginalRegisteredProperty(chain.p, IID_IChain, LIBID_COMMAPLib, L"Name2", L"IChain"));
        ORIGINAL_CLASS_CHECK(OriginalRegisteredProperty(aggregate.p, IID_IAgg, LIBID_AGGREGLib, L"Name", L"IAgg"));
        ORIGINAL_CLASS_CHECK(OriginalRegisteredProperty(blind.p, IID_IAggBlind, LIBID_AGGREGLib, L"Name", L"IAggBlind"));
        ORIGINAL_CLASS_CHECK(OriginalRegisteredProperty(automatic.p, IID_IAutoAgg, LIBID_AGGREGLib, L"Name", L"IAutoAgg"));
        ORIGINAL_CLASS_CHECK(OriginalRegisteredProperty(automaticBlind.p, IID_IAutoAggB, LIBID_AGGREGLib, L"Name", L"IAutoAggB"));
    }
    ATL::CComPtr<ITearOff2> cached;
    ORIGINAL_CLASS_CHECK(outer->QueryInterface(IID_ITearOff2, reinterpret_cast<void**>(&cached.p)) == S_OK);
    identity.Release(); outer.Release();
    ORIGINAL_CLASS_CHECK(commap.canUnload() == S_FALSE && aggregates.canUnload() == S_FALSE);
    ORIGINAL_CLASS_CHECK(commap.Unregister() == S_OK && aggregates.Unregister() == S_OK);
    ORIGINAL_CLASS_CHECK(OriginalTypeLibsAbsent());
    for (const auto* progId : progIds)
        ORIGINAL_CLASS_CHECK(RegistryAbsent(HKEY_CLASSES_ROOT, progId));
    ATL::CComPtr<IOuter> retained;
    ORIGINAL_CLASS_CHECK(cached->QueryInterface(IID_IOuter, reinterpret_cast<void**>(&retained.p)) == S_OK);
    for (short n = 0; n < 8; ++n) {
        ATL::CComBSTR result;
        ORIGINAL_CLASS_CHECK(retained->GetName(n, &result.m_str) == S_OK && NameIs(result, names[n]));
    }
    ATL::CComPtr<ITypeInfo> missing;
    ORIGINAL_CLASS_CHECK(retained->GetTypeInfo(0, LOCALE_SYSTEM_DEFAULT, &missing.p) == TYPE_E_LIBNOTREGISTERED && !missing.p);
    retained.Release(); cached.Release();
    ::CoFreeUnusedLibrariesEx(0, 0);
    ORIGINAL_CLASS_CHECK(commap.canUnload() == S_OK && aggregates.canUnload() == S_OK);
    ORIGINAL_CLASS_CHECK(commap.Close() && aggregates.Close());
    return true;
}
}
bool RunMicrosoftCommapClassTests() {
    const bool result = OriginalClassBodies();
    return result && !ATL::_pAtlModule && OriginalDllDeployment() && !ATL::_pAtlModule;
}
