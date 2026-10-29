add_library(dsnet STATIC
    src/enet_transport.cpp
    src/loopback_transport.cpp
    src/relay_client.cpp
    src/session.cpp)
add_library(dscoop::net ALIAS dsnet)
target_include_directories(dsnet PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(dsnet PUBLIC dscoop::proto dscoop::enet PRIVATE dscoop::warnings)
target_compile_features(dsnet PUBLIC cxx_std_20)
if(WIN32)
    target_link_libraries(dsnet PUBLIC ws2_32 winmm)
endif()
