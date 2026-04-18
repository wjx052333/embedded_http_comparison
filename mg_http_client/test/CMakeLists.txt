enable_testing()
add_test(NAME basic COMMAND test_basic)

add_executable(test_basic test_basic.c)
target_link_libraries(test_basic mhc)
target_compile_options(test_basic PRIVATE -UNDEBUG)

add_executable(bench bench.c)
target_link_libraries(bench mhc)
