find_package(SDL3 CONFIG REQUIRED COMPONENTS SDL3)
find_program(BB_GLSLANG_VALIDATOR glslangValidator REQUIRED)
set(BB_SDL_GPU_TEST_DRIVER "vulkan" CACHE STRING "SDL GPU driver used by the generated-set smoke test")
set_property(CACHE BB_SDL_GPU_TEST_DRIVER PROPERTY STRINGS vulkan webgpu)
if(NOT BB_SDL_GPU_TEST_DRIVER STREQUAL "vulkan" AND NOT BB_SDL_GPU_TEST_DRIVER STREQUAL "webgpu")
  message(FATAL_ERROR "BB_SDL_GPU_TEST_DRIVER must be vulkan or webgpu")
endif()

set(BB_SDL_GPU_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${BB_SDL_GPU_GENERATED_DIR}")

set(BB_SDL_GPU_VERT_SPV "${BB_SDL_GPU_GENERATED_DIR}/binblock.vert.spv")
set(BB_SDL_GPU_FRAG_SPV "${BB_SDL_GPU_GENERATED_DIR}/binblock.frag.spv")
set(BB_SDL_GPU_VERT_SPV_HEADER "${BB_SDL_GPU_GENERATED_DIR}/binblock_vert_spirv.h")
set(BB_SDL_GPU_FRAG_SPV_HEADER "${BB_SDL_GPU_GENERATED_DIR}/binblock_frag_spirv.h")
set(BB_SDL_GPU_VERT_WGSL_HEADER "${BB_SDL_GPU_GENERATED_DIR}/binblock_vert_wgsl.h")
set(BB_SDL_GPU_FRAG_WGSL_HEADER "${BB_SDL_GPU_GENERATED_DIR}/binblock_frag_wgsl.h")

add_custom_command(
  OUTPUT "${BB_SDL_GPU_VERT_SPV}"
  COMMAND "${BB_GLSLANG_VALIDATOR}" -V --target-env vulkan1.0 -S vert
          "${CMAKE_CURRENT_SOURCE_DIR}/shaders/binblock.vert.glsl"
          -o "${BB_SDL_GPU_VERT_SPV}"
  DEPENDS shaders/binblock.vert.glsl
  VERBATIM
)
add_custom_command(
  OUTPUT "${BB_SDL_GPU_FRAG_SPV}"
  COMMAND "${BB_GLSLANG_VALIDATOR}" -V --target-env vulkan1.0 -S frag
          "${CMAKE_CURRENT_SOURCE_DIR}/shaders/binblock.frag.glsl"
          -o "${BB_SDL_GPU_FRAG_SPV}"
  DEPENDS shaders/binblock.frag.glsl
  VERBATIM
)
add_custom_command(
  OUTPUT "${BB_SDL_GPU_VERT_SPV_HEADER}"
  COMMAND "${CMAKE_COMMAND}"
          -DINPUT=${BB_SDL_GPU_VERT_SPV}
          -DOUTPUT=${BB_SDL_GPU_VERT_SPV_HEADER}
          -DSYMBOL=binblock_sdl_gpu_vert_spirv
          -P "${PROJECT_SOURCE_DIR}/cmake/embed_file.cmake"
  DEPENDS "${BB_SDL_GPU_VERT_SPV}" "${PROJECT_SOURCE_DIR}/cmake/embed_file.cmake"
  VERBATIM
)
add_custom_command(
  OUTPUT "${BB_SDL_GPU_FRAG_SPV_HEADER}"
  COMMAND "${CMAKE_COMMAND}"
          -DINPUT=${BB_SDL_GPU_FRAG_SPV}
          -DOUTPUT=${BB_SDL_GPU_FRAG_SPV_HEADER}
          -DSYMBOL=binblock_sdl_gpu_frag_spirv
          -P "${PROJECT_SOURCE_DIR}/cmake/embed_file.cmake"
  DEPENDS "${BB_SDL_GPU_FRAG_SPV}" "${PROJECT_SOURCE_DIR}/cmake/embed_file.cmake"
  VERBATIM
)
add_custom_command(
  OUTPUT "${BB_SDL_GPU_VERT_WGSL_HEADER}"
  COMMAND "${CMAKE_COMMAND}"
          -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/shaders/binblock.vert.wgsl
          -DOUTPUT=${BB_SDL_GPU_VERT_WGSL_HEADER}
          -DSYMBOL=binblock_sdl_gpu_vert_wgsl
          -DAPPEND_ZERO=ON
          -DAPPEND_ZERO_SUBTRACT=-1
          -P "${PROJECT_SOURCE_DIR}/cmake/embed_file.cmake"
  DEPENDS shaders/binblock.vert.wgsl "${PROJECT_SOURCE_DIR}/cmake/embed_file.cmake"
  VERBATIM
)
add_custom_command(
  OUTPUT "${BB_SDL_GPU_FRAG_WGSL_HEADER}"
  COMMAND "${CMAKE_COMMAND}"
          -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/shaders/binblock.frag.wgsl
          -DOUTPUT=${BB_SDL_GPU_FRAG_WGSL_HEADER}
          -DSYMBOL=binblock_sdl_gpu_frag_wgsl
          -DAPPEND_ZERO=ON
          -DAPPEND_ZERO_SUBTRACT=-1
          -P "${PROJECT_SOURCE_DIR}/cmake/embed_file.cmake"
  DEPENDS shaders/binblock.frag.wgsl "${PROJECT_SOURCE_DIR}/cmake/embed_file.cmake"
  VERBATIM
)

add_library(binblock-sdl-gpu STATIC
  binblock_sdl_gpu.c
  lower.c
  pipeline.c
  "${BB_SDL_GPU_VERT_SPV_HEADER}"
  "${BB_SDL_GPU_FRAG_SPV_HEADER}"
  "${BB_SDL_GPU_VERT_WGSL_HEADER}"
  "${BB_SDL_GPU_FRAG_WGSL_HEADER}"
)
target_include_directories(binblock-sdl-gpu
  PUBLIC include
  PRIVATE . "${BB_SDL_GPU_GENERATED_DIR}"
)
target_compile_features(binblock-sdl-gpu PUBLIC c_std_11)
set_target_properties(binblock-sdl-gpu PROPERTIES C_EXTENSIONS OFF)
target_link_libraries(binblock-sdl-gpu PUBLIC binblock::binblock SDL3::SDL3)
if(UNIX AND NOT APPLE)
  target_link_libraries(binblock-sdl-gpu PRIVATE m)
endif()
bb_enable_project_warnings(binblock-sdl-gpu)
bb_enable_sanitizers(binblock-sdl-gpu)
add_library(binblock::sdl-gpu ALIAS binblock-sdl-gpu)

if(BB_BUILD_SDL_GPU_BROWSER_DEMO)
  set(BB_SDL_GPU_DEMO_SOURCE_HEADER "${BB_SDL_GPU_GENERATED_DIR}/generated_set_binscript.h")
  add_custom_command(
    OUTPUT "${BB_SDL_GPU_DEMO_SOURCE_HEADER}"
    COMMAND "${CMAKE_COMMAND}"
            -DINPUT=${PROJECT_SOURCE_DIR}/examples/generated-set.binscript
            -DOUTPUT=${BB_SDL_GPU_DEMO_SOURCE_HEADER}
            -DSYMBOL=binblock_sdl_gpu_demo_source
            -P "${PROJECT_SOURCE_DIR}/cmake/embed_file.cmake"
    DEPENDS
      "${PROJECT_SOURCE_DIR}/examples/generated-set.binscript"
      "${PROJECT_SOURCE_DIR}/cmake/embed_file.cmake"
    VERBATIM
  )
  add_executable(binblock-sdl-gpu-browser-demo
    examples/browser.c
    "${BB_SDL_GPU_DEMO_SOURCE_HEADER}"
  )
  target_include_directories(binblock-sdl-gpu-browser-demo PRIVATE "${BB_SDL_GPU_GENERATED_DIR}")
  target_compile_features(binblock-sdl-gpu-browser-demo PRIVATE c_std_11)
  target_link_libraries(binblock-sdl-gpu-browser-demo PRIVATE binblock::sdl-gpu)
  target_link_options(binblock-sdl-gpu-browser-demo PRIVATE
    "-sALLOW_MEMORY_GROWTH=1"
    "-sASSERTIONS=1"
    "-sASYNCIFY=1"
    "-sASYNCIFY_STACK_SIZE=65536"
    "-sENVIRONMENT=web"
  )
  set_target_properties(binblock-sdl-gpu-browser-demo PROPERTIES
    C_EXTENSIONS OFF
    OUTPUT_NAME binblock-sdl-gpu-demo
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}"
    SUFFIX ".html"
  )
  bb_enable_project_warnings(binblock-sdl-gpu-browser-demo)
endif()

if(BB_BUILD_TESTS)
  add_executable(binblock-sdl-gpu-tests tests/test_lower.c)
  target_include_directories(binblock-sdl-gpu-tests PRIVATE .)
  target_compile_features(binblock-sdl-gpu-tests PRIVATE c_std_11)
  target_link_libraries(binblock-sdl-gpu-tests PRIVATE binblock::sdl-gpu)
  set_target_properties(binblock-sdl-gpu-tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
  bb_enable_project_warnings(binblock-sdl-gpu-tests)
  bb_enable_sanitizers(binblock-sdl-gpu-tests)
  add_test(NAME binblock-sdl-gpu-lowering COMMAND binblock-sdl-gpu-tests)

  if(BB_BUILD_BINSCRIPT AND BB_BUILD_RASTER AND NOT EMSCRIPTEN)
    add_executable(binblock-sdl-gpu-smoke tests/smoke_generated_set.c)
    target_compile_features(binblock-sdl-gpu-smoke PRIVATE c_std_11)
    target_link_libraries(binblock-sdl-gpu-smoke PRIVATE binblock::sdl-gpu)
    set_target_properties(binblock-sdl-gpu-smoke PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
    if(BB_SDL_GPU_WEBGPU_PR_READBACK_WORKAROUND)
      target_compile_definitions(
        binblock-sdl-gpu-smoke
        PRIVATE BB_SDL_GPU_WEBGPU_PR_READBACK_WORKAROUND=1
      )
    endif()
    bb_enable_project_warnings(binblock-sdl-gpu-smoke)
    bb_enable_sanitizers(binblock-sdl-gpu-smoke)

    if(UNIX AND NOT APPLE)
      find_program(BB_XVFB_RUN xvfb-run)
      set(BB_SDL_GPU_TEST_ENV "SDL_GPU_DRIVER=${BB_SDL_GPU_TEST_DRIVER}")
      set(BB_SDL_GPU_TEST_READY ${BB_XVFB_RUN})
      if(BB_SDL_GPU_TEST_DRIVER STREQUAL "vulkan")
        find_file(BB_LAVAPIPE_ICD lvp_icd.json PATHS /usr/share/vulkan/icd.d NO_DEFAULT_PATH)
        if(BB_LAVAPIPE_ICD)
          list(APPEND BB_SDL_GPU_TEST_ENV "VK_ICD_FILENAMES=${BB_LAVAPIPE_ICD}")
        else()
          set(BB_SDL_GPU_TEST_READY OFF)
        endif()
      endif()
      if(BB_SDL_GPU_TEST_READY)
        add_test(
          NAME binblock-sdl-gpu-generated-set
          COMMAND ${BB_XVFB_RUN} -a ${CMAKE_COMMAND} -E env
                  ${BB_SDL_GPU_TEST_ENV}
                  $<TARGET_FILE:binblock-sdl-gpu-smoke>
                  ${PROJECT_SOURCE_DIR}/examples/generated-set.binscript
        )
      endif()
    endif()
  endif()
endif()
