# CompileShaders.cmake - converts a .spv binary into a C header with a uint32_t array
# Usage: cmake -DINPUT=shader.spv -DOUTPUT=shader.h -DARRAY_NAME=shaderSpv -P CompileShaders.cmake

file(READ "${INPUT}" SPV_HEX HEX)
string(LENGTH "${SPV_HEX}" SPV_HEX_LEN)

set(ARRAY_CONTENT "")
math(EXPR LAST_INDEX "${SPV_HEX_LEN} - 8")
set(FIRST TRUE)

foreach(IDX RANGE 0 ${LAST_INDEX} 8)
    # Read 4 bytes (8 hex chars) as a little-endian uint32_t
    string(SUBSTRING "${SPV_HEX}" ${IDX} 2 B0)
    math(EXPR NEXT "${IDX} + 2")
    string(SUBSTRING "${SPV_HEX}" ${NEXT} 2 B1)
    math(EXPR NEXT "${IDX} + 4")
    string(SUBSTRING "${SPV_HEX}" ${NEXT} 2 B2)
    math(EXPR NEXT "${IDX} + 6")
    string(SUBSTRING "${SPV_HEX}" ${NEXT} 2 B3)

    if(NOT FIRST)
        string(APPEND ARRAY_CONTENT ",")
    endif()
    set(FIRST FALSE)

    string(APPEND ARRAY_CONTENT "\n    0x${B3}${B2}${B1}${B0}u")
endforeach()

file(WRITE "${OUTPUT}"
    "#pragma once\n"
    "#include <cstdint>\n"
    "static const uint32_t ${ARRAY_NAME}[] = {${ARRAY_CONTENT}\n};\n"
)
