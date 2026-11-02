cmake_minimum_required(VERSION 3.24)
project(OpenWTL VERSION 0.2.0 LANGUAGES CXX)

# Only Windows-targeting compilers: llvm-mingw on Windows/Linux/macOS,
# or MSVC cl.exe on Windows. OpenWTL::WTL is the public GUI target.
if(NOT WIN32)
  message(FATAL_ERROR "OpenWTL targets Windows; cross-compile with llvm-mingw")
endif()
# ATL headers are built from this checkout, never an external cache provider.
# There is deliberately no supported OPENATL_ATL_INCLUDE_DIR override.
if(DEFINED OPENATL_ATL_INCLUDE_DIR)
  message(FATAL_ERROR "OPENATL_ATL_INCLUDE_DIR is obsolete; only source-tree alt/include/ is supported")
endif()
set(_openatl_atl_include "${CMAKE_CURRENT_SOURCE_DIR}/alt/include")
if(NOT EXISTS "${_openatl_atl_include}/atlbase.h" OR
   NOT EXISTS "${_openatl_atl_include}/atlwin.h" OR
   NOT EXISTS "${_openatl_atl_include}/atlfile.h" OR
   NOT EXISTS "${_openatl_atl_include}/atlpath.h" OR
   NOT EXISTS "${_openatl_atl_include}/atlsync.h" OR
   NOT EXISTS "${_openatl_atl_include}/atlmem.h" OR
   NOT EXISTS "${_openatl_atl_include}/atltime.h" OR
   NOT EXISTS "${_openatl_atl_include}/atlcoll.h" OR
   NOT EXISTS "${_openatl_atl_include}/atlimage.h")
  message(FATAL_ERROR "Missing repository-owned OpenATL headers")
endif()
if(DEFINED OPENATL_WTL_INCLUDE_DIR)
  message(FATAL_ERROR "OPENATL_WTL_INCLUDE_DIR is obsolete; OpenATL manages its pinned WTL dependency")
endif()
file(READ "${_openatl_atl_include}/atlbase.h" _openatl_marker LIMIT 240)
file(READ "${_openatl_atl_include}/atldef.h" _openatl_def_marker LIMIT 2048)
if(NOT _openatl_marker MATCHES "OpenATL - Clean-room ATL subset for WTL 10"
   OR NOT _openatl_def_marker MATCHES "OPENATL_REPOSITORY_HEADERS")
  message(FATAL_ERROR "Refuse missing, external, or unmarked ATL headers")
endif()

# Private clean-room ATL / COM backend. Consumers link only OpenWTL::WTL;
# the backend stays source-owned and transitively available to WTL.
add_library(openatl_atl INTERFACE)
target_include_directories(openatl_atl INTERFACE "${_openatl_atl_include}")
add_library(openwtl_wtl INTERFACE)
add_library(OpenWTL::WTL ALIAS openwtl_wtl)
target_link_libraries(openwtl_wtl INTERFACE openatl_atl)
target_compile_features(openatl_atl INTERFACE cxx_std_17)
target_compile_definitions(openatl_atl INTERFACE
  UNICODE _UNICODE _WIN32_WINNT=0x0601 WINVER=0x0601 _WIN32_IE=0x0601)

# One consumer target supplies the system dependencies for COM helpers and
# explicit registration. Linking these imports does not initialize COM or
# register any class; callers still opt into those operations explicitly.
target_link_libraries(openatl_atl INTERFACE
  ole32 oleaut32 uuid advapi32 comctl32 gdi32 user32 shell32 shlwapi
  comdlg32 uxtheme msimg32 dwmapi)
if(MINGW)
  # WTL's _tWinMain is wWinMain in this Unicode-only library. Apply the
  # matching CRT entry only to GUI executables, leaving console tests alone.
  target_link_options(openatl_atl INTERFACE
    "$<$<AND:$<BOOL:$<TARGET_PROPERTY:WIN32_EXECUTABLE>>,$<NOT:$<BOOL:$<TARGET_PROPERTY:OPENATL_USE_WINMAIN>>>>:-municode>")
endif()
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/OpenATLWTL.cmake")

# A selected native Windows SDK RC also supplies Ribbon declarations absent
# from mingw-w64. Keep this transitive on the one public consumer target.
if(MINGW AND CMAKE_HOST_WIN32 AND CMAKE_RC_COMPILER MATCHES "/rc\\.exe$")
  include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/OpenATLRibbon.cmake")
  openatl_use_ribbon_sdk(openwtl_wtl)
endif()

# Use the compiler target, never a vcpkg triplet or the build host's CPU.
if(MSVC)
  set(_openatl_target_arch "${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}")
else()
  set(_openatl_target_arch "${CMAKE_SYSTEM_PROCESSOR}")
endif()
set(_openatl_x64 FALSE)
if(_openatl_target_arch MATCHES "^(x64|x86_64|AMD64|amd64)$")
  set(_openatl_x64 TRUE)
endif()

# One Unicode implementation: native Windows NLS. Old optional Salts Unicode
# provider is intentionally removed; do not silently accept stale CMake flags.
if(DEFINED OPENATL_USE_SALTS_UNICODE)
  message(FATAL_ERROR
    "OPENATL_USE_SALTS_UNICODE is retired; use Win32 encoding APIs through OpenATL::Unicode")
endif()

# Qualification defaults to static CRT. An SDK-consuming profile can select
# its matching CRT at the standard CMake configuration owner.
if(MSVC AND NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

option(OPENATL_BUILD_QUALIFICATION "Compile native UI, ATL utility and COM client/server tests" OFF)
# Retain historical upstream Win32-only example support on explicit request;
# regular CI qualifies only Windows x64 and Windows ARM64.
option(OPENATL_BUILD_OFFICIAL_SUITE "Build all original 32-bit-only WTL release samples manually" OFF)
# Original WTL is the only include source for all consumers.
# This optional switch enables additional real native WTL API tests.
option(OPENATL_BUILD_WTL_UNMODIFIED "Run original WTL API native contract tests" OFF)
if(OPENATL_BUILD_WTL_UNMODIFIED AND NOT OPENATL_BUILD_QUALIFICATION)
  message(FATAL_ERROR "Original WTL compatibility requires native qualification")
endif()
if(OPENATL_BUILD_OFFICIAL_SUITE AND NOT OPENATL_BUILD_QUALIFICATION)
  message(FATAL_ERROR "The official suite requires OPENATL_BUILD_QUALIFICATION")
endif()
if(OPENATL_BUILD_OFFICIAL_SUITE AND NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "Use the official Win32 sample ABI: msvc-x86 or llvm-i686 preset")
endif()
if(OPENATL_BUILD_QUALIFICATION)
  if(MSVC)
    # A missing OpenATL header must fail, rather than pick up Microsoft's ATL
    # or MFC through VsDevCmd's INCLUDE. Keep the compiler and Windows SDK.
    file(TO_CMAKE_PATH "$ENV{INCLUDE}" _openatl_sdk_includes)
    list(FILTER _openatl_sdk_includes EXCLUDE REGEX "[Aa][Tt][Ll][Mm][Ff][Cc](/|$)")
    if(NOT _openatl_sdk_includes)
      message(FATAL_ERROR "Initialize the MSVC developer environment before qualification")
    endif()
    set(CMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES "${_openatl_sdk_includes}")
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/X>)
  endif()
  enable_language(RC)
  enable_testing()

  add_executable(openatl_gui_runtime
    tests/wtl10/gui_runtime.cpp tests/wtl10/gui_runtime.rc)
  target_include_directories(openatl_gui_runtime PRIVATE tests/wtl10)
  target_link_libraries(openatl_gui_runtime PRIVATE OpenWTL::WTL)
  add_test(NAME openatl.ui_no_com COMMAND openatl_gui_runtime)

  # Repository-owned ATL utilities (file/string/sync), independent of COM.
  # ole32 belongs ONLY to this test to verify apartment stays uninitialized.
  add_executable(openatl_atl_core
    tests/atl_conformance/atl_core.cpp
    tests/atl_conformance/cstring_resources.rc)
  target_link_libraries(openatl_atl_core PRIVATE OpenWTL::WTL ole32 advapi32)
  add_test(NAME openatl.atl_core_no_com COMMAND openatl_atl_core)

  # Repository-owned path and Unicode Win32 file test. No COM dependency.
  add_executable(openatl_atl_path tests/atl_conformance/atl_path.cpp)
  target_link_libraries(openatl_atl_path PRIVATE OpenWTL::WTL)
  add_test(NAME openatl.atl_path COMMAND openatl_atl_path)

  add_executable(openatl_unicode tests/atl_conformance/unicode.cpp)
  target_link_libraries(openatl_unicode PRIVATE OpenWTL::WTL)
  add_test(NAME openatl.unicode COMMAND openatl_unicode)
  add_executable(openatl_unicode_utf8 tests/atl_conformance/unicode.cpp)
  target_link_libraries(openatl_unicode_utf8 PRIVATE OpenWTL::WTL)
  if(MSVC)
    target_link_options(openatl_unicode_utf8 PRIVATE /MANIFEST:EMBED
      "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/tests/atl_conformance/unicode_utf8.manifest")
  else()
    set(_unicode_manifest_rc "${CMAKE_CURRENT_BINARY_DIR}/openatl_unicode_utf8.rc")
    file(WRITE "${_unicode_manifest_rc}"
      "1 24 \"${CMAKE_CURRENT_SOURCE_DIR}/tests/atl_conformance/unicode_utf8.manifest\"\n")
    target_sources(openatl_unicode_utf8 PRIVATE "${_unicode_manifest_rc}")
  endif()
  add_test(NAME openatl.unicode_utf8_acp COMMAND openatl_unicode_utf8 --utf8-acp)

  # Additional source-owned ATL-style heap, time and typed array utilities.
  # ole32 is used only by the native test to detect accidental COM init.
  add_executable(openatl_atl_extensions tests/atl_conformance/atl_extensions.cpp)
  target_link_libraries(openatl_atl_extensions PRIVATE OpenWTL::WTL ole32)
  add_test(NAME openatl.atl_extensions_no_com COMMAND openatl_atl_extensions)

  # Actual Win32 file/shared-memory mappings, offsets and handle lifetime.
  # No COM initialization: ole32 is only linked by the no-apartment test.
  add_executable(openatl_atl_mapping tests/atl_conformance/atl_mapping.cpp)
  target_link_libraries(openatl_atl_mapping PRIVATE OpenWTL::WTL ole32)
  add_test(NAME openatl.atl_mapping_no_com COMMAND openatl_atl_mapping)

  # GDI bitmap owners plus optional BMP/PNG GDI+ codec calls. No COM
  # initialization; ole32 is linked only for the no-apartment assertion.
  # Consumers calling CImage::Load/Save must link Windows gdiplus.
  add_executable(openatl_atl_image tests/atl_conformance/atl_image.cpp)
  target_link_libraries(openatl_atl_image PRIVATE OpenWTL::WTL gdi32 gdiplus ole32)
  add_test(NAME openatl.atl_image_no_com COMMAND openatl_atl_image)

  # Direct OS failure injection is compiled from the in-repo atlwin.h
  # and isolated behind OPENATL_WX_FAULT_TEST.
  add_executable(openatl_mitigation_runtime tests/wtl10/mitigation_runtime.cpp)
  target_link_libraries(openatl_mitigation_runtime PRIVATE OpenWTL::WTL)
  add_test(NAME openatl.mitigation_runtime COMMAND openatl_mitigation_runtime)
  # Opt into real toolchain CFG/CET on this isolated native binary. Its
  # PE and process policies are separately checked; these flags alone
  # never count as mitigation evidence for downstream consumers.
  if(MSVC)
    target_compile_options(openatl_mitigation_runtime PRIVATE /guard:cf)
    target_link_options(openatl_mitigation_runtime PRIVATE /GUARD:CF /DYNAMICBASE /NXCOMPAT)
    if(_openatl_x64)
      target_link_options(openatl_mitigation_runtime PRIVATE /CETCOMPAT)
    endif()
  else()
    target_compile_options(openatl_mitigation_runtime PRIVATE -Xclang -cfguard)
    target_link_options(openatl_mitigation_runtime PRIVATE -Wl,-Xlink,-guard:cf)
    if(_openatl_x64)
      target_link_options(openatl_mitigation_runtime PRIVATE -Wl,-Xlink,-cetcompat)
    endif()
  endif()

  # Hardware HSP diagnostics are manual only: no default build or CTest.
  # CI verifies CFG/CET PE metadata on openatl_mitigation_runtime instead.
  if(_openatl_x64)
    add_executable(openatl_cet_shadow_stack EXCLUDE_FROM_ALL
      tests/wtl10/cet_shadow_stack.cpp)
    target_link_libraries(openatl_cet_shadow_stack PRIVATE OpenWTL::WTL)
    if(MSVC)
      target_compile_options(openatl_cet_shadow_stack PRIVATE /guard:cf /W3 /EHsc)
      target_link_options(openatl_cet_shadow_stack PRIVATE
        /GUARD:CF /CETCOMPAT /DYNAMICBASE /NXCOMPAT /INCREMENTAL:NO)
    else()
      target_compile_options(openatl_cet_shadow_stack PRIVATE
        -Xclang -cfguard -fno-omit-frame-pointer -fno-optimize-sibling-calls)
      target_link_options(openatl_cet_shadow_stack PRIVATE
        -Wl,-Xlink,-guard:cf -Wl,-Xlink,-cetcompat -static)
    endif()
  endif()

  # Real test DLL, not a substitute for production plugin lifetime rules.
  # The host uses LoadLibrary/GetProcAddress, never a static import.
  set(_unload_fixture_exports "${CMAKE_CURRENT_SOURCE_DIR}/tests/wtl10/unload_fixture.def")
  set(_unload_probe_exports "${CMAKE_CURRENT_SOURCE_DIR}/tests/wtl10/unload_probe.def")
  if(CMAKE_HOST_WIN32)
    # Existing DLLs also host the original Microsoft modules. The COM server
    # subdirectory preserves these exports and appends the original COM names.
    set(_unload_fixture_exports "${CMAKE_CURRENT_BINARY_DIR}/tests/com_server/original_commap_exports.def")
    set(_unload_probe_exports "${CMAKE_CURRENT_BINARY_DIR}/tests/com_server/original_aggreg_exports.def")
  endif()
  add_library(openatl_unload_fixture SHARED tests/wtl10/unload_fixture.cpp "${_unload_fixture_exports}")
  target_link_libraries(openatl_unload_fixture PRIVATE OpenWTL::WTL user32)
  set_target_properties(openatl_unload_fixture PROPERTIES
    PREFIX "" OUTPUT_NAME "openatl_unload_fixture")
  add_executable(openatl_unload_runner tests/wtl10/unload_runner.cpp)
  target_link_libraries(openatl_unload_runner PRIVATE OpenWTL::WTL ole32 user32)
  add_dependencies(openatl_unload_runner openatl_unload_fixture)
  add_test(NAME openatl.dll_unload_native COMMAND openatl_unload_runner)
  set_tests_properties(openatl.dll_unload_native PROPERTIES TIMEOUT 35)

  # Independent blocked-callback probe from PR #9, alongside the queued-work
  # and caller-lease unload fixture above.
  add_library(openatl_unload_probe SHARED tests/wtl10/unload_probe.cpp "${_unload_probe_exports}")
  set_target_properties(openatl_unload_probe PROPERTIES PREFIX "")
  target_link_libraries(openatl_unload_probe PRIVATE OpenWTL::WTL user32)
  add_executable(openatl_unload_native tests/wtl10/unload_native.cpp)
  target_link_libraries(openatl_unload_native PRIVATE OpenWTL::WTL ole32 user32)
  add_dependencies(openatl_unload_native openatl_unload_probe)
  add_test(NAME openatl.dll_callback_unload COMMAND openatl_unload_native)
  set_tests_properties(openatl.dll_callback_unload PROPERTIES TIMEOUT 30)

  add_executable(openatl_thunk_fault tests/wtl10/thunk_fault.cpp)
  target_compile_definitions(openatl_thunk_fault PRIVATE OPENATL_WX_FAULT_TEST=1)
  target_link_libraries(openatl_thunk_fault PRIVATE OpenWTL::WTL)
  add_test(NAME openatl.thunk_fault COMMAND openatl_thunk_fault)

  add_executable(openatl_com_client tests/atl_conformance/com_client.cpp)
  target_link_libraries(openatl_com_client PRIVATE OpenWTL::WTL)
  add_test(NAME openatl.com_client COMMAND openatl_com_client)
  add_executable(openatl_com_site tests/atl_conformance/com_site.cpp)
  target_link_libraries(openatl_com_site PRIVATE OpenWTL::WTL)
  add_test(NAME openatl.com_site_no_apartment COMMAND openatl_com_site)
  # Every mode is its own process: COM apartment state is thread-local,
  # never shared with WTL UI or another COM client mode.
  foreach(_client_mode IN ITEMS sta mta changed-sta changed-mta uninitialized)
    add_test(NAME "openatl.com_client_${_client_mode}"
             COMMAND openatl_com_client "--${_client_mode}")
  endforeach()

  add_subdirectory(tests/com_server)
  add_subdirectory(tests/official_samples)
  if(OPENATL_BUILD_WTL_UNMODIFIED)
    add_subdirectory(tests/wtl_unmodified)
  endif()
  add_subdirectory(tests/atl_host)
  add_subdirectory(tests/ratl_samples)

  foreach(_qualified IN ITEMS openatl_gui_runtime openatl_com_client openatl_com_site openatl_unicode openatl_unicode_utf8 openatl_com_server_fixture openatl_com_server_tests openatl_thunk_fault openatl_mitigation_runtime openatl_unload_fixture openatl_unload_runner openatl_unload_probe openatl_unload_native openatl_atl_core openatl_atl_path openatl_atl_extensions openatl_atl_mapping openatl_atl_image)
    target_compile_definitions(${_qualified} PRIVATE NDEBUG)
    if(MSVC)
      target_compile_options(${_qualified} PRIVATE /W3 /EHsc)
      target_link_options(${_qualified} PRIVATE /INCREMENTAL:NO /DYNAMICBASE /NXCOMPAT)
    else()
      target_link_options(${_qualified} PRIVATE -static)
    endif()
  endforeach()

  set(OPENATL_WTL_DEMO_DIR "" CACHE PATH "External pinned wtltest root, optional")
  if(NOT OPENATL_WTL_DEMO_DIR STREQUAL "")
    if(NOT EXISTS "${OPENATL_WTL_DEMO_DIR}/main.cpp" OR
       NOT EXISTS "${OPENATL_WTL_DEMO_DIR}/wtltest.rc")
      message(FATAL_ERROR "Full WTL10 demo requires main.cpp + wtltest.rc")
    endif()
    add_executable(openatl_wtl10_sdi WIN32
      "${OPENATL_WTL_DEMO_DIR}/main.cpp"
      "${OPENATL_WTL_DEMO_DIR}/wtltest.rc")
    # The pinned SDI uses SysLink (comctl32 v6). MSVC already injects a
    # default RT_MANIFEST: merge its dependencies, do not add a duplicate.
    # MinGW/LLD does not generate that resource, so embed one there.
    if(MSVC)
      target_link_options(openatl_wtl10_sdi PRIVATE
        "/MANIFEST:EMBED"
        "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/tests/wtl10/sdi_comctl6.manifest")
    else()
      set(_sdi_manifest_rc "${CMAKE_CURRENT_BINARY_DIR}/openatl_sdi_comctl6.rc")
      file(WRITE "${_sdi_manifest_rc}"
        "1 24 \"${CMAKE_CURRENT_SOURCE_DIR}/tests/wtl10/sdi_comctl6.manifest\"\n")
      target_sources(openatl_wtl10_sdi PRIVATE "${_sdi_manifest_rc}")
    endif()
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_test(NAME openatl.wtl10_sdi_native_ui
      COMMAND "${Python3_EXECUTABLE}"
              "${CMAKE_CURRENT_SOURCE_DIR}/tests/wtl10/sdi_interactive.py"
              "$<TARGET_FILE:openatl_wtl10_sdi>")
    set_tests_properties(openatl.wtl10_sdi_native_ui PROPERTIES TIMEOUT 35)
    target_include_directories(openatl_wtl10_sdi PRIVATE "${OPENATL_WTL_DEMO_DIR}")
    target_link_libraries(openatl_wtl10_sdi PRIVATE OpenWTL::WTL)
    if(MSVC)
      target_compile_options(openatl_wtl10_sdi PRIVATE /W3 /EHsc)
      target_link_options(openatl_wtl10_sdi PRIVATE /INCREMENTAL:NO /DYNAMICBASE /NXCOMPAT)
    else()
      target_link_options(openatl_wtl10_sdi PRIVATE -static)
    endif()
  endif()
endif()

# Nonaggregated ATL object compatibility is explicit in atlcom.h; no obsolete module, RGS or RATL provider.
# The clean-room MIT ATL implementation is owned here under alt/include/.
# No checkout patch or external ATL implementation is ever used.
# Permanent source-tree-only API: no install(), export(), packaging, or
# installed-consumer acceptance. This project never distributes an Installed SDK.
