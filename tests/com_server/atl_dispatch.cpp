// Native ATL IDispatchImpl acceptance with real OLEAUT32 ITypeInfo.
// This test constructs type information through CreateDispTypeInfo and invokes
// a real vtable method; no mock IDispatch, fake typelib or fallback provider.
#include <atlcom.h>
#include "event_fixture_metadata.h"
#include <cstdio>
#include <oleauto.h>

bool RunAtlFileTypeLibTests();

#define DISPATCH_CHECK(x) do { if (!(x)) { \
    std::fprintf(stderr,"ATL_DISPATCH_FAIL line=%d condition=%s\n", \
        __LINE__,#x); return false; } } while (0)
namespace {
inline constexpr IID iidValue = {0xca951230,0xf149,0x46e1,
    {0xa1,0xf1,0x28,0x39,0x56,0x75,0x63,0x2a}};
inline constexpr GUID libValue = {0xca951231,0xf149,0x46e1,
    {0xa1,0xf1,0x28,0x39,0x56,0x75,0x63,0x2a}};

struct IValueDual : IDispatch {
    virtual LONG STDMETHODCALLTYPE ReadNumber() = 0;
};
class NativeTypeInfoHolder {
public:
    static inline ITypeInfo* source = nullptr;
    HRESULT GetTI(const IID* iid, const GUID* libid, WORD major, WORD minor,
                  LCID, ITypeInfo** output) const noexcept {
        if (!output) return E_POINTER;
        *output = nullptr;
        if (!source || !iid || !libid || !IsEqualIID(*iid,iidValue) ||
            !IsEqualGUID(*libid,libValue) || major != 1 || minor != 0)
            return TYPE_E_LIBNOTREGISTERED;
        source->AddRef();
        *output = source;
        return S_OK;
    }
};
class NativeDual : public ATL::CComObjectRootEx<ATL::CComSingleThreadModel>,
                   public ATL::IDispatchImpl<IValueDual,&iidValue,&libValue,
                                              1,0,NativeTypeInfoHolder> {
public:
    BEGIN_COM_MAP(NativeDual)
        COM_INTERFACE_ENTRY_IID(iidValue,IValueDual)
        COM_INTERFACE_ENTRY(IDispatch)
    END_COM_MAP()
    LONG STDMETHODCALLTYPE ReadNumber() override { return 314; }
};
class MissingDual : public ATL::CComObjectRootEx<ATL::CComSingleThreadModel>,
                    public ATL::IDispatchImpl<IValueDual,&iidValue,&libValue> {
public:
    BEGIN_COM_MAP(MissingDual)
        COM_INTERFACE_ENTRY_IID(iidValue,IValueDual)
        COM_INTERFACE_ENTRY(IDispatch)
    END_COM_MAP()
    LONG STDMETHODCALLTYPE ReadNumber() override { return 0; }
};
class FixtureTypeInfoHolder {
public:
    HRESULT GetTI(const IID* iid, const GUID* libid, WORD major, WORD minor,
                  LCID, ITypeInfo** output) const noexcept {
        if (!output) return E_POINTER;
        *output = nullptr;
        if (!iid || !libid || !IsEqualIID(*iid, IID_IOpenATLInvokeFixture) ||
            !IsEqualGUID(*libid, LIBID_OpenATLEventLib) || major != 1 || minor != 0)
            return TYPE_E_ELEMENTNOTFOUND;
        return LoadEventFixtureType(*iid, output);
    }
};
class OptionalDual : public ATL::CComObjectRootEx<ATL::CComSingleThreadModel>,
                     public ATL::IDispatchImpl<IOpenATLInvokeFixture,
                         &IID_IOpenATLInvokeFixture, &LIBID_OpenATLEventLib,
                         1, 0, FixtureTypeInfoHolder> {
public:
    BEGIN_COM_MAP(OptionalDual)
        COM_INTERFACE_ENTRY(IOpenATLInvokeFixture)
        COM_INTERFACE_ENTRY(IDispatch)
    END_COM_MAP()
    HRESULT STDMETHODCALLTYPE Combine(LONG requiredValue, LONG optionalValue,
                                      LONG* result) override {
        if (!result) return E_POINTER;
        *result = requiredValue * 10 + optionalValue;
        return S_OK;
    }
};
struct Apartment {
    HRESULT status = ::CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    ~Apartment() { if (SUCCEEDED(status)) ::CoUninitialize(); }
};
struct InfoScope {
    explicit InfoScope(ITypeInfo* info) { NativeTypeInfoHolder::source = info; }
    ~InfoScope() { NativeTypeInfoHolder::source = nullptr; }
};

bool TestRealOleTypeInfo() {
    METHODDATA method{};
    method.szName = const_cast<LPOLESTR>(L"ReadNumber");
    method.dispid = 77;
    method.iMeth = 7; // IUnknown (3 slots) + IDispatch (4 slots)
    method.cc = CC_STDCALL;
    method.cArgs = 0;
    method.wFlags = DISPATCH_METHOD;
    method.vtReturn = VT_I4;
    INTERFACEDATA metadata{&method,1};
    ATL::CComPtr<ITypeInfo> info;
    DISPATCH_CHECK(::CreateDispTypeInfo(&metadata, LOCALE_SYSTEM_DEFAULT,
                                       &info.p) == S_OK && info.p);
    InfoScope liveInfo(info.p);
    ATL::CComObject<NativeDual>* raw = nullptr;
    DISPATCH_CHECK(ATL::CComObject<NativeDual>::CreateInstance(&raw) == S_OK && raw);
    DISPATCH_CHECK(raw->AddRef() == 1);
    ATL::CComPtr<IValueDual> dual;
    ATL::CComPtr<IDispatch> dispatch;
    DISPATCH_CHECK(raw->QueryInterface(iidValue,
        reinterpret_cast<void**>(&dual.p)) == S_OK && dual.p);
    DISPATCH_CHECK(raw->QueryInterface(IID_IDispatch,
        reinterpret_cast<void**>(&dispatch.p)) == S_OK && dispatch.p);
    ATL::CComPtr<IUnknown> a,b;
    DISPATCH_CHECK(dual->QueryInterface(IID_IUnknown,
        reinterpret_cast<void**>(&a.p)) == S_OK);
    DISPATCH_CHECK(dispatch->QueryInterface(IID_IUnknown,
        reinterpret_cast<void**>(&b.p)) == S_OK);
    DISPATCH_CHECK(a.p == b.p && a.p == raw->GetUnknown());
    DISPATCH_CHECK(dual->ReadNumber() == 314);
    UINT count = 0;
    DISPATCH_CHECK(dispatch->GetTypeInfoCount(&count) == S_OK && count == 1);
    DISPATCH_CHECK(dispatch->GetTypeInfoCount(nullptr) == E_POINTER);
    ATL::CComPtr<ITypeInfo> acquired;
    DISPATCH_CHECK(dispatch->GetTypeInfo(1, LOCALE_SYSTEM_DEFAULT,
        &acquired.p) == DISP_E_BADINDEX && !acquired.p);
    DISPATCH_CHECK(dispatch->GetTypeInfo(0, LOCALE_SYSTEM_DEFAULT,
        &acquired.p) == S_OK && acquired.p == info.p);
    acquired.Release();
    LPOLESTR names[] = {const_cast<LPOLESTR>(L"readnumber")};
    DISPID member = DISPID_UNKNOWN;
    DISPATCH_CHECK(dispatch->GetIDsOfNames(IID_NULL,names,1,
        LOCALE_SYSTEM_DEFAULT,&member) == S_OK && member == 77);
    member = 7;
    DISPATCH_CHECK(dispatch->GetIDsOfNames(IID_IUnknown,names,1,
        LOCALE_SYSTEM_DEFAULT,&member) == DISP_E_UNKNOWNINTERFACE &&
        member == DISPID_UNKNOWN);
    DISPATCH_CHECK(dispatch->GetIDsOfNames(IID_NULL,names,0,
        LOCALE_SYSTEM_DEFAULT,&member) == E_INVALIDARG);
    LPOLESTR unknown[] = {const_cast<LPOLESTR>(L"NotPresent")};
    member = 7;
    DISPATCH_CHECK(dispatch->GetIDsOfNames(IID_NULL,unknown,1,
        LOCALE_SYSTEM_DEFAULT,&member) == DISP_E_UNKNOWNNAME &&
        member == DISPID_UNKNOWN);
    DISPPARAMS parameters{};
    VARIANT result; ::VariantInit(&result);
    UINT argument = 999;
    const HRESULT invoked = dispatch->Invoke(77,IID_NULL,
        LOCALE_SYSTEM_DEFAULT,DISPATCH_METHOD,&parameters,
        &result,nullptr,&argument);
    if (invoked != S_OK) std::fprintf(stderr,
        "ATL_DISPATCH_INVOKE_HRESULT=0x%08lx\n",
        static_cast<unsigned long>(invoked));
    DISPATCH_CHECK(invoked == S_OK && result.vt == VT_I4 &&
                   result.lVal == 314);
    DISPATCH_CHECK(::VariantClear(&result) == S_OK);
    DISPATCH_CHECK(dispatch->Invoke(77,IID_IUnknown,LOCALE_SYSTEM_DEFAULT,
        DISPATCH_METHOD,&parameters,nullptr,nullptr,&argument)
        == DISP_E_UNKNOWNINTERFACE);
    DISPATCH_CHECK(dispatch->Invoke(77,IID_NULL,LOCALE_SYSTEM_DEFAULT,
        DISPATCH_METHOD,nullptr,nullptr,nullptr,&argument) == E_POINTER);
    b.Release(); a.Release(); dispatch.Release(); dual.Release();
    DISPATCH_CHECK(raw->Release() == 0);
    return true;
}
bool TestMissingTypeLibrary() {
    ATL::CComObject<MissingDual>* raw = nullptr;
    DISPATCH_CHECK(ATL::CComObject<MissingDual>::CreateInstance(&raw)
                   == S_OK && raw);
    DISPATCH_CHECK(raw->AddRef() == 1);
    ATL::CComPtr<IDispatch> dispatch;
    DISPATCH_CHECK(raw->QueryInterface(IID_IDispatch,
        reinterpret_cast<void**>(&dispatch.p)) == S_OK);
    ITypeInfo* missing = reinterpret_cast<ITypeInfo*>(1);
    DISPATCH_CHECK(FAILED(dispatch->GetTypeInfo(0,LOCALE_SYSTEM_DEFAULT,
        &missing)) && missing == nullptr);
    LPOLESTR names[] = {const_cast<LPOLESTR>(L"ReadNumber")};
    DISPID member = 77;
    DISPATCH_CHECK(FAILED(dispatch->GetIDsOfNames(IID_NULL,names,1,
        LOCALE_SYSTEM_DEFAULT,&member)) && member == DISPID_UNKNOWN);
    DISPPARAMS params{};
    DISPATCH_CHECK(FAILED(dispatch->Invoke(77,IID_NULL,LOCALE_SYSTEM_DEFAULT,
        DISPATCH_METHOD,&params,nullptr,nullptr,nullptr)));
    ATL::CComTypeInfoHolder holder;
    DISPATCH_CHECK(FAILED(holder.GetTI(&iidValue,&libValue,0xffff,0xffff,
        LOCALE_SYSTEM_DEFAULT,&missing)) && missing == nullptr);
    DISPATCH_CHECK(holder.GetTI(&iidValue,&libValue,0xffff,0,
        LOCALE_SYSTEM_DEFAULT,&missing) == E_INVALIDARG && missing == nullptr);
    dispatch.Release();
    DISPATCH_CHECK(raw->Release() == 0);
    return true;
}
bool TestOptionalDefaultArguments() {
    ATL::CComObject<OptionalDual>* raw = nullptr;
    DISPATCH_CHECK(ATL::CComObject<OptionalDual>::CreateInstance(&raw) == S_OK && raw);
    DISPATCH_CHECK(raw->AddRef() == 1);
    ATL::CComPtr<IOpenATLInvokeFixture> typed;
    ATL::CComPtr<IDispatch> dispatch;
    DISPATCH_CHECK(raw->QueryInterface(IID_IOpenATLInvokeFixture,
        reinterpret_cast<void**>(&typed.p)) == S_OK && typed.p);
    DISPATCH_CHECK(raw->QueryInterface(IID_IDispatch,
        reinterpret_cast<void**>(&dispatch.p)) == S_OK && dispatch.p);
    LONG direct = 0;
    DISPATCH_CHECK(typed->Combine(4, 2, &direct) == S_OK && direct == 42);

    LPOLESTR names[] = {const_cast<LPOLESTR>(L"Combine"),
        const_cast<LPOLESTR>(L"requiredValue"),
        const_cast<LPOLESTR>(L"optionalValue")};
    DISPID ids[] = {DISPID_UNKNOWN, DISPID_UNKNOWN, DISPID_UNKNOWN};
    DISPATCH_CHECK(dispatch->GetIDsOfNames(IID_NULL, names, 3,
        LOCALE_SYSTEM_DEFAULT, ids) == S_OK && ids[0] == 301 &&
        ids[1] != DISPID_UNKNOWN && ids[2] != DISPID_UNKNOWN && ids[1] != ids[2]);

    ATL::CComVariant required(static_cast<LONG>(4));
    DISPPARAMS defaulted{&required, nullptr, 1, 0};
    ATL::CComVariant defaultedResult;
    DISPATCH_CHECK(dispatch->Invoke(ids[0], IID_NULL, LOCALE_SYSTEM_DEFAULT,
        DISPATCH_METHOD, &defaulted, &defaultedResult, nullptr, nullptr) == S_OK &&
        defaultedResult.vt == VT_I4 && defaultedResult.lVal == 47);

    ATL::CComVariant explicitArguments[2] = {
        ATL::CComVariant(static_cast<LONG>(2)),
        ATL::CComVariant(static_cast<LONG>(4))};
    DISPPARAMS explicitValues{explicitArguments, nullptr, 2, 0};
    ATL::CComVariant explicitResult;
    DISPATCH_CHECK(dispatch->Invoke(ids[0], IID_NULL, LOCALE_SYSTEM_DEFAULT,
        DISPATCH_METHOD, &explicitValues, &explicitResult, nullptr, nullptr) == S_OK &&
        explicitResult.vt == VT_I4 && explicitResult.lVal == 42);

    ATL::CComVariant namedArguments[2] = {
        ATL::CComVariant(static_cast<LONG>(9)),
        ATL::CComVariant(static_cast<LONG>(5))};
    DISPPARAMS namedValues{namedArguments, &ids[2], 2, 1};
    ATL::CComVariant namedResult;
    DISPATCH_CHECK(dispatch->Invoke(ids[0], IID_NULL, LOCALE_SYSTEM_DEFAULT,
        DISPATCH_METHOD, &namedValues, &namedResult, nullptr, nullptr) == S_OK &&
        namedResult.vt == VT_I4 && namedResult.lVal == 59);

    ATL::CComVariant wrongRequired(L"not-a-number");
    DISPPARAMS wrongValues{&wrongRequired, nullptr, 1, 0};
    UINT wrongArgument = 99;
    DISPATCH_CHECK(dispatch->Invoke(ids[0], IID_NULL, LOCALE_SYSTEM_DEFAULT,
        DISPATCH_METHOD, &wrongValues, nullptr, nullptr, &wrongArgument) ==
        DISP_E_TYPEMISMATCH && wrongArgument == 0);

    DISPPARAMS empty{};
    DISPATCH_CHECK(dispatch->Invoke(ids[0], IID_NULL, LOCALE_SYSTEM_DEFAULT,
        DISPATCH_METHOD, &empty, nullptr, nullptr, nullptr) == DISP_E_BADPARAMCOUNT);
    dispatch.Release(); typed.Release();
    DISPATCH_CHECK(raw->Release() == 0);
    return true;
}
} // namespace

bool RunAtlDispatchTests() {
    ULONG_PTR token = 0;
    DISPATCH_CHECK(::CoGetContextToken(&token) == CO_E_NOTINITIALIZED);
    {
        Apartment scope;
        DISPATCH_CHECK(scope.status == S_OK);
        DISPATCH_CHECK(TestRealOleTypeInfo());
        DISPATCH_CHECK(TestOptionalDefaultArguments());
        DISPATCH_CHECK(TestMissingTypeLibrary());
        DISPATCH_CHECK(RunAtlFileTypeLibTests());
    }
    DISPATCH_CHECK(::CoGetContextToken(&token) == CO_E_NOTINITIALIZED);
    return true;
}
