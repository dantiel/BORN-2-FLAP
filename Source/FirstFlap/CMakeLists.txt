add_executable(first_flap main.cpp)
target_link_libraries(first_flap PRIVATE OrniCore)
target_compile_features(first_flap PRIVATE cxx_std_20)
