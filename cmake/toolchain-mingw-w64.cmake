# Cross-compile Windows x64 binaries from Linux with mingw-w64 (apt install mingw-w64).
# Usage: cmake --preset mingw-cross  (see CMakePresets.json)
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)
set(TOOLCHAIN_PREFIX x86_64-w64-mingw32)

find_program(CMAKE_C_COMPILER   NAMES ${TOOLCHAIN_PREFIX}-gcc-posix ${TOOLCHAIN_PREFIX}-gcc)
find_program(CMAKE_CXX_COMPILER NAMES ${TOOLCHAIN_PREFIX}-g++-posix ${TOOLCHAIN_PREFIX}-g++)
find_program(CMAKE_RC_COMPILER  NAMES ${TOOLCHAIN_PREFIX}-windres)
set(CMAKE_FIND_ROOT_PATH /usr/${TOOLCHAIN_PREFIX})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Static runtime so version.dll / DSCoop.dll / testclient.exe do not need libgcc/libstdc++ DLLs in the game folder.
set(CMAKE_EXE_LINKER_FLAGS_INIT    "-static -static-libgcc -static-libstdc++")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-static -static-libgcc -static-libstdc++")
add_compile_definitions(_WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE)
