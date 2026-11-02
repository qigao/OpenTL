// OpenATL - Clean-room ATL subset for WTL 10.0
// COM client utilities

#ifndef __ATLCOMCLI_H__
#define __ATLCOMCLI_H__

#pragma once

#include "openatl/platform.h"
#include <ole2.h>
#include <memory>
#include <limits>
#include <cwchar>
#include <cstring>
#include <exception>
// Initialize the native SDK and C++ runtime before the legacy _MSC_VER shim.
#include "atldef.h"
#include "openatl/text_encoding.h"

namespace ATL {

///////////////////////////////////////////////////////////////////////////////
// _NoAddRefReleaseOnCComPtr - prevents calling AddRef/Release directly on CComPtr

template <typename T>
class _NoAddRefReleaseOnCComPtr : public T {
private:
    STDMETHOD_(ULONG, AddRef)() = 0;
    STDMETHOD_(ULONG, Release)() = 0;
};

///////////////////////////////////////////////////////////////////////////////
// CComPtr - COM smart pointer

template <typename T>
class CComPtr {
public:
    T* p;

    CComPtr() noexcept : p(NULL) {}

    CComPtr(T* lp) noexcept : p(lp)
    {
        if (p != NULL)
            p->AddRef();
    }

    CComPtr(const CComPtr<T>& lp) noexcept : p(lp.p)
    {
        if (p != NULL)
            p->AddRef();
    }

    CComPtr(CComPtr<T>&& lp) noexcept : p(lp.p)
    {
        lp.p = NULL;
    }

    ~CComPtr()
    {
        if (p != NULL)
            p->Release();
    }

    T* operator=(T* lp) noexcept
    {
        if (p != lp) {
            T* pOld = p;
            p = lp;
            if (p != NULL)
                p->AddRef();
            if (pOld != NULL)
                pOld->Release();
        }
        return *this;
    }

    T* operator=(const CComPtr<T>& lp) noexcept
    {
        return operator=(lp.p);
    }

    CComPtr<T>& operator=(CComPtr<T>&& lp) noexcept
    {
        if (this != std::addressof(lp)) {
            if (p != NULL)
                p->Release();
            p = lp.p;
            lp.p = NULL;
        }
        return *this;
    }

    operator T*() const noexcept
    {
        return p;
    }

    T& operator*() const
    {
        ATLASSERT(p != NULL);
        return *p;
    }

    _NoAddRefReleaseOnCComPtr<T>* operator->() const noexcept
    {
        ATLASSERT(p != NULL);
        return (_NoAddRefReleaseOnCComPtr<T>*)p;
    }

    T** operator&() noexcept
    {
        ATLASSERT(p == NULL);
        return &p;
    }

    bool operator!() const noexcept
    {
        return (p == NULL);
    }

    bool operator<(T* pT) const noexcept
    {
        return p < pT;
    }

    bool operator!=(T* pT) const noexcept
    {
        return !operator==(pT);
    }

    bool operator==(T* pT) const noexcept
    {
        return p == pT;
    }

    T* Detach() noexcept
    {
        T* pt = p;
        p = NULL;
        return pt;
    }

    void Attach(T* p2) noexcept
    {
        if (p == p2) return; // Same raw pointer cannot be released first.
        T* old = p;
        p = p2;
        if (old != NULL) old->Release();
    }

    void Release() noexcept
    {
        T* pTemp = p;
        if (pTemp != NULL) {
            p = NULL;
            pTemp->Release();
        }
    }

    HRESULT CopyTo(T** ppT) noexcept
    {
        ATLASSERT(ppT != NULL);
        if (ppT == NULL)
            return E_POINTER;
        *ppT = p;
        if (p != NULL)
            p->AddRef();
        return S_OK;
    }

    template <typename Q>
    HRESULT QueryInterface(Q** pp) const noexcept
    {
        if (pp == NULL) return E_POINTER;
        *pp = NULL;
        if (p == NULL) return E_POINTER;
        return p->QueryInterface(__uuidof(Q), (void**)pp);
    }

    HRESULT CoCreateInstance(REFCLSID rclsid, LPUNKNOWN pUnkOuter = NULL,
        DWORD dwClsContext = CLSCTX_ALL) noexcept
    {
        if (p != NULL) return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
        return ::CoCreateInstance(rclsid, pUnkOuter, dwClsContext, __uuidof(T), (void**)&p);
    }

    HRESULT CoCreateInstance(LPCOLESTR szProgID, LPUNKNOWN pUnkOuter = NULL,
        DWORD dwClsContext = CLSCTX_ALL) noexcept
    {
        if (p != NULL) return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
        if (szProgID == NULL) return E_INVALIDARG;
        CLSID clsid;
        HRESULT hr = ::CLSIDFromProgID(szProgID, &clsid);
        if (SUCCEEDED(hr))
            hr = ::CoCreateInstance(clsid, pUnkOuter, dwClsContext, __uuidof(T), (void**)&p);
        return hr;
    }

    bool IsEqualObject(IUnknown* pOther) noexcept
    {
        if (p == NULL && pOther == NULL)
            return true;
        if (p == NULL || pOther == NULL)
            return false;

        CComPtr<IUnknown> first, second;
        IUnknown* raw1 = NULL;
        HRESULT hr = p->QueryInterface(__uuidof(IUnknown), (void**)&raw1);
        first.Attach(raw1);
        if (FAILED(hr) || raw1 == NULL) return false;
        IUnknown* raw2 = NULL;
        hr = pOther->QueryInterface(__uuidof(IUnknown), (void**)&raw2);
        second.Attach(raw2);
        if (FAILED(hr) || raw2 == NULL) return false;
        return first.p == second.p;
    }
};

///////////////////////////////////////////////////////////////////////////////
// CComQIPtr - COM query interface smart pointer

template <typename T, const IID* piid = &__uuidof(T)>
class CComQIPtr : public CComPtr<T> {
public:
    CComQIPtr() noexcept {}

    CComQIPtr(T* lp) noexcept : CComPtr<T>(lp) {}

    CComQIPtr(const CComQIPtr<T, piid>& lp) noexcept : CComPtr<T>(lp.p) {}

    CComQIPtr(IUnknown* lp) noexcept
    {
        if (lp != NULL)
            lp->QueryInterface(*piid, (void**)&this->p);
    }

    template<class Q>
    CComQIPtr(const CComPtr<Q>& other) noexcept
        : CComQIPtr(static_cast<IUnknown*>(other.p)) {}

    template<class Q>
    T* operator=(const CComPtr<Q>& other) noexcept {
        return operator=(static_cast<IUnknown*>(other.p));
    }

    T* operator=(T* lp) noexcept
    {
        return CComPtr<T>::operator=(lp);
    }

    T* operator=(const CComQIPtr<T, piid>& lp) noexcept
    {
        return CComPtr<T>::operator=(lp.p);
    }

    T* operator=(IUnknown* lp) noexcept
    {
        T* pOld = this->p;
        this->p = NULL;
        if (lp != NULL)
            lp->QueryInterface(*piid, (void**)&this->p);
        if (pOld != NULL)
            pOld->Release();
        return this->p;
    }
};

// MinGW's __uuidof emulation need not return the address of IID_IUnknown.
// Match the interface type so its default cannot instantiate duplicate
// IUnknown*/T* overloads; still honor an explicitly selected IID.
template <const IID* piid>
class CComQIPtr<IUnknown, piid> : public CComPtr<IUnknown> {
public:
    CComQIPtr() noexcept {}
    CComQIPtr(IUnknown* lp) noexcept {
        if (lp) lp->QueryInterface(*piid, reinterpret_cast<void**>(&this->p));
    }
    template<class Q>
    CComQIPtr(const CComPtr<Q>& other) noexcept
        : CComQIPtr(static_cast<IUnknown*>(other.p)) {}
    CComQIPtr(const CComQIPtr<IUnknown, piid>& lp) noexcept : CComPtr<IUnknown>(lp.p) {}

    IUnknown* operator=(IUnknown* lp) noexcept
    {
        IUnknown* resolved = nullptr;
        if (lp) lp->QueryInterface(*piid, reinterpret_cast<void**>(&resolved));
        IUnknown* previous = this->p;
        this->p = resolved;
        if (previous) previous->Release();
        return this->p;
    }

    template<class Q>
    IUnknown* operator=(const CComPtr<Q>& other) noexcept {
        return operator=(static_cast<IUnknown*>(other.p));
    }

    IUnknown* operator=(const CComQIPtr<IUnknown, piid>& lp) noexcept
    {
        return CComPtr<IUnknown>::operator=(lp.p);
    }
};

///////////////////////////////////////////////////////////////////////////////
// CComBSTR - BSTR wrapper

class CComBSTR {
public:
    BSTR m_str;

    CComBSTR() noexcept : m_str(NULL) {}

    CComBSTR(int nSize, LPCOLESTR sz = NULL)
        : m_str(nSize < 0 ? NULL :
                ::SysAllocStringLen(sz, static_cast<UINT>(nSize)))
    {
    }

    CComBSTR(LPCOLESTR pSrc)
        : m_str(::SysAllocString(pSrc))
    {
    }

    CComBSTR(const CComBSTR& src) : m_str(NULL)
    {
        if (src.m_str != NULL)
            m_str = ::SysAllocStringLen(src.m_str, ::SysStringLen(src.m_str));
    }

    CComBSTR(CComBSTR&& src) noexcept : m_str(src.m_str)
    {
        src.m_str = NULL;
    }

    CComBSTR(LPCSTR pSrc) : m_str(NULL)
    {
        if (pSrc != nullptr) {
            const HRESULT hr = AssignAnsi(pSrc);
            if (FAILED(hr)) {
#ifdef _ATL_NO_EXCEPTIONS
                std::terminate();
#else
                AtlThrow(hr);
#endif
            }
        }
    }

    HRESULT AssignAnsi(std::string_view text) noexcept { return AssignEncoded(::GetACP(), text); }
    HRESULT AssignUtf8(std::string_view text) noexcept { return AssignEncoded(CP_UTF8, text); }
    HRESULT ToUtf8(std::string& output) const noexcept {
        return OpenATL::Unicode::Utf16ToUtf8(
            std::wstring_view(m_str ? m_str : L"", m_str ? ::SysStringLen(m_str) : 0), output);
    }

    ~CComBSTR()
    {
        ::SysFreeString(m_str);
    }

    CComBSTR& operator=(const CComBSTR& src)
    {
        if (this != &src) {
            BSTR copy = src.m_str == NULL ? NULL :
                ::SysAllocStringLen(src.m_str, ::SysStringLen(src.m_str));
            if (src.m_str != NULL && copy == NULL)
                return *this; // Preserve the old value on OOM.
            ::SysFreeString(m_str);
            m_str = copy;
        }
        return *this;
    }

    CComBSTR& operator=(CComBSTR&& src) noexcept
    {
        if (this != std::addressof(src)) {
            ::SysFreeString(m_str);
            m_str = src.m_str;
            src.m_str = NULL;
        }
        return *this;
    }

    CComBSTR& operator=(LPCOLESTR pSrc)
    {
        // Allocate before release; pSrc may equal m_str or point inside it.
        BSTR copy = pSrc == NULL ? NULL : ::SysAllocString(pSrc);
        if (pSrc != NULL && copy == NULL) return *this;
        ::SysFreeString(m_str);
        m_str = copy;
        return *this;
    }

    operator BSTR() const noexcept { return m_str; }
    BSTR* operator&() noexcept { return &m_str; }

    unsigned int Length() const noexcept
    {
        return ::SysStringLen(m_str);
    }

    unsigned int ByteLength() const noexcept
    {
        return ::SysStringByteLen(m_str);
    }

    HRESULT WriteToStream(IStream* stream) noexcept
    {
        if (!stream) return E_POINTER;
        const ULONG bytes = ByteLength();
        if (m_str && bytes > (std::numeric_limits<ULONG>::max)() - sizeof(OLECHAR))
            return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
        const ULONG stored = m_str ? bytes + sizeof(OLECHAR) : 0;
        ULONG done = 0;
        HRESULT hr = stream->Write(&stored, sizeof(stored), &done);
        if (FAILED(hr)) return hr;
        if (done != sizeof(stored)) return STG_E_WRITEFAULT;
        if (!stored) return S_OK;
        done = 0;
        hr = stream->Write(m_str, stored, &done);
        if (FAILED(hr)) return hr;
        return done == stored ? S_OK : STG_E_WRITEFAULT;
    }

    // All reads use a finite default budget, including the legacy signature.
    // Applications with a larger trusted record may opt in explicitly.
    static constexpr ULONG kDefaultStreamByteBudget = 16u * 1024u * 1024u;

    HRESULT ReadFromStream(IStream* stream) noexcept
    {
        return ReadFromStream(stream, kDefaultStreamByteBudget);
    }

    HRESULT ReadFromStream(IStream* stream, ULONG maxPayloadBytes) noexcept
    {
        if (!stream) return E_POINTER;
        ULONG stored = 0, done = 0;
        HRESULT hr = stream->Read(&stored, sizeof(stored), &done);
        if (FAILED(hr)) return hr;
        if (done != sizeof(stored)) return STG_E_READFAULT;
        if (!stored) { Empty(); return S_OK; }
        if (stored < sizeof(OLECHAR)) return E_FAIL;
        const ULONG bytes = stored - sizeof(OLECHAR);
        // Count comes from the stream, not the actual remaining data size.
        // Reject it before allocating or reading the payload.
        if (bytes > maxPayloadBytes)
            return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
        BSTR staged = ::SysAllocStringByteLen(nullptr, bytes);
        if (!staged) return E_OUTOFMEMORY;
        done = 0;
        hr = stream->Read(staged, stored, &done);
        if (SUCCEEDED(hr) && done != stored) hr = STG_E_READFAULT;
        const auto raw = reinterpret_cast<const unsigned char*>(staged);
        if (SUCCEEDED(hr) && (raw[bytes] || raw[bytes + 1])) hr = E_FAIL;
        if (FAILED(hr)) { ::SysFreeString(staged); return hr; }
        Attach(staged);
        return S_OK;
    }

    BSTR Copy() const
    {
        if (m_str == NULL)
            return NULL;
        return ::SysAllocStringLen(m_str, ::SysStringLen(m_str));
    }

    HRESULT CopyTo(BSTR* pbstr) const
    {
        ATLASSERT(pbstr != NULL);
        if (pbstr == NULL)
            return E_POINTER;
        *pbstr = Copy();
        if (m_str != NULL && *pbstr == NULL)
            return E_OUTOFMEMORY;
        return S_OK;
    }

    void Attach(BSTR src) noexcept
    {
        if (m_str != src) {
            ::SysFreeString(m_str);
            m_str = src;
        }
    }

    BSTR Detach() noexcept
    {
        BSTR s = m_str;
        m_str = NULL;
        return s;
    }

    void Empty() noexcept
    {
        ::SysFreeString(m_str);
        m_str = NULL;
    }

    bool operator!() const noexcept
    {
        return m_str == NULL;
    }

    HRESULT Append(LPCOLESTR lpsz, int nLen) noexcept
    {
        if (nLen < 0) return E_INVALIDARG;
        if (nLen == 0) return S_OK;
        if (lpsz == NULL) return E_POINTER;
        const UINT oldLen = Length();
        if (oldLen > static_cast<UINT>((std::numeric_limits<int>::max)() - nLen))
            return E_INVALIDARG;
        const UINT size = oldLen + static_cast<UINT>(nLen);
        BSTR copy = ::SysAllocStringLen(NULL, size);
        if (copy == NULL) return E_OUTOFMEMORY;
        if (oldLen != 0)
            std::memcpy(copy, m_str, static_cast<size_t>(oldLen)*sizeof(OLECHAR));
        // lpsz can alias the original m_str until after copying it.
        std::memcpy(copy + oldLen, lpsz,
                    static_cast<size_t>(nLen)*sizeof(OLECHAR));
        copy[size] = L'\0';
        ::SysFreeString(m_str);
        m_str = copy;
        return S_OK;
    }

    HRESULT Append(LPCOLESTR lpsz) noexcept
    {
        if (lpsz == NULL)
            return S_OK;
        const size_t len = std::wcslen(lpsz);
        if (len > static_cast<size_t>((std::numeric_limits<int>::max)()))
            return E_INVALIDARG;
        return Append(lpsz, static_cast<int>(len));
    }

    HRESULT Append(const CComBSTR& bstrSrc) noexcept
    {
        if (bstrSrc.Length() >
            static_cast<UINT>((std::numeric_limits<int>::max)()))
            return E_INVALIDARG;
        return Append(bstrSrc.m_str, static_cast<int>(bstrSrc.Length()));
    }

    CComBSTR& operator+=(const CComBSTR& bstrSrc)
    {
        Append(bstrSrc);
        return *this;
    }

    bool operator==(const CComBSTR& bstrSrc) const noexcept
    {
        if (m_str == NULL && bstrSrc.m_str == NULL)
            return true;
        if (m_str == NULL || bstrSrc.m_str == NULL)
            return false;
        const UINT a = ::SysStringLen(m_str);
        const UINT b = ::SysStringLen(bstrSrc.m_str);
        return a == b && std::wmemcmp(m_str, bstrSrc.m_str, a) == 0;
    }

    bool operator!=(const CComBSTR& bstrSrc) const noexcept
    {
        return !operator==(bstrSrc);
    }

    bool operator<(const CComBSTR& bstrSrc) const noexcept
    {
        if (m_str == NULL)
            return bstrSrc.m_str != NULL;
        if (bstrSrc.m_str == NULL)
            return false;
        const UINT a = ::SysStringLen(m_str);
        const UINT b = ::SysStringLen(bstrSrc.m_str);
        const UINT common = a < b ? a : b;
        const int result = std::wmemcmp(m_str, bstrSrc.m_str, common);
        return result < 0 || (result == 0 && a < b);
    }
private:
    HRESULT AssignEncoded(UINT codePage, std::string_view text) noexcept {
        std::wstring converted;
        HRESULT hr = OpenATL::Unicode::CodePageToUtf16(codePage, text, converted);
        if (FAILED(hr)) return hr;
        const size_t maximum = ((std::numeric_limits<UINT>::max)() - sizeof(UINT)) / sizeof(OLECHAR) - 1;
        if (converted.size() > maximum) return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
        BSTR staged = ::SysAllocStringLen(converted.data(), static_cast<UINT>(converted.size()));
        if (!staged) return E_OUTOFMEMORY;
        ::SysFreeString(m_str);
        m_str = staged;
        return S_OK;
    }

};

///////////////////////////////////////////////////////////////////////////////
// CComVariant - VARIANT wrapper

class CComVariant : public tagVARIANT {
public:
    CComVariant() noexcept
    {
        ::VariantInit(this);
    }

    ~CComVariant()
    {
        Clear();
    }

    CComVariant(const VARIANT& varSrc)
    {
        ::VariantInit(this);
        (void)Copy(&varSrc);
    }

    CComVariant(const CComVariant& varSrc)
    {
        ::VariantInit(this);
        (void)Copy(static_cast<const VARIANT*>(&varSrc));
    }

    CComVariant(LPSAFEARRAY source)
    {
        ::VariantInit(this);
        if (!source) return;
        VARTYPE element = VT_EMPTY;
        HRESULT hr = ::SafeArrayGetVartype(source, &element);
        if (FAILED(hr)) AtlThrow(hr);
        SAFEARRAY* copy = nullptr;
        hr = ::SafeArrayCopy(source, &copy);
        if (FAILED(hr)) AtlThrow(hr);
        vt = static_cast<VARTYPE>(VT_ARRAY | element);
        parray = copy;
    }

    CComVariant(LPCOLESTR lpszSrc)
    {
        ::VariantInit(this);
        vt = VT_BSTR;
        bstrVal = ::SysAllocString(lpszSrc);
    }

    CComVariant(int nSrc) noexcept
    {
        ::VariantInit(this);
        vt = VT_I4;
        lVal = nSrc;
    }

    CComVariant(long nSrc) noexcept
    {
        ::VariantInit(this);
        vt = VT_I4;
        lVal = nSrc;
    }

    CComVariant(bool bSrc) noexcept
    {
        ::VariantInit(this);
        vt = VT_BOOL;
        boolVal = bSrc ? VARIANT_TRUE : VARIANT_FALSE;
    }

    CComVariant& operator=(const CComVariant& varSrc)
    {
        (void)Copy(static_cast<const VARIANT*>(&varSrc));
        return *this;
    }

    CComVariant& operator=(const VARIANT& varSrc)
    {
        (void)Copy(&varSrc);
        return *this;
    }

    CComVariant& operator=(LPCOLESTR lpszSrc)
    {
        BSTR copy = lpszSrc == NULL ? NULL : ::SysAllocString(lpszSrc);
        if (lpszSrc != NULL && copy == NULL) return *this;
        if (FAILED(Clear())) {
            ::SysFreeString(copy);
            return *this;
        }
        vt = VT_BSTR;
        bstrVal = copy;
        return *this;
    }

    CComVariant& operator=(int nSrc) noexcept
    {
        if (vt != VT_I4) {
            // A locked SAFEARRAY cannot be cleared. Preserve its VARIANT
            // tag and owner instead of overwriting a live resource.
            if (FAILED(Clear()))
                return *this;
            vt = VT_I4;
        }
        lVal = nSrc;
        return *this;
    }

    bool operator==(const VARIANT& varSrc) const noexcept
    {
        if (vt != varSrc.vt)
            return false;
        switch (vt) {
        case VT_EMPTY:
        case VT_NULL:
            return true;
        case VT_I4:
            return lVal == varSrc.lVal;
        case VT_BOOL:
            return boolVal == varSrc.boolVal;
        case VT_BSTR: {
            if (bstrVal == NULL || varSrc.bstrVal == NULL)
                return bstrVal == varSrc.bstrVal;
            const UINT len = ::SysStringLen(bstrVal);
            return len == ::SysStringLen(varSrc.bstrVal) &&
                std::wmemcmp(bstrVal, varSrc.bstrVal, len) == 0;
        }
        default:
            break;
        }
        return false;
    }

    bool operator!=(const VARIANT& varSrc) const noexcept
    {
        return !operator==(varSrc);
    }

    HRESULT Clear() noexcept
    {
        return ::VariantClear(this);
    }

    HRESULT Copy(const VARIANT* pSrc) noexcept
    {
        if (pSrc == NULL) return E_POINTER;
        if (pSrc == static_cast<const VARIANT*>(this)) return S_OK;
        VARIANT tmp;
        ::VariantInit(&tmp);
        HRESULT hr = ::VariantCopy(&tmp, const_cast<VARIANT*>(pSrc));
        if (FAILED(hr)) {
            (void)::VariantClear(&tmp);
            return hr;
        }
        hr = ::VariantClear(this);
        if (FAILED(hr)) {
            (void)::VariantClear(&tmp);
            return hr;
        }
        std::memcpy(static_cast<VARIANT*>(this), &tmp, sizeof(VARIANT));
        ::VariantInit(&tmp); // Ownership is now held only by this VARIANT.
        return S_OK;
    }

    // Exact successful wire length for the supported scalar/BSTR subset.
    // As in ATL, sizeof(VARTYPE) indicates an unsupported/overflow result;
    // it is also a valid size for VT_EMPTY and VT_NULL.
    ULONG GetSize() const noexcept
    {
        const int bytes = StreamScalarSize(vt);
        if (bytes >= 0)
            return static_cast<ULONG>(sizeof(VARTYPE)) +
                   static_cast<ULONG>(bytes);
        if (vt != VT_BSTR)
            return static_cast<ULONG>(sizeof(VARTYPE));

        constexpr ULONG envelope =
            static_cast<ULONG>(sizeof(VARTYPE) + sizeof(ULONG));
        if (!bstrVal)
            return envelope;
        const ULONG payload = ::SysStringByteLen(bstrVal);
        const ULONG terminator = static_cast<ULONG>(sizeof(OLECHAR));
        if (payload > (std::numeric_limits<ULONG>::max)() -
                      envelope - terminator)
            return static_cast<ULONG>(sizeof(VARTYPE));
        return envelope + payload + terminator;
    }

    // Bounded stream subset: no implicit coercion or COM activation.
    HRESULT WriteToStream(IStream* stream) noexcept
    {
        if (!stream) return E_POINTER;
        const int bytes = StreamScalarSize(vt);
        if (bytes < 0 && vt != VT_BSTR) return DISP_E_BADVARTYPE;
        ULONG done = 0;
        HRESULT hr = stream->Write(&vt, sizeof(vt), &done);
        if (FAILED(hr)) return hr;
        if (done != sizeof(vt)) return STG_E_WRITEFAULT;
        if (vt == VT_BSTR) {
            CComBSTR borrowed;
            borrowed.m_str = bstrVal;
            hr = borrowed.WriteToStream(stream);
            borrowed.Detach();
            return hr;
        }
        if (!bytes) return S_OK;
        done = 0;
        hr = stream->Write(&bVal, static_cast<ULONG>(bytes), &done);
        if (FAILED(hr)) return hr;
        return done == static_cast<ULONG>(bytes) ? S_OK : STG_E_WRITEFAULT;
    }

    HRESULT ReadFromStream(IStream* stream) noexcept
    {
        return ReadFromStream(stream, CComBSTR::kDefaultStreamByteBudget);
    }

    // maxBstrPayloadBytes is in bytes, excluding the wire terminator.
    HRESULT ReadFromStream(IStream* stream, ULONG maxBstrPayloadBytes) noexcept
    {
        if (!stream) return E_POINTER;
        VARTYPE type = VT_EMPTY;
        ULONG done = 0;
        HRESULT hr = stream->Read(&type, sizeof(type), &done);
        if (FAILED(hr)) return hr;
        if (done != sizeof(type)) return STG_E_READFAULT;
        const int bytes = StreamScalarSize(type);
        if (bytes < 0 && type != VT_BSTR) return DISP_E_BADVARTYPE;
        CComVariant staged;
        if (type == VT_BSTR) {
            CComBSTR text;
            hr = text.ReadFromStream(stream, maxBstrPayloadBytes);
            if (FAILED(hr)) return hr;
            staged.vt = VT_BSTR;
            staged.bstrVal = text.Detach();
        } else {
            staged.vt = type;
            if (bytes) {
                done = 0;
                hr = stream->Read(&staged.bVal, static_cast<ULONG>(bytes), &done);
                if (FAILED(hr)) return hr;
                if (done != static_cast<ULONG>(bytes)) return STG_E_READFAULT;
            }
        }
        return staged.Detach(this);
    }

    HRESULT ChangeType(VARTYPE vtNew, const VARIANT* pSrc = NULL) noexcept
    {
        VARIANT* pVar = const_cast<VARIANT*>(pSrc);
        if (pVar == NULL)
            pVar = this;
        return ::VariantChangeType(this, pVar, 0, vtNew);
    }

    HRESULT Detach(VARIANT* pDest) noexcept
    {
        if (pDest == NULL) return E_POINTER;
        if (pDest == static_cast<VARIANT*>(this)) return E_INVALIDARG;
        HRESULT hr = ::VariantClear(pDest);
        if (SUCCEEDED(hr)) {
            std::memcpy(pDest, static_cast<VARIANT*>(this), sizeof(VARIANT));
            ::VariantInit(this);
        }
        return hr;
    }
private:
    static int StreamScalarSize(VARTYPE type) noexcept
    {
        switch (type) {
        case VT_EMPTY: case VT_NULL: return 0;
        case VT_I1: case VT_UI1: return 1;
        case VT_I2: case VT_UI2: case VT_BOOL: return 2;
        case VT_I4: case VT_UI4: case VT_INT: case VT_UINT:
        case VT_R4: case VT_ERROR: return 4;
        case VT_I8: case VT_UI8: case VT_R8: case VT_CY: case VT_DATE: return 8;
        default: return -1;
        }
    }

};

} // namespace ATL

#ifndef _ATL_NO_AUTOMATIC_NAMESPACE
using namespace ATL;
#endif

#endif // __ATLCOMCLI_H__
