add_executable(ornicore_tests OrniCoreTests.cpp)
target_link_libraries(ornicore_tests PRIVATE OrniCore)
target_compile_features(ornicore_tests PRIVATE cxx_std_20)
add_test(NAME ornicore_tests COMMAND ornicore_tests)

add_executable(rc_input_tests RcInputTests.cpp)
target_include_directories(rc_input_tests PRIVATE ../Native/include)
target_compile_features(rc_input_tests PRIVATE cxx_std_17)
add_test(NAME rc_input_tests COMMAND rc_input_tests)
