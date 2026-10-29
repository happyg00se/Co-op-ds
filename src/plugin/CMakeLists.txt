set(DSCOOP_PLUGIN_SOURCES
    src/plugin_main.cpp
    src/coop.cpp
    src/game_bridge.cpp)
if(DSCOOP_WITH_IMGUI)
    list(APPEND DSCOOP_PLUGIN_SOURCES src/dx12_hook.cpp src/overlay.cpp)
endif()
add_library(dscoop_plugin SHARED ${DSCOOP_PLUGIN_SOURCES})
target_include_directories(dscoop_plugin PRIVATE ${CMAKE_SOURCE_DIR}/src/loader/include ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(dscoop_plugin PRIVATE dscoop::net dscoop::common dscoop::json dscoop::warnings)
target_compile_definitions(dscoop_plugin PRIVATE DSCOOP_WITH_IMGUI=$<BOOL:${DSCOOP_WITH_IMGUI}>)
if(DSCOOP_WITH_IMGUI)
    target_link_libraries(dscoop_plugin PRIVATE dscoop::imgui d3d12 dxgi)
endif()
set_target_properties(dscoop_plugin PROPERTIES
    OUTPUT_NAME "DSCoop"
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/dscoop/plugins
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/dscoop/plugins)
install(TARGETS dscoop_plugin RUNTIME DESTINATION dscoop/plugins)
