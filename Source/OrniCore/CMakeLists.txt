add_library(OrniCore STATIC
    src/WingSimulation.cpp
)

target_include_directories(OrniCore PUBLIC include)
target_compile_features(OrniCore PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(OrniCore PRIVATE /W4 /permissive-)
else()
    target_compile_options(OrniCore PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()
