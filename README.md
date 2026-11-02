# OpenWTL

OpenWTL brings WTL 10.0.10320 and a repository-owned ATL implementation
into one source tree for building native Windows applications with
**MSVC or llvm-mingw, on x64 and ARM64**.

WTL supplies the UI layer: windows, dialogs, frames, message loops, common
controls and application helpers. The OpenATL backend supplies ATL utility
classes, Win32 windowing, COM Client and COM Server support. Both layers
are available through the public CMake target **`OpenWTL::WTL`**.

The project targets ATL/WTL source compatibility. The implemented ATL API
is a tested subset; see the [compatibility matrix](docs/ATL_COMPATIBILITY.md)
for supported behavior and remaining work.

Version 0.1.2 adds bounded BSTR/VARIANT streams, COM property-page authoring,
original WTL ActiveX page/wizard qualification, and broader native Automation
argument coverage. It also includes the standalone COM header, CSize::SetSize
and CWindow::GetWindowText(CString&) fixes used by Weasel.

## Use in an application

Check out [qigao/OpenWTL](https://github.com/qigao/OpenWTL) into your project
and pin the commit used by your application:

```sh
git clone https://github.com/qigao/OpenWTL.git external/OpenWTL
```

```cmake
cmake_minimum_required(VERSION 3.24)
project(MyApp LANGUAGES CXX)

add_subdirectory(external/OpenWTL)
add_executable(my_app WIN32 main.cpp)
target_link_libraries(my_app PRIVATE OpenWTL::WTL)
```

`OpenWTL::WTL` provides:

- WTL headers from `wtl/Include` and ATL headers from `alt/include`.
- C++17, Unicode definitions and the common Windows UI/COM/registry link
  dependencies.
- The private `openatl_atl` backend through the same target.

Applications continue to include `<atlbase.h>`, `<atlcom.h>`, `<atlapp.h>`,
`<atlframe.h>` and other ATL/WTL headers directly. `OpenATL::COM` and the
`openatl/*.h` extension headers keep their existing names.

Build integration uses the source tree. There is no installed SDK or
installer/consumer project. Microsoft ATL and a separate WTL package are
not dependencies.

## Repository layout

| Path | Contents |
| --- | --- |
| `alt/include/` | OpenATL backend: ATL-compatible headers and `openatl/` extensions |
| `wtl/Include/` | WTL 10.0.10320 UI headers with the documented compatibility patches |
| `wtl/Samples/` | All 11 original WTL sample trees |
| `wtl/AppWizard/` | Original WTL application wizard |
| `cmake/` | Source integration, Windows resources and llvm-mingw toolchain |
| `tests/` | Native ATL/WTL, COM, GUI, resource and lifetime tests |
| `docs/` | Architecture, API contracts, compatibility and acceptance details |
| `third_party_licenses/` | Third-party license notices |

The former root `include/` directory is now `alt/include/`. The WTL release
layout remains under `wtl/`. Source users who set include paths manually
must update the ATL path; CMake users receive it from `OpenWTL::WTL`.

## Build and test

The existing qualification presets use the `OPENATL_*` option names for
the ATL backend and native suites. Set up the corresponding compiler
before running them.

For llvm-mingw, set `OPENATL_LLVM_MINGW_BIN` to its `bin` directory:

```sh
cmake --preset llvm-x86_64
cmake --build --preset llvm-x86_64
ctest --preset llvm-x86_64 --output-on-failure
```

Use `llvm-aarch64` to cross-build Windows ARM64. Execute ARM64 binaries on
a real Windows ARM64 host. Linux/macOS hosts can cross-compile with the
same toolchain; native runtime acceptance runs on Windows.

For MSVC, use an x64 developer environment:

```sh
cmake --preset msvc-x64
cmake --build --preset msvc-x64
ctest --preset msvc-x64 --output-on-failure
```

Hardware CET/HSP enforcement and real printer probes are outside CI and are
not merge or feature-completion gates. Optional local diagnostics are documented
in [hardware diagnostics](docs/CET_HARDWARE_ACCEPTANCE.md) and
[WTL acceptance](docs/WTL_UNMODIFIED.md).
Use `msvc-arm64` in a HostX64-to-ARM64 developer environment for ARM64.

The regular CI matrix builds with MSVC and llvm-mingw for Windows x64 and
ARM64, then runs the existing Windows native suites, including real ARM64
execution. See the [toolchain matrix](docs/TOOLCHAIN_MATRIX.md) and
[acceptance criteria](docs/ACCEPTANCE.md).

## Capabilities and contracts

| Area | Implementation and details |
| --- | --- |
| WTL UI | Frames, dialogs, message maps, controls, resources, MRU and settings; [original WTL compatibility](docs/WTL_UNMODIFIED.md) |
| ATL utilities | `CString`, `CAtlFile`, `CPath`, collections, heap, time, synchronization, mapping and the bounded `CImage` subset; [API matrix](docs/ATL_COMPATIBILITY.md) |
| Text encoding | Counted UTF-8/UTF-16/ACP conversions through native Windows NLS; [encoding contracts](docs/UNICODE.md) |
| COM Client | Smart pointers, BSTR/VARIANT ownership and Windows component activation; [API matrix](docs/ATL_COMPATIBILITY.md) |
| Automation arrays | Native `CComSafeArray`, SAFEARRAY/BSTR/VARIANT ownership and `IObjectSafetyImpl`; original ArrayManager class/IDL dispatch subset |
| COM Server | Class factories, ordinary/poly aggregation and tear-off COM maps, server connection points/enumerators, rich error information, registration, RGS and DLL module lifetime; [server contracts](docs/COM_SERVER.md) and [module contracts](docs/ATL_MODULE.md) |
| ActiveX | Windowed hosting, property-map page discovery/browsing, caller-owned property-bag persistence, native storage-to-stream persistence and OLE property frames, external UI dispatch handlers, resource dialogs with list/combo initialization and owner-draw callbacks, CLSID-class conversion and DLGINIT stream/storage loading, license keys and INITNEW, typed and TypeLib-driven event sinks, shared source discovery, batch subscriptions and property notifications; [hosting contracts and gaps](docs/ACTIVEX_HOST.md) and [API matrix](docs/ATL_COMPATIBILITY.md) |

Win32 UI and utility classes do not initialize COM. Applications that use
COM explicitly own their apartment initialization and lifetime. Registry
operations are explicit. Optional `CImage` BMP/PNG codec callers link
Windows `gdiplus` themselves; it is not a transitive UI dependency.

Full ATL compatibility, complete Automation/type-library support,
custom factory policies, exact tear-off pointer/layout compatibility, windowless ActiveX and some original WTL native behaviors
remain open. API-level limits belong to the linked contracts; downstream
applications still need acceptance for their own behavior and lifetimes.

Unchanged Microsoft COMMap/aggreg class bodies also have native source-subset
coverage, including original DLL module/resources, per-user registration and
registered dispatch in existing test images; unchanged original project builds
remain unqualified. See the
[original sample scope](docs/OFFICIAL_SAMPLES.md).

## Source provenance and licensing

| Component | License and source |
| --- | --- |
| Project-owned additions, tests and build glue | [Apache-2.0](LICENSE) |
| Clean-room ATL foundation in `alt/include/` | [MIT](third_party_licenses/OPENATL-MIT.txt), derived from `kkhaike/openatl` revision `7b587f3e10f3f8d93a01522033de80007368bad5` |
| WTL 10.0.10320 in `wtl/` | [MS-PL](wtl/MS-PL.txt); [source provenance](wtl/SOURCE.md) |

The complete WTL release retains its headers, samples, AppWizard, binary
resources and original notices. Seven pre-existing compatibility patches
are recorded alongside the 296-file byte-level
[source manifest](wtl/source-manifest.json). The ATL directory move keeps
all header contents and notices unchanged.

See the [architecture](docs/ARCHITECTURE.md),
[project scope](docs/PROJECT_SCOPE.md) and
[original sample inventory](docs/OFFICIAL_SAMPLES.md) for further details.
