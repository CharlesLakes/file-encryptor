# Toolchain file to cross-compile the Windows build from Linux with MinGW-w64.
#
#   cmake -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64.cmake \
#         -DCMAKE_LIBRARY_PATH=/path/to/libsodium-win64/lib
#   cmake --build build-win

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(MINGW_PREFIX x86_64-w64-mingw32 CACHE STRING "Prefix of the MinGW-w64 tools")
set(CMAKE_C_COMPILER ${MINGW_PREFIX}-gcc)
set(CMAKE_RC_COMPILER ${MINGW_PREFIX}-windres)

set(CMAKE_FIND_ROOT_PATH /usr/${MINGW_PREFIX})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
