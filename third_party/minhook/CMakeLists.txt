# Vendored MinHook 1.3.3 (https://github.com/TsudaKageyu/minhook) — BSD-2 licence, see LICENSE.txt.
cmake_minimum_required(VERSION 3.21)
project(minhook_vendored C)
add_library(minhook STATIC
    src/buffer.c src/hook.c src/trampoline.c src/hde/hde32.c src/hde/hde64.c)
target_include_directories(minhook PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_library(dscoop::minhook ALIAS minhook)
