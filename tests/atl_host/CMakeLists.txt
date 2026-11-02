add_executable(openatl_activex tests.cpp lifecycle.cpp dialog_resources.rc)
target_link_libraries(openatl_activex PRIVATE OpenWTL::WTL)
set_target_properties(openatl_activex PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
if(MINGW)
  target_link_options(openatl_activex PRIVATE -static)
endif()
foreach(mode IN ITEMS events browser lifecycle)
  add_test(NAME openatl.activex_${mode} COMMAND openatl_activex ${mode})
  # These modes create native top-level OLE/Property Sheet UI on the same
  # interactive desktop. Keep them mutually exclusive even when a developer
  # runs CTest in parallel; unrelated native tests may still overlap.
  set_tests_properties(openatl.activex_${mode} PROPERTIES
    TIMEOUT 25 RESOURCE_LOCK openatl_activex_native_ui)
endforeach()
# The lifecycle case includes native property frames, normal/user-cancel
# wizards and guarded creation teardown. A local successful run takes ~29s;
# keep a bounded timeout with headroom for Windows GUI scheduling.
set_tests_properties(openatl.activex_lifecycle PROPERTIES TIMEOUT 60)

# Run the pinned original class sources inside the existing STA events case.
# No new executable, test mode, exports or external project are introduced.
if(CMAKE_HOST_WIN32)
  set(_commap_original "${PROJECT_SOURCE_DIR}/tests/official_samples/microsoft_atl/commap")
  set(_commap_generated "${PROJECT_BINARY_DIR}/tests/com_server/microsoft-commap")
  set(_safe_original "${PROJECT_SOURCE_DIR}/tests/official_samples/microsoft_atl/atlsafearray")
  set(_safe_generated "${PROJECT_BINARY_DIR}/tests/com_server/microsoft-atlsafearray")
  target_sources(openatl_activex PRIVATE
    "${_safe_original}/ArrayManager.cpp" "${_safe_original}/safearray_native.cpp"
    "${_safe_generated}/AtlSafeArray_i.c"
    "${_commap_original}/Outer.cpp" "${_commap_original}/Chain.cpp"
    "${_commap_original}/aggreg/Agg.cpp" "${_commap_original}/aggreg/AggBlind.cpp"
    "${_commap_original}/aggreg/AutoAgg.cpp" "${_commap_original}/aggreg/AutoAggB.cpp"
    "${_commap_original}/commap_classes_native.cpp"
    "${_commap_generated}/COMMap_i.c" "${_commap_generated}/aggreg/aggreg_i.c"
    "${PROJECT_BINARY_DIR}/tests/com_server/fixture_typelib.rc")
  set_source_files_properties("${_commap_generated}/COMMap_i.c"
    "${_commap_generated}/aggreg/aggreg_i.c" "${_safe_generated}/AtlSafeArray_i.c"
    PROPERTIES GENERATED TRUE LANGUAGE CXX)
  target_include_directories(openatl_activex PRIVATE
    "${_commap_generated}" "${_commap_generated}/aggreg" "${_safe_generated}")
  # The original premap/preagg headers select apartment threading. Keep every
  # translation unit of this STA test binary on the same ATL type definitions.
  target_compile_definitions(openatl_activex PRIVATE
    _ATL_APARTMENT_THREADED= OPENATL_ORIGINAL_COMMAP_CLASSES=1)
  set(_commap_registry_rc "${CMAKE_CURRENT_BINARY_DIR}/original_commap_registry.rc")
  file(WRITE "${_commap_registry_rc}"
    "1 REGISTRY \"${PROJECT_SOURCE_DIR}/tests/atl_rgs/fixtures/Outer.rgs\"\n2 REGISTRY \"${PROJECT_SOURCE_DIR}/tests/atl_rgs/fixtures/Chain.rgs\"\n102 REGISTRY \"${PROJECT_SOURCE_DIR}/tests/atl_rgs/fixtures/ArrayManager.rgs\"\n")
  target_sources(openatl_activex PRIVATE "${_commap_registry_rc}")
  set_source_files_properties("${PROJECT_BINARY_DIR}/tests/com_server/fixture_typelib.rc"
    PROPERTIES OBJECT_DEPENDS "${_commap_generated}/COMMap.tlb;${_commap_generated}/aggreg/Aggreg.tlb;${_safe_generated}/AtlSafeArray.tlb;${PROJECT_BINARY_DIR}/tests/com_server/event-fixture/EventFixture.tlb")
  add_dependencies(openatl_activex openatl_microsoft_commap_midl openatl_com_server_fixture
    openatl_unload_fixture openatl_unload_probe)
  set_tests_properties(openatl.activex_events PROPERTIES
    RESOURCE_LOCK "openatl_activex_native_ui;openatl_com_server_registration")
endif()
