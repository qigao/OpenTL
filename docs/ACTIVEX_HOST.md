# Windowed OLE control hosting

`atlhost.h` provides windowed containment and `atlcom.h` provides typed
Automation event sinks. Neither inclusion nor `AtlAxWinInit` initializes COM.
The application initializes OLE/COM on its UI thread and keeps it initialized
until controls, host windows, sinks and retained interfaces have been released.
No MFC or Microsoft ATL implementation is used.

Each host window owns one host reference and at most one control. Successful
attachment retains `IOleObject` and installs the host as its client site.
Creation holds a single transaction across factory activation, persistence and
in-place activation. A control requesting `OLEMISC_SETCLIENTSITEFIRST` receives
its site before `InitNew`/`Load`; other controls receive it afterwards. Reentrant
creation/attachment is rejected until that transaction finishes. Every external
initialization step checks whether the window/control was retired; failure
detaches only that transaction's control. Forwarding an accelerator retains the
active object through its call, even if a callback replaces it or destroys the
host window. The same creation protocol applies to licensed factories.
Creation/attachment failure closes and detaches the partial control. Destroying
the window removes the subclass/property, closes the control, clears its client
site, releases the active object and drops the window reference. A retained host
interface can outlive the HWND; subsequent control queries return
`CO_E_OBJNOTCONNECTED`. COM callbacks retain the host while executing. All
window/control mutation belongs to the window's creating thread. Host discovery
and control queries from another thread return `RPC_E_WRONG_THREAD`.
An active `CAtlModule` is pinned through final host destruction, including
retained interfaces after HWND destruction. DLL owners must destroy host
windows, release retained interfaces and call `AtlAxWinTerm` before unloading
their hosting module; registered window classes are not an automatic DLL
unload protocol.

The typed sink retains its connection point after a successful Advise. Duplicate
Advise fails. Unadvise checks the source's COM identity, disconnects the exact
cookie and releases that connection point; destruction also disconnects a
remaining connection. As with ATL's simple sink, the containing C++ object owns
the sink: AddRef/Release do not delete a stack object or window. The UI owner
must unadvise and finish synchronous callbacks before destroying that C++ object.
Invoke borrows incoming arguments only for the call and converts value arguments
into local owning VARIANTs. It validates counts, reverses Automation argument
order, preserves by-reference parameters and releases conversions on every exit.
By-reference floating-point metadata must match the C++ pointee width; a
four-byte float may not be exposed as a writable eight-byte double.
It invokes typed member functions directly rather than interpreting the
compiler-specific member-pointer layout. Maps support up to eight arguments.

IDispEventImpl obtains native FUNCDESC parameter metadata from registered
TypeLibs or source discovery through IProvideClassInfo2, IProvideClassInfo, or
IDispatch/IPersist and uses the same typed handler
path. Source binding owns metadata per connection; failed subscriptions roll
back, and failed explicit Unadvise retains the binding for retry. The existing
WebBrowser admission uses the native DocumentComplete TypeLib description.
AtlGetObjectSourceInterface shares this discovery and returns the actual library
identity, version and default outgoing IID, with cleared outputs on failure.
IDispEventSimpleImpl uses the same discovery for Advise and null-IID
DispEventAdvise. Explicit-IID DispEventAdvise needs no source metadata, and
SINK_ENTRY_INFO continues to describe invocation. Successful Unadvise restores
the initial sink identity; failed explicit Unadvise keeps the binding for retry.
AtlAdviseSinkMap binds one sink base per control ID, deduplicates handler entries
and rolls back subscriptions on failure. CWindow::GetDlgControl retrieves hosted
child interfaces when atlhost.h is included. Batch Unadvise uses retained
sources, attempts every group and preserves failures for retry, including after
child lookup becomes unavailable. See [sink-map scope](ATL_COMPATIBILITY.md#sink-map-subscriptions)
for preflight, reentrancy, rollback-failure and remaining composite-control limits.
CAxDialogImpl and AtlAxCreateDialog/AtlAxDialogBox create native resource dialogs
whose controls name the existing AxWin host classes. The class automatically
subscribes before WM_INITDIALOG and unadvises before destruction, using the same
dialog thunk/dispatcher. Explicit close failures preserve the window for retry.
Standard/extended resources may also name a literal CLSID as a control class:
validated empty placeholders are converted into the existing AxWin host with
the CLSID as its creation text. Ordinary controls and original resource bytes
are preserved. Bounded DLGINIT 0x0376 records supply counted license keys and
persisted bytes to the existing native factory/stream initialization. Records
with message 0x0377 use the same counted key prefix followed by a compound-file
image. Windows opens a private, read-only IStorage over native ILockBytes and
the host selects IPersistStorage::Load without stream/default fallback. Empty
0x0378 INITNEW records use a null stream and the existing InitNew path; even an
empty 0x0376 stream still calls Load. Nonempty INITNEW payloads remain unsupported.
Initialization finishes before application WM_INITDIALOG and automatic sink subscriptions;
failed or reentrant retirement destroys the partial dialog. Storage saving/switching, binding,
owner-drawn list/combo initialization without HASSTRINGS, no-data controls and
nonempty inline placeholder data still fail explicitly. Ordinary ListBox/ComboBox DLGINIT 0x401/0x403 strings
are applied before application WM_INITDIALOG. Counted ANSI bytes use native
SendMessageA and Windows character-set translation; repeated strings retain
resource order within each control. Malformed strings and class/ID mismatches
fail before window creation. This path creates all native children before initializing controls. Native early
application callbacks are forwarded, including owner-drawn fixed/variable item
measurement and drawing. Resource initialization still precedes application
WM_INITDIALOG and automatic sink binding. A temporary native window subclass and
call-scoped thread context are removed before handoff or destruction; nested
creation restores the outer context. See
[control-dialog scope](ATL_COMPATIBILITY.md#control-hosting-dialogs).
See [TypeLib event scope](ATL_COMPATIBILITY.md#typelib-driven-event-sinks) for
supported signatures, map spellings and remaining provider/type limitations.

Windowless drawing/input, full OLE document saving/storage switching and menu merging are not yet
implemented. Windowless activation is explicitly
declined through QueryInterface and its property setter. Returning S_FALSE from
host UI methods requests the control's standard UI; it does not fabricate an
implemented feature. Full official Microsoft ATL compatibility remains pending.
`CAxWindow::SetExternalUIHandler` and the host interface accept the documented
`IDocHostUIHandlerDispatch` contract. The handler need not implement native
`IDocHostUIHandler`. All 15 native UI callbacks delegate through a retained
adapter, including modeless notifications. HRESULT return-value outputs are
distinct from transport failures. BSTR results are copied into CoTaskMem strings;
drop-target/data-object results are queried for the actual native interface.
The application supplies its handler's IDispatch implementation; no TypeLib is
generated or registered by the host. `CAxWindow::SetExternalDispatch` supplies
the default external object when no UI handler is installed. Setters belong to
the window thread; retained host interfaces may clear these objects after HWND
retirement. See [external host UI](ATL_COMPATIBILITY.md#external-host-ui).
`IOleControlSite::ShowPropertyFrame` obtains the current control's native
`ISpecifyPropertyPages` list and displays it with Windows OleCreatePropertyFrame.
Windows owns the modal frame, page activation, Apply and Cancel. The operation
retains the host/control, rejects recursive display and creation-time calls,
checks retirement after external calls and releases native page-list/title
allocations on every exit. An absent page interface propagates E_NOINTERFACE;
an empty list returns E_NOTIMPL so the control can choose its own UI.
`ISpecifyPropertyPagesImpl` and source property-map declarations supply the page
list from PROP_PAGE/PROP_ENTRY_TYPE metadata, deduplicating page CLSIDs in their
first-declaration order and ignoring CLSID_NULL. Each call returns an independent
CoTaskMem array; headers/static maps do not initialize COM. PROP_DATA_ENTRY records
member offset/size/type metadata. `IPerPropertyBrowsingImpl` maps individual
Automation DISPIDs to pages and obtains current display text through native
IDispatch/VariantChangeType; EX entries select their declared dispatch interface.
Its default predefined-value list is empty. `IPersistPropertyBagImpl` loads/saves
mapped dispatch properties and checked scalar data members through the caller's
IPropertyBag. `IPersistStorageImpl` delegates native IStorage Contents streams
to a separately exposed IPersistStreamInit implementation, without retaining
the storage between calls. The full ATL property-page classes, property-map stream serialization and
interface-serialization callbacks remain unfinished; declaring a map alone does
not save data.
See [property-map scope](ATL_COMPATIBILITY.md#property-map-page-discovery).
See [per-property browsing](ATL_COMPATIBILITY.md#per-property-browsing).
See [property-bag persistence](ATL_COMPATIBILITY.md#property-bag-persistence).
See [storage-to-stream persistence](ATL_COMPATIBILITY.md#storage-to-stream-persistence).
See [native property frames](ATL_COMPATIBILITY.md#native-property-frames).
Simple sink handlers currently return void or HRESULT; the complete Automation
return-value surface is not yet implemented.

## Modeless property-sheet creation and forced teardown

Native `PropertySheet` retains its own state while creating pages. A control
calling `DestroyWindow(sheet)` from `IPersistStreamInit::Load` can free that state
inside the still-active native creation stack. Retaining the COM object or ATL
callback thunk cannot retain Windows' separate allocation. A debugger capture
on Windows x64 identified `comctl32!PropSheetDlgProc` freeing the sheet state
during nested `WM_NCDESTROY`, followed by `comctl32!RemovePropPageData` reading
the same freed block when `PageChange` resumed.

Owners that permit controls to request destruction during modeless creation can
opt into `ATL::CPropertySheetCreationGuard` from `atlhost.h` (or include
`openatl/atl_property_sheet_creation.h` directly). The scope installs a CBT hook
on the calling thread and rejects native destruction of that specific sheet
until creation returns. OpenATL hosting detects the pending cancellation after
external initialization calls and returns `E_ABORT` before activation. `Complete`
removes the hook, destroys the cancelled sheet and returns NULL with
`ERROR_CANCELLED`. No message-loop iteration or posted cancellation is required.

```cpp
// The caller owns the sheet and its pages through final HWND retirement.
HWND CreateSettingsSheet(WTL::CPropertySheet& sheet, HWND parent) {
    ATL::CPropertySheetCreationGuard creation(sheet.m_hWnd);
    if (!creation.IsInstalled())
        return nullptr; // Preserve the native hook-installation error.
    return creation.Complete(sheet.Create(parent));
}
```

Use this scope only around modeless `Create`, on the window thread, in the same
module as its OpenATL host. Keep the HWND storage and C++ sheet/page owners
alive through completion. Nested scopes restore the outer context and affect
only their bound HWNDs. If cancellation was requested, scope exit also performs
the deferred retirement on an early return. Hook removal and retirement fail
fast if Windows refuses cleanup, so no callback can outlive the scope.

This is a changed teardown contract: the in-flight `DestroyWindow` returns
FALSE and the sheet stays alive until native creation has unwound. Unprotected
immediate destruction remains unsafe. Existing WTL source and unguarded window
behavior are unchanged. For ordinary user Cancel, continue to use native
`PSBTN_CANCEL`/`PSN_QUERYCANCEL`/`PSN_RESET`, then destroy a modeless sheet after
`PSM_GETCURRENTPAGEHWND` becomes NULL, as specified by
[PropertySheetW](https://learn.microsoft.com/en-us/windows/win32/api/prsht/nf-prsht-propertysheetw).

Rejected property pages can leave an unconsumed WTL dialog-creation record.
The ATL dialog owner unlinks its pending record on destruction before releasing
its thunk storage, so a later unrelated dialog cannot traverse a dead page's
creation record.
