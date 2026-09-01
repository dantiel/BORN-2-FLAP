cmake_minimum_required(VERSION 3.18)

project(Born2Flap VERSION 0.1.0 LANGUAGES CXX)

option(BORN2FLAP_BUILD_TESTS "Build OrniCore tests" ON)

add_subdirectory(Source/OrniCore)
add_subdirectory(Source/FirstFlap)

if(BORN2FLAP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(Tests)
endif()
