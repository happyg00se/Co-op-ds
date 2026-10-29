cmake_minimum_required(VERSION 3.21)

project(DSCoop
    VERSION 0.1.0
    DESCRIPTION "DSCoop — story co-op mod for Death Stranding Director's Cut (Decima)"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(DSCOOP_BUILD_LOADER   "Build DSLoader (version.dll proxy). Windows only."  ${WIN32})
option(DSCOOP_BUILD_PLUGIN   "Build DSCoop.dll plugin. Windows only."           ${WIN32})
option(DSCOOP_BUILD_TOOLS    "Build dscoop_testclient and tools."              ON)
option(DSCOOP_BUILD_TESTS    "Build unit tests."                               ON)
option(DSCOOP_WITH_IMGUI     "Build ImGui overlay into the plugin."            ON)
option(DSCOOP_VENDORED_DEPS  "Prefer third_party/ sources over find_package()." OFF)
option(DSCOOP_WARNINGS_AS_ERRORS "Treat warnings as errors."                   OFF)

if((DSCOOP_BUILD_LOADER OR DSCOOP_BUILD_PLUGIN) AND NOT WIN32)
    message(FATAL_ERROR "DSLoader/DSCoop.dll are Windows-only. Use -DDSCOOP_BUILD_LOADER=OFF -DDSCOOP_BUILD_PLUGIN=OFF on this platform.")
endif()

include(CompilerWarnings)
include(Dependencies)

# Generated version header
configure_file(cmake/version.hpp.in ${CMAKE_BINARY_DIR}/generated/dscommon/version.hpp @ONLY)

# ---------------------------------------------------------------------------
# Targets
# ---------------------------------------------------------------------------
add_subdirectory(src/proto)     # header-only: packet protocol v0
add_subdirectory(src/common)    # logging, paths, config
add_subdirectory(src/net)       # ENet transport, relay client, session

if(DSCOOP_BUILD_LOADER)
    add_subdirectory(src/loader)
endif()
if(DSCOOP_BUILD_PLUGIN)
    add_subdirectory(src/plugin)
endif()
if(DSCOOP_BUILD_TOOLS)
    add_subdirectory(tools/testclient)
endif()
if(DSCOOP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# Install layout:  <game dir>/version.dll + <game dir>/dscoop/...
# ---------------------------------------------------------------------------
install(DIRECTORY data/ DESTINATION dscoop/data)
install(FILES README.md CHANGELOG.md DESTINATION dscoop/docs)
install(DIRECTORY docs/ DESTINATION dscoop/docs)
