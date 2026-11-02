#ifndef OPENWTL_TEST_STREAM_TRANSFER_FAULT_H
#define OPENWTL_TEST_STREAM_TRANSFER_FAULT_H
#pragma once
#include <objidl.h>

// Delegates storage to Windows while injecting one short or failed transfer.
struct StreamTransferFault final : IStream {
    IStream* inner;
    unsigned calls = 0, failAt;
    HRESULT result;
    HRESULT seekResult = S_OK, statResult = S_OK;
    StreamTransferFault(IStream* s, unsigned at, HRESULT hr)
        : inner(s), failAt(at), result(hr) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE Read(void* p, ULONG n, ULONG* done) override {
        if (++calls == failAt) { if (done) *done = 0; return result; }
        return inner->Read(p,n,done);
    }
    HRESULT STDMETHODCALLTYPE Write(const void* p, ULONG n, ULONG* done) override {
        if (++calls == failAt) { if (done) *done = 0; return result; }
        return inner->Write(p,n,done);
    }
    HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER p,DWORD o,ULARGE_INTEGER* n) override { return seekResult == S_OK ? inner->Seek(p,o,n) : seekResult; }
    HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER n) override { return inner->SetSize(n); }
    HRESULT STDMETHODCALLTYPE CopyTo(IStream*,ULARGE_INTEGER,ULARGE_INTEGER*,ULARGE_INTEGER*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Commit(DWORD) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Revert() override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER,ULARGE_INTEGER,DWORD) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER,ULARGE_INTEGER,DWORD) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Stat(STATSTG* s,DWORD f) override { return statResult == S_OK ? inner->Stat(s,f) : statResult; }
    HRESULT STDMETHODCALLTYPE Clone(IStream**) override { return E_NOTIMPL; }
};
#endif
