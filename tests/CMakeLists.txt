add_executable(binblock-tests
  ../bindings/wasm/wasm_api.c
  ../integrations/wii/binblock_wii.c
  unit/test_collection.c
  unit/test_context.c
  unit/test_foundation.c
  unit/test_main.c
  unit/test_program.c
  unit/test_support.c
  unit/test_syntax.c
  unit/test_wasm.c
  unit/test_wii.c
)

if(BB_BUILD_RASTER)
  find_package(ZLIB REQUIRED)
  target_sources(binblock-tests PRIVATE
    support/png_fixture.c
    unit/test_graph.c
    unit/test_raster.c
  )
  target_include_directories(binblock-tests PRIVATE ${PROJECT_SOURCE_DIR}/tests/support)
  target_compile_definitions(binblock-tests PRIVATE BB_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_link_libraries(binblock-tests PRIVATE ZLIB::ZLIB)
endif()

target_compile_features(binblock-tests PRIVATE c_std_11)
set_target_properties(binblock-tests PROPERTIES C_EXTENSIONS OFF)
target_include_directories(binblock-tests PRIVATE ${PROJECT_SOURCE_DIR}/lib/core)
target_include_directories(binblock-tests PRIVATE ${PROJECT_SOURCE_DIR}/integrations/wii/include)
target_link_libraries(binblock-tests PRIVATE binblock::binblock)
bb_enable_project_warnings(binblock-tests)
bb_enable_sanitizers(binblock-tests)

add_test(NAME binblock-unit COMMAND binblock-tests)
