cmake_minimum_required(VERSION 3.20)

project(
  binblock
  VERSION 0.1.0
  DESCRIPTION "Portable semantic graphics runtime for BinScript"
  LANGUAGES C
)

include(CTest)
include(GNUInstallDirs)

option(BB_BUILD_TESTS "Build libbinblock native tests" ON)
option(BB_BUILD_SHARED "Build libbinblock as a shared library" OFF)
option(BB_BUILD_BINSCRIPT "Build the portable BinScript frontend and semantic compiler" ON)
option(BB_BUILD_RASTER "Build the canonical software rasterizer" ON)
option(BB_BUILD_CLI "Build the native BinBlock command-line host" ON)
option(BB_BUILD_WASM "Build the Emscripten host ABI module" OFF)
option(BB_BUILD_GODOT "Build the Godot 4 GDExtension consumer" OFF)
option(BB_BUILD_WII "Build the portable Wii/GX texture adapter" OFF)
option(BB_BUILD_SDL_GPU "Build the optional SDL3 GPU graph renderer" OFF)
option(BB_BUILD_SDL_GPU_BROWSER_DEMO "Build the SDL3/WebGPU generated-set browser demo" OFF)
option(
  BB_SDL_GPU_WEBGPU_PR_READBACK_WORKAROUND
  "Work around SDL WebGPU PR #16020 using layer as texture-copy depth in GPU tests"
  OFF
)
option(BB_BUILD_FUZZERS "Build libFuzzer entry points with Clang" OFF)
option(BB_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

if(BB_BUILD_SDL_GPU_BROWSER_DEMO AND NOT BB_BUILD_SDL_GPU)
  message(FATAL_ERROR "BB_BUILD_SDL_GPU_BROWSER_DEMO requires BB_BUILD_SDL_GPU")
endif()
if(BB_BUILD_SDL_GPU_BROWSER_DEMO AND NOT EMSCRIPTEN)
  message(FATAL_ERROR "BB_BUILD_SDL_GPU_BROWSER_DEMO requires the Emscripten CMake toolchain")
endif()

function(bb_enable_project_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /WX)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
endfunction()

function(bb_enable_sanitizers target)
  if(NOT BB_ENABLE_SANITIZERS)
    return()
  endif()
  if(MSVC OR EMSCRIPTEN)
    message(FATAL_ERROR "BB_ENABLE_SANITIZERS is only supported by native Clang and GCC builds")
  endif()
  target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(${target} PRIVATE -fsanitize=address,undefined)
endfunction()

if(BB_BUILD_SHARED)
  set(BB_LIBRARY_KIND SHARED)
else()
  set(BB_LIBRARY_KIND STATIC)
endif()

add_library(binblock ${BB_LIBRARY_KIND}
  lib/core/checked_math.c
  lib/core/collection.c
  lib/core/context.c
  lib/core/diagnostic.c
  lib/core/graph.c
  lib/core/utf8.c
)

if(BB_BUILD_BINSCRIPT)
  target_sources(binblock PRIVATE
    lib/frontend/syntax.c
    lib/semantic/program_assets.c
    lib/semantic/program_collections.c
    lib/semantic/program_compile.c
    lib/semantic/program_core.c
    lib/semantic/program_evaluate.c
    lib/semantic/program_gradients.c
    lib/semantic/program_members.c
    lib/semantic/program_runtime.c
  )
  target_compile_definitions(binblock PUBLIC BB_HAS_BINSCRIPT=1)
endif()

if(BB_BUILD_RASTER)
  target_sources(binblock PRIVATE
    lib/raster/gradient.c
    lib/raster/graph_render.c
    lib/raster/resize.c
    lib/raster/composite.c
    lib/raster/surface.c
    lib/raster/transform.c
    lib/reference/reference_set.c
  )
  target_include_directories(binblock PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/lib/raster)
  target_compile_definitions(binblock PUBLIC BB_HAS_RASTER=1)
endif()

if(UNIX AND NOT APPLE AND BB_BUILD_RASTER)
  target_link_libraries(binblock PRIVATE m)
endif()
add_library(binblock::binblock ALIAS binblock)

target_compile_features(binblock PUBLIC c_std_11)
set_target_properties(binblock PROPERTIES
  C_EXTENSIONS OFF
  C_VISIBILITY_PRESET hidden
  OUTPUT_NAME binblock
  POSITION_INDEPENDENT_CODE ON
)
target_include_directories(binblock
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/core
)
if(BB_BUILD_SHARED)
  target_compile_definitions(binblock PRIVATE BB_IMPLEMENTATION=1)
else()
  target_compile_definitions(binblock PUBLIC BB_STATIC=1)
endif()
bb_enable_project_warnings(binblock)
bb_enable_sanitizers(binblock)

if(BB_BUILD_FUZZERS)
  if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang" OR EMSCRIPTEN)
    message(FATAL_ERROR "BB_BUILD_FUZZERS requires a native Clang compiler")
  endif()
  if(NOT BB_BUILD_BINSCRIPT OR NOT BB_BUILD_RASTER)
    message(FATAL_ERROR "BB_BUILD_FUZZERS requires BB_BUILD_BINSCRIPT and BB_BUILD_RASTER")
  endif()
  target_compile_options(binblock PRIVATE -fsanitize=fuzzer-no-link,address,undefined -fno-omit-frame-pointer)
  foreach(fuzzer IN ITEMS syntax semantic graph collection raster)
    add_executable(binblock-fuzz-${fuzzer} tests/fuzz/fuzz_${fuzzer}.c)
    target_compile_features(binblock-fuzz-${fuzzer} PRIVATE c_std_11)
    target_link_libraries(binblock-fuzz-${fuzzer} PRIVATE binblock::binblock)
    target_compile_options(binblock-fuzz-${fuzzer} PRIVATE -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer)
    target_link_options(binblock-fuzz-${fuzzer} PRIVATE -fsanitize=fuzzer,address,undefined)
    bb_enable_project_warnings(binblock-fuzz-${fuzzer})
  endforeach()
endif()

if(BB_BUILD_CLI)
  if(NOT BB_BUILD_BINSCRIPT)
    message(FATAL_ERROR "BB_BUILD_CLI requires BB_BUILD_BINSCRIPT")
  endif()
  if(NOT BB_BUILD_RASTER)
    message(FATAL_ERROR "BB_BUILD_CLI requires BB_BUILD_RASTER")
  endif()
  find_package(ZLIB REQUIRED)
  add_executable(binblock-cli cli/inventory.c cli/main.c cli/png.c cli/sha256.c)
  set_target_properties(binblock-cli PROPERTIES OUTPUT_NAME binblock C_EXTENSIONS OFF)
  target_compile_features(binblock-cli PRIVATE c_std_11)
  target_link_libraries(binblock-cli PRIVATE binblock::binblock ZLIB::ZLIB)
  bb_enable_project_warnings(binblock-cli)
  bb_enable_sanitizers(binblock-cli)
  install(TARGETS binblock-cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

if(BB_BUILD_WASM)
  if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "BB_BUILD_WASM requires the Emscripten CMake toolchain")
  endif()
  if(NOT BB_BUILD_RASTER)
    message(FATAL_ERROR "BB_BUILD_WASM requires BB_BUILD_RASTER")
  endif()
  if(NOT BB_BUILD_BINSCRIPT)
    message(FATAL_ERROR "BB_BUILD_WASM requires BB_BUILD_BINSCRIPT")
  endif()
  add_executable(binblock-wasm bindings/wasm/wasm_api.c)
  set_target_properties(binblock-wasm PROPERTIES OUTPUT_NAME binblock SUFFIX ".mjs" C_EXTENSIONS OFF)
  target_compile_features(binblock-wasm PRIVATE c_std_11)
  target_link_libraries(binblock-wasm PRIVATE binblock::binblock)
  target_link_options(binblock-wasm PRIVATE
    --no-entry
    "-sMODULARIZE=1"
    "-sEXPORT_ES6=1"
    "-sEXPORT_NAME=createBinBlockModule"
    "-sENVIRONMENT=web,worker,node"
    "-sALLOW_MEMORY_GROWTH=1"
    "-sFILESYSTEM=0"
    "-sMALLOC=emmalloc"
    "-sEXPORTED_RUNTIME_METHODS=['HEAPU8','HEAPU32']"
    "-sEXPORTED_FUNCTIONS=['_malloc','_free','_bb_wasm_session_create','_bb_wasm_session_destroy','_bb_wasm_session_generation','_bb_wasm_session_clear_resources','_bb_wasm_session_add_module','_bb_wasm_session_add_asset_rgba','_bb_wasm_session_add_asset_metadata','_bb_wasm_session_hydrate_asset','_bb_wasm_session_compile','_bb_wasm_diagnostic_count','_bb_wasm_diagnostic_get','_bb_wasm_diagnostic_copy_message','_bb_wasm_trace_at','_bb_wasm_parameter_count','_bb_wasm_parameter_get','_bb_wasm_parameter_copy_name','_bb_wasm_parameter_set_bool','_bb_wasm_parameter_set_integer','_bb_wasm_parameter_set_number','_bb_wasm_parameter_set_color','_bb_wasm_output_count','_bb_wasm_output_get','_bb_wasm_artifact_get','_bb_wasm_artifact_copy_key','_bb_wasm_artifact_copy_path','_bb_wasm_graph_node_get','_bb_wasm_graph_asset_copy_content_id','_bb_wasm_graph_gradient_stop_get','_bb_wasm_render_output_rgba']"
  )
  bb_enable_project_warnings(binblock-wasm)
endif()

if(BB_BUILD_WII)
  if(NOT BB_BUILD_RASTER)
    message(FATAL_ERROR "BB_BUILD_WII requires BB_BUILD_RASTER")
  endif()
  if(NOT BB_BUILD_BINSCRIPT)
    message(FATAL_ERROR "BB_BUILD_WII requires BB_BUILD_BINSCRIPT")
  endif()
  add_library(binblock-wii STATIC integrations/wii/binblock_wii.c)
  target_include_directories(binblock-wii PUBLIC integrations/wii/include)
  target_compile_features(binblock-wii PUBLIC c_std_11)
  set_target_properties(binblock-wii PROPERTIES C_EXTENSIONS OFF)
  target_link_libraries(binblock-wii PUBLIC binblock::binblock)
  bb_enable_project_warnings(binblock-wii)
  add_library(binblock::wii ALIAS binblock-wii)
endif()

if(BB_BUILD_SDL_GPU)
  add_subdirectory(integrations/sdl_gpu)
endif()

if(BB_BUILD_GODOT)
  if(NOT BB_BUILD_RASTER)
    message(FATAL_ERROR "BB_BUILD_GODOT requires BB_BUILD_RASTER")
  endif()
  if(NOT BB_BUILD_BINSCRIPT)
    message(FATAL_ERROR "BB_BUILD_GODOT requires BB_BUILD_BINSCRIPT")
  endif()
  set(GODOT_CPP_ROOT "" CACHE PATH "Path to a compatible godot-cpp checkout")
  if(NOT EXISTS "${GODOT_CPP_ROOT}/CMakeLists.txt")
    message(FATAL_ERROR "BB_BUILD_GODOT requires -DGODOT_CPP_ROOT=/path/to/godot-cpp")
  endif()
  enable_language(CXX)
  set(GODOTCPP_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
  add_subdirectory("${GODOT_CPP_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/godot-cpp" EXCLUDE_FROM_ALL)
  add_library(binblock-godot SHARED
    integrations/godot/src/bin_program.cpp
    integrations/godot/src/register_types.cpp
  )
  target_include_directories(binblock-godot PRIVATE integrations/godot/src)
  target_include_directories(binblock-godot SYSTEM PRIVATE
    "${GODOT_CPP_ROOT}/include"
    "${GODOT_CPP_ROOT}/gdextension"
    "${CMAKE_CURRENT_BINARY_DIR}/godot-cpp/gen/include"
  )
  target_compile_features(binblock-godot PRIVATE cxx_std_17)
  target_link_libraries(binblock-godot PRIVATE binblock::binblock godot::cpp)
  set_target_properties(binblock-godot PROPERTIES
    CXX_EXTENSIONS OFF
    OUTPUT_NAME binblock_godot
    PREFIX "lib"
  )
  bb_enable_project_warnings(binblock-godot)
endif()

if(BB_BUILD_TESTS)
  if(NOT BB_BUILD_BINSCRIPT OR NOT BB_BUILD_RASTER)
    message(FATAL_ERROR "BB_BUILD_TESTS requires BB_BUILD_BINSCRIPT and BB_BUILD_RASTER")
  endif()
  add_subdirectory(tests)
  if(BB_BUILD_CLI)
    add_test(
      NAME binblock-cli-list-304
      COMMAND binblock-cli list ${CMAKE_CURRENT_SOURCE_DIR}/examples/preview-304.binscript
    )
    set_tests_properties(binblock-cli-list-304 PROPERTIES PASS_REGULAR_EXPRESSION "cardinality=304")
    add_test(
      NAME binblock-cli-list-generated-product-256
      COMMAND binblock-cli list ${CMAKE_CURRENT_SOURCE_DIR}/examples/generated-set.binscript --summary
    )
    set_tests_properties(binblock-cli-list-generated-product-256 PROPERTIES PASS_REGULAR_EXPRESSION "cardinality=256")
    add_test(
      NAME binblock-cli-list-reference-4312
      COMMAND binblock-cli list ${CMAKE_CURRENT_SOURCE_DIR}/reference-set/reference-set.binscript --summary
    )
    set_tests_properties(binblock-cli-list-reference-4312 PROPERTIES PASS_REGULAR_EXPRESSION "cardinality=4312")
    add_test(
      NAME binblock-cli-golden-language
      COMMAND ${CMAKE_COMMAND}
        -DBINBLOCK_CLI=$<TARGET_FILE:binblock-cli>
        -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/language/golden-starter.binscript
        -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/language/golden-starter.graph.txt
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/cli_golden.cmake
    )
    add_test(
      NAME binblock-cli-compare-reference-flat-colors
      COMMAND binblock-cli compare ${CMAKE_CURRENT_SOURCE_DIR}/reference-set/reference-set.binscript
              --start 0 --count 16 --fixtures ${CMAKE_CURRENT_SOURCE_DIR}/reference-set
    )
    set_tests_properties(
      binblock-cli-compare-reference-flat-colors
      PROPERTIES PASS_REGULAR_EXPRESSION "\"matched\":16"
    )
    add_test(
      NAME binblock-cli-compare-reference-alpha-maps
      COMMAND binblock-cli compare ${CMAKE_CURRENT_SOURCE_DIR}/reference-set/reference-set.binscript
              --start 16 --count 18 --fixtures ${CMAKE_CURRENT_SOURCE_DIR}/reference-set --alpha-only
    )
    set_tests_properties(
      binblock-cli-compare-reference-alpha-maps
      PROPERTIES PASS_REGULAR_EXPRESSION "\"matched\":18"
    )
    add_test(
      NAME binblock-cli-compare-reference-blue-analytic
      COMMAND binblock-cli compare ${CMAKE_CURRENT_SOURCE_DIR}/reference-set/reference-set.binscript
              --start 276 --count 16 --fixtures ${CMAKE_CURRENT_SOURCE_DIR}/reference-set
              --contracts ${CMAKE_CURRENT_SOURCE_DIR}/reference-set/reference-conformance.tsv
    )
    set_tests_properties(
      binblock-cli-compare-reference-blue-analytic
      PROPERTIES PASS_REGULAR_EXPRESSION "\"matched\":16"
    )
    add_test(
      NAME binblock-cli-compare-reference-downscaled
      COMMAND binblock-cli compare ${CMAKE_CURRENT_SOURCE_DIR}/reference-set/reference-set.binscript
              --start 1237 --count 88 --fixtures ${CMAKE_CURRENT_SOURCE_DIR}/reference-set
              --contracts ${CMAKE_CURRENT_SOURCE_DIR}/reference-set/reference-conformance.tsv
    )
    set_tests_properties(
      binblock-cli-compare-reference-downscaled
      PROPERTIES PASS_REGULAR_EXPRESSION "\"matched\":88"
    )
    add_test(
      NAME binblock-cli-render-slice
      COMMAND binblock-cli render ${CMAKE_CURRENT_SOURCE_DIR}/examples/preview-304.binscript
              --start 100 --count 16 --dir ${CMAKE_CURRENT_BINARY_DIR}/cli-preview
    )
    set_tests_properties(binblock-cli-render-slice PROPERTIES PASS_REGULAR_EXPRESSION "rendered[	]16")
    add_test(
      NAME binblock-cli-compare-slice
      COMMAND binblock-cli compare ${CMAKE_CURRENT_SOURCE_DIR}/examples/preview-304.binscript
              --start 100 --count 16 --fixtures ${CMAKE_CURRENT_BINARY_DIR}/cli-preview
    )
    set_tests_properties(
      binblock-cli-compare-slice
      PROPERTIES DEPENDS binblock-cli-render-slice PASS_REGULAR_EXPRESSION "\"matched\":16"
    )
  endif()
endif()

install(TARGETS binblock EXPORT binblock-targets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/binblock DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(
  EXPORT binblock-targets
  FILE binblock-targets.cmake
  NAMESPACE binblock::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/binblock
)
