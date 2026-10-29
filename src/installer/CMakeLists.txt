# Installer core: platform-neutral, unit-tested on Linux.
add_library(dsinstcore STATIC core/vdf.cpp core/steam.cpp core/pe.cpp core/install.cpp)
target_include_directories(dsinstcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/core)
target_link_libraries(dsinstcore PUBLIC dscoop::json PRIVATE dscoop::warnings)
target_compile_features(dsinstcore PUBLIC cxx_std_20)

if(WIN32 AND DSCOOP_BUILD_INSTALLER)
    # Payload: the freshly built loader/plugin (+ offsets.json) embedded as RCDATA.
    # MinGW builds carry DWARF debug info, so the embedded copies are stripped.
    set(PAYLOAD_DIR ${CMAKE_CURRENT_BINARY_DIR}/payload)
    if(MINGW AND CMAKE_STRIP)
        set(COPY_LOADER ${CMAKE_STRIP} --strip-debug -o ${PAYLOAD_DIR}/version.dll $<TARGET_FILE:dsloader>)
        set(COPY_PLUGIN ${CMAKE_STRIP} --strip-debug -o ${PAYLOAD_DIR}/DSCoop.dll $<TARGET_FILE:dscoop_plugin>)
    else()
        set(COPY_LOADER ${CMAKE_COMMAND} -E copy $<TARGET_FILE:dsloader> ${PAYLOAD_DIR}/version.dll)
        set(COPY_PLUGIN ${CMAKE_COMMAND} -E copy $<TARGET_FILE:dscoop_plugin> ${PAYLOAD_DIR}/DSCoop.dll)
    endif()
    add_custom_command(OUTPUT ${PAYLOAD_DIR}/version.dll
        COMMAND ${CMAKE_COMMAND} -E make_directory ${PAYLOAD_DIR}
        COMMAND ${COPY_LOADER}
        DEPENDS dsloader VERBATIM)
    add_custom_command(OUTPUT ${PAYLOAD_DIR}/DSCoop.dll
        COMMAND ${CMAKE_COMMAND} -E make_directory ${PAYLOAD_DIR}
        COMMAND ${COPY_PLUGIN}
        DEPENDS dscoop_plugin VERBATIM)
    add_custom_command(OUTPUT ${PAYLOAD_DIR}/offsets.json
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/data/offsets.json ${PAYLOAD_DIR}/offsets.json
        DEPENDS ${CMAKE_SOURCE_DIR}/data/offsets.json VERBATIM)

    configure_file(win/installer.rc.in ${CMAKE_CURRENT_BINARY_DIR}/installer.rc @ONLY)
    configure_file(win/installer.manifest ${CMAKE_CURRENT_BINARY_DIR}/installer.manifest COPYONLY)
    set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/installer.rc PROPERTIES
        OBJECT_DEPENDS "${PAYLOAD_DIR}/version.dll;${PAYLOAD_DIR}/DSCoop.dll;${PAYLOAD_DIR}/offsets.json;${CMAKE_CURRENT_BINARY_DIR}/installer.manifest")

    add_executable(dscoop_setup WIN32 win/main_win.cpp ${CMAKE_CURRENT_BINARY_DIR}/installer.rc)
    target_include_directories(dscoop_setup PRIVATE ${CMAKE_BINARY_DIR}/generated ${CMAKE_CURRENT_SOURCE_DIR}/win)
    target_link_libraries(dscoop_setup PRIVATE dsinstcore dscoop::warnings comctl32 ole32 shell32 uuid advapi32 user32 gdi32)
    target_compile_definitions(dscoop_setup PRIVATE UNICODE _UNICODE)
    if(MINGW)
        target_link_options(dscoop_setup PRIVATE -municode)
    elseif(MSVC)
        # Our own manifest (common controls v6, asInvoker, DPI) comes from installer.rc.
        target_link_options(dscoop_setup PRIVATE /MANIFEST:NO)
    endif()
    set_target_properties(dscoop_setup PROPERTIES OUTPUT_NAME "DSCoopSetup" RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    if(MINGW AND CMAKE_STRIP)
        add_custom_command(TARGET dscoop_setup POST_BUILD COMMAND ${CMAKE_STRIP} --strip-debug $<TARGET_FILE:dscoop_setup> VERBATIM)
    endif()
    install(TARGETS dscoop_setup RUNTIME DESTINATION .)
endif()
