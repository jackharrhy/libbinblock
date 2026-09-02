if(NOT DEFINED INPUT OR NOT DEFINED OUTPUT OR NOT DEFINED SYMBOL)
  message(FATAL_ERROR "embed_file.cmake requires INPUT, OUTPUT, and SYMBOL")
endif()

file(READ "${INPUT}" contents HEX)
string(REGEX REPLACE "([0-9a-fA-F][0-9a-fA-F])" "0x\\1," contents "${contents}")
if(APPEND_ZERO)
  string(APPEND contents "0x00,")
endif()
file(WRITE "${OUTPUT}"
  "static const unsigned char ${SYMBOL}[] = {${contents}};\n"
  "static const size_t ${SYMBOL}_size = sizeof(${SYMBOL})${APPEND_ZERO_SUBTRACT};\n"
)
