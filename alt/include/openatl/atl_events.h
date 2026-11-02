// OpenATL: typed Automation event sinks, independent of member-pointer ABI.
#ifndef OPENATL_ATL_EVENTS_H
#define OPENATL_ATL_EVENTS_H
#pragma once
#include <ocidl.h>
#include <olectl.h>
#include <array>
#include <functional>
#include <type_traits>
#include <utility>
#include <vector>

namespace ATL {
struct _ATL_FUNC_INFO {
    CALLCONV cc;
    VARTYPE vtReturn;
    SHORT nParams;
    VARTYPE pVarTypes[8];
};

inline HRESULT AtlAdvise(IUnknown* source, IUnknown* sink, const IID& iid, DWORD* cookie) noexcept {
    if (!cookie) return E_POINTER;
    *cookie = 0;
    if (!source || !sink) return E_POINTER;
    CComPtr<IConnectionPointContainer> container;
    HRESULT hr = source->QueryInterface(IID_IConnectionPointContainer, reinterpret_cast<void**>(&container));
    if (FAILED(hr)) return hr;
    CComPtr<IConnectionPoint> point;
    hr = container->FindConnectionPoint(iid, &point);
    return FAILED(hr) ? hr : point->Advise(sink, cookie);
}
inline HRESULT AtlUnadvise(IUnknown* source, const IID& iid, DWORD cookie) noexcept {
    if (!source) return E_POINTER;
    CComPtr<IConnectionPointContainer> container;
    HRESULT hr = source->QueryInterface(IID_IConnectionPointContainer, reinterpret_cast<void**>(&container));
    if (FAILED(hr)) return hr;
    CComPtr<IConnectionPoint> point;
    hr = container->FindConnectionPoint(iid, &point);
    return FAILED(hr) ? hr : point->Unadvise(cookie);
}

namespace detail {
template<UINT, class> struct SinkMapBinding;
struct EventEntry {
    UINT control;
    const IID* iid;
    DISPID member;
    const _ATL_FUNC_INFO* info;
    HRESULT (*call)(void*, DISPPARAMS*, const _ATL_FUNC_INFO*, UINT*);
    HRESULT (*connection)(void*, IUnknown*, const IID*, bool);
    bool (*connected)(void*);
};
template<class A> bool EventAccepts(VARTYPE type) {
    if constexpr (std::is_same_v<A, BSTR>) return type == VT_BSTR;
    else if constexpr (std::is_same_v<A, IDispatch*>) return type == VT_DISPATCH;
    else if constexpr (std::is_same_v<A, IUnknown*>) return type == VT_UNKNOWN;
    else if constexpr (std::is_same_v<A, VARIANT*>) return type == (VT_BYREF | VT_VARIANT);
    else if constexpr (std::is_same_v<A, VARIANT>) return type == VT_VARIANT;
    else if constexpr (std::is_pointer_v<A>) return (type & VT_BYREF) && EventAccepts<std::remove_pointer_t<A>>(type & ~VT_BYREF);
    else if constexpr (std::is_enum_v<A>) return EventAccepts<std::underlying_type_t<A>>(type);
    else if constexpr (std::is_integral_v<A>) {
        if constexpr (sizeof(A) == 1) return type == VT_I1 || type == VT_UI1;
        if constexpr (sizeof(A) == 2) return type == VT_I2 || type == VT_UI2 || type == VT_BOOL;
        if constexpr (sizeof(A) == 4) return type == VT_I4 || type == VT_UI4 || type == VT_INT || type == VT_UINT;
        if constexpr (sizeof(A) == 8) return type == VT_I8 || type == VT_UI8;
        return false;
    } else if constexpr (std::is_floating_point_v<A>) {
        if constexpr (sizeof(A) == 4) return type == VT_R4;
        if constexpr (sizeof(A) == 8) return type == VT_R8 || type == VT_DATE;
        return false;
    }
    else return false;
}
template<class A> A EventArgument(VARIANTARG& v) {
    if constexpr (std::is_same_v<A, BSTR>) return v.bstrVal;
    else if constexpr (std::is_same_v<A, IDispatch*>) return v.pdispVal;
    else if constexpr (std::is_same_v<A, IUnknown*>) return v.punkVal;
    else if constexpr (std::is_same_v<A, VARIANT*>) return v.pvarVal;
    else if constexpr (std::is_same_v<A, VARIANT>) return v;
    else if constexpr (std::is_enum_v<A>) return static_cast<A>(EventArgument<std::underlying_type_t<A>>(v));
    else if constexpr (std::is_pointer_v<A>) return static_cast<A>(v.byref);
    else {
        switch (v.vt) {
        case VT_I1: return static_cast<A>(v.cVal);
        case VT_UI1: return static_cast<A>(v.bVal);
        case VT_I2: return static_cast<A>(v.iVal);
        case VT_UI2: return static_cast<A>(v.uiVal);
        case VT_BOOL: return static_cast<A>(v.boolVal);
        case VT_I4: case VT_INT: return static_cast<A>(v.lVal);
        case VT_UI4: case VT_UINT: return static_cast<A>(v.ulVal);
        case VT_I8: return static_cast<A>(v.llVal);
        case VT_UI8: return static_cast<A>(v.ullVal);
        case VT_R4: return static_cast<A>(v.fltVal);
        case VT_R8: case VT_DATE: return static_cast<A>(v.dblVal);
        default: return A{};
        }
    }
}
template<class R, class C, class... A> struct EventSignature {
    using Class = C;
    template<auto Method, size_t... I>
    static HRESULT Call(void* object, DISPPARAMS* params, const _ATL_FUNC_INFO* info,
                        UINT* argument, std::index_sequence<I...>) {
        if (!info || info->nParams != sizeof...(A) || info->nParams > 8 ||
            (info->cc != CC_STDCALL && info->cc != CC_CDECL)) return E_INVALIDARG;
        if (!(EventAccepts<A>(info->pVarTypes[I]) && ...)) return E_INVALIDARG;
        if (!params || params->cArgs != sizeof...(A)) return DISP_E_BADPARAMCOUNT;
        if (params->cNamedArgs) return DISP_E_NONAMEDARGS;
        if (params->cArgs && !params->rgvarg) return E_POINTER;
        struct Arguments {
            std::array<VARIANTARG, sizeof...(A)> values{};
            ~Arguments() { for (auto& value : values) ::VariantClear(&value); }
        } converted;
        for (UINT i = 0; i < sizeof...(A); ++i) {
            const UINT reverse = params->cArgs - 1 - i;
            VARIANTARG& original = params->rgvarg[reverse];
            const VARTYPE type = info->pVarTypes[i];
            HRESULT hr;
            if (type & VT_BYREF) {
                if (original.vt != type || !original.byref) hr = DISP_E_TYPEMISMATCH;
                else { converted.values[i] = original; hr = S_OK; }
            } else if (type == VT_VARIANT) {
                hr = ::VariantCopyInd(&converted.values[i], &original);
            } else {
                hr = ::VariantChangeType(&converted.values[i], &original, 0, type);
            }
            if (FAILED(hr)) { if (argument) *argument = reverse; return hr; }
        }
        if constexpr (std::is_void_v<R>) {
            std::invoke(Method, *static_cast<C*>(object), EventArgument<A>(converted.values[I])...);
            return S_OK;
        } else {
            static_assert(std::is_same_v<R, HRESULT>, "Event handlers return void or HRESULT");
            return std::invoke(Method, *static_cast<C*>(object), EventArgument<A>(converted.values[I])...);
        }
    }
};
template<class M> struct EventTraits;
template<class R, class C, class... A>
struct EventTraits<R (C::*)(A...)> : EventSignature<R, C, A...> {
    using Sequence = std::index_sequence_for<A...>;
};
#if defined(_M_IX86) || defined(__i386__)
template<class R, class C, class... A>
struct EventTraits<R (__stdcall C::*)(A...)> : EventSignature<R, C, A...> {
    using Sequence = std::index_sequence_for<A...>;
};
#endif
template<auto Method> HRESULT InvokeEvent(void* object, DISPPARAMS* params,
                                          const _ATL_FUNC_INFO* info, UINT* argument) {
    using Traits = EventTraits<decltype(Method)>;
    try { return Traits::template Call<Method>(object, params, info, argument, typename Traits::Sequence{}); }
    catch (const CAtlException& e) { return e.m_hr; }
    catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
    catch (...) { return DISP_E_EXCEPTION; }
}
template<class Owner, auto Method> HRESULT InvokeMappedEvent(void* object, DISPPARAMS* params,
    const _ATL_FUNC_INFO* info, UINT* argument) {
    using Class = typename EventTraits<decltype(Method)>::Class;
    return InvokeEvent<Method>(static_cast<Class*>(static_cast<Owner*>(object)), params, info, argument);
}
} // namespace detail

inline HRESULT WINAPI AtlGetObjectSourceInterface(IUnknown* object, GUID* library,
    IID* source, unsigned short* major, unsigned short* minor) noexcept;

// As in ATL, the containing C++ object owns this sink's lifetime. Unadvise
// before destroying it; AddRef/Release do not delete a window or stack owner.
template<UINT nID, class T, const IID* pdiid>
class IDispEventSimpleImpl : public IDispatch {
    template<UINT, class> friend struct detail::SinkMapBinding;
public:
    IDispEventSimpleImpl() noexcept : iid_(IID_NULL) {
        if constexpr (pdiid != nullptr) iid_ = *pdiid;
    }
    IDispEventSimpleImpl(const IDispEventSimpleImpl&) = delete;
    IDispEventSimpleImpl& operator=(const IDispEventSimpleImpl&) = delete;
    ~IDispEventSimpleImpl() { if (point_) point_->Unadvise(m_dwEventCookie); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** output) override {
        if (!output) return E_POINTER;
        *output = nullptr;
        if (iid != IID_IUnknown && iid != IID_IDispatch && iid != iid_) return E_NOINTERFACE;
        *output = static_cast<IDispatch*>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT* count) override {
        if (count) *count = 0;
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT, LCID, ITypeInfo** info) override {
        if (info) *info = nullptr;
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID, LPOLESTR*, UINT, LCID, DISPID*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Invoke(DISPID member, REFIID iid, LCID, WORD flags, DISPPARAMS* params,
                                     VARIANT* result, EXCEPINFO*, UINT* argument) override {
        return InvokeMapped(member, iid, 0, flags, params, result, argument,
            [](DISPID, LCID, _ATL_FUNC_INFO&) { return E_NOTIMPL; });
    }
protected:
    template<class Resolver>
    HRESULT InvokeMapped(DISPID member, REFIID iid, LCID locale, WORD flags, DISPPARAMS* params,
                         VARIANT* result, UINT* argument, Resolver&& resolve) {
        if (iid != IID_NULL) return DISP_E_UNKNOWNINTERFACE;
        if (!(flags & DISPATCH_METHOD)) return DISP_E_MEMBERNOTFOUND;
        if (result) ::VariantInit(result);
        for (auto entry = T::_GetSinkMap(); entry->iid; ++entry)
            if (entry->control == nID && (*entry->iid == IID_NULL || *entry->iid == iid_) && entry->member == member) {
                _ATL_FUNC_INFO discovered{};
                const _ATL_FUNC_INFO* info = entry->info;
                if (!info) {
                    const HRESULT hr = resolve(member, locale, discovered);
                    if (FAILED(hr)) return hr;
                    info = &discovered;
                }
                return entry->call(static_cast<T*>(this), params, info, argument);
            }
        return DISP_E_MEMBERNOTFOUND;
    }
    HRESULT ConnectPoint(IUnknown* source, REFIID selected) noexcept {
        CComPtr<IConnectionPointContainer> container;
        HRESULT hr = source->QueryInterface(IID_IConnectionPointContainer, reinterpret_cast<void**>(&container.p));
        if (FAILED(hr)) return hr;
        if (!container.p) return E_UNEXPECTED;
        CComPtr<IConnectionPoint> point;
        hr = container->FindConnectionPoint(selected, &point.p);
        if (FAILED(hr)) return hr;
        if (!point.p) return E_UNEXPECTED;
        const IID previous = iid_;
        iid_ = selected; // QI/Invoke may occur synchronously inside Advise.
        DWORD cookie = 0;
        hr = point->Advise(static_cast<IDispatch*>(this), &cookie);
        if (hr == S_OK && cookie) { point_ = point; m_dwEventCookie = cookie; return S_OK; }
        iid_ = previous;
        return FAILED(hr) ? hr : E_UNEXPECTED;
    }
    HRESULT DisconnectPoint() noexcept {
        if (!point_) return CONNECT_E_NOCONNECTION;
        const HRESULT hr = point_->Unadvise(m_dwEventCookie);
        if (hr == S_OK) { point_.Release(); m_dwEventCookie = 0xFEFEFEFE; }
        return hr;
    }
public:
    HRESULT DispEventAdvise(IUnknown* source, const IID* iid = pdiid) noexcept {
        if (!source) return E_POINTER;
        if (changing_) return E_UNEXPECTED;
        if (point_) return CONNECT_E_ADVISELIMIT;
        Change guard(changing_);
        IID selected = iid ? *iid : IID_NULL;
        if (selected == IID_NULL) {
            GUID library = GUID_NULL;
            unsigned short major = 0, minor = 0;
            const HRESULT hr = AtlGetObjectSourceInterface(source, &library, &selected, &major, &minor);
            if (FAILED(hr)) return hr;
        }
        return ConnectPoint(source, selected);
    }
    HRESULT DispEventUnadvise(IUnknown* source, const IID* iid = pdiid) noexcept {
        if (!source) return E_POINTER;
        if (changing_) return E_UNEXPECTED;
        if (!point_) return CONNECT_E_NOCONNECTION;
        Change guard(changing_);
        if (iid && *iid != IID_NULL && *iid != iid_) return CONNECT_E_NOCONNECTION;
        CComPtr<IConnectionPointContainer> owner;
        HRESULT hr = point_->GetConnectionPointContainer(&owner);
        if (FAILED(hr)) return hr;
        CComPtr<IUnknown> actual, requested;
        hr = owner->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&actual));
        if (FAILED(hr)) return hr;
        hr = source->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&requested));
        if (FAILED(hr)) return hr;
        if (actual.p != requested.p) return CONNECT_E_NOCONNECTION;
        hr = DisconnectPoint();
        if (hr == S_OK) {
            if constexpr (pdiid != nullptr) iid_ = *pdiid;
            else iid_ = IID_NULL;
        }
        return hr;
    }
    HRESULT Advise(IUnknown* source) noexcept { return DispEventAdvise(source, &IID_NULL); }
    HRESULT Unadvise(IUnknown* source) noexcept { return DispEventUnadvise(source, &IID_NULL); }
    DWORD m_dwEventCookie = 0xFEFEFEFE;
protected:
    struct Change { bool& flag; explicit Change(bool& value) : flag(value) { flag = true; } ~Change() { flag = false; } };
    bool changing_ = false;
    IID iid_;
    CComPtr<IConnectionPoint> point_;
};

namespace detail {
struct EventSourceInformation {
    GUID library = GUID_NULL;
    IID source = IID_NULL;
    WORD major = 0, minor = 0;
    CComPtr<ITypeInfo> info;
};
inline HRESULT DefaultEventSource(ITypeInfo* coclass, IID& selected) noexcept {
    selected = IID_NULL;
    if (!coclass) return E_POINTER;
    TYPEATTR* attributes = nullptr;
    HRESULT hr = coclass->GetTypeAttr(&attributes);
    if (FAILED(hr)) return hr;
    if (!attributes) return E_UNEXPECTED;
    const TYPEKIND kind = attributes->typekind;
    const WORD count = attributes->cImplTypes;
    coclass->ReleaseTypeAttr(attributes);
    if (kind != TKIND_COCLASS) return E_UNEXPECTED;
    IID found = IID_NULL;
    for (UINT n = 0; n < count; ++n) {
        INT flags = 0;
        hr = coclass->GetImplTypeFlags(n, &flags);
        if (FAILED(hr)) return hr;
        if ((flags & (IMPLTYPEFLAG_FDEFAULT | IMPLTYPEFLAG_FSOURCE)) !=
            (IMPLTYPEFLAG_FDEFAULT | IMPLTYPEFLAG_FSOURCE)) continue;
        if (found != IID_NULL) return TYPE_E_AMBIGUOUSNAME;
        HREFTYPE reference = 0;
        hr = coclass->GetRefTypeOfImplType(n, &reference);
        if (FAILED(hr)) return hr;
        CComPtr<ITypeInfo> source;
        hr = coclass->GetRefTypeInfo(reference, &source.p);
        if (FAILED(hr)) return hr;
        if (!source.p) return E_UNEXPECTED;
        attributes = nullptr;
        hr = source->GetTypeAttr(&attributes);
        if (FAILED(hr)) return hr;
        if (!attributes) return E_UNEXPECTED;
        const bool correct = attributes->typekind == TKIND_DISPATCH && attributes->guid != IID_NULL;
        found = attributes->guid;
        source->ReleaseTypeAttr(attributes);
        if (!correct) return E_UNEXPECTED;
    }
    if (found == IID_NULL) return CONNECT_E_NOCONNECTION;
    selected = found; return S_OK;
}
inline HRESULT GetEventSourceInformation(IUnknown* object, REFIID requested,
                                         EventSourceInformation& output) noexcept {
    output = {};
    if (!object) return E_POINTER;
    CComPtr<IUnknown> keepAlive(object);
    CComPtr<ITypeInfo> coclass;
    CComPtr<ITypeLib> library;
    IID selected = requested;
    CComPtr<IProvideClassInfo2> provider2;
    HRESULT hr = object->QueryInterface(IID_IProvideClassInfo2, reinterpret_cast<void**>(&provider2.p));
    if (hr == S_OK) {
        if (!provider2.p) return E_UNEXPECTED;
        if (selected == IID_NULL) {
            hr = provider2->GetGUID(GUIDKIND_DEFAULT_SOURCE_DISP_IID, &selected);
            if (FAILED(hr)) return hr;
            if (hr != S_OK || selected == IID_NULL) return E_UNEXPECTED;
        }
        hr = provider2->GetClassInfo(&coclass.p);
        if (FAILED(hr)) return hr;
        if (hr != S_OK || !coclass.p) return E_UNEXPECTED;
    } else {
        if (hr != E_NOINTERFACE) return FAILED(hr) ? hr : E_UNEXPECTED;
        CComPtr<IProvideClassInfo> provider;
        hr = object->QueryInterface(IID_IProvideClassInfo, reinterpret_cast<void**>(&provider.p));
        if (hr == S_OK) {
            if (!provider.p) return E_UNEXPECTED;
            hr = provider->GetClassInfo(&coclass.p);
            if (FAILED(hr)) return hr;
            if (hr != S_OK || !coclass.p) return E_UNEXPECTED;
        } else {
            if (hr != E_NOINTERFACE) return FAILED(hr) ? hr : E_UNEXPECTED;
            CComPtr<IDispatch> dispatch;
            hr = object->QueryInterface(IID_IDispatch, reinterpret_cast<void**>(&dispatch.p));
            if (FAILED(hr)) return hr;
            if (hr != S_OK || !dispatch.p) return E_UNEXPECTED;
            CComPtr<ITypeInfo> incoming;
            hr = dispatch->GetTypeInfo(0, LOCALE_USER_DEFAULT, &incoming.p);
            if (FAILED(hr)) return hr;
            if (hr != S_OK || !incoming.p) return E_UNEXPECTED;
            UINT index = 0;
            hr = incoming->GetContainingTypeLib(&library.p, &index);
            if (FAILED(hr)) return hr;
            if (hr != S_OK || !library.p) return E_UNEXPECTED;
            if (selected == IID_NULL) {
                CComPtr<IPersist> persist;
                hr = object->QueryInterface(IID_IPersist, reinterpret_cast<void**>(&persist.p));
                if (FAILED(hr)) return hr;
                if (hr != S_OK || !persist.p) return E_UNEXPECTED;
                CLSID clsid = CLSID_NULL;
                hr = persist->GetClassID(&clsid);
                if (FAILED(hr)) return hr;
                if (hr != S_OK || clsid == CLSID_NULL) return E_UNEXPECTED;
                hr = library->GetTypeInfoOfGuid(clsid, &coclass.p);
                if (FAILED(hr)) return hr;
                if (hr != S_OK || !coclass.p) return E_UNEXPECTED;
            }
        }
    }
    if (coclass.p) {
        TYPEATTR* type = nullptr;
        hr = coclass->GetTypeAttr(&type);
        if (FAILED(hr)) return hr;
        if (!type) return E_UNEXPECTED;
        const bool correct = type->typekind == TKIND_COCLASS;
        coclass->ReleaseTypeAttr(type);
        if (!correct) return E_UNEXPECTED;
    }
    if (!library.p) {
        UINT index = 0;
        hr = coclass->GetContainingTypeLib(&library.p, &index);
        if (FAILED(hr)) return hr;
        if (hr != S_OK || !library.p) return E_UNEXPECTED;
    }
    if (selected == IID_NULL) {
        hr = DefaultEventSource(coclass.p, selected);
        if (FAILED(hr)) return hr;
    }
    EventSourceInformation candidate;
    TLIBATTR* attributes = nullptr;
    hr = library->GetLibAttr(&attributes);
    if (FAILED(hr)) return hr;
    if (!attributes) return E_UNEXPECTED;
    candidate.library = attributes->guid;
    candidate.major = attributes->wMajorVerNum; candidate.minor = attributes->wMinorVerNum;
    library->ReleaseTLibAttr(attributes);
    if (candidate.library == GUID_NULL) return E_UNEXPECTED;
    hr = library->GetTypeInfoOfGuid(selected, &candidate.info.p);
    if (FAILED(hr)) return hr;
    if (hr != S_OK || !candidate.info.p) return E_UNEXPECTED;
    TYPEATTR* type = nullptr;
    hr = candidate.info->GetTypeAttr(&type);
    if (FAILED(hr)) return hr;
    if (!type) return E_UNEXPECTED;
    const bool correct = type->typekind == TKIND_DISPATCH && type->guid == selected;
    candidate.info->ReleaseTypeAttr(type);
    if (!correct) return TYPE_E_ELEMENTNOTFOUND;
    candidate.source = selected;
    output = std::move(candidate); return S_OK;
}
inline HRESULT EventVariantType(ITypeInfo* info, const TYPEDESC& description,
                                VARTYPE& type, unsigned depth = 0) noexcept {
    type = VT_EMPTY;
    if (!info || depth >= 16) return E_INVALIDARG;
    if (description.vt == VT_USERDEFINED) {
        CComPtr<ITypeInfo> referenced;
        HRESULT hr = info->GetRefTypeInfo(description.hreftype, &referenced.p);
        if (FAILED(hr)) return hr;
        if (!referenced.p) return E_UNEXPECTED;
        TYPEATTR* attr = nullptr;
        hr = referenced->GetTypeAttr(&attr);
        if (FAILED(hr)) return hr;
        if (!attr) return E_UNEXPECTED;
        switch (attr->typekind) {
        case TKIND_ALIAS: hr = EventVariantType(referenced.p, attr->tdescAlias, type, depth + 1); break;
        case TKIND_ENUM: type = VT_I4; break;
        case TKIND_DISPATCH: type = VT_DISPATCH; break;
        case TKIND_INTERFACE: type = attr->guid == IID_IDispatch ? VT_DISPATCH : VT_UNKNOWN; break;
        default: hr = E_NOTIMPL; break;
        }
        referenced->ReleaseTypeAttr(attr);
        return hr;
    }
    if (description.vt == VT_PTR) {
        if (!description.lptdesc) return E_UNEXPECTED;
        HRESULT hr = EventVariantType(info, *description.lptdesc, type, depth + 1);
        if (FAILED(hr)) return hr;
        // A pointer to a user-defined COM interface is a value interface,
        // whereas a scalar/VARIANT pointer is an Automation BYREF argument.
        if (description.lptdesc->vt == VT_USERDEFINED && (type == VT_UNKNOWN || type == VT_DISPATCH)) return S_OK;
        if (type & VT_BYREF) return E_NOTIMPL;
        type |= VT_BYREF; return S_OK;
    }
    switch (description.vt) {
    case VT_I1: case VT_UI1: case VT_I2: case VT_UI2: case VT_I4: case VT_UI4:
    case VT_I8: case VT_UI8: case VT_INT: case VT_UINT: case VT_R4: case VT_R8:
    case VT_BOOL: case VT_DATE: case VT_BSTR: case VT_VARIANT: case VT_UNKNOWN:
    case VT_DISPATCH: case VT_VOID: case VT_HRESULT: case VT_EMPTY:
        type = description.vt; return S_OK;
    default: return E_NOTIMPL;
    }
}
}

inline HRESULT WINAPI AtlGetObjectSourceInterface(IUnknown* object, GUID* library,
    IID* source, unsigned short* major, unsigned short* minor) noexcept {
    if (library) *library = GUID_NULL;
    if (source) *source = IID_NULL;
    if (major) *major = 0;
    if (minor) *minor = 0;
    if (!object || !library || !source || !major || !minor) return E_POINTER;
    detail::EventSourceInformation information;
    const HRESULT hr = detail::GetEventSourceInformation(object, IID_NULL, information);
    if (FAILED(hr)) return hr;
    *library = information.library; *source = information.source;
    *major = information.major; *minor = information.minor;
    return S_OK;
}

template<UINT nID, class T, const IID* pdiid = &IID_NULL,
         const GUID* plibid = &GUID_NULL, WORD wMajor = 0, WORD wMinor = 0,
         class tihclass = CComTypeInfoHolder>
class IDispEventImpl : public IDispEventSimpleImpl<nID, T, pdiid> {
    using Base = IDispEventSimpleImpl<nID, T, pdiid>;
    CComPtr<ITypeInfo> bound_;
    static inline tihclass holder_{};
    HRESULT TypeInfo(LCID locale, ITypeInfo** output) noexcept {
        if (bound_) return bound_.CopyTo(output);
        if (!pdiid || !plibid || *pdiid == IID_NULL || *plibid == GUID_NULL) return TYPE_E_LIBNOTREGISTERED;
        return holder_.GetTI(pdiid, plibid, wMajor, wMinor, locale, output);
    }
public:
    using _tihclass = tihclass;
    ~IDispEventImpl() {
        if (this->point_ && this->DisconnectPoint() != S_OK) std::terminate();
    }
    HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT* output) override {
        if (!output) return E_POINTER; *output = 1; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT index, LCID locale, ITypeInfo** output) override {
        if (!output) return E_POINTER; *output = nullptr;
        if (index) return DISP_E_BADINDEX;
        const HRESULT hr = TypeInfo(locale, output);
        return SUCCEEDED(hr) && !*output ? E_UNEXPECTED : hr;
    }
    HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID iid, LPOLESTR* names, UINT count,
                                            LCID locale, DISPID* output) override {
        if (!names || !output) return E_POINTER;
        if (!count || count > 1024) return E_INVALIDARG;
        for (UINT n = 0; n < count; ++n) { output[n] = DISPID_UNKNOWN; if (!names[n]) return E_INVALIDARG; }
        if (iid != IID_NULL) return DISP_E_UNKNOWNINTERFACE;
        CComPtr<ITypeInfo> info;
        const HRESULT hr = GetTypeInfo(0, locale, &info.p);
        return FAILED(hr) ? hr : info->GetIDsOfNames(names, count, output);
    }
    VARTYPE GetUserDefinedType(ITypeInfo* info, HREFTYPE reference) noexcept {
        TYPEDESC description{}; description.vt = VT_USERDEFINED; description.hreftype = reference;
        VARTYPE type = VT_EMPTY;
        return SUCCEEDED(detail::EventVariantType(info, description, type)) ? type : VT_EMPTY;
    }
    HRESULT GetFuncInfoFromId(const IID& iid, DISPID member, LCID locale, _ATL_FUNC_INFO& output) noexcept {
        output = {};
        if (iid != this->iid_) return E_NOINTERFACE;
        CComPtr<ITypeInfo> info;
        HRESULT hr = GetTypeInfo(0, locale, &info.p);
        if (FAILED(hr)) return hr;
        TYPEATTR* attr = nullptr;
        hr = info->GetTypeAttr(&attr);
        if (FAILED(hr)) return hr;
        if (!attr) return E_UNEXPECTED;
        const WORD functions = attr->cFuncs;
        info->ReleaseTypeAttr(attr);
        for (UINT n = 0; n < functions; ++n) {
            FUNCDESC* function = nullptr;
            hr = info->GetFuncDesc(n, &function);
            if (FAILED(hr)) return hr;
            if (!function) return E_UNEXPECTED;
            struct Release { ITypeInfo* info; FUNCDESC* value; ~Release() { info->ReleaseFuncDesc(value); } } release{info.p, function};
            if (function->memid != member || function->invkind != INVOKE_FUNC) continue;
            _ATL_FUNC_INFO candidate{};
            if (function->cParams < 0 || function->cParams > 8 || function->cParamsOpt ||
                (function->callconv != CC_STDCALL && function->callconv != CC_CDECL)) return E_NOTIMPL;
            candidate.cc = function->callconv; candidate.nParams = function->cParams;
            hr = detail::EventVariantType(info.p, function->elemdescFunc.tdesc, candidate.vtReturn);
            if (FAILED(hr)) return hr;
            if (candidate.vtReturn != VT_VOID && candidate.vtReturn != VT_EMPTY && candidate.vtReturn != VT_HRESULT) return E_NOTIMPL;
            for (SHORT p = 0; p < function->cParams; ++p) {
                const auto& parameter = function->lprgelemdescParam[p];
                if (parameter.paramdesc.wParamFlags & (PARAMFLAG_FRETVAL | PARAMFLAG_FLCID | PARAMFLAG_FOPT)) return E_NOTIMPL;
                hr = detail::EventVariantType(info.p, parameter.tdesc, candidate.pVarTypes[p]);
                if (FAILED(hr)) return hr;
            }
            output = candidate; return S_OK;
        }
        return DISP_E_MEMBERNOTFOUND;
    }
    HRESULT STDMETHODCALLTYPE Invoke(DISPID member, REFIID iid, LCID locale, WORD flags,
        DISPPARAMS* params, VARIANT* result, EXCEPINFO*, UINT* argument) override {
        return this->InvokeMapped(member, iid, locale, flags, params, result, argument,
            [&](DISPID id, LCID language, _ATL_FUNC_INFO& info) { return GetFuncInfoFromId(this->iid_, id, language, info); });
    }
    HRESULT DispEventAdvise(IUnknown* source, const IID* iid = pdiid) noexcept {
        if (!source) return E_POINTER;
        if (this->changing_) return E_UNEXPECTED;
        if (this->point_) return CONNECT_E_ADVISELIMIT;
        typename Base::Change guard(this->changing_);
        IID selected = iid ? *iid : IID_NULL;
        CComPtr<ITypeInfo> candidate;
        if (selected == IID_NULL || !plibid || *plibid == GUID_NULL) {
            detail::EventSourceInformation information;
            const HRESULT hr = detail::GetEventSourceInformation(source, selected, information);
            if (FAILED(hr)) return hr;
            selected = information.source;
            if (!plibid || *plibid == GUID_NULL) {
                candidate = information.info;
            }
        }
        if (!candidate) {
            const HRESULT hr = holder_.GetTI(&selected, plibid, wMajor, wMinor, LOCALE_USER_DEFAULT, &candidate.p);
            if (FAILED(hr)) return hr;
        }
        if (!candidate.p) return E_UNEXPECTED;
        TYPEATTR* attr = nullptr;
        HRESULT hr = candidate->GetTypeAttr(&attr);
        if (FAILED(hr)) return hr;
        if (!attr) return E_UNEXPECTED;
        const bool correct = attr->guid == selected && attr->typekind == TKIND_DISPATCH;
        candidate->ReleaseTypeAttr(attr);
        if (!correct) return TYPE_E_ELEMENTNOTFOUND;
        bound_ = candidate;
        hr = this->ConnectPoint(source, selected);
        if (hr != S_OK) bound_.Release();
        return hr;
    }
    HRESULT DispEventUnadvise(IUnknown* source, const IID* iid = pdiid) noexcept {
        const HRESULT hr = Base::DispEventUnadvise(source, iid);
        if (hr == S_OK) {
            bound_.Release();
            this->iid_ = pdiid ? *pdiid : IID_NULL;
        }
        return hr;
    }
    HRESULT Advise(IUnknown* source) noexcept { return DispEventAdvise(source); }
    HRESULT Unadvise(IUnknown* source) noexcept { return DispEventUnadvise(source); }
};

namespace detail {
template<UINT id, class T, const IID* iid>
IDispEventSimpleImpl<id, T, iid>* LocateEventSink(IDispEventSimpleImpl<id, T, iid>* sink) noexcept { return sink; }
template<UINT id, class T, const IID* iid, const GUID* library, WORD major, WORD minor, class Holder>
IDispEventImpl<id, T, iid, library, major, minor, Holder>* LocateEventSink(
    IDispEventImpl<id, T, iid, library, major, minor, Holder>* sink) noexcept { return sink; }

template<UINT id, class T> struct SinkMapBinding {
    static bool Connected(void* object) noexcept {
        return LocateEventSink<id>(static_cast<T*>(object))->point_.p != nullptr;
    }
    static HRESULT Connection(void* object, IUnknown* source, const IID* iid, bool advise) noexcept {
        auto sink = LocateEventSink<id>(static_cast<T*>(object));
        if (advise) return sink->DispEventAdvise(source, iid);
        if (!sink->point_) return S_FALSE;
        // Disconnect the retained source, even after its host window changes
        // or disappears. The most-derived sink releases its own metadata.
        CComPtr<IConnectionPointContainer> owner;
        const HRESULT hr = sink->point_->GetConnectionPointContainer(&owner.p);
        if (FAILED(hr)) return hr;
        if (hr != S_OK || !owner.p) return E_UNEXPECTED;
        const HRESULT disconnected = sink->DispEventUnadvise(owner.p, &IID_NULL);
        return disconnected == S_OK || FAILED(disconnected) ? disconnected : E_UNEXPECTED;
    }
};
}

namespace detail {
template<class T, class = void> struct HasAxSinkMap : std::false_type {};
template<class T> struct HasAxSinkMap<T, std::void_t<decltype(T::_GetSinkMap())>> : std::true_type {};
}

// WTL's __if_exists bridge is unconditional on the MinGW compatibility path.
// A sinkless dialog has no subscriptions to change, matching CAxDialogImpl.
template<class T, std::enable_if_t<!detail::HasAxSinkMap<T>::value, int> = 0>
HRESULT AtlAdviseSinkMap(T* object, bool /*advise*/) noexcept {
    return object ? S_OK : E_POINTER;
}

template<class T, std::enable_if_t<detail::HasAxSinkMap<T>::value, int> = 0>
HRESULT AtlAdviseSinkMap(T* object, bool advise) noexcept {
    if (!object) return E_POINTER;
    auto owner = static_cast<typename T::_OpenATLSinkMapOwner*>(object);
    bool& changing = object->_OpenATLSinkMapChanging;
    if (changing) return E_UNEXPECTED;
    struct Change { bool& flag; explicit Change(bool& value) : flag(value) { flag = true; } ~Change() { flag = false; } } guard(changing);
    try {
        struct Group {
            const detail::EventEntry* entry;
            const IID* iid;
            CComPtr<IUnknown> source;
        };
        std::vector<Group> groups;
        for (auto entry = T::_GetSinkMap(); entry->iid; ++entry) {
            auto found = groups.end();
            for (auto i = groups.begin(); i != groups.end(); ++i)
                if (i->entry->control == entry->control) { found = i; break; }
            if (found == groups.end()) groups.push_back({entry, entry->iid, {}});
            else if (*entry->iid != IID_NULL) {
                if (*found->iid != IID_NULL && *found->iid != *entry->iid) return E_INVALIDARG;
                found->iid = entry->iid;
            }
        }
        if (!advise) {
            HRESULT failure = S_OK;
            for (auto& group : groups) {
                const HRESULT hr = group.entry->connection(owner, nullptr, group.iid, false);
                if (hr != S_OK && hr != S_FALSE && failure == S_OK) failure = FAILED(hr) ? hr : E_UNEXPECTED;
            }
            return failure;
        }
        // Acquire every source before changing any connection. One nID selects
        // one compiler-adjusted sink base; repeated handlers share that binding.
        for (auto& group : groups)
            if (group.entry->connected(owner)) return CONNECT_E_ADVISELIMIT;
        for (auto& group : groups) {
            const HRESULT hr = object->GetDlgControl(static_cast<int>(group.entry->control),
                IID_IUnknown, reinterpret_cast<void**>(&group.source.p));
            if (FAILED(hr)) return hr;
            if (hr != S_OK || !group.source.p) return E_UNEXPECTED;
        }
        for (size_t n = 0; n < groups.size(); ++n) {
            auto& group = groups[n];
            HRESULT hr = group.entry->connection(owner, group.source.p, group.iid, true);
            if (hr == S_OK) continue;
            HRESULT failure = FAILED(hr) ? hr : E_UNEXPECTED;
            while (n) {
                auto& previous = groups[--n];
                const HRESULT undone = previous.entry->connection(owner, nullptr, previous.iid, false);
                if (undone != S_OK && undone != S_FALSE) failure = FAILED(undone) ? undone : E_UNEXPECTED;
            }
            return failure;
        }
        return S_OK;
    } catch (...) { return detail::AtlObjectException(); }
}
} // namespace ATL

#define BEGIN_SINK_MAP(x) \
public: \
    using _OpenATLSinkMapOwner = x; \
    bool _OpenATLSinkMapChanging = false; \
    static const ATL::detail::EventEntry* _GetSinkMapFinder() { return _GetSinkMap(); } \
    static const ATL::detail::EventEntry* _GetSinkMap() { \
        using _SinkClass = x; \
        static const ATL::detail::EventEntry entries[] = {
#define SINK_ENTRY_INFO(id, iid, member, handler, info) \
    { id, &(iid), member, info, &ATL::detail::InvokeMappedEvent<_SinkClass, &_SinkClass::handler>, \
      &ATL::detail::SinkMapBinding<id, _SinkClass>::Connection, &ATL::detail::SinkMapBinding<id, _SinkClass>::Connected },
#define SINK_ENTRY_EX_P(id, piid, member, handler) \
    { id, piid, member, nullptr, &ATL::detail::InvokeMappedEvent<_SinkClass, handler>, \
      &ATL::detail::SinkMapBinding<id, _SinkClass>::Connection, &ATL::detail::SinkMapBinding<id, _SinkClass>::Connected },
#define SINK_ENTRY_EX(id, iid, member, handler) SINK_ENTRY_EX_P(id, &(iid), member, handler)
#define SINK_ENTRY(id, member, handler) SINK_ENTRY_EX(id, IID_NULL, member, handler)
#define END_SINK_MAP() \
    { 0, nullptr, 0, nullptr, nullptr, nullptr, nullptr } }; return entries; }
#endif
