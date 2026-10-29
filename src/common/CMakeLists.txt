add_library(dscommon STATIC
    src/log.cpp
    src/paths.cpp
    src/config.cpp)
add_library(dscoop::common ALIAS dscommon)
target_include_directories(dscommon PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(dscommon PUBLIC dscoop::spdlog dscoop::json PRIVATE dscoop::warnings)
# Compile every log level in; the runtime level (log_level in the configs) decides what is written.
target_compile_definitions(dscommon PUBLIC SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)
target_compile_features(dscommon PUBLIC cxx_std_20)
if(WIN32)
    target_link_libraries(dscommon PUBLIC shlwapi)
endif()
