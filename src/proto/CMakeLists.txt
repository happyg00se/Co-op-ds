add_library(dsproto INTERFACE)
add_library(dscoop::proto ALIAS dsproto)
target_include_directories(dsproto INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(dsproto INTERFACE cxx_std_20)
