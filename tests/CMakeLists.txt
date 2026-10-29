add_executable(test_proto test_proto.cpp)
target_link_libraries(test_proto PRIVATE dscoop::proto dscoop::warnings)
add_test(NAME proto COMMAND test_proto)

add_executable(test_sigscan test_sigscan.cpp ${CMAKE_SOURCE_DIR}/src/loader/src/sigscan.cpp ${CMAKE_SOURCE_DIR}/src/loader/src/rtti_scan.cpp)
target_include_directories(test_sigscan PRIVATE ${CMAKE_SOURCE_DIR}/src/loader/include ${CMAKE_SOURCE_DIR}/src/loader/src)
target_link_libraries(test_sigscan PRIVATE dscoop::json dscoop::warnings)
add_test(NAME sigscan COMMAND test_sigscan)

add_executable(test_session test_session.cpp)
target_link_libraries(test_session PRIVATE dscoop::net dscoop::warnings)
add_test(NAME session COMMAND test_session)
