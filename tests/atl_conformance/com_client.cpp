/* OpenATL optional COM CLIENT: apartment ownership and actual Windows
 * system COM activation. No app-owned COM class, server, registrar or RGS.
 *
 * Each invocation is a fresh process/thread, making apartment checks
 * independent and permitting exact repetition on Windows x64 and ARM64.
 */
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
#include <objidl.h>
#include <ocidl.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cwchar>
#include <limits>
#include <utility>
#include <type_traits>
#include <atlcomcli.h>
#include "../stream_transfer_fault.h"

// WeaselTSF includes atlcomcli.h directly before declaring this alias.
// Keep this check before atlbase.h can supply its own namespace import.
template<typename I> using com_ptr = CComPtr<I>;
static_assert(std::is_same<com_ptr<IUnknown>, ATL::CComPtr<IUnknown>>::value,
              "Standalone atlcomcli.h must expose the default ATL namespace");
static_assert(std::is_same<CComQIPtr<IUnknown>, ATL::CComQIPtr<IUnknown>>::value &&
              std::is_same<CComBSTR, ATL::CComBSTR>::value &&
              std::is_same<CComVariant, ATL::CComVariant>::value,
              "Standalone COM client names must match the ATL implementation");
#include <atlbase.h>

#if !defined(OPENATL_REPOSITORY_HEADERS) || OPENATL_REPOSITORY_HEADERS != 0x4F41544CUL
#error "This target must compile repository-owned OpenATL headers"
#endif

#if defined(__RATL__)
#error "COM client must use pinned clean-room OpenATL headers"
#endif

static int failure(int code, const char* step, HRESULT hr) {
    std::fprintf(stderr, "ATL_COM_CLIENT_FAIL code=%d step=%s hr=0x%08lx\n",
                 code, step, static_cast<unsigned long>(hr));
    return code;
}
static bool no_apartment() {
    ULONG_PTR context=0;
    return ::CoGetContextToken(&context)==CO_E_NOTINITIALIZED;
}

// In-process test double only: never registered, activated or published.
// It rejects QueryInterface to check failed COM identity claims.
struct BrokenIdentity final : IUnknown {
    ULONG refs = 0;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** out) override {
        if (out == nullptr) return E_POINTER;
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { return --refs; }
};

static int com_client_ownership_regressions(IGlobalInterfaceTable* real) {
    ATL::CComPtr<IGlobalInterfaceTable> empty;
    IUnknown* stale = reinterpret_cast<IUnknown*>(static_cast<uintptr_t>(1));
    if (empty.QueryInterface(&stale) != E_POINTER || stale != nullptr ||
        empty.QueryInterface<IUnknown>(nullptr) != E_POINTER)
        return failure(50, "null_QueryInterface", E_FAIL);

    ATL::CComPtr<IGlobalInterfaceTable> git(real);
    IUnknown* id = nullptr;
    HRESULT hr = git.QueryInterface(&id);
    if (FAILED(hr) || id == nullptr)
        return failure(51, "valid_QueryInterface", hr);
    ATL::CComPtr<IUnknown> identity;
    identity.Attach(id);
    if (!git.IsEqualObject(id))
        return failure(52, "same_COM_identity", E_FAIL);
    git.Attach(git.p);
    if (!git.p || !git.IsEqualObject(id))
        return failure(53, "self_Attach_refcount", E_FAIL);

    BrokenIdentity b1, b2;
    ATL::CComPtr<IUnknown> broken(&b1);
    if (broken.IsEqualObject(&b2))
        return failure(54, "two_failed_QIs_are_not_equal", E_FAIL);
    ATL::CComPtr<IUnknown> moved(&b2);
    moved = std::move(broken);
    if (broken.p || moved.p != &b1 || b1.refs != 1 || b2.refs != 0)
        return failure(72, "CComPtr_move_replaces_owner", E_FAIL);
    auto& moveAlias = moved;
    moved = std::move(moveAlias);
    if (moved.p != &b1 || b1.refs != 1)
        return failure(73, "CComPtr_self_move_preserves_owner", E_FAIL);
    moved = std::move(broken);
    if (moved.p || b1.refs != 0 || b2.refs != 0)
        return failure(74, "CComPtr_move_empty_releases_owner", E_FAIL);

    ATL::CComPtr<IGlobalInterfaceTable> created;
    hr = created.CoCreateInstance(CLSID_StdGlobalInterfaceTable,
                                  nullptr, CLSCTX_INPROC_SERVER);
    if (FAILED(hr) || created.p == nullptr)
        return failure(55, "CComPtr_CoCreateInstance", hr);
    IGlobalInterfaceTable* original = created.p;
    hr = created.CoCreateInstance(CLSID_StdGlobalInterfaceTable,
                                  nullptr, CLSCTX_INPROC_SERVER);
    if (hr != HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS) ||
        created.p != original)
        return failure(56, "CComPtr_no_overwrite", hr);

    ATL::CComBSTR text(L"alpha");
    LPCOLESTR alias = text.m_str;
    text = alias;
    if (text.Length() != 5 || !text.m_str ||
        std::wcscmp(text.m_str,L"alpha") != 0)
        return failure(57, "BSTR_self_assignment", E_FAIL);
    if (text.Append(text.m_str, static_cast<int>(text.Length())) != S_OK ||
        text.Length() != 10 || std::wcscmp(text.m_str,L"alphaalpha") != 0)
        return failure(58, "BSTR_self_append", E_FAIL);
    if (text.Append(L"x", -1) != E_INVALIDARG ||
        text.Append(nullptr, 3) != E_POINTER ||
        text.Append(L"x", (std::numeric_limits<int>::max)()) != E_INVALIDARG ||
        text.Length() != 10)
        return failure(59, "BSTR_invalid_length", E_FAIL);

    const OLECHAR embeddedA[3] = {L'A',0,L'B'};
    const OLECHAR embeddedB[3] = {L'A',0,L'C'};
    ATL::CComBSTR a(3,embeddedA), same(3,embeddedA), b(3,embeddedB);
    if (!a.m_str || !same.m_str || !b.m_str ||
        a.Length() != 3 || !(a == same) || a == b || !(a < b))
        return failure(60, "binary_BSTR_comparison", E_FAIL);
    ATL::CComBSTR negative(-1,L"hello");
    if (negative.m_str != nullptr)
        return failure(61, "BSTR_negative_constructor", E_FAIL);

    ATL::CComPtr<IStream> strings;
    hr = ::CreateStreamOnHGlobal(nullptr, TRUE, &strings);
    if (FAILED(hr)) return failure(75, "BSTR_stream_create", hr);
    if (a.WriteToStream(strings) != S_OK)
        return failure(76, "BSTR_stream_write", E_FAIL);
    LARGE_INTEGER start{};
    ULONG stored = 0, read = 0;
    OLECHAR raw[4]{};
    if (strings->Seek(start, STREAM_SEEK_SET, nullptr) != S_OK ||
        strings->Read(&stored, sizeof(stored), &read) != S_OK ||
        read != sizeof(stored) || stored != sizeof(raw) ||
        strings->Read(raw, sizeof(raw), &read) != S_OK || read != sizeof(raw) ||
        std::memcmp(raw, embeddedA, sizeof(embeddedA)) || raw[3])
        return failure(77, "BSTR_stream_independent_bytes", E_FAIL);
    ATL::CComBSTR restored(L"previous");
    if (strings->Seek(start, STREAM_SEEK_SET, nullptr) != S_OK ||
        restored.ReadFromStream(strings) != S_OK || restored != a ||
        restored.ReadFromStream(strings) != STG_E_READFAULT || restored != a ||
        restored.ReadFromStream(nullptr) != E_POINTER ||
        restored.WriteToStream(nullptr) != E_POINTER)
        return failure(78, "BSTR_stream_read_and_truncation", E_FAIL);

    // Independent producers: null, allocated empty, odd-byte binary BSTR,
    // malformed count/terminator, and incomplete records.
    const unsigned char records[][9] = {
        {0,0,0,0}, {2,0,0,0,0,0}, {5,0,0,0,0x41,0,0x42,0,0},
        {1,0,0,0}, {2,0,0,0,1,0}, {4,0,0,0,0x41,0}, {0,0}
    };
    const ULONG sizes[] = {4,6,9,4,6,6,2};
    for (unsigned i=0;i<7;++i) {
        ATL::CComPtr<IStream> fixture;
        if (::CreateStreamOnHGlobal(nullptr,TRUE,&fixture) != S_OK ||
            fixture->Write(records[i],sizes[i],nullptr) != S_OK ||
            fixture->Seek(start,STREAM_SEEK_SET,nullptr) != S_OK)
            return failure(79,"BSTR_fixture_setup",E_FAIL);
        ATL::CComBSTR value(L"keep");
        hr = value.ReadFromStream(fixture);
        if (i<3) {
            const ULONG bytes[] = {0,0,3};
            if (hr != S_OK || value.ByteLength()!=bytes[i] ||
                (i==0 ? value.m_str!=nullptr : value.m_str==nullptr) ||
                (i==2 && std::memcmp(value.m_str,records[i]+4,3)))
                return failure(80,"BSTR_independent_producer",hr);
            ATL::CComPtr<IStream> encoded;
            unsigned char actual[9]{};
            if (::CreateStreamOnHGlobal(nullptr,TRUE,&encoded) != S_OK ||
                value.WriteToStream(encoded)!=S_OK ||
                encoded->Seek(start,STREAM_SEEK_SET,nullptr)!=S_OK ||
                encoded->Read(actual,sizes[i],&read)!=S_OK || read!=sizes[i] ||
                std::memcmp(actual,records[i],sizes[i]))
                return failure(81,"BSTR_fixture_reencode",E_FAIL);
        } else if (hr != (i<5 ? E_FAIL : STG_E_READFAULT) ||
                   std::wcscmp(value.m_str,L"keep"))
            return failure(82,"BSTR_malformed_preserves_owner",hr);
    }
    // A wire-declared BSTR length must be rejected before allocation.
    // A small explicit budget also prevents valid but unwanted large reads.
    const HRESULT oversizedHr = HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    ATL::CComPtr<IStream> hugeBstr;
    const ULONG hostileCount = 0xfffffffeu;
    if (::CreateStreamOnHGlobal(nullptr, TRUE, &hugeBstr)!=S_OK ||
        hugeBstr->Write(&hostileCount, sizeof(hostileCount), nullptr)!=S_OK ||
        hugeBstr->Seek(start, STREAM_SEEK_SET, nullptr)!=S_OK)
        return failure(103, "BSTR_hostile_count_setup", E_FAIL);
    ATL::CComBSTR budgeted(L"keep");
    BSTR originalBstr = budgeted.m_str;
    if (budgeted.ReadFromStream(hugeBstr)!=oversizedHr ||
        budgeted.m_str!=originalBstr ||
        std::wcscmp(budgeted.m_str,L"keep"))
        return failure(104, "BSTR_default_budget_preallocation", E_FAIL);
    ATL::CComPtr<IStream> smallBstr;
    if (::CreateStreamOnHGlobal(nullptr, TRUE, &smallBstr)!=S_OK ||
        smallBstr->Write(records[2], sizes[2], nullptr)!=S_OK ||
        smallBstr->Seek(start, STREAM_SEEK_SET, nullptr)!=S_OK ||
        budgeted.ReadFromStream(smallBstr, 2)!=oversizedHr ||
        budgeted.m_str!=originalBstr ||
        smallBstr->Seek(start, STREAM_SEEK_SET, nullptr)!=S_OK ||
        budgeted.ReadFromStream(smallBstr, 3)!=S_OK ||
        budgeted.ByteLength()!=3 ||
        std::memcmp(budgeted.m_str, records[2]+4, 3))
        return failure(105, "BSTR_custom_budget_retry", E_FAIL);

    for (unsigned at=1;at<=2;++at) {
        const HRESULT results[] = {S_OK,S_FALSE,E_ACCESSDENIED};
        for (HRESULT injected : results) {
            if (strings->Seek(start,STREAM_SEEK_SET,nullptr)!=S_OK)
                return failure(83,"BSTR_fault_rewind",E_FAIL);
            StreamTransferFault reader(strings,at,injected);
            ATL::CComBSTR value(L"keep");
            hr=value.ReadFromStream(&reader);
            if (hr != (FAILED(injected)?injected:STG_E_READFAULT) ||
                std::wcscmp(value.m_str,L"keep"))
                return failure(84,"BSTR_injected_read",hr);
            StreamTransferFault writer(strings,at,injected);
            hr=a.WriteToStream(&writer);
            if (hr != (FAILED(injected)?injected:STG_E_WRITEFAULT))
                return failure(85,"BSTR_injected_write",hr);
        }
    }

    const VARTYPE scalarTypes[] = {VT_EMPTY,VT_NULL,VT_I1,VT_UI1,VT_I2,
        VT_UI2,VT_BOOL,VT_I4,VT_UI4,VT_INT,VT_UINT,VT_R4,VT_ERROR,
        VT_I8,VT_UI8,VT_R8,VT_CY,VT_DATE};
    const ULONG scalarSizes[] = {0,0,1,1,2,2,2,4,4,4,4,4,4,8,8,8,8,8};
    for (unsigned i=0;i<sizeof(scalarTypes)/sizeof(scalarTypes[0]);++i) {
        ATL::CComPtr<IStream> fixture;
        const unsigned char payload[8] = {1,0,0,0,0,0,0,0};
        if (::CreateStreamOnHGlobal(nullptr,TRUE,&fixture)!=S_OK ||
            fixture->Write(&scalarTypes[i],sizeof(VARTYPE),nullptr)!=S_OK ||
            fixture->Write(payload,scalarSizes[i],nullptr)!=S_OK ||
            fixture->Seek(start,STREAM_SEEK_SET,nullptr)!=S_OK)
            return failure(86,"VARIANT_scalar_setup",E_FAIL);
        ATL::CComVariant value(L"old");
        hr=value.ReadFromStream(fixture);
        if (hr!=S_OK || value.vt!=scalarTypes[i] ||
            std::memcmp(&value.bVal,payload,scalarSizes[i]))
            return failure(87,"VARIANT_scalar_independent_read",hr);
        ATL::CComPtr<IStream> encoded;
        if (::CreateStreamOnHGlobal(nullptr,TRUE,&encoded)!=S_OK ||
            value.WriteToStream(encoded)!=S_OK ||
            encoded->Seek(start,STREAM_SEEK_SET,nullptr)!=S_OK)
            return failure(88,"VARIANT_scalar_write",E_FAIL);
        STATSTG scalarInfo{};
        if (value.GetSize()!=sizeof(VARTYPE)+scalarSizes[i] ||
            encoded->Stat(&scalarInfo,STATFLAG_NONAME)!=S_OK ||
            scalarInfo.cbSize.QuadPart!=value.GetSize())
            return failure(109,"VARIANT_scalar_GetSize_matches_stream",E_FAIL);
        VARTYPE tag=VT_EMPTY;
        unsigned char actual[8]{};
        if (encoded->Read(&tag,sizeof(tag),&read)!=S_OK || read!=sizeof(tag) ||
            tag!=scalarTypes[i] ||
            encoded->Read(actual,scalarSizes[i],&read)!=S_OK ||
            read!=scalarSizes[i] || std::memcmp(actual,payload,read))
            return failure(89,"VARIANT_scalar_independent_bytes",E_FAIL);
    }
    ATL::CComPtr<IStream> variantStream;
    ATL::CComVariant variantText;
    variantText.vt=VT_BSTR;
    variantText.bstrVal=a.Copy();
    ATL::CComVariant variantRead(L"old");
    if (!variantText.bstrVal ||
        ::CreateStreamOnHGlobal(nullptr,TRUE,&variantStream)!=S_OK ||
        variantText.WriteToStream(variantStream)!=S_OK ||
        variantStream->Seek(start,STREAM_SEEK_SET,nullptr)!=S_OK ||
        variantRead.ReadFromStream(variantStream)!=S_OK || variantRead!=variantText ||
        variantRead.ReadFromStream(variantStream)!=STG_E_READFAULT || variantRead!=variantText)
        return failure(90,"VARIANT_BSTR_stream_ownership",E_FAIL);
    STATSTG variantTextInfo{};
    if (variantText.GetSize()!=sizeof(VARTYPE)+sizeof(ULONG)+
                                 a.ByteLength()+sizeof(OLECHAR) ||
        variantStream->Stat(&variantTextInfo,STATFLAG_NONAME)!=S_OK ||
        variantTextInfo.cbSize.QuadPart!=variantText.GetSize())
        return failure(110,"VARIANT_BSTR_GetSize_matches_stream",E_FAIL);
    ATL::CComVariant rejected;
    rejected.vt=VT_BYREF|VT_I4;
    rejected.plVal=nullptr;
    STATSTG before{},after{};
    if (variantStream->Stat(&before,STATFLAG_NONAME)!=S_OK ||
        rejected.WriteToStream(variantStream)!=DISP_E_BADVARTYPE ||
        rejected.GetSize()!=sizeof(VARTYPE) ||
        variantStream->Stat(&after,STATFLAG_NONAME)!=S_OK ||
        before.cbSize.QuadPart!=after.cbSize.QuadPart ||
        variantRead.ReadFromStream(nullptr)!=E_POINTER ||
        variantText.WriteToStream(nullptr)!=E_POINTER)
        return failure(91,"VARIANT_unsupported_no_write",E_FAIL);

    // A BSTR record has three transfers: tag, byte count, and payload.
    // Every fault must retain the destination and leave the source owned.
    for (unsigned at=1;at<=3;++at) {
        const HRESULT results[] = {S_OK,S_FALSE,E_ACCESSDENIED};
        for (HRESULT injected : results) {
            if (variantStream->Seek(start,STREAM_SEEK_SET,nullptr)!=S_OK)
                return failure(92,"VARIANT_fault_rewind",E_FAIL);
            StreamTransferFault reader(variantStream,at,injected);
            ATL::CComVariant destination(L"keep");
            BSTR previous=destination.bstrVal;
            hr=destination.ReadFromStream(&reader);
            if (hr!=(FAILED(injected)?injected:STG_E_READFAULT) ||
                destination.vt!=VT_BSTR || destination.bstrVal!=previous ||
                std::wcscmp(destination.bstrVal,L"keep"))
                return failure(93,"VARIANT_injected_read_preserves_owner",hr);
            ATL::CComPtr<IStream> target;
            if (::CreateStreamOnHGlobal(nullptr,TRUE,&target)!=S_OK)
                return failure(94,"VARIANT_fault_target",E_FAIL);
            StreamTransferFault writer(target,at,injected);
            BSTR sourceOwner=variantText.bstrVal;
            hr=variantText.WriteToStream(&writer);
            if (hr!=(FAILED(injected)?injected:STG_E_WRITEFAULT) ||
                variantText.vt!=VT_BSTR || variantText.bstrVal!=sourceOwner ||
                ::SysStringLen(sourceOwner)!=3 ||
                std::memcmp(sourceOwner,embeddedA,sizeof(embeddedA)))
                return failure(95,"VARIANT_injected_write_preserves_owner",hr);
        }
    }
    // An unsupported input tag is never passed to VariantClear or used as
    // an allocation/activation request. Only the two-byte tag is consumed.
    const VARTYPE unsupported[] = {VT_ARRAY|VT_UI1, VT_BYREF|VT_I4,
        VT_RECORD, VT_DECIMAL, VT_UNKNOWN, VT_DISPATCH, 0xffff};
    for (VARTYPE tag : unsupported) {
        ATL::CComPtr<IStream> input;
        if (::CreateStreamOnHGlobal(nullptr,TRUE,&input)!=S_OK ||
            input->Write(&tag,sizeof(tag),nullptr)!=S_OK ||
            input->Seek(start,STREAM_SEEK_SET,nullptr)!=S_OK)
            return failure(96,"VARIANT_bad_tag_setup",E_FAIL);
        ATL::CComVariant destination(123);
        if (destination.ReadFromStream(input)!=DISP_E_BADVARTYPE ||
            destination.vt!=VT_I4 || destination.lVal!=123)
            return failure(97,"VARIANT_bad_tag_preserves_value",E_FAIL);
    }
    // Reuse independently specified BSTR records inside a VARIANT envelope;
    // compare byte counts rather than the text-only equality operator.
    for (unsigned i=0;i<3;++i) {
        ATL::CComPtr<IStream> input, output;
        const VARTYPE tag=VT_BSTR;
        if (::CreateStreamOnHGlobal(nullptr,TRUE,&input)!=S_OK ||
            input->Write(&tag,sizeof(tag),nullptr)!=S_OK ||
            input->Write(records[i],sizes[i],nullptr)!=S_OK ||
            input->Seek(start,STREAM_SEEK_SET,nullptr)!=S_OK)
            return failure(100,"VARIANT_binary_BSTR_setup",E_FAIL);
        ATL::CComVariant value(123);
        hr=value.ReadFromStream(input);
        if (hr!=S_OK || value.vt!=VT_BSTR ||
            (i==0 ? value.bstrVal!=nullptr : value.bstrVal==nullptr) ||
            ::SysStringByteLen(value.bstrVal)!=(i==2?3u:0u) ||
            (i==2 && std::memcmp(value.bstrVal,records[i]+4,3)))
            return failure(101,"VARIANT_binary_BSTR_read",hr);
        unsigned char actual[11]{};
        if (::CreateStreamOnHGlobal(nullptr,TRUE,&output)!=S_OK ||
            value.WriteToStream(output)!=S_OK ||
            value.GetSize()!=sizeof(VARTYPE)+sizes[i] ||
            output->Seek(start,STREAM_SEEK_SET,nullptr)!=S_OK ||
            output->Read(actual,sizes[i]+sizeof(tag),&read)!=S_OK ||
            read!=sizes[i]+sizeof(tag) || actual[0]!=VT_BSTR || actual[1] ||
            std::memcmp(actual+sizeof(tag),records[i],sizes[i]))
            return failure(102,"VARIANT_binary_BSTR_write",E_FAIL);
    }
    // A VARIANT's tag must not bypass the BSTR allocation limit.
    ATL::CComPtr<IStream> hugeVariant;
    const VARTYPE bstrType = VT_BSTR;
    if (::CreateStreamOnHGlobal(nullptr, TRUE, &hugeVariant)!=S_OK ||
        hugeVariant->Write(&bstrType, sizeof(bstrType), nullptr)!=S_OK ||
        hugeVariant->Write(&hostileCount, sizeof(hostileCount), nullptr)!=S_OK ||
        hugeVariant->Seek(start, STREAM_SEEK_SET, nullptr)!=S_OK)
        return failure(106, "VARIANT_hostile_count_setup", E_FAIL);
    ATL::CComVariant guardedBstr(L"keep");
    BSTR originalVariantBstr = guardedBstr.bstrVal;
    if (guardedBstr.ReadFromStream(hugeVariant)!=oversizedHr ||
        guardedBstr.vt!=VT_BSTR ||
        guardedBstr.bstrVal!=originalVariantBstr)
        return failure(107, "VARIANT_default_budget_preserves_owner", E_FAIL);
    ATL::CComPtr<IStream> smallVariant;
    if (::CreateStreamOnHGlobal(nullptr, TRUE, &smallVariant)!=S_OK ||
        smallVariant->Write(&bstrType, sizeof(bstrType), nullptr)!=S_OK ||
        smallVariant->Write(records[2], sizes[2], nullptr)!=S_OK ||
        smallVariant->Seek(start, STREAM_SEEK_SET, nullptr)!=S_OK ||
        guardedBstr.ReadFromStream(smallVariant, 2)!=oversizedHr ||
        guardedBstr.bstrVal!=originalVariantBstr ||
        smallVariant->Seek(start, STREAM_SEEK_SET, nullptr)!=S_OK ||
        guardedBstr.ReadFromStream(smallVariant, 3)!=S_OK ||
        guardedBstr.vt!=VT_BSTR || ::SysStringByteLen(guardedBstr.bstrVal)!=3 ||
        std::memcmp(guardedBstr.bstrVal, records[2]+4, 3))
        return failure(108, "VARIANT_custom_budget_retry", E_FAIL);

    ATL::CComPtr<IStream> truncatedScalar;
    const unsigned char incompleteI4[] = {VT_I4,0,0x42,0};
    if (::CreateStreamOnHGlobal(nullptr,TRUE,&truncatedScalar)!=S_OK ||
        truncatedScalar->Write(incompleteI4,sizeof(incompleteI4),nullptr)!=S_OK ||
        truncatedScalar->Seek(start,STREAM_SEEK_SET,nullptr)!=S_OK ||
        variantRead.ReadFromStream(truncatedScalar)!=STG_E_READFAULT ||
        variantRead!=variantText)
        return failure(98,"VARIANT_short_scalar_preserves_BSTR",E_FAIL);

    ATL::CComVariant va;
    va.vt = VT_BSTR;
    va.bstrVal = ::SysAllocStringLen(embeddedA,3);
    ATL::CComVariant vb;
    vb.vt = VT_BSTR;
    vb.bstrVal = ::SysAllocStringLen(embeddedB,3);
    if (!va.bstrVal || !vb.bstrVal ||
        va == static_cast<const VARIANT&>(vb))
        return failure(62, "binary_VARIANT_BSTR_equality", E_FAIL);
    ATL::CComVariant copied(va);
    if (!(copied == static_cast<const VARIANT&>(va)) ||
        copied.Copy(static_cast<const VARIANT*>(&copied)) != S_OK ||
        copied.Copy(nullptr) != E_POINTER)
        return failure(63, "VARIANT_copy_and_alias", E_FAIL);
    copied = static_cast<const VARIANT&>(copied);
    if (!(copied == static_cast<const VARIANT&>(va)))
        return failure(64, "VARIANT_self_assignment", E_FAIL);
    if (copied.Detach(nullptr) != E_POINTER ||
        copied.Detach(static_cast<VARIANT*>(&copied)) != E_INVALIDARG ||
        copied.vt != VT_BSTR)
        return failure(65, "VARIANT_reject_self_Detach", E_FAIL);
    VARIANT transferred;
    ::VariantInit(&transferred);
    hr = copied.Detach(&transferred);
    if (FAILED(hr) || copied.vt != VT_EMPTY || transferred.vt != VT_BSTR ||
        ::SysStringLen(transferred.bstrVal) != 3) {
        (void)::VariantClear(&transferred);
        return failure(66, "VARIANT_transfer_ownership", hr);
    }
    (void)::VariantClear(&transferred);

    ATL::CComVariant live(L"another");
    LPCOLESTR source = live.bstrVal;
    live = source;
    if (live.vt != VT_BSTR || !live.bstrVal ||
        std::wcscmp(live.bstrVal,L"another") != 0)
        return failure(67, "VARIANT_BSTR_self_alias", E_FAIL);
    // VariantClear refuses to destroy a locked SAFEARRAY. Assignment
    // must retain its original tag/handle until the lock is released.
    SAFEARRAY* lockedArray = ::SafeArrayCreateVector(VT_UI1, 0, 3);
    if (lockedArray == nullptr)
        return failure(68, "VARIANT_locked_array_setup", E_OUTOFMEMORY);
    ATL::CComVariant guarded;
    guarded.vt = VT_ARRAY | VT_UI1;
    guarded.parray = lockedArray;
    hr = ::SafeArrayLock(lockedArray);
    if (FAILED(hr))
        return failure(69, "VARIANT_array_lock", hr);
    HRESULT streamClear = variantStream->Seek(start,STREAM_SEEK_SET,nullptr);
    if (streamClear==S_OK) streamClear=guarded.ReadFromStream(variantStream);
    const bool streamRetained=streamClear==DISP_E_ARRAYISLOCKED &&
        guarded.vt==(VT_ARRAY|VT_UI1) && guarded.parray==lockedArray;
    guarded = 314;
    const bool retained = guarded.vt == (VT_ARRAY | VT_UI1) &&
                          guarded.parray == lockedArray;
    hr = ::SafeArrayUnlock(lockedArray);
    if (FAILED(hr) || !retained || !streamRetained) {
        if (!retained)
            (void)::SafeArrayDestroy(lockedArray);
        return failure(70, "VARIANT_locked_array_owner_preserved", hr);
    }
    // The same staged load succeeds once native ownership can be released.
    if (variantStream->Seek(start,STREAM_SEEK_SET,nullptr)!=S_OK ||
        guarded.ReadFromStream(variantStream)!=S_OK || guarded!=variantText)
        return failure(99,"VARIANT_stream_after_unlock",E_FAIL);
    guarded = 314;
    if (guarded.vt != VT_I4 || guarded.lVal != 314)
        return failure(71, "VARIANT_assignment_after_unlock", E_FAIL);
    return 0;
}

// IGlobalInterfaceTable is an OS-provided COM *client* target, not our own
// factory, service or registration. No COM object survives apartment exit.
static int system_component_client() {
    IGlobalInterfaceTable* raw=nullptr;
    HRESULT hr=::CoCreateInstance(CLSID_StdGlobalInterfaceTable,nullptr,
        CLSCTX_INPROC_SERVER,IID_IGlobalInterfaceTable,
        reinterpret_cast<void**>(&raw));
    if(FAILED(hr)||raw==nullptr)return failure(11,"CoCreateInstance_GIT",hr);
    {
        com_ptr<IGlobalInterfaceTable> git;
        git.Attach(raw);
        raw=nullptr;
        ATL::CComPtr<IGlobalInterfaceTable> shared(git);
        if(!shared.p || shared.p!=git.p)
            return failure(12,"CComPtr_copy",E_FAIL);

        IUnknown* identity=nullptr;
        hr=git->QueryInterface(IID_IUnknown,
               reinterpret_cast<void**>(&identity));
        if(FAILED(hr)||identity==nullptr)
            return failure(13,"QueryInterface_IUnknown",hr);
        ATL::CComPtr<IUnknown> owner;
        owner.Attach(identity);
        if(owner.p!=identity)return failure(14,"CComPtr_Attach",E_FAIL);
        IUnknown* detached=owner.Detach();
        if(owner.p!=nullptr||detached!=identity)
            return failure(15,"CComPtr_Detach",E_FAIL);
        detached->Release();

        ATL::CComBSTR name(L"Client");
        ATL::CComVariant number(42);
        if(name.Length()!=6 || name.m_str==nullptr ||
           number.vt!=VT_I4 || number.lVal!=42)
            return failure(16,"BSTR_and_VARIANT",E_FAIL);
        if (int result = com_client_ownership_regressions(git.p))
            return result;
    }
    return 0;
}

static int run_uninitialized() {
    if(!no_apartment())return failure(20,"unexpected_initial_apartment",E_FAIL);
    IGlobalInterfaceTable* object=nullptr;
    const HRESULT hr=::CoCreateInstance(CLSID_StdGlobalInterfaceTable,nullptr,
        CLSCTX_INPROC_SERVER,IID_IGlobalInterfaceTable,
        reinterpret_cast<void**>(&object));
    if(hr!=CO_E_NOTINITIALIZED || object!=nullptr) {
        if(object)object->Release();
        return failure(21,"activation_requires_apartment",hr);
    }
    if(!no_apartment())return failure(22,"negative_test_implicitly_initialized",E_FAIL);
    std::puts("ATL_COM_CLIENT_MODE_OK mode=uninitialized apartment=0 activation_denied=1");
    return 0;
}

static int run_apartment(const char* mode,bool mta,bool changed) {
    if(!no_apartment())return failure(30,"fresh_thread_uninitialized",E_FAIL);
    const DWORD flags=mta?COINIT_MULTITHREADED:COINIT_APARTMENTTHREADED;
    const DWORD opposite=mta?COINIT_APARTMENTTHREADED:COINIT_MULTITHREADED;
    HRESULT hr=::CoInitializeEx(nullptr,flags);
    if(hr!=S_OK)return failure(31,"first_CoInitializeEx",hr);

    int result=0;
    ULONG_PTR context=0;
    hr=::CoGetContextToken(&context);
    if(hr!=S_OK)
        result=failure(32,"apartment_context_token",hr);

    if(!result&&changed) {
        hr=::CoInitializeEx(nullptr,opposite);
        if(hr!=RPC_E_CHANGED_MODE) {
            // Defensive cleanup if an unexpected successful initialization
            // was returned. A failed changed-mode call needs no Uninit.
            if(SUCCEEDED(hr))::CoUninitialize();
            result=failure(33,"changed_apartment_model_must_fail",hr);
        }
        if(!result && ::CoGetContextToken(&context)!=S_OK)
            result=failure(34,"changed_mode_damaged_apartment",E_FAIL);
    }

    if(!result) {
        // Same model returns S_FALSE and still needs its own CoUninitialize.
        hr=::CoInitializeEx(nullptr,flags);
        if(hr!=S_FALSE) {
            if(SUCCEEDED(hr))::CoUninitialize();
            result=failure(35,"same_model_nested_initialize",hr);
        } else {
            ::CoUninitialize();
            if(::CoGetContextToken(&context)!=S_OK)
                result=failure(36,"nested_CoUninitialize_unbalanced",E_FAIL);
        }
    }
    if(!result)result=system_component_client();

    ::CoUninitialize(); // Exactly once for the original S_OK
    if(!result && !no_apartment())
        result=failure(37,"COM_apartment_leaked",E_FAIL);
    if(result)return result;

    // Keep the original default output stable for legacy consumer tests.
    if(std::strcmp(mode,"default")==0)
        std::puts("ATL_COM_CLIENT_OK activate=1 qi=1 comptr=1 bstr=1 variant=1 unregister=0");
    else
        std::printf("ATL_COM_CLIENT_MODE_OK mode=%s apartment=1 activation=1 balance=1 changed=%d\n",
                    mode,changed?1:0);
    return 0;
}

int main(int argc,char** argv) {
    if(argc==1)return run_apartment("default",false,false);
    if(argc!=2)return failure(40,"unexpected_arguments",E_INVALIDARG);
    const char* mode=argv[1];
    if(std::strcmp(mode,"--sta")==0)
        return run_apartment("sta",false,false);
    if(std::strcmp(mode,"--mta")==0)
        return run_apartment("mta",true,false);
    if(std::strcmp(mode,"--changed-sta")==0)
        return run_apartment("changed-sta",false,true);
    if(std::strcmp(mode,"--changed-mta")==0)
        return run_apartment("changed-mta",true,true);
    if(std::strcmp(mode,"--uninitialized")==0)
        return run_uninitialized();
    return failure(41,"unsupported_mode",E_INVALIDARG);
}
