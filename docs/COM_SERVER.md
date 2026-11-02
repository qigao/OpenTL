# ATL COM Server — implementation and compatibility roadmap

COM Server and COM Client are both mandatory OpenATL capabilities. The
existing source-owned server core under `OpenATL::COM` complements
`atlcom.h` and provides
ordinary and aggregated COM objects, explicit interface/class tables, class factories,
module lifetime accounting, and explicit registry registration. Standard ATL
DLL module exports and `OBJECT_ENTRY_AUTO` class discovery exist. A class
can now supply its own real `UpdateRegistry(BOOL)` callback to participate
in deterministic automatic DLL registration; the callback is not inferred
from the CLSID. Bounded RGS resources, per-user type-library registration,
basic/automatic/poly aggregation and tear-offs have implemented subsets.
Full RGS/registration semantics and the complete ATL COM Server API remain gaps.

For conventional ATL object declarations, `atlcom.h` supplies the
[object, aggregation, tear-off and COM-map compatibility subsets](ATL_COMPATIBILITY.md).
Their module-aware creators plug into the same class table. Local
ATL-style creation retains zero-ref semantics and pins an active application
CAtlModule if present; DLL classes still use the explicit COM::Module-aware
creator. The [modern EXE module](ATL_MODULE.md) separately owns its COM startup
and explicit process-local class registration.

## Ownership and concurrency protocol

- The DLL owns one `Module` and an immutable `ClassEntry` table. Both remain
  alive until all factories, objects, server locks and external DLL callers
  have retired. Neither table mutation nor module destruction is concurrent.
- Every factory and concrete object owns one `ModuleReference`. Interface
  pointers own object references via `QueryInterface`/`AddRef`/`Release`.
  The last release destroys the derived object before releasing its module
  reference. Constructor failure rolls back that reference.
- Each successful `LockServer(TRUE)` adds a separately balanced module
  reference. Unbalanced unlock returns `E_UNEXPECTED`; counter exhaustion
  fails acquisition. Misuse of a released interface is outside the contract.
- Module and interface reference counters support multiple concurrent callers.
  This does not synchronize application object fields or marshal interfaces.
  A `Both` class must implement its own data synchronization, and clients
  must marshal pointers when crossing apartment boundaries.
- `CanUnloadNow()` returns `S_OK` only with no retained factory, object or
  server lock. It does not authorize forced `FreeLibrary` during an export,
  callback, worker, or an external DLL call. The host must obey COM unload
  rules and retire its own external callers.
- `GetClassObject` and `CreateInstance` clear valid output pointers before
  failure. Unknown CLSIDs/IIDs fail explicitly; aggregation follows the class
  creator policy, and requires IID_IUnknown when a controlling outer is supplied.
  Derived constructors may throw; creation translates allocation failure to
  `E_OUTOFMEMORY` and other C++ exceptions to `E_UNEXPECTED`.
- The server never initializes COM, including in `DllMain`. The client owns
  apartment initialization. DLL entry points forward to the module and
  explicit registration API; they are ordinary application exports.

## Registration boundary

Registration accepts an explicit classes-root handle, one CLSID, optional
ProgID, description, absolute DLL path and threading model. The caller owns
the root handle and coordinates registration/unregistration exclusively with
installers and other registry writers. The process-default registry view is
selected by that handle; there is no hidden HKLM or elevation fallback.
Current-user COM deployment uses `HKCU\Software\Classes` explicitly.
ProgIDs are optional, start with an ASCII letter, and contain at most 39
ASCII letters, digits or periods. DLL paths are absolute and bounded to
32,000 UTF-16 code units; descriptions are bounded to 4,096 code units.

Registration creates new class/ProgID keys only and refuses existing keys.
It records ownership and rolls back its new entries if a subsequent step
fails. Unregistration validates ownership and expected values/children before
deleting known entries; unexpected or modified content is preserved and
reported as a conflict. No recursive deletion or general RGS execution occurs.
Registry operations are not a transaction against concurrent external writers;
cleanup failure is returned and may require installer intervention.

## Multi-class registration milestone

`RegisterClasses(root, entries, count)` and
`UnregisterClasses(root, entries, count)` extend the existing single-class
registrar to a bounded, explicit table of up to 1,024 COM class descriptors.
They share the **same** `RegisterClass`/`UnregisterClass` owner markers,
CLSID/ProgID registry plan, safe enumeration and nonrecursive deletion;
no second registry engine, COM apartment, elevation or fallback is introduced.

The entire table is validated **before registry mutation**, including each
module path, threading model, CLSID and case-insensitively unique ProgID.
If a later class registration fails, the API attempts reverse-order rollback
of only the classes installed by this batch. A rollback error is returned
instead of hidden. Batch unregistration checks every expected owner/value
and child key before removing *any* class, so a pre-existing foreign value
in a later entry does not partially remove earlier entries.

A batch call is **not an atomic Win32 registry transaction**. The caller must
serialize its installer writes against other processes and handle partial
registry cleanup after OS failures. Neither method infers a ProgID or
registration metadata from `OBJECT_ENTRY_AUTO` alone: that macro currently
selects class factories, not RGS/Windows installation semantics. The
standard ATL class-directed registrar and resource scripts remain upcoming
compatibility milestones.

## Class-directed ATL `UpdateRegistry` milestone

A class declared with `OBJECT_ENTRY_AUTO(clsid, Class)` may implement
`static HRESULT WINAPI UpdateRegistry(BOOL install)` using its actual
CLSID, ProgID, path, registry root and threading model. The header stores
a non-owning typed callback in each static object-map node, alongside the
factory creator. The DLL module invokes the callbacks only on explicit
`RegisterServer`/`UnregisterServer` requests; no registry operation
happens in `DllMain` or during simple factory lookup.

Entries are sorted deterministically by CLSID after a bounded validation
pass. All selected class callbacks must exist before any registry mutation;
otherwise registration fails with `E_NOTIMPL`. Successful calls register
in that order. If a later class fails, the DLL module invokes earlier
completed registrations' inverse callbacks in reverse order. Unregistration
also uses reverse order. A derived DLL module may supply its own registrar.
An explicitly bound manual factory table remains supported and does not
silently borrow unrelated auto-map registration callbacks.

For intentional classes requiring no Windows registration,
`DECLARE_NO_REGISTRY()` implements the deliberate `UpdateRegistry` no-op.
Without such an explicit decision, absent callbacks are **errors**.

The formal DLL fixture registers two distinct classes to
`HKCU\\Software\\Classes`, tests actual STA/MTA `CoCreateInstance`,
and injects a foreign second ProgID to prove first-class rollback.
All installation metadata remains class/application owned, with no implicit
HKLM registration or elevation. Callback-based rollback cannot prevent
concurrent external registry writes; use the strict owner-verified
`RegisterClasses`/`UnregisterClasses` APIs when a shared descriptor
batch and complete preflight are needed.

**Further work:** the `DECLARE_REGISTRY_RESOURCEID(id)` path now loads the
actual Windows `REGISTRY` resource and delegates to the existing bounded
RGS parser/owner-verified executor through the module's default or an explicit
context. Named resources and static module replacement-map helpers use that
same engine. Full ATL RGS variants, complete TypeLib
registration, and broad unchanged Microsoft ATL project compatibility
remain unqualified.

## RGS grammar admission — read-only, source-owned

The new `openatl/rgs_parser.h` implements a **bounded C++17 parser and
read-only projection** for an initial subset of real ATL registration
resources. It accepts a symbolic `HKCR` root; nested `NoRemove` and
`ForceRemove` keys; case-insensitive key/value names; default and named
`REG_SZ` values (`= s '...'` and `val Name = s '...'`);
GUID key names; quoted bare `Programmable` keys; `TypeLib`,
`Version`, `AppID`, `LocalizedString`; and explicit, bounded
`%MODULE%` and `%MODULE_RAW%` replacement.

Inputs must fit **65,536 UTF-16 code units**, **512 keys**, **1,024 values**,
**20 nested scopes** and bounded name/path/value lengths. Unsupported
roots, value types or unknown substitutions, malformed quotes/braces, case-folded
duplicate keys/values, traversal names and ownership-marker collisions
fail closed. Failure leaves the output document unchanged.
The parser can additionally accept a bounded replacement map for custom
key/name/string tokens. Replacement text is never re-tokenized as grammar;
unknown/duplicate names, recursive `%` data and invalid expanded names are
rejected. `ResolveModule` produces an independent expanded document and never reads
environment variables or the registry.

**The parser alone never executes registry instructions.** `ForceRemove`
and `NoRemove` are AST directives until an application separately calls
the opt-in `RegisterRgs` / `UnregisterRgs` methods with an opened
caller-owned Classes root. The parser never opens HKCR/HKLM,
recursively deletes keys, loads RGS DLL resources or initializes COM,
and resource-driven `DECLARE_REGISTRY_RESOURCEID` is supported only for the
bounded source-owned RGS subset under explicit installer policy, not arbitrary
`CAtlRegistrar` compatibility. Subsequent wider registrar work must
accept an **explicit caller-owned root** and reject foreign-key/value
changes rather than treating `ForceRemove` as unqualified authorization
to delete another program's data.

The binary-native COM core test parses **four byte-identical Microsoft
VCSamples RGS resources** (pinned revision
`9e1d4475555b76a17a3568369867f1d7b6cc6126`): COMMap Chain/Outer,
ATLSafeArray ArrayManager and ATLCollections MapCollection. Their original
Git blob identities are checked at CMake configure time, their node/value
counts and values are compared, and malformed inputs are tested
independently. A successful parse is **not** evidence that those original
Microsoft ATL projects build, register or activate against OpenATL.

## Owner-verified RGS execution — explicit HKCU/Classes root

`openatl/rgs_registration.h` now provides
`OpenATL::COM::RegisterRgs(HKEY classesRoot, const RGS::Document&, modulePath, ownerTag)`
and `UnregisterRgs(...)`, reusing the existing
`registration_detail::Plan`, `inspect` and `remove` implementation rather
than maintaining a second COM/registration provider. The parser itself
(`rgs_parser.h`) remains read-only. A symbolic source `HKCR` does **not**
select a Windows root: callers must supply an **opened, writable** Classes
key. Predefined `HKEY_CLASSES_ROOT`, `HKEY_LOCAL_MACHINE`,
`HKEY_CURRENT_USER` and other predefined handles are rejected, and there
is no implicit elevation or registry-view switch.

Registration requires an absolute, bounded, canonicalized module path and a
caller-owned ASCII registration tag (1–64 letters/digits/periods/hyphens/
underscores). The entire resolved RGS tree is validated **before mutation**:
bounded depth/keys/values, ordered parents, canonical safe names and paths,
no duplicates, unsupported substitutions, owner-marker collision or foreign
preexisting keys. `ForceRemove` never overwrites an existing key and never
calls `RegDeleteTree`. Only the literal value-free
`NoRemove CLSID` and `NoRemove AppID` may be shared; these top-level containers
are not owned and are never removed, even if created by this registration.
Scripted values on either shared container and nested NoRemove groupings remain
rejected; foreign container values and unowned siblings are preserved.

Every newly owned key receives `OpenATL.COMOwner` as its first REG_SZ value
(`RGS1:<ownerTag>:<canonical module path>`); all original default/named
REG_SZ values are then written unmodified apart from bounded
`%MODULE%` / `%MODULE_RAW%` expansion. A write failure triggers an owner/content-verified
reverse cleanup of newly created keys. Cleanup errors override the initial
write failure; external changes are **never** silently deleted.

`UnregisterRgs` checks every expected owned key's marker, REG_SZ
type/byte count/content and complete set of direct child names **before
deleting a single key**. Foreign values/children, modified owners or
unexpected data return access denied and preserve earlier owned keys;
only verified owned keys are deleted nonrecursively in reverse order.
A missing previously owned key is accepted for recovery from interrupted
installation; duplicate unregistration is idempotent.

Native Windows tests run all four original, byte-pinned Microsoft
`.rgs` files under freshly GUID-named private
`HKCU\\Software\\OpenATL.RGSTest.*` roots. They compare the entire
resolved path/value projection, verify shared `NoRemove CLSID`, block
foreign/modified keys, and inject a deterministic write failure at every
owned node to prove reverse rollback. The test's final cleanup may use
`RegDeleteTree` **only on its uniquely owned private test root**; this is
not used in production registration.

This is **not an atomic Windows registry transaction**, and callers must
exclude concurrent installer writers. No COM initialization or registry
operation occurs during RGS parsing, `DllMain`, or object-map discovery.
The bounded executor is invoked from `DECLARE_REGISTRY_RESOURCEID` using the
DLL module's automatic synchronous context or an explicit
`ScopedRgsRegistrationContext`. The RGS executor itself does not register a
TypeLib; the module coordinates that separate OLEAUT32 operation. The complete
Microsoft ATL registrar grammar remains unsupported.

## Genuine `REGISTRY` resource-ID installation

`openatl/rgs_resource.h` provides numeric-ID and string-name overloads of
`ReadRgsResource` and `UpdateRegistryResource`. The first is a
**read-only** loader using exactly `FindResourceW` with resource type
`REGISTRY`, `SizeofResource`, `LoadResource`, and `LockResource`.
It handles strict UTF-8 (including BOM) and BOM-declared UTF-16 LE/BE,
rejecting malformed encoding, embedded NULs and over-budget resources before
parsing. Failed loads leave an existing output document unchanged. There
is **no** fallback to another resource type, file path or RGS engine.

`DECLARE_REGISTRY_RESOURCEID(IDR_X)` and `DECLARE_REGISTRY_RESOURCE(name)`
in `atlcom.h` provide the ATL
class's `static UpdateRegistry(BOOL)` hook, reusing the same immutable
`OBJECT_ENTRY_AUTO` class list and `CAtlDllModuleT` registration ordering
from #29. `CAtlDllModuleT` supplies its own image and a lazy registration
context. The operation captures `AtlSetPerUserRegistration` once, shared with
its TypeLib operation: true selects the current user's `Software\Classes`;
false selects the current token's merged Classes view through
`RegOpenUserClassesRoot`. Registry access occurs only on the first actual RGS
callback, and the opened root remains alive through rollback. Automatic owner
markers contain `OpenATL.Module` and the canonical DLL path.

An explicit `ScopedRgsRegistrationContext` takes priority and still accepts
an opened HKEY, the actual resource HMODULE and a custom owner tag. It validates
the module's absolute path and borrows its HKEY/HMODULE for that synchronous
invocation on the same module and thread. Direct class callbacks use the
active ATL module's static registrar; without that module they need an
explicit scope. Invalid/nested explicit scopes, root-access failures,
wrong resource type, unknown ID and invalid resource contents fail with an
HRESULT. Class-factory lookup and UI windows need no such scope or COM init.

`CAtlModule::UpdateRegistryFromResourceS` accepts either a UINT or an LPCTSTR
resource identifier and a standard `_ATL_REGMAP_ENTRY` sentinel array. The
default `UpdateRegistryFromResource` name calls this static implementation.
Both helpers preserve an existing operation's mode/context or supply a lazy
own-image context when called directly. Replacement names/data are copied
and validated before execution, and the resource is decoded/parsed before
opening a default registry root. See [module limits](ATL_MODULE.md).

Static resource helpers also invoke virtual
`AddCommonRGSReplacements(IRegistrarBase*)` after the explicit map is validated.
The published base interface supports real QI/reference counting and bounded
owned `AddReplacement`/`ClearReplacements` operations. Hook errors and exceptions
are translated before default registry writes. Callback exit freezes or closes
the capability on every path; a retained interface stays valid, pins its module,
and rejects later mutation. The per-user snapshot is captured before the hook.
The separate persistent `IRegistrar` execution interface and static CRegObject
facade now use that same replacement-map implementation, parser and executor.
They support string scripts, bounded strict-encoding disk files and REGISTRY
resources in explicit PE files. External resources use owned read-only/datafile
mapping handles; DllMain/static imports are not executed. A persistent MODULE
binding supplies the server identity, otherwise the resource source path is used.
Both tokens derive from the canonical path. Automatic MODULE expansion quotes
EXE paths, while MODULE_RAW and the registration owner marker stay unquoted.
Resource-file calls select formatting from the source PE header; explicit MODULE
bindings preserve the caller's enclosing-quote choice. The surrounding root
context is unchanged. The common-hook interface
still rejects extended IRegistrar QI and MODULE overrides.
CLSID_Registrar activation, dynamic registrar APIs and full CRegObject
inheritance compatibility remain separate work; see [module limits](ATL_MODULE.md).

The native `openatl_com_server_fixture.dll` now embeds two actual
`REGISTRY` resources with class-matching CLSIDs, plus two byte-identical
Microsoft `Chain.rgs`/`Outer.rgs` resources in a Windows .rc file. Its
two ATL classes use the numeric and named resource macros, not custom
`UpdateRegistry` code or synthetic strings. The DLL exports select per-user
mode and call the standard ATL module registrar, without manually opening a
registry root or constructing an RGS context. The STA/MTA client
tests verify both registered classes, ProgID lookup, a deliberately foreign
auxiliary ProgID conflict and cleanup; separate tests load the original
Microsoft REGISTRY resources from this real DLL and exercise a GUID-isolated
HKCU root. Native TypeLib write-denial tests also verify class rollback through
this automatic context; export checks detect context leaks after success and
failure. An isolated-root test proves explicit policy overrides the lazy
module default. Machine-wide permissions remain separately unqualified.

The same native client calls static module helpers on an original Microsoft
resource and a named parameterized resource under its GUID-isolated root.
It checks actual key/default/named values, embedded quote/brace data,
case-insensitive parameters, `%MODULE%`, conflicting re-registration,
modified-map unregistration rejection, missing resources/types, malformed
sentinel tables and cleanup. These use the existing executable and DLL.
The native DLL's common hook now supplies a real ThreadingModel parameter and
changes the global per-user mode during callbacks. Existing STA/MTA, native
TypeLib registration and DACL-denial rollback therefore verify that the whole
operation keeps its original policy. Additional native tests verify retained
interface identity/module locks, immutable callback exit, atomic failed additions,
clearing the map, bounded growth, and HRESULT/exception failure cleanup.
Existing native tests also call the static facade and retained IRegistrar:
actual string/default/module values, duplicate/modified-map rejection, UTF-8
and both UTF-16 file byte orders, writer-sharing denial, invalid/oversized
files, numeric/named resources, resource-image identity, QI identity, interface
survival after facade destruction, and final module-pin cleanup. These are
extensions of the same executable; no installer/consumer project is added.
External-resource tests additionally use a copied DLL with a rejected executable
entry point, confirm resource-only loading succeeds, reject wrong-identity
unregistration, verify MODULE binding/reset and sharing errors, delete the copy
after all calls, and perform native HKCU per-user registration/cleanup with a
fresh GUID key. Existing failure/TypeLib/class rollback tests remain active.
The same resource set is also embedded in the test EXE. Native checks cover EXE
module helpers, external resource-only EXE loading, mixed MODULE/MODULE_RAW,
explicit binding quotes/reset, raw owner markers and formatting-sensitive
unregister preservation. The portable parser suite checks case folding, command
suffixes, raw icon paths, quote budgets and unchanged outputs after failure.
The common module and EXE persistent-registration paths now reuse the DLL's
frozen automatic object map and registration workflow. Existing module tests
drive a real class callback/resource under a GUID private HKCU root, verify EXE
quoted/raw paths and raw ownership, reject deletion after a value change, and
check preflight, wrong-thread, failed COM initialization and terminated state.
Base-module execution retains its no-COM contract. EXE RegisterClassObjects now
creates automatic factories from that same map unless SetClassObjects explicitly
selects a table (including an intentional empty table). Both paths use the same
native cookie helpers and rollback. STA/MTA tests cover real same-process
CoGetClassObject/CoCreateInstance, class identity, object/server locks, retained
factory lifetime after revocation, wrong-thread/duplicate-map rejection,
repeated Run, loop exceptions and Term/destructor cleanup before COM teardown.
Factory activation performs no persistent class registration. A separate part
of the existing module admission installs a unique owner-verified HKCU
LocalServer32 entry and lets Windows SCM launch this same executable. STA/MTA
children expose system IClassFactory/IDispatch/IPersist interfaces; the client
checks the remote PID/image/CLSID through real proxy calls, both creation APIs,
LockServer, retained proxies after revocation, native process exit and exact
registry cleanup. Quoted MODULE/unquoted MODULE_RAW supply the launch command
and ServerExecutable. There is no direct CreateProcess launch or alternate
transport, new binary, project or workflow. AppID security policy, arbitrary
custom-interface marshaling and idle shutdown remain separate.

AppID resource registration is now available through RegisterAppId/UnregisterAppId
and DECLARE_REGISTRY_APPID_RESOURCEID. DLL exports and EXE registration commands
install AppID before classes and remove it after successful class unregistration,
with owner-verified rollback after registration failure. Nested helpers reuse the
whole operation's captured mode/root. Native tests use the existing embedded
resource set and a GUID private HKCU root to check GUID/EXE-alias metadata,
ownership and foreign/shared preservation, command/export order, mode changes,
exception propagation and rollback error priority. An undeclared AppID returns
S_FALSE without writes. This does not qualify arbitrary AppID security settings,
DCOM permissions, RunAs or service management.

RegServerPerUser/UnregServerPerUser now enable the image-local per-user setting
and invoke that same application-registration workflow with TRUE for TypeLib.
Native tests cover actual default-root HKCU RGS work under a fresh unique key,
both command spellings, AppID ordering/rollback and captured policy, failure and
exception propagation, persistent mode and ordinary-command continuity. Invalid
input/state does not change the setting; a nested command incompatible with its
enclosing module policy is rejected. The application hook used for the HKCU
test alone is not EXE embedded-TypeLib qualification; see [command limits](ATL_MODULE.md).

The existing EXE additionally embeds the same actual COMMap.tlb resource as the
DLL and compiles the shared dispatch fixture. Separate STA/MTA module cases run
the full per-user registration commands with real AppID and selected automatic
class resource callbacks, standard module registration and OLEAUT32 TypeLib work.
They verify the registered path is this EXE, actual library identity/version/
SYSKIND, embedded and registered ITypeInfo::Invoke, missing-index/invalid-image
preflight, absence after unregistration and zero locks/COM balance. A real
TypeLib-key DACL denial must preserve the native failure and roll both class and
AppID resources back, even when common hooks change the global mode. Preexisting
sample metadata is rejected before writes. No new binary, target or workflow is
added, and the original Microsoft IDL remains byte-identical.

This is a restricted source-compatible resource-ID API, **not** a general
RGS interpreter or the complete standalone registrar API. It preserves
the existing owner markers and nonrecursive deletion semantics and does not
authorize blind `ForceRemove` replacement. The caller must serialize
installer writes; no actions run under DllMain. Original Microsoft ATL
sample builds remain to be qualified in #22.

## First native IDispatch / type-info milestone

`atlcom.h` now exports a first `ATL::IDispatchImpl<T, &IID, &LIBID,
major, minor, holder>` API, backed by one `ATL::CComTypeInfoHolder`
which uses Windows' actual `LoadRegTypeLib` and
`ITypeLib::GetTypeInfoOfGuid`. `GetTypeInfoCount`,
`GetTypeInfo`, `GetIDsOfNames` and `Invoke` use real
`ITypeInfo` and enforce non-null outputs, `IID_NULL`, bounded names,
argument consistency and authentic OLE HRESULTs.

The native COM Server core test uses Windows OLEAUT32
`CreateDispTypeInfo` to construct an actual `ITypeInfo`, injected via
the optional ATL type-info-holder parameter. It verifies late-bound
case-insensitive member name resolution, `ITypeInfo::Invoke`,
return-VARIANT ownership, correct COM `IUnknown` identity,
missing LIBID/IID failures, and image-TLB mode fail-closed behavior.
It intentionally creates a true OS Automation type-info object, not a
fake IDispatch function returning success.

The existing own MIDL event TypeLib also defines a dual `Combine` method with
one required `LONG` and one typed `defaultvalue(7)` parameter. The core test
implements that generated vtable through `IDispatchImpl` and asks Windows'
`ITypeInfo::Invoke` to supply the omitted default. Explicit positional and
named-optional calls, reverse `DISPPARAMS` ordering, a missing required value
and the native type-mismatch argument index are checked independently. This
qualifies typed MIDL defaults; untyped optional VARIANTs and broader LCID
conversion remain separate work.

The same generated dual interface has an `[in, out] LONG*` method. Its native
test supplies `VT_BYREF | VT_I4` through `DISPPARAMS`, observes both mutation
of the caller's storage and the independent retval, and checks native
type-mismatch indexing for an incompatible BYREF value. This qualifies typed
scalar BYREF dispatch; nested VARIANT, array, record and interface-reference
forms remain separate compatibility boundaries.

An additional `[optional] VARIANT` method verifies the untyped omission rule:
Windows fills a missing trailing argument with `VT_ERROR` and
`DISP_E_PARAMNOTFOUND` before calling the generated vtable. Explicit positional
and named values remain ordinary VARIANTs. This does not qualify varargs or
locale-sensitive conversion.

**This is partial ATL Automation compatibility.** There is no successful
registered/installed TypeLib native acceptance yet; the standard holder
does not cache type-info across apartment/module lifetime boundaries.
The ATL `0xffff/0xffff` image-based TypeLib mode returns
`E_NOTIMPL` instead of falling back to the registry or claiming success.
The original Microsoft COMMap/AtlSafeArray project compile/link
admission, real TypeLib deployment, BYREF/EXCEPINFO edge cases and
`DECLARE_LIBID` integration remain separately tracked under #22/#37.

## Actual file-backed TypeLib loading and module LIBID

The `CComTypeInfoHolder::GetTIFromFile(absolutePath, IID, LIBID, major,
minor, ITypeInfo**)` extension loads a real binary Windows TypeLib using
`LoadTypeLibEx(..., REGKIND_NONE)` (never `REGKIND_DEFAULT` or
`REGKIND_REGISTER`). It validates the typelib's GUID and exact version
using `ITypeLib::GetLibAttr`, then resolves the requested IID with
`GetTypeInfoOfGuid`. Missing files, mismatched LIBID/version/IID,
incomplete/relative paths and null output pointers fail without exposing a
partial interface. Every result uses ordinary COM ownership and the helper
does not cache COM pointers across apartment or module retirement.

The native test creates an **actual binary .tlb** in a GUID-owned temporary
path with Windows' `CreateTypeLib2`, adds an Automation method using
`ICreateTypeInfo`, calls `SaveAllChanges`, then exercises the **on-disk**
loaded `ITypeInfo::GetIDsOfNames`, version/LIBID/IID rejection, and proves
`LoadRegTypeLib` still fails because no typelib was installed globally.
The temporary file is removed via its owning fixture.

`DECLARE_LIBID(LIBID_...)` now provides the original ATL-derived module
`InitLibId()`, which `CAtlModuleT<T>` invokes during module construction
without any COM initialization. Its source-compatible static
`CAtlModule::m_libid` is per-image module metadata, not an authorization
for registry writes. A native module test asserts the exact LIBID and
`GetLibID` output/null-pointer contracts.

**Scope:** on-disk `REGKIND_NONE` load is independently useful and *not*
the same as image-embedded resource TLB resolution. Automatic
`0xffff/0xffff` loading from an image, genuine installed typelib lookup,
MIDL codegen, full ATL `RegisterTypeLib`/`UnRegisterTypeLib` ownership,
and original Microsoft COMMap native project builds are still missing.
No static bypass or handwritten ITypeInfo provider is introduced.

## Integration and validation

Link the single `OpenWTL::WTL` target. It supplies `ole32`, `oleaut32`,
`uuid` and `advapi32`; no separate client/server/registration targets are
required. Include `openatl/com_server.h` or `openatl/com_registration.h`
explicitly. Neither linking nor including a header initializes COM or
registers a class.

For a user-defined `IExample : IUnknown`, use an explicit IID descriptor:

```cpp
using namespace OpenATL::COM;
class Example final : public Object<Example, Interface<IExample, &IID_IExample>> {
public:
    explicit Example(Module& module) : Object(module) {}
    // Implement all IExample methods; methods must respect COM error/ABI rules.
};
const ClassEntry classes[] = {{CLSID_Example, &Example::CreateInstance}};
Module module(classes, 1);
```

The interface descriptors must use distinct IIDs; the first interface is the
canonical `IUnknown` identity. An inherited base IID is exposed with
`InterfaceAlias<IBase, &IID_IBase, IDerived>` alongside the implemented
`Interface<IDerived, &IID_IDerived>`; this reuses the same subobject.
A custom `Creator` must obey COM output/refcount
rules and retain the module for its concrete object's entire lifetime. The
provided `Object` template does that automatically. The concrete destructor
must be accessible to `Object` and must not throw.

`DllGetClassObject` forwards to `module.GetClassObject`, and `DllCanUnloadNow`
forwards to `module.CanUnloadNow`. Export them through a `.def` file. Registration
is separate: `RegisterClass(root, entry)` and `UnregisterClass(root, entry)` use
the same `ClassRegistration` description and return `HRESULT`. A new
registration conflict returns `HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS)`;
ownership/content conflicts during removal return access denied. Missing
owned keys are tolerated during removal. When rollback fails, its error is
returned in preference to the initial write error, and remaining owned keys
may need cleanup. Successful registration requires exclusive installer access;
it does not promise atomicity across multiple class registrations.

The complete compilable DLL/export and client examples are the formal
`tests/com_server/fixture.cpp` and `tests/com_server/server.cpp` fixtures.

Formal native tests cover interface identity, creation failures, concurrent
reference accounting, module/server locks, a real server DLL, STA/MTA COM
activation, registry conflicts, rollback, foreign-content preservation and
unregistration. Windows x64/ARM64 use the same source with MSVC and LLVM-MinGW;
cross builds establish PE compilation only.

References:
- https://learn.microsoft.com/en-us/windows/win32/api/unknwnbase/nn-unknwnbase-iclassfactory
- https://learn.microsoft.com/en-us/windows/win32/api/combaseapi/nf-combaseapi-dllcanunloadnow
- https://learn.microsoft.com/en-us/windows/win32/com/in-process-server-threading-issues

## Basic ATL aggregation and creator policies

CComCoClass selects the standard default aggregatable policy. The class may
select DECLARE_AGGREGATABLE, DECLARE_NOT_AGGREGATABLE or
DECLARE_ONLY_AGGREGATABLE. OBJECT_ENTRY_AUTO honors these built-in policies;
it no longer hardwires every class to CComObject. An aggregate factory request
must ask for IID_IUnknown; invalid requests fail before allocation or touching
the outer. Aggregate-only nonaggregated creation returns E_FAIL.

CComAggObject owns a separate nondelegating IUnknown and contains a
CComContainedObject whose public interface QI/AddRef/Release delegate to the
borrowed controlling outer. Raw CreateInstance(outer, &raw) returns zero
references, matching CComObject. Acquire a raw reference immediately. The outer
owns the nondelegating inner IUnknown and must release it during destruction;
the inner must not retain the controlling outer just for existing.

COM_INTERFACE_ENTRY_AGGREGATE and its BLIND form query an explicitly supplied
inner. Simple entries return borrowed pointers internally, while delegated QI
returns an owned result. That distinction propagates through chained maps so
public QI owns exactly one reference and every exposed interface returns the
same controlling IUnknown. A simple/chained entry supplies outer IUnknown;
aggregate entries never replace it.

The explicit ClassEntry {CLSID, Creator} form keeps its existing nonaggregated
callback. An optional third createWithOuter callback accepts the controlling
IUnknown and uses the same Module/ClassFactory engine. Exactly one callback
must be present. Both object wrappers bind the engine before FinalConstruct
and retain it through FinalRelease and the contained destructor.

Existing --core tests cover creator policies, raw zero refs, pointer errors,
independent inner/outer counts, specific/blind/chained aggregate maps, failure
HRESULTs/exceptions and module pins. The existing real DLL --sta/--mta tests
exercise direct factory creation without COM initialization and native
CoCreateInstance aggregation after installation, then verify real methods,
canonical identity, reference retirement, DllCanUnloadNow and unload.

This basic aggregation milestone does not qualify CComPolyObject,
arbitrary custom factory policies or the whole
original COMMap application. It introduces no second factory provider or test
entry point, and no printer/hardware-HSP CI requirement.

## Ordinary and cached tear-offs

CComTearOffObject implements a separately counted interface object. It retains
one controlling-owner reference while alive, queries its own map first, uses
the owner's IUnknown for identity and forwards missing interfaces to the owner.
Another owner QI creates a new ordinary instance. The owner reference survives
FinalRelease and the tear-off Base destructor.

CComCachedTearOffObject has a private, separately counted IUnknown. The owner
keeps one private reference in the declared raw slot (including CComPtr.p),
while every public contained interface retains the controlling owner. Repeated
queries reuse the same cached instance; there is no permanent owner/cache
reference cycle. The owner releases the private IUnknown in FinalRelease.

The existing core suite tests independent instance/cache identities, fallback
QI, owner retention after the original pointer is released, exact destruction
ordering, HRESULT/exception/allocation failure and retry, recursive construction
rejection, eight-thread first-publication and aggregated owner data. The same
MIDL-generated original ITearOff1/2 interfaces now run through real vtable
calls in the existing EXE/DLL cases. The unchanged original library omits
these unreferenced interface descriptions; GetTypeInfo must report
TYPE_E_ELEMENTNOTFOUND. Both tear-offs additionally implement the original
IOuter interface, whose actual embedded/registered type information drives
GetTypeInfo/GetIDsOfNames/Invoke with a short argument and BSTR result.
A retained cached IOuter is invoked after the DLL owner pointer is released.
No original Microsoft application is thereby marked qualified.

m_pOwner deliberately stores the actual Owner*; the exact Microsoft
CComObject<Owner>* member type and layout remain unqualified. See
ATL_COMPATIBILITY.md for the typed-owner and construction-lock contracts.
CComPolyObject, full original COMMap admission and
arbitrary custom factories remain separate work. Hardware HSP and printer
CI remain retired.


## Automatic aggregate COM-map entries

COM_INTERFACE_ENTRY_AUTOAGGREGATE and its BLIND form lazily activate a native
in-process class with the controlling outer and IID_IUnknown. The owner holds
one private inner reference and releases it during FinalRelease; public inner
interfaces retain the owner. A successful activation is cached even when the
requested interface is absent. Activation failures keep a null slot and may
retry. Blind entries continue to later map entries after any failure; specific
entries return the failure. Existing explicit AGGREGATE_BLIND entries follow
the same failed-query fallthrough rule.

Existing core cases prove absent-COM failure without implicit initialization,
prepopulated slots without activation and explicit blind failure fallthrough.
The existing native STA/MTA cases register an actual process-local class
factory through the same Module engine and exercise raw/CComPtr slots,
specific/blind/chained maps, exact QI reference ownership, missing-interface
cache retention, activation HRESULTs/exceptions/allocation failures and retry,
recursive creation rejection, nested controlling owners and module pinning
after factory revocation. The MTA case checks eight-thread first creation.
The same cases also auto-aggregate the real registered DLL class, call its
methods and prove owner retention and DLL unload after the last public release.
No test entry, target, workflow or printer/hardware probe is added.

The caller owns COM startup and apartment/marshalling policy. Construction
uses the owner's lock and bounded recursion guard; see ATL_COMPATIBILITY.md
for its reentrancy, blocking-hook and slot replacement contracts. This does
not qualify remote aggregation or the complete original COMMap project.


## Native rich error information

CComCoClass::Error and AtlReportError expose all six documented text/ANSI/resource
forms using native OLE ICreateErrorInfo/IErrorInfo. ISupportErrorInfoImpl provides
the single-IID support declaration. Metadata is copied into the native error
object, with IID, help context/file and an optional registered ProgID source.
Explicit HRESULTs are preserved; text defaults to DISP_E_EXCEPTION and resource
IDs use FACILITY_ITF. Missing resources report `Unknown Error.` in release builds
and assert in debug builds. Metadata preparation failures clear stale state and
preserve the specified/default HRESULT. No second error runtime is present.

Existing STA/MTA cases verify all twelve overloads, Unicode/ACP inputs, long
resources/text, native GetErrorInfo consumption/replacement and thread isolation.
The real registered DLL reports an error from its own string resource through
its existing GetValue method; the client retains that native IErrorInfo through
server-object release, unregistration and actual DLL unload. Existing ordinary
and cached tear-off IOuter TypeLib cases execute a failing real Invoke and check
its native EXCEPINFO. The registered IOuter route also resolves its parameter
name and invokes it by that DISPID, performs native BSTR-to-I2 conversion, and
reports the failing argument index for an unconvertible value. These checks
qualify this error-reporting and single-argument dispatch subset, not optional
parameters, all locale conversions, all Automation exceptions or complete
original Microsoft application compatibility.


## Original COMMap class-source subset

The existing STA ActiveX events case now builds the unchanged Outer/Chain and
four nested aggreg class bodies from a byte-pinned Microsoft snapshot. Native
process-local registration exercises all six logical OBJECT_ENTRY_AUTO classes
without persistent class registration. Tests execute the eight original GetName routes,
class factories, initial/partial FinalConstruct failure cleanup, specific/blind/
automatic aggregation, chained canonical identity, ordinary/cached tear-offs,
explicit native TypeLib Invoke and original rich errors. A cached interface
retains the owner and module pins after factory revocation until final release.

Repeated identical object entries from headers coalesce by type/CLSID/callback
identity. Conflicting registrations remain invalid. No original methods/macros
are transformed; no test target or mode is added. This does not admit the
unchanged original VCXPROJ builds, original output/export layout or
machine-wide DLL deployment. Original module/resource integration follows below. The original classes
remain single-threaded and are used on the existing STA owner.


The existing case also executes the six original RGS callbacks in an isolated
HKCU root, selecting the existing EXE/DLL resource images for their overlapping
IDs. It verifies actual paths, ProgIDs, threading values, duplicate-install
rejection and reverse cleanup. Two embedded MIDL TypeLibs are explicitly
registered per user through the public ATL helpers. Original registered-mode
IDispatchImpl executes the eight IOuter routes plus IChain/four aggregate
properties; independent unregister removes fresh lookup while retained native
ITypeInfo remains invocable. Missing libraries never fall back to the embedded
images. Original tear-off IIDs omitted by COMMap.tlb return
TYPE_E_ELEMENTNOTFOUND while its library is registered. Tests reject preexisting
sample keys and share the existing COM registration resource lock.


## Original DLL module/resource integration

Two existing unload DLLs additionally compile the unchanged COMMap.cpp and
Aggreg.cpp module entry points, class bodies and original .rc resources.
Their original private COM exports join the existing GUI exports. A test-only
per-image initializer selects per-user registration metadata; COM and registry
work occur only through the original exports after LoadLibrary returns.
Existing GUI unload/no-COM checks and test entry points remain intact.

After local source-subset factory retirement, the existing STA events case
registers both DLLs, checks actual HKCU class/TypeLib paths, and performs native
CoCreateInstance, direct/registered Invoke on all eight routes and five property
interfaces. Interface vtable allocation bases must identify the correct DLL;
each module rejects the other module's classes. Both unregister while a cached
interface retains the owner/inners. All owned interfaces and COM caches retire
before both images are verified unmapped. Original project builds, binary
filename/export layout, machine-wide deployment and exact ATL layout remain open.


## Poly objects on the existing factory engine

CComPolyObject<T> and DECLARE_POLY_AGGREGATABLE add the standard single-wrapper
policy to the existing automatic class creator path. m_contained delegates to
the wrapper's private IUnknown for nonaggregated creation, or to the borrowed
outer when aggregated. Private references and outer references stay independent.
Raw creation returns zero references; owned creators retain a construction
reference through QI and destroy all failed objects. Application and explicit
COM module pins outlive the contained object's destructor.

Existing core tests cover both modes, construction/exception failures,
unsupported interfaces, exact ownership, concurrent references, factory
retirement and tear-offs on poly owners. The existing Value DLL selects the
poly macro and retains its actual STA/MTA native activation/aggregation,
registration, error and unload coverage. No new engine, target, mode, workflow
or custom export is added. Earlier milestone gaps for CComPolyObject are
superseded by this subset; custom factories and exact ATL layout stay open.
