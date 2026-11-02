// Resource-bounded adapters for original WTL's split-then-create page path.
#ifndef OPENATL_ATL_DIALOG_SPLIT_H
#define OPENATL_ATL_DIALOG_SPLIT_H
#pragma once
#include "../atlbase.h"
#include "atl_dialog_template.h"

namespace ATL { namespace detail {
struct DialogResourceSpan {
    const BYTE* bytes = nullptr;
    size_t size = 0;
    bool Contains(const void* pointer) const noexcept {
        const UINT_PTR start = reinterpret_cast<UINT_PTR>(bytes);
        const UINT_PTR address = reinterpret_cast<UINT_PTR>(pointer);
        return bytes && address >= start && address - start < size;
    }
};
// Enumeration supplies the actual PE resource size before any caller pointer
// is read. Include all languages, not just the thread's FindResource fallback.
struct DialogResourceSearch {
    const void* pointer;
    DialogResourceSpan found;
    static BOOL CALLBACK Language(HMODULE module, LPCWSTR type, LPCWSTR name,
        WORD language, LONG_PTR parameter) noexcept {
        auto& search = *reinterpret_cast<DialogResourceSearch*>(parameter);
        const HRSRC resource = ::FindResourceExW(module, type, name, language);
        if (resource) {
            DialogResourceSpan span;
            span.size = ::SizeofResource(module, resource);
            span.bytes = static_cast<const BYTE*>(::LockResource(::LoadResource(module, resource)));
            if (span.Contains(search.pointer)) search.found = span;
        }
        return search.found.bytes ? FALSE : TRUE;
    }
    static BOOL CALLBACK Name(HMODULE module, LPCWSTR type, LPWSTR name, LONG_PTR parameter) noexcept {
        auto& search = *reinterpret_cast<DialogResourceSearch*>(parameter);
        ::EnumResourceLanguagesW(module, type, name, Language, parameter);
        return search.found.bytes ? FALSE : TRUE;
    }
};
inline DWORD FindDialogResourceSpan(const void* pointer, WORD type, DialogResourceSpan& output) noexcept {
    output = {};
    if (!pointer) return ERROR_INVALID_PARAMETER;
    DialogResourceSearch search{pointer, {}};
    ::EnumResourceNamesW(_AtlBaseModule.GetResourceInstance(), MAKEINTRESOURCEW(type),
        DialogResourceSearch::Name, reinterpret_cast<LONG_PTR>(&search));
    if (!search.found.bytes) return ERROR_RESOURCE_DATA_NOT_FOUND;
    output = search.found;
    return ERROR_SUCCESS;
}
struct DialogResourceView {
    DialogResourceSpan resource;
    bool extended = false;
    WORD count = 0;
    size_t first = 0, next = 0;
    DialogTemplateItem selected;
};
inline DWORD InspectDialogResource(const void* pointer, bool itemPointer, BOOL extended,
    DialogResourceView& output) noexcept {
    output = {};
    DialogResourceView candidate;
    DWORD error = FindDialogResourceSpan(pointer, 5, candidate.resource);
    if (error) return error;
    if (!itemPointer && pointer != candidate.resource.bytes) return ERROR_INVALID_DATA;
    DialogTemplateCursor cursor(candidate.resource.bytes, candidate.resource.size);
    error = cursor.ReadHeader();
    if (error) return error;
    candidate.extended = cursor.extended;
    candidate.count = cursor.count;
    if (itemPointer && candidate.extended != (extended != FALSE)) return ERROR_INVALID_DATA;
    bool found = false;
    for (WORD n = 0; n < cursor.count; ++n) {
        DialogTemplateItem item;
        error = cursor.ReadItem(item);
        if (!error) error = cursor.ValidateActiveXClass(item);
        if (error) return error;
        if (!n) candidate.first = item.begin;
        if (itemPointer && pointer == candidate.resource.bytes + item.begin) {
            candidate.selected = item; found = true;
        } else if (found && !candidate.next) candidate.next = item.begin;
    }
    if (itemPointer && !found) return ERROR_INVALID_DATA;
    output = candidate;
    return ERROR_SUCCESS;
}
inline DLGITEMTEMPLATE* DialogResourceItem(const DialogResourceView& view, size_t offset) noexcept {
    return offset ? reinterpret_cast<DLGITEMTEMPLATE*>(const_cast<BYTE*>(view.resource.bytes + offset)) : nullptr;
}
} // namespace detail

namespace _DialogSplitHelper {
// Legacy pointer-only traversal is admitted for original RT_DIALOG resources
// in the current ATL resource module. Heap templates use the bounded cursor.
inline BOOL IsDialogEx(const DLGTEMPLATE* dialog) noexcept {
    detail::DialogResourceView view;
    const DWORD error = detail::InspectDialogResource(dialog, false, FALSE, view);
    ::SetLastError(error);
    return !error && view.extended ? TRUE : FALSE;
}
inline WORD DlgTemplateItemCount(const DLGTEMPLATE* dialog) noexcept {
    detail::DialogResourceView view;
    const DWORD error = detail::InspectDialogResource(dialog, false, FALSE, view);
    ::SetLastError(error);
    return error ? 0 : view.count;
}
inline DLGITEMTEMPLATE* FindFirstDlgItem(const DLGTEMPLATE* dialog) noexcept {
    detail::DialogResourceView view;
    const DWORD error = detail::InspectDialogResource(dialog, false, FALSE, view);
    ::SetLastError(error);
    return error ? nullptr : detail::DialogResourceItem(view, view.first);
}
inline DLGITEMTEMPLATE* FindNextDlgItem(const DLGITEMTEMPLATE* item, BOOL extended) noexcept {
    detail::DialogResourceView view;
    const DWORD error = detail::InspectDialogResource(item, true, extended, view);
    ::SetLastError(error);
    return error ? nullptr : detail::DialogResourceItem(view, view.next);
}
inline BOOL IsActiveXControl(const DLGITEMTEMPLATE* item, BOOL extended) noexcept {
    detail::DialogResourceView view;
    const DWORD error = detail::InspectDialogResource(item, true, extended, view);
    ::SetLastError(error);
    return !error && view.selected.activeX ? TRUE : FALSE;
}
inline DWORD FindCreateData(DWORD id, const BYTE* initData, BYTE** output) noexcept {
    if (!output) { ::SetLastError(ERROR_INVALID_PARAMETER); return 0; }
    *output = nullptr;
    if (!initData) { ::SetLastError(ERROR_SUCCESS); return 0; }
    detail::DialogResourceSpan resource;
    DWORD error = detail::FindDialogResourceSpan(initData, 240, resource);
    if (!error && initData != resource.bytes) error = ERROR_INVALID_DATA;
    std::vector<detail::AxDialogInit> records;
    if (!error) error = detail::ReadAxDialogInit(resource.bytes, resource.size, records);
    if (!error) for (const auto& record : records)
        if (record.kind == detail::AxDialogInit::Kind::Storage) { error = ERROR_NOT_SUPPORTED; break; }
    if (error) { ::SetLastError(error); return 0; }
    for (const auto& record : records) if (record.id == id) {
        if (record.IsString()) { ::SetLastError(ERROR_NOT_SUPPORTED); return 0; }
        if (record.payloadSize) *output = const_cast<BYTE*>(resource.bytes + record.payloadOffset);
        ::SetLastError(ERROR_SUCCESS); return record.payloadSize;
    }
    ::SetLastError(ERROR_SUCCESS); return 0;
}
// Output-only BSTR: the caller supplies empty storage and owns the result.
// WTL supplies a native seekable HGlobal stream. Stat bounds the counted key;
// the original resource envelope is independently checked before this call.
inline HRESULT ParseInitData(IStream* stream, BSTR* license) noexcept {
    if (!license) return E_POINTER;
    *license = nullptr;
    if (!stream) return S_OK;
    LARGE_INTEGER zero{}; ULARGE_INTEGER position{}; STATSTG stat{};
    HRESULT hr = stream->Seek(zero, STREAM_SEEK_CUR, &position);
    if (hr != S_OK) return FAILED(hr) ? hr : E_UNEXPECTED;
    hr = stream->Stat(&stat, STATFLAG_NONAME);
    if (hr != S_OK) return FAILED(hr) ? hr : E_UNEXPECTED;
    if (position.QuadPart > stat.cbSize.QuadPart ||
        stat.cbSize.QuadPart - position.QuadPart < sizeof(DWORD)) return STG_E_READFAULT;
    DWORD characters = 0; ULONG transferred = 0;
    hr = stream->Read(&characters, sizeof(characters), &transferred);
    if (FAILED(hr)) return hr;
    if (transferred != sizeof(characters)) return STG_E_READFAULT;
    ULONG bytes = 0;
    if (!detail::AxDialogLicenseByteCount(characters,
        stat.cbSize.QuadPart - position.QuadPart - sizeof(DWORD), bytes)) return STG_E_READFAULT;
    if (!characters) return S_OK; // Zero characters means no license key.
    CComBSTR staged;
    staged.m_str = ::SysAllocStringLen(nullptr, characters);
    if (!staged.m_str) return E_OUTOFMEMORY;
    transferred = 0;
    hr = stream->Read(staged.m_str, bytes, &transferred);
    if (FAILED(hr)) return hr;
    if (transferred != bytes) return STG_E_READFAULT;
    *license = staged.Detach(); return S_OK;
}
inline const DLGTEMPLATE* SplitDialogTemplate(const DLGTEMPLATE* dialog, const BYTE* initData = nullptr) noexcept {
    detail::DialogResourceSpan resource, initialization;
    DWORD error = detail::FindDialogResourceSpan(dialog, 5, resource);
    if (!error && dialog != reinterpret_cast<const DLGTEMPLATE*>(resource.bytes)) error = ERROR_INVALID_DATA;
    if (!error && initData) {
        error = detail::FindDialogResourceSpan(initData, 240, initialization);
        if (!error && initData != initialization.bytes) error = ERROR_INVALID_DATA;
    }
    detail::AxDialogTemplate prepared;
    if (!error) error = detail::SplitAxDialogTemplate(resource.bytes, resource.size, prepared,
        initialization.bytes, initialization.size);
    // The original WTL page passes an IStream to CreateControlLic. Storage
    // records need the explicit host's IStorage path and cannot use that ABI.
    if (!error) for (const auto& record : prepared.initialization)
        if (record.kind == detail::AxDialogInit::Kind::Storage) { error = ERROR_NOT_SUPPORTED; break; }
    if (error) { ::SetLastError(error); return nullptr; }
    if (!prepared.converted) { ::SetLastError(ERROR_SUCCESS); return dialog; }
    const HGLOBAL memory = ::GlobalAlloc(GMEM_FIXED, prepared.size);
    if (!memory) { ::SetLastError(ERROR_NOT_ENOUGH_MEMORY); return nullptr; }
    std::memcpy(memory, prepared.Get(), prepared.size);
    ::SetLastError(ERROR_SUCCESS);
    return reinterpret_cast<const DLGTEMPLATE*>(memory);
}
} // namespace _DialogSplitHelper
} // namespace ATL
#endif
