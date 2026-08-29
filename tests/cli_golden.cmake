if(NOT DEFINED BINBLOCK_CLI OR NOT DEFINED SOURCE OR NOT DEFINED EXPECTED)
  message(FATAL_ERROR "BINBLOCK_CLI, SOURCE, and EXPECTED are required")
endif()

execute_process(
  COMMAND "${BINBLOCK_CLI}" graph "${SOURCE}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE actual
  ERROR_VARIABLE diagnostics
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "graph dump failed (${result}):\n${diagnostics}")
endif()

file(READ "${EXPECTED}" expected)
if(NOT actual STREQUAL expected)
  message(FATAL_ERROR "golden graph dump drifted\n--- expected ---\n${expected}\n--- actual ---\n${actual}")
endif()
