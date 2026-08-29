if(NOT DEFINED GODOT_EXECUTABLE OR NOT EXISTS "${GODOT_EXECUTABLE}")
  message(FATAL_ERROR "GODOT_EXECUTABLE must name an existing Godot executable")
endif()
if(NOT DEFINED GODOT_EXTENSION OR NOT EXISTS "${GODOT_EXTENSION}")
  message(FATAL_ERROR "GODOT_EXTENSION must name the built libbinblock Godot extension")
endif()
if(NOT DEFINED PROJECT_ROOT)
  message(FATAL_ERROR "PROJECT_ROOT is required")
endif()

set(DEMO_ROOT "${PROJECT_ROOT}/integrations/godot/demo")
file(MAKE_DIRECTORY "${DEMO_ROOT}/bin")
get_filename_component(EXTENSION_NAME "${GODOT_EXTENSION}" NAME)
file(COPY_FILE "${GODOT_EXTENSION}" "${DEMO_ROOT}/bin/${EXTENSION_NAME}" ONLY_IF_DIFFERENT)
file(MAKE_DIRECTORY "${DEMO_ROOT}/.godot")
file(WRITE "${DEMO_ROOT}/.godot/extension_list.cfg" "res://binblock.gdextension\n")

execute_process(
  COMMAND "${GODOT_EXECUTABLE}" --headless --path "${DEMO_ROOT}" --verbose
  RESULT_VARIABLE GODOT_RESULT
  OUTPUT_VARIABLE GODOT_OUTPUT
  ERROR_VARIABLE GODOT_ERROR
  TIMEOUT 60
)
set(GODOT_LOG "${GODOT_OUTPUT}\n${GODOT_ERROR}")
if(NOT GODOT_RESULT EQUAL 0)
  message(FATAL_ERROR "Godot demo exited with ${GODOT_RESULT}:\n${GODOT_LOG}")
endif()
if(NOT GODOT_LOG MATCHES "BINBLOCK_GODOT_WORLD_OK outputs=2 tiles=6 cells=240 actor=40x40 parameters=3 collision=water")
  message(FATAL_ERROR "Godot demo did not report the expected integration result:\n${GODOT_LOG}")
endif()
message(STATUS "Godot rendered the Binblock tile atlas and actor, populated the TileMapLayer, and passed collision smoke")
