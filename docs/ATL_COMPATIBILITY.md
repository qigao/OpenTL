# ATL source compatibility — COM Client and COM Server

**OpenATL targets ATL source compatibility for both COM Client and COM
Server applications**, together with Win32/WTL. The current implementation
qualifies a subset; missing APIs are implementation backlog, not permanently
excluded. Microsoft's internal ATL ABI/layout is not currently claimed
compatible. See [the project charter](PROJECT_SCOPE.md).
Consumers link only `OpenWTL::WTL`; it exposes the complete patched repository WTL frontend and private ATL/COM backend.

The acceptance target is now all official WTL samples and all pure ATL
Microsoft samples, unchanged on both MSVC and MinGW, with no MFC dependency.
The implemented subset below is the current state, not a restriction on that
target. Missing standard headers and APIs remain compatibility gaps; see the
[full sample inventory and build results](OFFICIAL_SAMPLES.md).

| Header/API | Supported contract |
| --- | --- |
| `atlbase.h`, `atlcomcli.h` | Existing Win32 modules, threading policies, COM smart pointers, BSTR/VARIANT helpers |
| `atlwin.h`, `atlstr.h`, utility headers | Existing documented Windows/WTL subset; Unicode-only, C++17 |
| `atlhost.h`, `atliface.h` | Windowed OLE containment, `CAxWindow`/`CAxWindow2`, site/ambient interfaces, external UI dispatch delegation and explicit control creation; see [ownership contract](ACTIVEX_HOST.md) |
| `atlsafe.h`: `CComSafeArray<T, VARTYPE>`, `CComSafeArrayBound` | Native OLEAUT32 array creation, deep copy, attach/detach, checked borrowed element access, copied/transferred resource elements, append, multidimensional access and resize. Explicit destruction preserves ownership on native failure. See SAFEARRAY scope below. |
| `atlctl.h`: `IObjectSafetyImpl<T, flags>` | Native IObjectSafety QI and supported/enabled flags, masked updates, unsupported-interface/mask failures and public m_dwCurrentSafety state. No COM initialization or security validation is implied. |
| `atlctl.h`: property notifications | `IPropertyNotifySinkCP<T, CDV>` and `CFirePropNotifyEvent::FireOnChanged/FireOnRequestEdit`, with native sink QI across all points, edit vetoes, preflight/snapshot ownership and reentrant callbacks. See property-notification scope below. |
| `atlcom.h` / `atlctl.h` | `AtlAdvise`/`AtlUnadvise`, `IDispEventSimpleImpl`, typed `SINK_ENTRY_INFO` maps |
| `atlcom.h`: property maps | `ISpecifyPropertyPagesImpl`, `BEGIN_PROP_MAP`/`END_PROP_MAP`, `PROP_PAGE`, typed/legacy property entries and direct-data metadata; native page-list allocation. Stream persistence remains pending. |
| `atlcom.h`: `IPersistPropertyBagImpl<T>` | Caller-supplied IPropertyBag loading/saving through real Automation properties and checked scalar direct-data entries, EX dispatch IIDs, native VARIANT copying/conversion, dirty-state completion and optional IErrorLog. See property-bag persistence below. |
| `atlcom.h`: `IPersistStorageImpl<T>` | Native IStorage Contents stream adapter to the object's queried IPersistStreamInit; InitNew/IsDirty delegation, full stream replacement and stateless SaveCompleted/HandsOffStorage defaults. The stream codec and full OLE container lifecycle remain separate. |
| `atlctl.h`: `IPerPropertyBrowsingImpl<T>` | DISPID-to-page mapping, native Automation display strings with EX-interface selection, empty default predefined lists and E_NOTIMPL predefined values. See per-property browsing below. |
| `atlcom.h`: `IDispEventImpl` | Native registered/source-provided TypeLib metadata, `SINK_ENTRY`/`SINK_ENTRY_EX`/`SINK_ENTRY_EX_P`, independent sink identities, transactional binding and typed invocation. See TypeLib event scope below. |
| `atlcom.h`: server connection points | `IConnectionPointImpl`, `IConnectionPointContainerImpl`, connection-point maps (including `_P`), `CComDynamicUnkArray`/`CComUnkArray`, native `IEnumConnections`/`IEnumConnectionPoints` snapshots and clones. See connection-point scope below. |
| `atlbase.h`: `CAtlModule`, `CAtlModuleT`, `CAtlExeModuleT` | Module locks, termination callbacks, balanced EXE COM startup and automatic/explicit native class-factory registration. EXE activation shares the DLL factory engine and cookie rollback; explicit empty tables opt out. Common/EXE persistent RegisterServer/UnregisterServer share the automatic class map, CLSID selection, resource context and TypeLib preflight/rollback. RegisterAppId/UnregisterAppId use the derived resource hook; DLL exports/EXE registration commands order AppID and class work with one captured policy and rollback. Per-user commands enable the image-local mode and retain the TRUE TypeLib request; incompatible enclosing policies are rejected. EXE operations validate thread/initialization/termination. Existing tests cover HKCU LocalServer32 SCM launch, STA/MTA servers and native IDispatch/IPersist marshaling across processes. AppID security policy, arbitrary custom-interface marshaling and idle shutdown remain pending; see [module protocol](ATL_MODULE.md). |
| `atlbase.h`: `CAtlDllModuleT<T>` | Real DLL exports and one `OpenATL::COM::Module` engine. Supports bound tables and static `OBJECT_ENTRY_AUTO` discovery, plus class-directed `UpdateRegistry(BOOL)` callbacks with ordered rollback. Supplies a lazy RGS context using one captured per-user mode, with explicit context overrides. TypeLib=TRUE preloads the own-image TypeLib, registers it through OLEAUT32 after classes, and rolls class callbacks back if TypeLib registration fails. Automatic ProgID inference remains pending. |
| `OBJECT_ENTRY_AUTO(clsid, type)` | Static per-image class map, built-in class-declared creator policy and optional typed `UpdateRegistry(BOOL)` callback, bounded duplicate/invalid CLSID rejection, real `IClassFactory`/object/module-lock ownership and native STA/MTA activation. No COM, registry I/O or heap allocation in static constructor. |
| `DECLARE_NO_REGISTRY()` | Explicit ATL class opt-out of persistent registration; absent `UpdateRegistry` without this opt-out fails `E_NOTIMPL`. |
| `DECLARE_REGISTRY_RESOURCEID(id)`, `DECLARE_REGISTRY_RESOURCE(name)` | Class-static `UpdateRegistry(BOOL)` loads a real numeric/named `REGISTRY` resource, parses a bounded ATL RGS subset and executes the owner-verified plan. The active ATL module supplies the own-image/root context; explicit scoped contexts override it. Without an ATL module, standalone callbacks require an explicit context. |
| `DECLARE_REGISTRY_APPID_RESOURCEID(id, "{GUID}")` | Module-static GetAppId/UpdateRegistryAppId hook, validated non-null GUID and actual resource execution through the same common replacements/RGS engine. Value-free top-level NoRemove AppID is shared; GUID/EXE-alias child keys remain owned and verified. An undeclared AppID explicitly reports S_FALSE/no work; security policy and service registration remain unqualified. |
| `CAtlModule::UpdateRegistryFromResourceS`, `UpdateRegistryFromResource`, `_ATL_REGMAP_ENTRY` | Standard numeric/name overloads and bounded, owned sentinel replacement maps feed the same static registrar. Existing operation mode/root is retained through callbacks; direct calls establish a lazy default context. Key/name/string substitutions are data, with no recursive `%` expansion or grammar injection. Common replacement hooks feed this same path; dynamic registrar APIs remain pending. See [limits](ATL_MODULE.md). |
| `IRegistrarBase`, `CAtlModule::AddCommonRGSReplacements` | Published base IID/vtable, real QI/refcounts and a virtual common-map hook in static resource registration. Owned additions/clears obey the same map limits. Hook exit freezes or closes mutation on all paths; retained interfaces pin the module until Release. The hook capability rejects extended IRegistrar QI. Callback failures/exceptions propagate before default-root writes. |
| `IRegistrar`, `AtlCreateRegistrar`, `statreg.h` / `CRegObject` | Built-in owned COM registrar and static facade for string/file/PE-resource registration and unregistration. External PE resources are mapped without DLL execution; persistent MODULE bindings and maps are snapshotted for each operation. Automatic MODULE quotes EXE paths; MODULE_RAW and owner paths stay raw. Explicit bindings retain the caller's quote choice. The same parser/executor and ownership checks apply. Retained QI interfaces outlive the facade and pin the active ATL module. CRegObject inheritance ABI, CLSID_Registrar activation and dynamic APIs remain pending; see [limits](ATL_MODULE.md). |
| `IDispatchImpl<T, &IID, &LIBID, major, minor, holder>`, `CComTypeInfoHolder` | Real Windows type-info holder using `LoadRegTypeLib` and `GetTypeInfoOfGuid`; four IDispatch methods and `ITypeInfo::GetIDsOfNames/Invoke` delegation, with owned temporary pointers and no static type-info cache. Native OLEAUT32 tests cover real late binding. `GetTIFromFile` additionally loads **actual binary .tlb files** via `LoadTypeLibEx(REGKIND_NONE)`, verifies GUID/version/IID and exposes owned ITypeInfo without registry writes. Image TLB mode `0xffff/0xffff` loads this binary image with `REGKIND_NONE`, verifies LIBID/IID, and supports real dispatch over an embedded TypeLib. The image path is bounded to 8192 UTF-16 units; per-user registered TypeLib loading and real dispatch have native tests; machine-wide permission behavior remains a separate acceptance boundary. |
| `AtlLoadTypeLib`, `AtlRegisterTypeLib`, `AtlUnRegisterTypeLib`, `AtlGetPerUserRegistration`, `AtlSetPerUserRegistration` | Standard global signatures, owning BSTR/ITypeLib outputs, bounded numeric resource index, own-image loading, and native system/per-user OLEAUT32 registration. Per-user mode applies to helpers and module TypeLib/RGS work. EXE STA/MTA command tests now register the real embedded COMMap TypeLib, verify its EXE path and registered/embedded dispatch, and exercise native DACL-denial class/AppID rollback and cleanup. |
| `DECLARE_LIBID(id)` | Original ATL module `InitLibId()` pattern initializes `CAtlModule::m_libid` at module construction. A native derived DLL module test checks actual GUID identity and avoids type-library registration side effects. |
| `atlcom.h`: `CComObjectRootBase`, `CComObjectRootEx`, `CComObjectRoot` | Nonaggregated reference counting, thread-model critical section, construction/release hooks |
| `CComObject<T>` | Concrete IUnknown, raw creation with initial reference count **zero**, typed QI, last-release destruction |
| `CComCoClass::Error`, `AtlReportError` | Six text/ANSI/resource overloads each; native ICreateErrorInfo/IErrorInfo publication, IID/description/help/source metadata and standard default/explicit HRESULT mapping. Resource text uses the selected image without fixed-buffer truncation. See error reporting below. |
| `ISupportErrorInfoImpl<&IID>` | Native ISupportErrorInfo declaration with S_OK for the declared IID and S_FALSE otherwise; ordinary and cached tear-off error views share the owner identity. |
| `CComCoClass<T, &CLSID>`, `CComCreator<Object>`, `CComCreator2` | Default aggregatable creator selection, CLSID accessor, owned local creation and class-declared nonaggregated/aggregate-only/poly policies, including the typed outer overload. Explicit CComCreator<CComObject<T>> remains nonaggregated. |
| `CComContainedObject<T>`, `CComAggObject<T>` | Delegating contained interfaces and separate nondelegating inner IUnknown, zero-ref raw creation, construction failure cleanup and module pins through contained destruction. The outer identity is borrowed, not retained cyclically. |
| `DECLARE_AGGREGATABLE`, `DECLARE_NOT_AGGREGATABLE`, `DECLARE_ONLY_AGGREGATABLE`, `DECLARE_POLY_AGGREGATABLE` | Built-in creator policies honored by direct CComCoClass creation and OBJECT_ENTRY_AUTO factories; aggregate factory creation requires IID_IUnknown. Poly uses one CComPolyObject wrapper in both modes. Custom factory policies remain unqualified. |
| `CComPolyObject<T>` | One wrapper and public CComContainedObject<T> member for both modes. Nonaggregated public interfaces delegate to the wrapper IUnknown; aggregated public interfaces delegate to the borrowed outer, while private IUnknown owns the allocation. Zero-ref raw creation, protected construction, failure cleanup and existing module pins are retained. |
| `COM_INTERFACE_ENTRY_AGGREGATE`, `COM_INTERFACE_ENTRY_AGGREGATE_BLIND` | Delegate to an explicitly owned inner IUnknown, preserve the outer canonical identity and one owning QI reference, including through chained maps. |
| `COM_INTERFACE_ENTRY_AUTOAGGREGATE`, `COM_INTERFACE_ENTRY_AUTOAGGREGATE_BLIND` | Lazy native in-process CoCreateInstance with the controlling outer and IID_IUnknown. Serialized slot creation, activation failure/retry, retained inner after an unsupported IID, same-slot reentrancy rejection and blind fallback through the map. Caller initializes COM and releases the private slot in FinalRelease. |
| `CComTearOffObjectBase`, `CComTearOffObject`, `CComCachedTearOffObject` | Ordinary tear-offs retain the owner until their own last release; cached private IUnknown is owner-held and public interfaces retain the controlling owner. Supports ordinary and aggregated owner subobjects without layout casts. See the typed-owner limitation below. |
| `COM_INTERFACE_ENTRY_TEAR_OFF`, `COM_INTERFACE_ENTRY_CACHED_TEAR_OFF` | Lazy ordinary instances and serialized cached publication, one QI reference and canonical owner identity, construction failures/retry and same-entry reentrancy rejection. Cached slots must be released by the owner's FinalRelease. |
| `DECLARE_GET_CONTROLLING_UNKNOWN`, `OuterQueryInterface` / `OuterAddRef` / `OuterRelease` | Explicit outer identity access/delegation; no automatic COM initialization. |
| `BEGIN_COM_MAP` / `END_COM_MAP` | First simple entry supplies canonical IUnknown; typed pointer adjustments |
| `COM_INTERFACE_ENTRY`, `COM_INTERFACE_ENTRY_IID` | Interface type or explicit IID mapping |
| `COM_INTERFACE_ENTRY2`, `COM_INTERFACE_ENTRY2_IID` | Select the inheritance branch for an ambiguous base interface |
| `COM_INTERFACE_ENTRY_CHAIN(Base)` | Chain a base ATL COM map without double AddRef; preserve outer-first canonical `IUnknown` or use the base identity for a chain-only map. Native MI/QI/identity tests cover both forms. |
| `DECLARE_PROTECT_FINAL_CONSTRUCT` | Retain/release the construction guard reference |
| `CComModule` / WTL `CAppModule::Init` | Legacy object-map initialization, optional LIBID metadata, factory/cookie registration and object-main hooks are implemented. Full legacy module/registration API compatibility is still incomplete. |
| `atlconv.h`: `CA2WEX<N>`, `CW2AEX<N>`, `CA2W`, `CW2A` | Object-owned inline/heap buffers, explicit code page and default thread ACP, public buffer members and Unicode `CA2T`/`CT2A` aliases. Strict malformed/lossy conversion throws; see [encoding differences](UNICODE.md). |
| `openatl/com_server.h` / `com_registration.h` | Explicit in-process COM Server, IClassFactory, module/server locks, CLSID/ProgID registration and Windows STA/MTA activation (implemented subset) |

Reference-count and ownership semantics follow the corresponding ATL APIs.
`CComObject<T>::CreateInstance(&raw)` returns zero references: immediately
`AddRef`, or obtain an owned interface through `QueryInterface`, then balance
every successful acquisition. `CComCoClass::CreateInstance` returns an owned
interface with one reference. `GetUnknown()` is borrowed; QI returns ownership.
Unsupported IIDs clear output and return `E_NOINTERFACE`; null outputs return
`E_POINTER`. A temporary reference protects QI/Release during `FinalConstruct`.
HRESULT failure or exceptions destroy partial objects and call `FinalRelease`;
a throwing constructor does not call it. `CAtlException` preserves a failure
HRESULT, allocation failure maps to `E_OUTOFMEMORY`, other exceptions to
`E_UNEXPECTED`. Destructors and `FinalRelease` must not throw.

Thread models have their usual separation: `CComSingleThreadModel` requires
external serialization; `CComMultiThreadModel` synchronizes the reference
count and provides explicit `Lock`/`Unlock`. This does not marshal interfaces
or synchronize application fields automatically.

When an application declares a `CAtlModule`-derived module, `CComObject`
automatically retains it through destruction. Without one, local creation is
independent of a module. The separate `OpenATL::COM::Module` lifetime still
requires its explicit creator overload for a DLL class table:

```cpp
const OpenATL::COM::ClassEntry classes[] = {
    {CLSID_MyObject, &ATL::CComObject<MyObject>::CreateInstance}
};
OpenATL::COM::Module module(classes, 1);
```

An object class uses the usual root + interfaces + COM-map pattern; it cannot
be `final` because `CComObject<T>` derives from it. The class may also inherit
`CComCoClass<T, &CLSID_MyObject>`. DLL exports and registration use the existing
[explicit server API](COM_SERVER.md). Direct local creation does not acquire a
DLL module reference; a DLL must use the module-aware creator or independently
retain its module across locally created objects and external callers.

The source-owned `openatl/rgs_parser.h` accepts a bounded, read-only
subset of actual Microsoft ATL RGS grammar, validated against four
untouched resources. The opt-in `openatl/rgs_registration.h` executes
these plans under an explicitly opened caller-owned Classes root using
the existing owner-verified registry plan/inspection machinery, with
fail-closed `ForceRemove`, reverse rollback and strict unregistration
preflight. The numeric and named resource macros support genuine Win32
`REGISTRY` resources with strict encoding, default module contexts and
explicit scoped owner/root overrides. Module static helpers additionally
support bounded sentinel replacement maps. **Full RGS grammar, complete standalone
registrar APIs and machine-wide registration acceptance remain incomplete.**
This isolated native admission does not qualify the original ATL applications'
complete compile/link/activation. See [server registration](COM_SERVER.md).

Basic aggregation is exercised through raw inner creation, explicit engine
factories and the real auto-map DLL. Tests distinguish nondelegating inner
IUnknown from the outer identity, exact QI reference ownership, failed
construction/exception cleanup, class policies and factory-independent module
pins. The DLL is also activated through native CoCreateInstance with a valid
controlling outer in both STA and MTA. This is in-process aggregation; remote
aggregation and the complete original COMMap application are not qualified.

Original Microsoft COMMap `COMMap.idl` is pinned and submitted to native
Windows SDK `midl.exe` on Windows CI. The generated binary TypeLib,
interface header and IID implementation are checked through actual OLEAUT32
loading and native compilation. The same MIDL output is embedded in the existing native DLL fixture. Its
IChain implementation uses standard 0xffff/0xffff dispatch for real property
Invoke in STA/MTA. This is original-IDL and embedded-dispatch admission, not
the whole original COMMap DLL build/link or any of the 37 pure-ATL projects passing.

**Tracked compatibility gaps:** exact Microsoft tear-off member/layout contracts and custom factory policies,
full RGS grammar and standalone registrar APIs, complete CAtlComModule/legacy TypeLib API coverage and machine-wide registration acceptance,
broader event-provider/signature compatibility, complete SAFEARRAY internal-wrapper/layout compatibility, windowless
hosting, resource-based ActiveX dialogs and complete control/server support.
`CComModule` has an implemented legacy object-map subset; its presence is
not a claim that complete legacy ATL applications already pass. Windowed containment and typed simple
event sinks are implemented. Missing APIs must not claim fake success.
Real COM Server support already covers explicit class factories, module
locks, registration and native STA/MTA activation. No claim of complete
application compatibility is made yet.

Existing native COM core tests cover zero-ref creation, typed/IID queries,
multiple inheritance, canonical identity, construction failure and cleanup,
threaded references, single-thread policy, and explicit module pin retirement.
The pinned community WTL SDI fixture builds and runs without source patches, including
its existing `<atlcom.h>` include and `Init(nullptr, hInstance)` call.
It is not an official sample. [Official sample coverage](OFFICIAL_SAMPLES.md)
records separate WTL GuidGen/MemDlg tests and the gaps observed in Microsoft's
COMMap, ATLCollections and ATLSafeArray sources. The separate
[RATL sample admission](RATL_SAMPLES.md) records three unchanged applications
passing native behavior tests against OpenATL, plus the module API coverage.

Public behavior references:

- [CComObject](https://learn.microsoft.com/en-us/cpp/atl/reference/ccomobject-class)
- [CComObjectRootEx](https://learn.microsoft.com/en-us/cpp/atl/reference/ccomobjectrootex-class)
- [COM map entries](https://learn.microsoft.com/en-us/cpp/atl/reference/com-interface-entry-macros)

## Tear-off source-contract boundary

CComTearOffObjectBase exposes m_pOwner as the actual typed Owner* subobject,
where Microsoft documents CComObject<Owner>*. This preserves owner-member
access and works safely for both CComObject and CComContainedObject owners;
it does not fabricate a CComObject pointer for an aggregated owner. Code that
requires the exact Microsoft member pointer type or object layout is not
qualified. The opaque void* constructors/creator helpers expect an actual
CComObject<Owner>*; map entries use explicit typed owner/control arguments
and do not depend on that cast.

Cached initialization uses the owner's existing Lock/Unlock policy. The
owner must remain externally referenced, SingleThreadModel access must stay
serialized, and user construction hooks must not wait for another thread
whose QI is blocked on the same owner lock. Same-thread construction cycles
fail E_UNEXPECTED before a second allocation (maximum nested construction
depth 64); failures leave a null cache so later requests can retry. Owners
release the private cached IUnknown during FinalRelease after external calls
retire. Concurrent manual replacement of the cache slot is not supported.


## Automatic aggregate activation

Auto-aggregate map entries use native CoCreateInstance with CLSCTX_INPROC_SERVER,
the outer controlling IUnknown and IID_IUnknown. They do not initialize COM or
substitute a local creator when activation fails. A populated IUnknown* or
CComPtr<IUnknown> slot is reused. Successful activation publishes one private
reference before the requested QI; a missing interface therefore leaves the
inner cached, while failed activation leaves the slot null for a later retry.

The owner's lock serializes initialization and QI; external calls must retain
the owner, SingleThreadModel use must be serialized, and activation hooks must
not wait on another thread blocked on that owner lock. Same-thread recursive
creation of the same slot fails E_UNEXPECTED before another activation, using
the same bounded construction guard as tear-offs. The owner releases the slot
in FinalRelease. Concurrent manual slot replacement is outside the contract.
Specific entries propagate failures; blind entries continue through the map
after any failed activation/interface query. IUnknown never triggers lazy
creation and always remains the owner's canonical identity.


## Rich COM error information

CComCoClass::Error forwards its class CLSID to AtlReportError. Both expose
wide/ANSI text, text with help metadata, resource ID and resource ID with help
metadata overloads. Text defaults to DISP_E_EXCEPTION; resource IDs default to
MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, id). A nonzero explicit HRESULT is
returned unchanged, including success statuses. Native ICreateErrorInfo stores
the supplied IID, copied description, help context/file and registered ProgID
source when ProgIDFromCLSID can resolve it. Missing registration leaves the
source empty; no substitute source or registry write is introduced.

The helpers replace the calling logical thread's native error object and clear
stale information if metadata preparation fails. Metadata is best effort and
does not replace the documented returned HRESULT. ANSI strings use Windows
CP_ACP. Resource IDs use the selected/default _AtlBaseModule resource image;
LoadStringW's pointer/length form is copied into an owned BSTR. Invalid/missing
resources assert in debug builds and report `Unknown Error.` in release builds.

ISupportErrorInfoImpl advertises one supported IID; clients retrieve native
GetErrorInfo immediately after a failed call. GetErrorInfo transfers ownership
and clears that thread's pending error. This does not marshal error objects,
initialize an application apartment, or guarantee propagation across arbitrary
proxy calls/message loops. Existing tests cover the twelve public overloads,
metadata ownership, replacement, consumed state and native thread isolation;
actual DLL resource errors survive object release, unregistration and DLL
unload. Existing TypeLib cases verify a real IOuter Invoke failure and its
native EXCEPINFO description, help metadata and HRESULT. Arbitrary Automation
argument conversion, remote error propagation and all original COMMap sources
remain separate acceptance work.


## Repeated automatic object-map declarations

OBJECT_ENTRY_AUTO uses a shared per-type identity to coalesce identical
CLSID/type/creator/registrar records declared in multiple translation units.
This is needed by the unchanged COMMap Chain/Outer and aggreg headers. The
metadata list remains allocation-free during static construction. Untagged
explicit records and conflicting type/callback records are not coalesced;
the existing module duplicate-CLSID validation still rejects conflicts.

The original six class bodies are additionally compiled/executed by the
existing STA events case; see OFFICIAL_SAMPLES.md. This qualifies their used
API subset, not complete original DLL module/resource/deployment or full ATL
layout. The same existing case now admits six original RGS callbacks in an
isolated HKCU root and both real per-user TypeLib registrations. The unchanged
registered-mode IDispatchImpl executes IOuter/IChain and all four aggregates;
independent unregister rejects fresh lookups with no image fallback. Original
tear-off IIDs omitted from the TypeLib remain TYPE_E_ELEMENTNOTFOUND while the
library is registered. This does not qualify full original DLL deployment.


The original COMMap/Aggreg module .cpp and .rc inputs are also compiled into
two existing DLLs. The existing STA events case qualifies their original
registration exports, per-user paths, native activation/registered dispatch,
interface image ownership, cached-owner retirement and actual DLL unmapping.
This uses the existing production module/RGS/TypeLib engines unchanged.
Untouched original project builds and exact binary/type layouts remain open.


## Poly aggregation creator policy

CComPolyObject<T> and DECLARE_POLY_AGGREGATABLE select the same wrapper for both
aggregated and nonaggregated creation. The wrapper owns its private IUnknown
and counter; m_contained is the public CComContainedObject<T> member in both
cases. With no outer, contained interfaces delegate to the wrapper. With an
outer, they delegate to that borrowed identity while private IUnknown remains
independent. Aggregate factory creation accepts only IID_IUnknown. Raw creation
starts with zero references. The typed CComCoClass outer overload uses the
same class creator policy as the explicit-IID overload.

The existing core case covers exact member/creator types, both identity/refcount
modes, temporary public references during FinalConstruct, failure HRESULTs,
exceptions/allocation failure, unsupported interfaces, concurrent references,
factory retirement and ordinary/cached tear-offs on real contained owners.
The existing registered Value DLL uses the poly macro, so its native STA/MTA
activation, aggregation, methods, error metadata and unload cases use this
policy. Targets, modes, workflows and stdout markers remain unchanged.
Exact ATL binary layout and the documented tear-off m_pOwner type difference
remain separate; no CComObject pointer is fabricated for a contained owner.

Public contracts: [CComPolyObject](https://learn.microsoft.com/en-us/cpp/atl/reference/ccompolyobject-class),
[aggregation macros](https://learn.microsoft.com/en-us/cpp/atl/reference/aggregation-and-class-factory-macros),
and [CComCoClass](https://learn.microsoft.com/en-us/cpp/atl/reference/ccomcoclass-class).


## Native SAFEARRAY and original ArrayManager

CComSafeArray<T, VARTYPE> owns a genuine OLEAUT32 SAFEARRAY and one native lock
for stable borrowed GetAt references. Create/Attach/CopyFrom validate element
VARTYPE and size. Native bound/index rules support negative lower bounds,
zero-length arrays and multidimensional Get/Set/Resize. CComSafeArrayBound
exposes SAFEARRAYBOUND fields and standard count/lower/upper helpers.
Supported native types include integer/floating types, CY/DECIMAL, BSTR,
VARIANT, IUnknown* and IDispatch*. VT_BOOL/DATE/ERROR can be selected explicitly
with a correctly sized native representation. Record arrays are not admitted.

SetAt/Add copy BSTR/VARIANT values by default; bCopy=FALSE transfers caller-owned
resources only on success. Interface elements acquire native AddRef ownership.
Append stages inputs before resizing, including copied self-appends; failed
resize preserves both destination and no-copy caller ownership. Destroy and
replacement retain the old array when native external locks prevent release.
Detach removes only the wrapper's lock. External locks must balance before
destruction; a failed destructor cleanup terminates as an ownership violation.
GetAt/operator[] expose borrowed native T references; use SetAt for owning
resource updates. Internal _ATL_TYPEWRAPPER aliases and exact Microsoft layout
are not qualified. GetSafeArrayPtr is an output receiver for an empty wrapper;
callers must not overwrite or independently free an owned m_psa.

CComVariant(SAFEARRAY*) makes a native deep copy and owns the VT_ARRAY result;
a null array produces VT_EMPTY. IObjectSafetyImpl exposes its template-declared
supported/current flags and validates interfaces through real QI. Unsupported
masks fail without changing state; enabled bits outside the mask are ignored.
It implements the flag protocol; the application owns its actual safety policy.

The existing no-COM core case exercises native type/size metadata, bounds,
locks, resize/destroy/replacement failure, resource transfer/copy, interface
references and multidimensional behavior against OLEAUT32. The existing STA
events case additionally compiles unchanged Microsoft ArrayManager.cpp/.h and
its IDL through native MIDL. Real class factories/CoCreateInstance, original
RGS callbacks, IObjectSafety and registered GetTypeInfo/GetIDsOfNames/Invoke
execute Add/Change/Remove/GetArray. Returned snapshots survive class/module
retirement. Full original DLL/project deployment and all Automation/record/
internal-wrapper contracts remain separate.

References: [CComSafeArray](https://learn.microsoft.com/en-us/cpp/atl/reference/ccomsafearray-class),
[CComSafeArrayBound](https://learn.microsoft.com/en-us/cpp/atl/reference/ccomsafearraybound-class),
[IObjectSafetyImpl](https://learn.microsoft.com/en-us/cpp/atl/reference/iobjectsafetyimpl-class).

## Server connection points

IConnectionPointImpl<T, &iid, CDV> and IConnectionPointContainerImpl<T> expose
native IConnectionPoint/IConnectionPointContainer interfaces through standard
BEGIN_CONNECTION_POINT_MAP, CONNECTION_POINT_ENTRY, CONNECTION_POINT_ENTRY_P
and END_CONNECTION_POINT_MAP declarations. An embedded point face has its own
IUnknown identity and delegates lifetime to its actual ordinary/contained owner.
GetConnectionPointContainer uses real owner QI, including aggregation's outer.
FindConnectionPoint returns stable adjusted point interfaces by outgoing IID.

Advise queries the exact outgoing interface and owns that reference until
Unadvise or owner destruction. Failed Advise clears the cookie; missing sink
interfaces, fixed-capacity limits and invalid disconnects return actual errors.
Owner Lock/Unlock serializes connection changes. Sink QI and the final Release
of a disconnected sink occur outside that lock. Standard STDMETHOD overrides
of point methods are dispatched through the native face; thrown ATL/allocation/
unexpected exceptions from throwing C++ overrides become HRESULT failures at
that COM boundary. Standard STDMETHOD overrides must honor the SDK's nothrow
annotations and return failures as HRESULTs. The public m_vec supports
generated-proxy-style indexed/range access; callers must use the owner's lock
and retain interfaces before invoking them outside the lock. Firing policy is
the proxy/application's responsibility; these templates do not invent events.

CComDynamicUnkArray and CComUnkArray<N> store borrowed outgoing pointers, with
nonzero 32-bit slot cookies independent of pointer width. Removing a slot keeps
other live cookies stable; vacant slots may be reused. GetSize reports allocated
slots, including holes. Array methods do not AddRef/Release; connection points
manage those references. GetCookie accepts a slot address, or finds the first
matching value for a separately supplied pointer. Do not independently mutate
an owning point's m_vec behind its connection-management methods.

Both native enumerators own immutable snapshots with independent Next/Skip/
Reset/Clone cursors. Each returned pointer carries a caller-owned reference.
Snapshots retain sinks/points and the originating owner/image even after
Unadvise and client release. Native core tests cover maps, duplicate sinks,
capacity/failure cleanup, interface pointer adjustment, partial enumeration,
clones, callback/release reentrancy, typed sink interoperation and concurrent
direct free-threaded C++ use without COM initialization. Existing registered
Value DLL STA/MTA and aggregated cases exercise actual outgoing vtable calls;
snapshot/clone retirement gates DllCanUnloadNow on the existing module path.

The implementation uses typed embedded faces rather than Microsoft's internal
layout. Internal locator/map metadata, exact inheritance/binary layouts,
complete TypeLib event-provider/signature compatibility,
cross-apartment marshaling of custom interfaces and unchanged original event
proxy/full-project admission remain unqualified. No target, test mode, export
or workflow is added. All 36 pinned original Microsoft blobs remain unchanged.

References: [IConnectionPointImpl](https://learn.microsoft.com/en-us/cpp/atl/reference/iconnectionpointimpl-class),
[IConnectionPointContainerImpl](https://learn.microsoft.com/en-us/cpp/atl/reference/iconnectionpointcontainerimpl-class),
[connection-point macros](https://learn.microsoft.com/en-us/cpp/atl/reference/connection-point-macros),
[CComDynamicUnkArray](https://learn.microsoft.com/en-us/cpp/atl/reference/ccomdynamicunkarray-class),
[CComUnkArray](https://learn.microsoft.com/en-us/cpp/atl/reference/ccomunkarray-class).

## Property notifications

IPropertyNotifySinkCP<T, CDV> exposes the native property-notification outgoing
interface through the same connection-point implementation. Its default
CComDynamicUnkArray or fixed CComUnkArray<N> policy retains ordinary Advise,
Unadvise, map lookup and enumeration behavior.

CFirePropNotifyEvent takes a sender IUnknown and DISPID. Both static helpers
enumerate every connection point and its native connections, then QI each
connected face for IPropertyNotifySink. A sink connected under another outgoing
IID is included; duplicate subscriptions are separate notification targets.
A valid sender without a connection-point container, an empty map or an empty
set of interested sinks returns S_OK. A null sender returns E_POINTER. Native
QI/enumeration failures propagate; only E_NOINTERFACE denotes an uninterested
source/sink. Malformed successful enumeration/QI results fail E_UNEXPECTED.

Connection snapshots and all target interfaces are collected before callbacks.
They retain source/image and sink ownership while callbacks unsubscribe, add
subscribers, release the caller's sender reference or reenter notification.
No owner lock is held across property callbacks. Subscribers added during a
callback join the next notification; a disconnected target in the current
snapshot remains valid for that notification. Each helper call captures its
own set; no atomic transaction across concurrent edits or connection points is
claimed. QI/enumeration/allocation preflight failures cause no notifications.

FireOnRequestEdit stops at the first result other than S_OK, preserving S_FALSE
as an edit veto as well as propagating errors. The setter owns the decision to
commit and must test hr != S_OK. FireOnChanged notifies every collected target;
callback return values are advisory, matching IPropertyNotifySink's requirement
to return S_OK even when it cannot act. DISPID_UNKNOWN and ordinary DISPIDs are
passed unchanged. These helpers do not validate or modify property values,
initialize COM, or implement CComControl/complete ActiveX control behavior.

Existing no-COM core cases verify absence/empty/capacity/error paths, adjusted
interfaces across different points, edit vetoes, advisory notifications,
preflight cleanup, reentrant subscriptions and sender/module retirement.
The existing Value DLL exposes an explicit test property: real direct/native
STA/MTA aggregated and registered activation cases verify old-value observation
on request, no commit/notification after denial, new-value observation after
commit, and notification to sinks reached through another outgoing IID.
Original event proxies/projects and broader TypeLib event compatibility remain open.
No target, test mode, export or workflow is added; all 36 original blobs remain
unchanged. Printing/hardware HSP remain outside CI.

References: [IPropertyNotifySinkCP](https://learn.microsoft.com/en-us/cpp/atl/reference/ipropertynotifysinkcp-class),
[CFirePropNotifyEvent](https://learn.microsoft.com/en-us/cpp/atl/reference/cfirepropnotifyevent-class),
[OnRequestEdit](https://learn.microsoft.com/en-us/windows/win32/api/ocidl/nf-ocidl-ipropertynotifysink-onrequestedit),
[OnChanged](https://learn.microsoft.com/en-us/windows/win32/api/ocidl/nf-ocidl-ipropertynotifysink-onchanged).

## TypeLib-driven event sinks

IDispEventImpl<nID, T, pdiid, plibid, major, minor, tihclass> derives from the
typed simple sink and exposes _tihclass, GetTypeInfoCount/GetTypeInfo,
GetIDsOfNames, GetFuncInfoFromId and GetUserDefinedType. Explicit library
identity/version uses the existing CComTypeInfoHolder::GetTI contract, including
custom holders with that same contract. GUID_NULL library selection obtains the
native source TypeLib through IProvideClassInfo2/GetClassInfo and
GetContainingTypeLib. When that interface is absent, IProvideClassInfo supplies
the coclass metadata; if both providers are absent, IDispatch::GetTypeInfo supplies
the containing TypeLib and IPersist::GetClassID selects the coclass in that same
library. An explicit outgoing IID needs only the IDispatch library on this last
path. IID_NULL selects IProvideClassInfo2's default source IID, or the coclass
interface marked with both IMPLTYPEFLAG_FDEFAULT and IMPLTYPEFLAG_FSOURCE.
Only E_NOINTERFACE permits provider fallback; other provider/metadata errors
propagate. Missing default sources fail with CONNECT_E_NOCONNECTION; ambiguous
defaults and malformed type kinds fail rather than select an arbitrary source.
AtlGetObjectSourceInterface exposes this shared discovery and returns the actual
library GUID, outgoing IID and library version. It clears every supplied output
on entry and publishes metadata only after complete success. These fallbacks
apply to both IDispEventImpl and IDispEventSimpleImpl through this shared helper.
For the simple sink, Advise discovers the default outgoing IID even when the
template declares a different IID. DispEventAdvise uses its explicit/template
IID without requiring source type information; a null pointer or IID_NULL
requests discovery. Its invocation still uses SINK_ENTRY_INFO metadata, and its
GetTypeInfo/GetTypeInfoCount/GetIDsOfNames remain E_NOTIMPL. Successful simple
sink Unadvise restores the initial template IID and clears the cookie; failed
discovery/subscription restores the previous identity, while failed explicit
Unadvise preserves the active identity/cookie for retry. Unadvise disconnects
the stored default binding rather than rediscovering mutable source metadata.
Unavailable metadata returns its actual error, without implicit registration
or a registry/image fallback. There is no static owned ITypeInfo cache.

Native FUNCDESC/TYPEDESC data supplies calling convention and argument types.
Enum/alias and interface references are resolved through native referenced type
information with bounded recursion. Supported typed calls include scalar
integers/floats, BOOL/DATE representations, BSTR, VARIANT, COM interfaces and
single-level scalar/VARIANT BYREF pointers, up to eight parameters with void or
HRESULT handlers. Writable pointees must match the native type/width. Optional,
named/LCID/retval parameters, arrays/records, nested BYREF and non-void/HRESULT
return contracts are not admitted; unsupported metadata fails before a handler
is called. GetUserDefinedType returns VT_EMPTY for unresolved/unsupported types.

SINK_ENTRY, SINK_ENTRY_EX and SINK_ENTRY_EX_P take an explicit typed member
pointer such as &Owner::Handler. They can reference a handler base subobject:
the compiler performs its pointer adjustment before invocation. The existing
SINK_ENTRY_INFO shorthand remains available for manually specified metadata.
Legacy bare-handler spelling on the new macros, internal ATL sink-map/holder
layouts and the complete ATL macro/helper surface remain unqualified.

Each nID base is an independent IDispatch/IUnknown identity. Connection metadata
is staged before native Advise so synchronous callbacks can use it. Reentrant
binding changes fail; subscription failure restores the original identity and
releases staged metadata. Wrong-source/failed explicit Unadvise preserves the
connection for retry. Successful Unadvise releases its metadata and allows
rebinding. Destructor cleanup disconnects before destroying that metadata;
failure terminates rather than leave a source pointing at a destroyed sink.
The containing C++ object owns the sink's memory and must finish callbacks and
unadvise before destruction. Sink methods do not provide cross-thread marshaling.

Existing native core cases load an own event fixture generated by Windows SDK
MIDL (resource 4), with no replacement of pinned original IDLs or handwritten
type-info mocks. They exercise file/registered/source metadata, descriptors,
conversion/ownership/BYREF checks, unsupported signatures, independent bases,
eager callbacks, rollback/retry, rebinding and per-user registration cleanup.
Existing Value DLL direct/native STA/MTA aggregation and registered activation
exercise IProvideClassInfo-only discovery and emit actual dispatch events from
native metadata. Registered AuxValue activation exercises the IDispatch/IPersist
path without a class-info provider. Both expose a native described incoming
property and verify default event delivery and disconnection. Core cases cover
missing defaults, malformed coclass metadata, absent IPersist, metadata/class-ID
failures, cleared outputs, failed subscriptions and successful retry. Simple
sinks exercise the same native sources, including synchronous callbacks during
Advise, wrong-source/failed Unadvise, identity reset, explicit metadata-free
subscriptions and destructor cleanup. Existing Value/AuxValue DLL event cases
also bind manually described sinks through both discovery fallback paths.
The existing WebBrowser case
uses IDispEventImpl for real DocumentComplete events. No target/test mode/export/
workflow is added; all 36 pinned Microsoft blobs remain unchanged.

Wider Automation signature/locale behavior, full composite-control integration,
exact member layouts and whole original event proxy/project admission remain
open. This bounded sink implementation is not complete ActiveX control support.

References: [IDispEventImpl](https://learn.microsoft.com/en-us/cpp/atl/reference/idispeventimpl-class),
[IDispEventSimpleImpl](https://learn.microsoft.com/en-us/cpp/atl/reference/idispeventsimpleimpl-class),
[source-discovery helper](https://learn.microsoft.com/en-us/cpp/atl/reference/composite-control-global-functions),
[IProvideClassInfo](https://learn.microsoft.com/en-us/windows/win32/api/ocidl/nn-ocidl-iprovideclassinfo),
[IPersist](https://learn.microsoft.com/en-us/windows/win32/api/objidl/nn-objidl-ipersist),
[sink-map macros](https://learn.microsoft.com/en-us/cpp/atl/reference/composite-control-macros).

## External host UI

The public IDocHostUIHandlerDispatch declaration follows the ATL containment
IDL, including its UUID, 15-method order, pointer-sized accelerator values and
separate HRESULT result outputs. CAxWindow/CAxWindow2 expose SetExternalDispatch
and SetExternalUIHandler through the existing IAxWinHostWindow interface.
The latter accepts the documented dispatch interface directly; implementing
native IDocHostUIHandler on the same object is no longer required. The host
uses typed COM calls, not a guessed IDispatch name/DISPID table. Applications
remain responsible for their handler's IDispatch implementation and marshaling.

An owned adapter forwards native MSHTML UI calls. ShowContextMenu, ShowUI and
TranslateAccelerator return the dispatch method's HRESULT output on successful
transport, or the transport failure itself. An omitted result defaults to S_FALSE.
GetHostInfo starts with the host's ambient flags and double-click policy.
BOOL inputs become VARIANT_BOOL; accelerator handles/WPARAM/LPARAM retain their
width and the command-group GUID becomes an optional BSTR. BSTR input/output
temporaries are owned for the call. Native option-key/URL outputs use a separate
CoTaskMem allocation; failed outputs remain null. Drop targets and data objects
are obtained with native QueryInterface, preserving multiple-inheritance pointer
adjustments and releasing intermediate results, including failed calls.

Every delegated call retains the adapter and its handler through completion;
replacement, clearing and HWND destruction from a callback cannot retire the
handler while it is executing. The adapter pins the active ATL module. Setters
reject another thread, recheck HWND retirement after retaining an external
object, and allow clearing through a retained host interface after destruction.
Clearing the UI handler restores the existing ambient/default behavior and
external dispatch object. Windowless activation, OLE menu merging, the full ATL
property-page class framework and composite controls remain unfinished.

Existing lifecycle cases call all 15 native UI methods with a dispatch-only
handler, verify negative coordinates, high-bit accelerator values, native string
allocators, interface identity/ownership, null and failed outputs, transport vs
callback status, wrong-thread mutation and reentrant replacement/retirement.
The existing real WebBrowser case obtains IHTMLWindow2::external from the loaded
MSHTML document and invokes the application's Automation property. Executables,
modes, stdout/stderr contracts, timeout budgets and workflows are unchanged;
real printing and hardware HSP remain outside CI. The 36 original Microsoft
blobs and the whole-application admission graph are unchanged.

References: [IDocHostUIHandlerDispatch](https://learn.microsoft.com/en-us/cpp/atl/reference/idochostuihandlerdispatch-interface),
[CAxWindow setters](https://learn.microsoft.com/en-us/cpp/atl/reference/caxwindow-class),
[native host UI contract](https://learn.microsoft.com/en-us/previous-versions/windows/internet-explorer/ie-developer/platform-apis/aa753260(v=vs.85)),
[MSHTML external object](https://learn.microsoft.com/en-us/previous-versions/aa741462(v=vs.85)).

## Property-map page discovery

ISpecifyPropertyPagesImpl<T> implements native GetPages using the class's static
GetPropertyMap. Expose ISpecifyPropertyPages in the normal COM map and use
BEGIN_PROP_MAP/END_PROP_MAP with PROP_PAGE, PROP_ENTRY_TYPE or PROP_ENTRY_TYPE_EX.
Legacy PROP_ENTRY/PROP_ENTRY_EX retain untyped VT_EMPTY metadata. PROP_DATA_ENTRY
records a member's compiler-provided offset, size and VARTYPE; nested extent
members are supported. Offsets/sizes outside DWORD range fail at compilation.
Descriptors use OLE wide strings; EX entries retain the specified dispatch IID.
Maps are sentinel-terminated, image-owned source metadata. Their record layout
is not Microsoft ATL ABI or a persistence wire format; callers must keep the
map and referenced GUIDs stable while it is being read.

GetPages copies the non-null page CLSIDs, removes duplicates by GUID value and
keeps their first-declaration order. Direct-data entries and CLSID_NULL do not
advertise pages. Each successful nonempty call returns an independent native
CoTaskMem allocation, owned by the caller. Empty/data-only maps return S_OK with
zero count and a null pointer. Null output storage returns E_POINTER; outputs
are cleared before lookup/allocation, and a null map returns E_UNEXPECTED.
Allocation/map exceptions are translated through the existing ATL error path.
No COM apartment, object factory or registry operation is created by declarations
or page-list collection. The implementation adds no IUnknown ownership engine;
normal ATL COM objects provide identity and reference counting.

Existing pre-OLE cases exercise COM-map exposure with multiple inheritance,
mixed/page-only/data-only/empty maps, value-equal GUIDs at different addresses,
ordered deduplication, independent writable output arrays, explicit native
freeing, member offset/size/type and dispatch-IID metadata, failed lookups and
retry, concurrent collection and unchanged apartment state. The existing native
property-frame cases also use a mapped control with repeated page declarations
for actual Apply/Cancel, reopening, LCID/caption and cleanup checks. Executables,
modes, stdout/stderr contracts, timeout budgets and workflows remain unchanged;
real printing/hardware HSP remain outside CI and all 36 original blobs and
whole-application admission are unchanged.

This implements declarations and page discovery; per-property browsing and
property-bag persistence are described below. IPersistStreamInitImpl, interface-serialization callback
entries, complete IPropertyPageImpl behavior, CAxPropertyPageImpl, control splitting and composite
controls remain unfinished. A property-map declaration alone does not implement
storage, loading or saving. The persistence mixin must also be exposed to a caller
supplying IPropertyBag. Display-string browsing does not access direct-data members.

References: [ISpecifyPropertyPagesImpl](https://learn.microsoft.com/en-us/cpp/atl/reference/ispecifypropertypagesimpl-class),
[property-map macros](https://learn.microsoft.com/en-us/cpp/atl/reference/property-map-macros),
[page declaration order](https://learn.microsoft.com/en-us/cpp/atl/specifying-property-pages),
[native GetPages ownership](https://learn.microsoft.com/en-us/windows/win32/api/ocidl/nf-ocidl-ispecifypropertypages-getpages).

## Per-property browsing

IPerPropertyBrowsingImpl<T> in atlctl.h exposes the four native IPerPropertyBrowsing
methods through the normal COM map and existing object identity/reference counts.
MapPropertyToPage selects the first dispatch-property entry for the DISPID,
including negative and zero DISPIDs. It returns the declared page GUID, including
an explicit CLSID_NULL for an unpaged property. Page-only and direct-data entries
do not declare DISPIDs; absent mappings return E_INVALIDARG with CLSID_NULL.

GetDisplayString selects a mapped property's dispatch IID (including EX entries)
or IID_IDispatch for an unmapped DISPID. It retains the queried interface, invokes
the actual DISPID with DISPATCH_PROPERTYGET, IID_NULL, LOCALE_USER_DEFAULT and no
arguments, then delegates conversion to Windows VariantChangeType with flags zero
and VT_BSTR. Descriptions in the map are not substituted for current values.
The returned BSTR belongs to the caller and must be freed with SysFreeString;
embedded zeros and the native BSTR length are preserved. Numeric, Boolean,
BYREF and other conversions follow the operating system's Automation rules.

GetPredefinedStrings returns S_OK with two empty arrays; no predefined choices
are synthesized from type information. GetPredefinedValue initializes its output
to VT_EMPTY and returns E_NOTIMPL. Applications can override these virtual methods
to supply choices. Null output addresses return E_POINTER; provided outputs are
cleared before lookup/callbacks. A null map returns E_UNEXPECTED; map/callback
exceptions use the existing ATL error translation. Query/Invoke/native conversion
failures propagate with empty outputs; non-S_OK successful Query/Invoke results
are rejected as E_UNEXPECTED. Temporary variants and retained interfaces are
released on success, failure and exceptions. No COM apartment or registry state
is created by the helper.

The existing pre-OLE ActiveX setup tests distinct default/EX dispatch subobjects,
canonical IUnknown identity under multiple inheritance, first-match mapping,
zero/negative/unmapped DISPIDs, page/data-entry exclusion, explicit null pages,
native numeric/Boolean/BYREF conversion, independent length-preserving BSTRs,
missing interfaces/members, failure/exception cleanup with actual reference
counts, retry and reentrant page lookup. Existing native property-frame commit
and cancel cases also read the same control before and after the real modal
frame: Apply changes the displayed value and Cancel preserves it. No executable,
test mode, registration, timeout budget, workflow or stderr contract is added.
Full property-page classes, stream persistence, interface serialization and original
whole-application qualification remain pending; real printing/hardware HSP
remain outside CI.

References: [IPerPropertyBrowsingImpl](https://learn.microsoft.com/en-us/cpp/atl/reference/iperpropertybrowsingimpl-class),
[MapPropertyToPage](https://learn.microsoft.com/en-us/windows/win32/api/ocidl/nf-ocidl-iperpropertybrowsing-mappropertytopage),
[GetDisplayString ownership](https://learn.microsoft.com/en-us/windows/win32/api/ocidl/nf-ocidl-iperpropertybrowsing-getdisplaystring),
[native VariantChangeType](https://learn.microsoft.com/en-us/windows/win32/api/oleauto/nf-oleauto-variantchangetype).

## Property-bag persistence

IPersistPropertyBagImpl<T> in atlcom.h implements GetClassID, InitNew, Load and
Save over the caller's native IPropertyBag interface. T supplies GetObjectCLSID,
the stable GetPropertyMap and its usual m_bRequiresSave flag; expose IPersist and
IPersistPropertyBag in the existing COM map. InitNew returns S_OK without changing
class defaults. Successful Load clears m_bRequiresSave. Save clears it only after
complete success with fClearDirty=TRUE; fClearDirty=FALSE leaves it unchanged.
As in ATL's documented default, both fSaveAllProperties values save all named
entries. Failed work does not actively clear the dirty flag. The helper retains
the object through callbacks and the final flag update, and retains the bag/log
only for this call. It never keeps the medium for a later operation.

Entries are visited in declaration order; PROP_PAGE entries are skipped. Dispatch
properties are queried through the declared IID, including EX interfaces. Save
uses native DISPATCH_PROPERTYGET; Load supplies one value and the named
DISPID_PROPERTYPUT argument, choosing DISPATCH_PROPERTYPUTREF for interface
values. IID_NULL and LOCALE_USER_DEFAULT are used for Invoke. VariantCopyInd
turns BYREF results into independently owned values; VariantChangeType performs
typed conversion when necessary. Legacy VT_EMPTY entries preserve the bag/getter's
native type. BSTR length, SAFEARRAY and interface ownership follow OLEAUT32 and
the client bag's copy/serialization contract; this helper creates no object
factory, wire format or recursive object serializer.

PROP_DATA_ENTRY supports fixed-width I1/UI1, I2/UI2, I4/UI4/INT/UINT, I8/UI8,
R4/R8, BOOL, DATE, CY and ERROR members, including nested SIZEL extents. Trusted
compiler metadata is checked against the exact scalar size and sizeof(T) before
any property read/write. Unsupported direct-data types, mismatched sizes and
out-of-object offsets return E_INVALIDARG before transfers. BSTR/interface pointers,
VARIANT, SAFEARRAY, DECIMAL and compound byte blobs are not raw scalar data and
must use a supported Automation property or a future dedicated implementation.
The map is trusted source metadata, not an untrusted external schema.

Read receives the declared VARTYPE (VT_EMPTY for legacy entries) and the optional
IErrorLog. E_INVALIDARG from Read is treated as an absent property and preserves
that property's existing/default value. Other read failures stop Load; the bag
owns reporting those read errors. Query/Invoke/conversion errors are reported
to an optional IErrorLog with the property name and HRESULT; a logger's failure
does not replace the property error. Exceptions use the existing ATL translation.
Non-S_OK successful callback results are rejected as E_UNEXPECTED. Null media
and GetClassID output addresses return E_POINTER; failed GetClassID clears its
output. A null map returns E_UNEXPECTED. Native temporary variants/interfaces are
released on failure and exception paths. Transfers are sequential, not atomic:
earlier setters/writes remain applied if a later property fails. The caller's
normal COM apartment/synchronization and property setter rules still apply.

Existing pre-OLE ActiveX cases exercise a multiple-inheritance COM object and a
caller bag/error-log implementation: adjusted EX dispatch pointers, IPersist QI,
nested extent offsets with surrounding guards, BYREF numbers, embedded-zero
BSTR independence, legacy Boolean values, interface PUTREF/refcounts, typed
conversion, missing defaults, both Save flags, read/write/getter/setter errors,
exception cleanup, optional/advisory logging, invalid metadata preflight,
page-only maps, retry, reentrant GetClassID and a Write callback dropping the
last outside object reference. The latter checks destruction only after the
successful dirty update. No test mode, executable, registration, workflow,
timeout or stderr allowance is added. COM initialization remains caller-owned.

The property-map stream persistence mixin, global persistence helper APIs, arbitrary
direct-data serialization, interface-serialization callback entries, full property
page classes and complete original-application qualification remain pending.
The 36 pinned original blobs, all 37 whole-application admission gates and real
printing/hardware HSP exclusion remain unchanged.

References: [IPersistPropertyBagImpl](https://learn.microsoft.com/en-us/cpp/atl/reference/ipersistpropertybagimpl-class),
[native Load](https://learn.microsoft.com/en-us/windows/win32/api/ocidl/nf-ocidl-ipersistpropertybag-load),
[native Save](https://learn.microsoft.com/en-us/windows/win32/api/ocidl/nf-ocidl-ipersistpropertybag-save),
[IPropertyBag::Read](https://learn.microsoft.com/en-us/windows/win32/api/oaidl/nf-oaidl-ipropertybag-read).

## Storage-to-stream persistence

IPersistStorageImpl<T> in atlcom.h adapts the native IPersistStorage interface to
an IPersistStreamInit implementation exposed by the object's ordinary COM map.
T supplies GetObjectCLSID; the storage and stream interfaces share the object's
existing controlling identity and reference counts. The adapter queries the
stream interface on each operation, so applications can supply their own native
stream codec independently of the still-pending IPersistStreamInitImpl property-map
serializer. GetClassID uses T's CLSID and clears failed output; null output/media
addresses return E_POINTER. Missing stream support propagates E_NOINTERFACE;
non-S_OK or null successful QI results return E_UNEXPECTED.

InitNew delegates to the stream's InitNew without creating a Contents stream.
IsDirty preserves the stream implementation's HRESULT, including S_OK/S_FALSE.
Load opens Contents read-only with exclusive sharing and passes its freshly
opened stream at position zero to IPersistStreamInit::Load. Save creates/replaces
Contents with native STGM_CREATE | STGM_READWRITE | STGM_SHARE_EXCLUSIVE and
passes fSameAsLoad as the stream's clear-dirty request. A shorter replacement
therefore has no old tail; copy saves (FALSE) preserve dirty state when the codec
honors that request. QI is completed before opening/replacing a stream, so absent
codec support cannot truncate existing data. Actual stream/storage/codec failures
propagate, and exceptions use the existing ATL translation. Dirty-state changes
and byte encoding are owned by the stream codec, not duplicated in this adapter.

The object, supplied storage and queried codec are retained for the complete
operation. Native stream references are released on success, failure and
exceptions, including callbacks that drop the last outside object reference.
No storage/stream pointer is cached across calls. SaveCompleted and HandsOffStorage
are stateless S_OK defaults, as documented for ATL; they neither reopen a storage
nor implement a child-object/NoScribble state machine. An application requiring
retained/incremental storage, nested objects or the full OLE container lifecycle
must override those policies. Save does not write the storage CLSID or call
IStorage::Commit: the caller or native OleSave owns those steps. Native storage
replacement is not a rollback transaction; failed codec writes can leave partial
Contents until the caller reverts or retries. No compound-file engine, wire format,
object activation or COM apartment is introduced here.

Existing lifecycle cases use Windows CreateILockBytesOnHGlobal and
StgCreateDocfileOnILockBytes/StgOpenStorageOnILockBytes with actual native
transacted compound storage. They cover adjusted COM pointers/identity,
InitNew and duplicate initialization, missing Contents, round trips, dirty/copy
flags, replacement truncation, sibling preservation, native OleSave/class metadata,
Revert proving no implicit Commit, caller Commit/reopen, read-only denial before
codec Save, absent/malformed QI, codec HRESULTs/exceptions and retry. A COM lifetime
observer forwards every IStorage operation to Windows; its destruction while
both objects remain alive proves no reference to the supplied medium survived.
Contents reopening checks released stream handles. HGLOBAL-backed storage
reopening is a byte round trip, not proof of cross-instance sharing locks, which
depend on the ILockBytes implementation. A codec callback dropping the last
outside object reference verifies call lifetime.
The existing lifecycle executable/mode, timeout, stdout/stderr and workflows
remain unchanged. All 36 pinned blobs and 37 whole-application admission gates
remain intact; real printing/hardware HSP remain excluded from CI.

References: [IPersistStorageImpl](https://learn.microsoft.com/en-us/cpp/atl/reference/ipersiststorageimpl-class),
[native Save responsibilities](https://learn.microsoft.com/en-us/windows/win32/api/objidl/nf-objidl-ipersiststorage-save),
[native CreateStream replacement](https://learn.microsoft.com/en-us/windows/win32/api/objidl/nf-objidl-istorage-createstream),
[ILockBytes sharing limits](https://learn.microsoft.com/en-us/windows/win32/api/coml2api/nf-coml2api-stgopenstorageonilockbytes),
[full SaveCompleted lifecycle](https://learn.microsoft.com/en-us/windows/win32/api/objidl/nf-objidl-ipersiststorage-savecompleted).

## Native property frames

IOleControlSite::ShowPropertyFrame displays the hosted control's own property
pages through ISpecifyPropertyPages and native OleCreatePropertyFrame. The
existing host HWND is the owner and one captured ambient LCID is passed to the
native modal frame. A successful short GetUserType supplies its caption; absent
or failed optional caption metadata uses a default title. The control supplies
registered native IPropertyPage classes; Windows owns their creation, object
binding, activation, modal loop, Apply/Cancel and teardown. No second property
sheet implementation or Microsoft ATL fallback is used.

The operation retains the host and current control through callbacks and the
native modal call. It rejects another thread, recursive display and calls during
control creation/closing. Successful external calls are followed by a check for
HWND retirement or a changed control. Retirement reports E_ABORT; a subsequent
call through a retained, disconnected site returns CO_E_OBJNOTCONNECTED. Query
and GetPages failures propagate. GetPages must return S_OK, an empty page list
returns E_NOTIMPL, and a nonempty list with a null array fails E_UNEXPECTED.
All page-list/title CoTaskMem allocations are released even on failure. Native
frame failures propagate; a successful call means the native modal frame has
closed, including a user cancellation. It does not imply that Apply occurred.

Existing lifecycle cases register a repository-owned IPropertyPage factory in
COM's process class table. A real native property frame activates its child UI,
queues an actual OK/Cancel button click, and applies an edit through the bound
control's native IOleObject interface. Cases check object identity, LCID,
creation-time and discovery/page callback reentrancy, commit vs cancellation,
owner enabled-state restoration, reopening, page/site/object teardown, caption
failure fallback, empty/malformed/failed page lists, wrong-thread calls and
HWND destruction during interface/page discovery. Existing executable/modes,
stdout/stderr contracts, 25-second CTest budgets and workflows are unchanged.
Real printing and hardware HSP remain outside CI; all 36 pinned Microsoft blobs
and whole-application admission remain unchanged.

Source property-map declarations and page discovery are supported as described
above, together with per-property browsing and property-bag persistence.
The property-page authoring foundation is described below. Full IPropertyPageImpl
behavior, CAxPropertyPageImpl, property-map stream persistence,
control splitting and full composite-control behavior remain unfinished.

References: [IOleControlSite::ShowPropertyFrame](https://learn.microsoft.com/en-us/windows/win32/api/ocidl/nf-ocidl-iolecontrolsite-showpropertyframe),
[OleCreatePropertyFrame](https://learn.microsoft.com/en-us/windows/win32/api/olectl/nf-olectl-olecreatepropertyframe),
[IPropertyPage::SetObjects](https://learn.microsoft.com/en-us/windows/win32/api/ocidl/nf-ocidl-ipropertypage-setobjects),
[REGCLS process registration](https://learn.microsoft.com/en-us/windows/win32/api/combaseapi/ne-combaseapi-regcls).

## Sink-map subscriptions

AtlAdviseSinkMap(T*, bool) binds the existing typed sink map through the owner's
GetDlgControl(int, REFIID, void**) method. With atlhost.h included,
CWindow::GetDlgControl retrieves the actual hosted child control through
AtlAxGetControl; absent children return E_FAIL, unsupported interfaces propagate
E_NOINTERFACE, and supplied output storage is cleared before lookup.

One nID selects one sink base. Compiler-checked base conversions select the
most-derived IDispEventImpl or IDispEventSimpleImpl implementation, preserving
per-connection TypeLib ownership and manually described invocation respectively.
Inherited maps adjust the declaring owner before invoking binding callbacks,
including when another polymorphic base precedes that owner.
Multiple handlers for the same control share one subscription. Wildcard IID
entries use default-source discovery; explicit IID entries select that source.
Conflicting explicit IIDs for one control fail E_INVALIDARG before lookup.
Multiple sink bases with the same nID are outside this bounded implementation.
The internal map representation and member layouts are not Microsoft ATL ABI.

The helper preflights all sources and rejects already connected bases before
starting subscriptions. A failure rolls back only connections established by
that call, in reverse order. If rollback itself fails, a rollback error is
returned and the retained connection can be retried through Unadvise. Unadvise
uses each sink's retained connection point, without querying changed or missing
child controls; it attempts all groups, returns the first failure and preserves
failed subscriptions for retry. Already disconnected groups are harmless.
Unexpected success codes from a connected source's Unadvise fail E_UNEXPECTED;
they are not confused with an already disconnected sink.
Recursive map operations on one owner fail E_UNEXPECTED. No global binding
registry, implicit COM initialization or automatic owner deletion is added.

Existing native core cases cover mixed sink types, repeated entries, preflight,
subscription/rollback failures, reentrant callbacks, retained metadata, partial
Unadvise/retry and rebinding. Existing Value/AuxValue DLL aggregation/activation
cases deliver actual events to both map bases. The existing real WebBrowser
case now uses a hosted child control, CWindow::GetDlgControl and
AtlAdviseSinkMap for DocumentComplete, preserving its original runtime budget
and stdout contract. No target, test mode, export, workflow or original blob is
added or replaced; all 36 pinned original files remain byte-checked.

Full composite controls,
same-nID multiple-IID bases, exact internal layouts and whole original event
projects remain open. Applications must unadvise successfully before destroying
the C++ sink owner and must own COM/apartment and UI-thread lifetimes.

References: [AtlAdviseSinkMap](https://learn.microsoft.com/en-us/cpp/atl/reference/connection-point-global-functions),
[CWindow::GetDlgControl](https://learn.microsoft.com/en-us/cpp/atl/reference/cwindow-class).

## Control-hosting dialogs

With atlhost.h included, CAxDialogImpl<T, TBase> uses the existing
CDialogImplBaseT dispatcher, native window thunk and per-thread creation data.
Create (including the RECT overload), DoModal, DestroyWindow, EndDialog,
GetDialogProc, GetIDD and AdviseSinkMap are available; native IsDialogMessage
is inherited. AtlAxCreateDialog and AtlAxDialogBox provide the corresponding
resource-name/ID entry points. They register the existing AxWin classes without
initializing COM, then use native resource or indirect dialog creation APIs.

Supported resources directly name AtlAxWin140/AtlAxWinLic140 host classes and a
control CLSID, ProgID or supported URL as the control's text. Windows creates
ordinary and hosted children, with its standard/extended template, font,
geometry and parameter handling. CAxDialogImpl subscribes its sink map before
the user's WM_INITDIALOG handler and unadvises before destruction. An owner
without a sink map has no subscriptions. Init subscription failures destroy the
dialog and report failure. Explicit DestroyWindow/EndDialog preserves the
window if Unadvise fails so the application can retry; forced external
destruction with failed cleanup terminates rather than leave dangling sinks.

Creation status is kept outside the C++ owner's lifetime. WM_INITDIALOG may
close the window, and OnFinalMessage may delete the owner, without creation
cleanup dereferencing it afterward. Modeless destruction during initialization
returns NULL; normal modal EndDialog returns its result. Thunk setup failures
use the shared dialog failure hook and terminate the appropriate native creation
path. Existing pending-data removal and deferred final notification are reused.

Literal CLSID control classes with empty titles and no inline creation data are
converted to AtlAxWin140 with the original CLSID as the control's text. A bounded
reader validates standard/extended headers, version, item count, strings,
ordinals, fonts, DWORD alignment and creation-data spans before creating any
window. Header and fixed control records preserve styles, help IDs, identifiers
and geometry. Ordinary class/title/creation-data records stay opaque and retain
their bytes; standard creation-data counts include their size word, while
extended counts exclude it. Input resources are never modified. Converted templates and resources with
initialization records use owned DWORD-aligned storage with
CreateDialogIndirectParamW or DialogBoxIndirectParamW. Other unchanged resources
retain their original native resource path. A string-only template keeps its
exact bytes even though its initialization uses the indirect path.

Malformed/truncated templates and malformed brace-prefixed CLSID classes fail
ERROR_INVALID_DATA. Nonempty placeholder titles or inline ActiveX creation data,
Unsupported DLGINIT records and DS_NOFAILCREATE fail ERROR_NOT_SUPPORTED.
Unknown ordinary native child classes fail through Windows.
Unimplemented resource initialization is not silently skipped. Data binding,
storage saving/switching, full control-splitting/property pages, advanced OLE
accelerators and full composite controls remain open. Header-independent and
exact internal layout compatibility remain unqualified.

DLGINIT stream records (message 0x0376) are bounded by the resource size. Each
record has a WORD control ID, WORD message and DWORD payload length, followed
by a DWORD UTF-16 license character count, that many characters (no terminator),
and the opaque persisted bytes. A zero WORD terminates the resource; up to three
zero padding bytes are accepted. The license is a counted BSTR, including any
embedded zero characters. An absent key uses normal factory creation. Duplicate
ActiveX records, conflicting string record kinds, missing/mismatched control IDs,
duplicate matching dialog IDs, truncated
headers/payloads/keys and nonzero trailing data fail ERROR_INVALID_DATA before
any window is created. Data-binding extension and unsupported initialization
messages fail ERROR_NOT_SUPPORTED.

Ordinary DLGINIT list/combo strings use resource messages 0x0401/0x0403.
Their payload length includes exactly one final NUL byte; zero-length,
unterminated and embedded-NUL payloads fail ERROR_INVALID_DATA before window
creation. Records are contiguous even at odd byte offsets. Repeated strings for
one control are allowed and retain resource order; control groups initialize in
dialog order. Mixing list/combo kinds or ActiveX and string records for one ID,
missing IDs and duplicate matching dialog IDs is invalid.

Only native ListBox/ComboBox class ordinals (0x83/0x85) or their case-insensitive
class names are admitted for these messages. Class mismatches fail
ERROR_INVALID_DATA. Owner-drawn controls require LBS_HASSTRINGS/CBS_HASSTRINGS. Pointer-valued item
data and LBS_NODATA remain ERROR_NOT_SUPPORTED. Fixed and variable owner drawing
receive native measurement/drawing callbacks through the application procedure. Ordinary sorted/case-converting controls use native behavior.
The initializer sends native LB_ADDSTRING/CB_ADDSTRING through SendMessageA with
owned, bounded ANSI bytes. Windows translates to the control's character set
using its native code-page behavior; no alternate encoding library or assumed
UTF-8 interpretation is added. Empty strings are valid. Native LB_ERR/CB_ERR
fails initialization; ERRSPACE reports out-of-memory. The existing dialog
rollback and window-retirement checks apply before forwarding application
WM_INITDIALOG. Pure list/combo resources do not initialize COM.

DLGINIT storage records (message 0x0377) use the same counted UTF-16 license
prefix, followed by a complete compound-file image. The bounded resource reader
retains a distinct Storage kind. During dialog initialization, the host copies
the image into a new native ILockBytes with an exact logical size and opens it
with StgOpenStorageOnILockBytes in STGM_READ | STGM_SHARE_EXCLUSIVE mode. The
original resource is not writable and each record gets independent backing
bytes. Windows validates the compound file; corrupt/empty images fail dialog
initialization and roll back every partial host. Only envelope bounds/IDs/keys
are validated before creating windows; compound-file validation occurs during
initialization before the application WM_INITDIALOG callback.

Storage initialization queries IPersistStorage and calls Load between the same
OLEMISC_SETCLIENTSITEFIRST-aware site/activation steps. A missing interface or
failed Load is an error, with no fallback to IPersistStreamInit, IPersistStream
or InitNew. Stream and storage inputs are mutually exclusive internally. Counted
keys still use IClassFactory2::CreateInstanceLic. The control must AddRef/cache
its incoming storage as required by IPersistStorage::Load; the local container
reference is released after each initialization. Native storage retains its
owned backing array, including when the control reads after the dialog callback
has advanced. Normal Close/detachment remains the existing cleanup protocol.
This is a load-only windowed resource path: storage Save/SaveCompleted,
HandsOffStorage-based file switching, full compound-document hosting and
storage-only InitNew are not implemented.

Empty DLGINIT INITNEW records (message 0x0378, payload length zero) select the
existing host's default initialization path with a null IStream. They call
IPersistStreamInit::InitNew instead of Load; a control without that interface
uses the existing creation path without a persistence initialization call. Nonempty 0x0378 payloads are
recognized but fail ERROR_NOT_SUPPORTED before window creation. License-bearing
INITNEW variants are not admitted. In contrast, 0x0376 with a zero license count
and no persisted bytes still supplies a real empty stream and calls Load. It is
never silently changed to InitNew. Mixed stream/INITNEW records use the same ID
validation, dialog ordering, transaction and cleanup rules.

For supported initialization records, conversion creates an empty AxWin child. A
private dialog procedure initializes the matching controls in dialog order before
forwarding WM_INITDIALOG with the caller's original parameter. Stream records use a native
IStream positioned at the first persisted byte, then the existing host's
IPersistStreamInit::Load (or IPersistStream fallback) and licensed factory path.
It does not call InitNew before Load. Initialization failure destroys the native
dialog and every partial host, skips the application initializer and reports
failure. Initialization that destroys a host or dialog is also a failed creation.
Successful initialization hands the native dialog procedure slot to the caller;
CAxDialogImpl then binds its usual thunk and sink map. There is no dialog-context
registry or retained pointer into a temporary resource. This path deliberately
initializes ActiveX state after all native children exist; it does not claim
ATL's exact WM_CREATE callback sequence or inline creation-data format. Early
application callbacks are forwarded through the actual current dialog procedure.
A scoped, image-local thread pointer associates only the first native callback
with its creation frame; nested creation restores the previous pointer. A native
SetWindowSubclass hook then intercepts WM_INITDIALOG before application dispatch.
It initializes resources once, blocks recursive WM_INITDIALOG during that work,
and removes itself before application WM_INITDIALOG. Measurement and other
messages during initialization continue to the application, so a StartDialogProc
thunk installed by an early callback is preserved. WM_NCDESTROY retires the
context before forwarding owner cleanup, including self-deleting final callbacks.
No modeless window keeps a pointer to a returned creation frame. If a remaining
subclass cannot be removed safely, cleanup terminates instead of returning a
dangling callback. Scoped cleanup preserves the native last-error result. Resources
without initialization records retain the existing creation path.

Existing ActiveX cases embed own standard/extended and named dialog resources.
Plain dialogs verify modal/modeless parameters, reuse, failure/close retry,
self-deleting final callbacks and unchanged COM apartment state. The existing
WebBrowser case also verifies real resource-created controls, automatic sink
subscriptions, modal navigation/events, teardown and rollback when a later
control cannot activate. The executable, test modes, original 25-second budgets,
workflow and stdout markers are unchanged. All 36 pinned original blobs remain
byte-checked; printing and hardware HSP stay outside CI.

The CLSID-class paths additionally verify standard/extended and named resources,
round trips, unchanged original bytes, ordinary creation-data preservation,
truncated/corrupt records, help/style/ID retention, actual WebBrowser events in
both modal/modeless paths and cleanup after a later converted control fails.

References: [CAxDialogImpl](https://learn.microsoft.com/en-us/cpp/atl/reference/caxdialogimpl-class),
[dialog helpers](https://learn.microsoft.com/en-us/cpp/atl/reference/composite-control-global-functions),
[automatic event handling](https://learn.microsoft.com/en-us/cpp/atl/atl-event-handling-summary).
Native callbacks: [WM_MEASUREITEM](https://learn.microsoft.com/en-us/windows/win32/controls/wm-measureitem),
[SetWindowSubclass](https://learn.microsoft.com/en-us/windows/win32/api/commctrl/nf-commctrl-setwindowsubclass),
[RemoveWindowSubclass](https://learn.microsoft.com/en-us/windows/win32/api/commctrl/nf-commctrl-removewindowsubclass).
Native string initialization: [LB_ADDSTRING](https://learn.microsoft.com/en-us/windows/win32/controls/lb-addstring),
[CB_ADDSTRING](https://learn.microsoft.com/en-us/windows/win32/controls/cb-addstring),
[automatic message translation](https://learn.microsoft.com/en-us/windows/win32/intl/automatic-message-translation).
Microsoft's [BITS resource sample](https://github.com/microsoft/Windows-classic-samples/blob/main/Samples/Win7Samples/web/bits/bits_ie/bits_ie.rc)
provides ordinary DLGINIT format evidence, not an original ATL application admission.
Native storage: [IPersistStorage::Load](https://learn.microsoft.com/en-us/windows/win32/api/objidl/nf-objidl-ipersiststorage-load),
[StgOpenStorageOnILockBytes](https://learn.microsoft.com/en-us/windows/win32/api/coml2api/nf-coml2api-stgopenstorageonilockbytes),
[CreateILockBytesOnHGlobal](https://learn.microsoft.com/en-us/windows/win32/api/coml2api/nf-coml2api-createilockbytesonhglobal),
[MS-CFB format](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-cfb/53989ce4-7b05-4f8d-829b-d08d6148375b).
Native persistence: [IPersistStreamInit::InitNew](https://learn.microsoft.com/en-us/windows/win32/api/ocidl/nf-ocidl-ipersiststreaminit-initnew),
[IPersistStreamInit::Load](https://learn.microsoft.com/en-us/windows/win32/api/ocidl/nf-ocidl-ipersiststreaminit-load),
[IClassFactory2::CreateInstanceLic](https://learn.microsoft.com/en-us/windows/win32/api/ocidl/nf-ocidl-iclassfactory2-createinstancelic).
A Microsoft-published [resource example](https://download.microsoft.com/download/1/6/d/16d24ada-5317-4de1-b2b2-890b51813d6e/VS2005_DeviceDev_en-us.pdf)
contains a 0x0376 record; it is format evidence, not a newly admitted application.
Wire formats: [standard header](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-dlgtemplate),
[standard item](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-dlgitemtemplate),
[extended header](https://learn.microsoft.com/en-us/windows/win32/dlgbox/dlgtemplateex),
[extended item](https://learn.microsoft.com/en-us/windows/win32/dlgbox/dlgitemtemplateex).

The existing lifecycle case additionally registers an own IClassFactory2 control
and uses compiled standard/extended DLGINIT resources. It verifies counted keys,
Load instead of InitNew, exact stream lengths including an odd final byte, both
client-site orders, IPersistStream fallback, modal/modeless parameters and
cleanup, owner reuse, failed license/load, rollback after a later control fails,
and reentrant host/dialog destruction. The existing pre-OLE resource checks cover
bounds, malformed records, ID matching, dialog-order loading and empty DLGINIT
without creating an apartment. No original application admission is inferred.

The same lifecycle case also exercises empty INITNEW records in standard and
extended compiled resources, mixed INITNEW/stream records in reversed resource
order, both client-site orders, controls without IPersistStreamInit, modal and
modeless creation, recursive WM_INITDIALOG, InitNew failure, later-control
failure, reentrant child/dialog retirement, close during initialization and owner
reuse. A distinct empty-stream fixture must fail Load without calling InitNew.
Pre-OLE checks cover truncated zero-payload records, duplicate/missing/non-host
IDs and nonempty INITNEW payload rejection. No new test mode or executable is
introduced; complete resource-editor or original-application admission is not
inferred from these own fixtures.
The existing WebBrowser case additionally creates an INITNEW resource dialog in
both modal and modeless paths and verifies native navigation, TypeLib events,
automatic sink subscription, retained resource styles/help IDs and teardown.
The executable, modes and original 25-second budget remain unchanged.

The existing lifecycle case additionally consumes a compiled, own 2560-byte
MS-CFB v3 fixture with a four-byte Marker stream. Standard/extended resources
exercise licensed/unlicensed storage loads, explicit kind selection even when
stream interfaces exist, both site orders, mixed storage/INITNEW/stream records
in reverse resource order, read-only enforcement, cached reads after local
container references expire, close with a retained control, absent IPersistStorage,
license rejection, failed first/later Load, reentrant storage QI/load retirement,
modal/modeless rollback and owner reuse. Corrupt compound images fail through
native APIs. Pre-OLE checks cover bounded storage payloads and license prefixes;
no COM initialization or original application admission is inferred.

Existing pre-OLE dialog cases also use compiled standard/extended and named
list/combo resources. They verify repeated and interleaved records, native
sorting, empty entries, odd byte offsets, process-ACP conversion against native
MultiByteToWideChar, native uppercase behavior, exact template/resource bytes,
modal/modeless parameters, owner reuse and self-deleting final callbacks. Invalid
lengths/terminators/IDs/classes, conflicting kinds and unsupported owner-drawn or
no-data or owner-drawn styles without HASSTRINGS fail before user initialization. The existing lifecycle case mixes
these strings with a real registered ActiveX INITNEW control. Existing executable,
modes, workflow, 25-second budgets and stdout markers are unchanged. Nonempty or
licensed INITNEW still has no admitted wire format and remains unsupported.

Existing pre-OLE cases add compiled standard/extended fixed/variable owner-drawn
list/combo resources with HASSTRINGS. They verify native WM_MEASUREITEM before
application initialization, measured item heights, sorted/copied strings and
WM_DRAWITEM while painting into an in-memory DIB DC. Painting after modeless
Create also verifies that application callbacks work after creation-context
handoff. Measurement creates a nested modal resource dialog; variable measurement
sends a recursive WM_INITDIALOG which must not initialize twice. Immediate close
from application WM_INITDIALOG, owner reuse and self-deleting final callbacks
verify retirement after context handoff. Early measurement only supplies dimensions;
child contents are queried after native construction. Existing ActiveX load-failure tests count final callbacks
exactly when an early message attached the C++ owner. Native storage/stream/sink
regressions remain in their existing modes and 25-second budgets. This software
painting does not invoke a printer or hardware HSP.
# BSTR stream foundation (post-0.1.0, qualification pending)

`CComBSTR::ReadFromStream` / `WriteToStream` use a Windows ULONG byte
count followed by BSTR bytes and the two-byte terminator. Zero denotes
a null BSTR; an allocated empty BSTR has a count of two. Embedded NULs
and odd-byte BSTR payloads are counted, not scanned. Short reads/writes
return STG_E_READFAULT/STG_E_WRITEFAULT; failed HRESULTs are propagated.
Invalid terminators are rejected. Reads stage native BSTR ownership and
preserve the previous value on failure; the stream position is not rolled
back. Writes can leave a partial record. Both one-argument reads impose a
16 MiB default BSTR payload budget before allocating. The two-argument
ReadFromStream(stream, maxPayloadBytes) overload allows callers to choose a
smaller limit for untrusted records, or explicitly permit larger trusted
records. The budget counts payload bytes, excluding the two-byte wire
terminator; declared lengths over budget return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE),
without changing the existing owner. The encoded ULONG, not the actual
stream size, controls allocation; an oversized record consumes only its
length field before rejection. Writes do not impose a payload cap.

This is the first subset of #94. The bounded VARIANT stream candidate is documented below; property-map stream
persistence and IPersistStreamInitImpl remain unimplemented. #95–#97 are
reactivated independent tracks, not completed by these primitives.

## VARIANT stream foundation (qualification pending)

CComVariant stream methods encode a two-byte VARTYPE followed by the native
Windows little-endian scalar bytes, or the counted CComBSTR record for VT_BSTR.
Supported: EMPTY, NULL, I1/UI1, I2/UI2, I4/UI4, INT/UINT, I8/UI8, R4/R8,
ERROR, BOOL, CY and DATE. No raw pointers or VARIANT padding are serialized.
ARRAY, BYREF, RECORD, DECIMAL and interface values return DISP_E_BADVARTYPE;
writes reject them before emitting a tag. There is no coercion or activation.
Reads stage native ownership, preserving the previous value on malformed,
unsupported or short input, and propagate VariantClear failures (such as a
locked destination SAFEARRAY). The stream position is not rolled back.
Property-map persistence is still missing under #94. This is a bounded subset,
not a claim that all ATL stream formats or interface persistence are supported.

The scalar/BSTR fixture checks prove this implementation's explicit byte
contract and ownership, not interoperability with every Microsoft ATL release.
EMPTY/NULL and I8/UI8 stream representations in particular still require
independent target-version ATL evidence before claiming interoperability.
The public CComVariant methods do not coerce unsupported values through BSTR.
For the admitted scalar/BSTR subset, CComVariant::GetSize() now matches
the successful WriteToStream byte count: VARTYPE + fixed scalar bytes,
or VARTYPE + ULONG + nullable BSTR bytes and terminator. It returns
sizeof(VARTYPE) for unsupported values and on BSTR size overflow,
consistent with the documented error marker. VT_EMPTY and VT_NULL also
have that legitimate two-byte stream length; the caller must check
the variant type rather than interpreting the size alone as success.
It does not query IPersistStream/IPersistStreamInit or activate interface
values, because those values are intentionally unsupported here.
The existing real IStream suite compares GetSize() with native Stat()
for all admitted scalar tags and null/allocated-empty/binary BSTRs.

The same 16 MiB one-argument budget applies to VT_BSTR inside VARIANT;
ReadFromStream(stream, maxBstrPayloadBytes) bounds the nested BSTR before
allocation. Scalar records do not allocate, and the budget does not change
the fixed-width scalar encoding. The existing native suite tests both default
and caller-selected limits, hostile encoded lengths, transactionality and
retry. These checks do not establish Microsoft ATL version-specific wire
interoperability.

## COM property-page authoring foundation (qualification pending)

`atlctl.h` exposes IPropertyPageImpl<T> and IPropertyPage2Impl<T> for native
COM property-page objects. The class supplies T's CDialogImpl-style
Create/DestroyWindow/m_hWnd implementation, COM map and application Apply
logic. This is separate from ordinary WTL property-sheet pages and the gated
WTL ActiveX page/wizard framework in #96; _ATL_NO_HOSTING remains defined.

The initial subset owns page-site/object references, stages SetObjects before
replacing the old array, embeds a native dialog, and provides Move/Show/
Deactivate, CoTaskMemAlloc page-info strings, dirty notifications and dialog
accelerator processing. SetDirty retains the page/site across notification,
including release of the last external page owner in a callback. Reentrant
site/object replacement and activation/deactivation transitions are rejected.
Rectangles are checked in wide arithmetic before conversion to native sizes.
The container must Deactivate before releasing its final page reference.

A positive m_size explicitly overrides the page size in pixels. With both fields
zero, GetPageInfo uses the bounded current-context resource measurement below.
Resource IDs m_dwTitle/m_dwDocString/m_dwHelpFile use the current resource instance
and normal LoadStringW resource selection; no locale-specific resource policy is
implemented yet. Empty title ID zero
returns an independently allocated empty title. The default Apply returns S_OK
without applying properties; derived pages supply validation/update logic and
clear dirty only on success. Help and EditProperty default to E_NOTIMPL.

Existing native host cases exercise a real resource dialog over repeated
activation/deactivation, metadata allocation/missing-resource cleanup, aliased
object-array replacement, Apply failure/success, checked rectangle bounds and
site callback final-reference release. This candidate does not qualify native
OleCreatePropertyFrame integration, help dispatch, per-property selection or
locale handling. #95 remains open until its remaining acceptance is complete.

### Authored pages in the native property frame (qualification pending)

The existing native OLE property-frame fixture now authors its registered page
with the production IPropertyPage2Impl/CDialogImpl implementation. Windows still
owns OleCreatePropertyFrame and its modal loop. The existing scenarios retain
actual IOleObject edits, Apply versus Cancel, repeated frame opening, owner
state, explicit French site LCID, reentrant ShowPropertyFrame rejection and
site/object/window cleanup counters. There is no second property-frame engine.

Direct page-info cases load nonzero title/doc/help resource IDs, verify copied
strings are independent, and verify cleanup when a later resource field fails.
The native frame uses the same repository-owned metadata resources. These are
pending native execution; successful site-LCID propagation does not establish
locale-specific resource selection or help UI. Font/template size inference
is a separate candidate described below.

### Preactivation page-size inference (qualification pending)

When m_size is entirely zero, GetPageInfo reads T::IDD from the current resource
instance and derives pixel size without creating a dialog, initializing COM or
running application callbacks. A positive explicit m_size wins; invalid partial
sizes are rejected. Custom Create classes without a constant numeric IDD may
continue to supply an explicit size; automatic lookup returns E_NOTIMPL for them.
Inferred size is not written into m_size or cached across DPI contexts.

The helper reuses the bounded DialogTemplateReader for standard and extended
headers, positive signed dimensions and terminated font names. No child-control
creation data is interpreted. Native GetDialogBaseUnits handles unstyled fonts;
CreateFontIndirectW, GetTextMetricsW and GetTextExtentPoint32W supply the font
metrics for ordinary DS_SETFONT resources. Mapping uses MulDiv in dialog units,
and all font/DC selections are released before returning.

The scope is system/current-context DPI, not a future parent's per-monitor DPI.
DS_FIXEDSYS/shell-font substitution, special point size 0x7fff, non-normal font
weights and overlong face names return ERROR_NOT_SUPPORTED; explicit pixel size
remains available. No Windows10-only DPI imports are introduced.

Existing host cases compare standard/no-font, standard/FONT and extended/FONT
headers to real native MapDialogRect results. Oracle dialogs have zero controls,
so original CLSID child controls are not activated. Cases verify header/font
truncation, invalid dimensions and unsupported encodings; direct GetPageInfo
checks assert no page HWND/final callback or inferred-size cache. Native matrix
qualification of this new inference path is pending.
