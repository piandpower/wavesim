string (TIMESTAMP WAVESIM_BUILD_TIME UTC)

# Update build number
file (STRINGS "${WAVESIM_SOURCE_DIR}/cmake/build_number.txt" WAVESIM_BUILD_NUMBER)
math (EXPR WAVESIM_BUILD_NUMBER "${WAVESIM_BUILD_NUMBER}+1")
file (WRITE "${WAVESIM_SOURCE_DIR}/cmake/build_number.txt" "# If you ever get merge conflicts with this file, figure out how many times you've built the file since the last successful pull and add that to the conflicting branch.\n")
file (APPEND "${WAVESIM_SOURCE_DIR}/cmake/build_number.txt" ${WAVESIM_BUILD_NUMBER})

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/include/wavesim/build_info_dynamic.h"
    "/* build_info_dynamic.h generated by CMake. Changes will be lost! */\n"
    "#define WAVESIM_BUILD_TIME \"${WAVESIM_BUILD_TIME}\"\n"
    "#define WAVESIM_BUILD_NUMBER ${WAVESIM_BUILD_NUMBER}\n")
