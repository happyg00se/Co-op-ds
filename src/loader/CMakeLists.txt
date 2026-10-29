# DSLoader — built as version.dll (proxy) and dropped next to the game exe.
add_library(dsloader SHARED
    src/dllmain.cpp
    src/proxy_version.cpp
    src/loader.cpp
    src/game_module.cpp
    src/hooks.cpp
    src/offsets.cpp
    src/sigscan.cpp
    src/rtti_scan.cpp
    src/rtti_dump.cpp
    src/plugins.cpp
    src/save_backup.cpp
    src/crash_handler.cpp
    src/version.def)
target_include_directories(dsloader PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(dsloader PRIVATE dscoop::common dscoop::json dscoop::minhook dscoop::warnings dbghelp shlwapi ole32 shell32)
set_target_properties(dsloader PROPERTIES
    OUTPUT_NAME "version"
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
if(MINGW)
    target_link_options(dsloader PRIVATE -Wl,--kill-at -Wl,--enable-stdcall-fixup)
endif()
install(TARGETS dsloader RUNTIME DESTINATION .)
