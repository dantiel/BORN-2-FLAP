cmake_minimum_required(VERSION 3.18)

project(Born2Flap VERSION 0.1.0 LANGUAGES CXX)

add_library(born2flap_math SHARED Native/src/born2flap_math_bridge.cpp)
target_include_directories(born2flap_math PUBLIC Native/include)
target_compile_features(born2flap_math PRIVATE cxx_std_17)

option(BORN2FLAP_BUILD_TESTS "Build OrniCore tests" ON)

add_subdirectory(Source/OrniCore)
add_subdirectory(Source/FirstFlap)

if(BORN2FLAP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(Tests)
endif()
