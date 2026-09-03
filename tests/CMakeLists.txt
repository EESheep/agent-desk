cmake_minimum_required(VERSION 3.16)
project(panel_model_tests C)
enable_testing()
add_executable(test_model test_model.c ../firmware/main/panel_model.c)
target_include_directories(test_model PRIVATE ../firmware/main)
set_property(TARGET test_model PROPERTY C_STANDARD 11)
add_test(NAME panel_model COMMAND test_model)

