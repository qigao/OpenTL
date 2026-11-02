// OpenWTL: bounded dialog-resource measurement without constructing a window.
#ifndef OPENATL_PROPERTY_PAGE_METRICS_H
#define OPENATL_PROPERTY_PAGE_METRICS_H
#pragma once
#include "atl_dialog_template.h"
#include <limits>

namespace ATL { namespace detail {
struct PropertyPageTemplateMetrics {
    SHORT cx = 0, cy = 0;
    DWORD style = 0;
    bool extended = false, hasFont = false;
    WORD pointSize = 0, weight = FW_NORMAL;
    BYTE italic = FALSE, charset = DEFAULT_CHARSET;
    WCHAR face[LF_FACESIZE]{};
    SIZE_T headerBytes = 0;
};

// This reads only header/font metadata. Control creation data is irrelevant to
// measurement and is neither interpreted nor executed here.
inline DWORD ReadPropertyPageTemplateMetrics(const void* data, SIZE_T size,
    PropertyPageTemplateMetrics& output) noexcept {
    output = {};
    if (!data) return ERROR_INVALID_DATA;
    DialogTemplateReader reader(data,size);
    PropertyPageTemplateMetrics parsed;
    WORD version = 0, signature = 0, count = 0;
    if (!reader.Read(version) || !reader.Read(signature)) return ERROR_INVALID_DATA;
    parsed.extended = signature == 0xffff;
    if (parsed.extended) {
        if (version != 1 || !reader.Skip(8) || !reader.Read(parsed.style)) return ERROR_INVALID_DATA;
    } else {
        reader.position = 0;
        if (!reader.Read(parsed.style) || !reader.Skip(4)) return ERROR_INVALID_DATA;
    }
    // Fixed-system-font and shell-font substitution policies need their own
    // native oracle cases; do not silently measure them with SYSTEM_FONT.
    if (parsed.style & DS_FIXEDSYS) return ERROR_NOT_SUPPORTED;
    if (!reader.Read(count) || !reader.Skip(4) || !reader.Read(parsed.cx) || !reader.Read(parsed.cy) ||
        parsed.cx <= 0 || parsed.cy <= 0) return ERROR_INVALID_DATA;
    DialogTemplateReader::Field menu, windowClass, title, font;
    if (!reader.Array(menu) || !reader.Array(windowClass) || !reader.Array(title,false)) return ERROR_INVALID_DATA;
    parsed.hasFont = (parsed.style & DS_SETFONT) != 0;
    if (parsed.hasFont) {
        if (!reader.Read(parsed.pointSize)) return ERROR_INVALID_DATA;
        // Special message-font encodings lack a supported public-format
        // contract here. Keep them explicit instead of guessing native policy.
        if (parsed.pointSize == 0x7fff) return ERROR_NOT_SUPPORTED;
        if (!parsed.pointSize) return ERROR_INVALID_DATA;
        if (parsed.extended && (!reader.Read(parsed.weight) || !reader.Read(parsed.italic) ||
            !reader.Read(parsed.charset))) return ERROR_INVALID_DATA;
        // DLGTEMPLATEEX documentation describes normal weight normalization.
        // Other weight behavior awaits independent native qualification.
        if (parsed.weight != FW_NORMAL && parsed.weight != FW_DONTCARE) return ERROR_NOT_SUPPORTED;
        if (!reader.Array(font,false)) return ERROR_INVALID_DATA;
        const SIZE_T bytes = font.end - font.begin;
        if (bytes <= sizeof(WCHAR)) return ERROR_INVALID_DATA;
        if (bytes > sizeof(parsed.face)) return ERROR_NOT_SUPPORTED;
        std::memcpy(parsed.face,reader.bytes + font.begin,bytes);
    }
    parsed.headerBytes = reader.position;
    output = parsed;
    return ERROR_SUCCESS;
}

inline HRESULT PropertyPageDialogSize(const void* data, SIZE_T size, SIZE& output) noexcept {
    output = {};
    PropertyPageTemplateMetrics metrics;
    const DWORD parsed = ReadPropertyPageTemplateMetrics(data,size,metrics);
    if (parsed != ERROR_SUCCESS) return HRESULT_FROM_WIN32(parsed);
    int baseX = 0, baseY = 0;
    if (!metrics.hasFont) {
        const LONG units = ::GetDialogBaseUnits();
        baseX = LOWORD(units); baseY = HIWORD(units);
    } else {
        // LOGPIXELSY is a system/current-context measurement, not a promise
        // about a future per-monitor parent. No Windows 10 import is required.
        HDC dc = ::GetDC(nullptr);
        if (!dc) return E_FAIL;
        const int dpi = ::GetDeviceCaps(dc,LOGPIXELSY);
        LOGFONTW description{};
        const int height = dpi > 0 ? ::MulDiv(metrics.pointSize,dpi,72) : 0;
        if (height <= 0) { ::ReleaseDC(nullptr,dc); return E_INVALIDARG; }
        description.lfHeight = -height;
        description.lfWeight = FW_NORMAL;
        description.lfItalic = metrics.italic;
        description.lfCharSet = metrics.charset;
        std::memcpy(description.lfFaceName,metrics.face,sizeof(metrics.face));
        HFONT font = ::CreateFontIndirectW(&description);
        if (!font) { ::ReleaseDC(nullptr,dc); return E_OUTOFMEMORY; }
        HGDIOBJ old = ::SelectObject(dc,font);
        if (!old || old == HGDI_ERROR) {
            ::DeleteObject(font); ::ReleaseDC(nullptr,dc); return E_FAIL;
        }
        TEXTMETRICW text{};
        SIZE alphabet{};
        const BOOL measured = ::GetTextMetricsW(dc,&text) &&
            ::GetTextExtentPoint32W(dc,L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz",52,&alphabet);
        ::SelectObject(dc,old);
        ::DeleteObject(font);
        ::ReleaseDC(nullptr,dc);
        if (!measured || text.tmHeight <= 0 || alphabet.cx <= 0) return E_FAIL;
        const LONGLONG average = (static_cast<LONGLONG>(alphabet.cx) + 26) / 52;
        if (average <= 0 || average > (std::numeric_limits<int>::max)()) return E_FAIL;
        baseX = static_cast<int>(average); baseY = text.tmHeight;
    }
    if (baseX <= 0 || baseY <= 0) return E_FAIL;
    const int width = ::MulDiv(metrics.cx,baseX,4), height = ::MulDiv(metrics.cy,baseY,8);
    if (width <= 0 || height <= 0) return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
    output = {width,height};
    return S_OK;
}

inline HRESULT PropertyPageResourceSize(HINSTANCE instance, LPCWSTR name, SIZE& output) noexcept {
    output = {};
    if (!name) return E_POINTER;
    const HRSRC resource = ::FindResourceW(instance,name,MAKEINTRESOURCEW(5));
    if (!resource) {
        const DWORD error = ::GetLastError();
        return HRESULT_FROM_WIN32(error ? error : ERROR_RESOURCE_NAME_NOT_FOUND);
    }
    const DWORD size = ::SizeofResource(instance,resource);
    const void* bytes = ::LockResource(::LoadResource(instance,resource));
    if (!bytes || !size) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    return PropertyPageDialogSize(bytes,size,output);
}
} }
#endif
