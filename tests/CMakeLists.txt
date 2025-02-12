cmake_minimum_required(VERSION 3.10)
project(ServerTests)

# Set the path where Homebrew installs googletest (adjust as necessary)
set(GTEST_ROOT "/home/linuxbrew/.linuxbrew/opt/googletest")
set(GTEST_INCLUDE_DIR "${GTEST_ROOT}/include")
set(GTEST_LIB_DIR "${GTEST_ROOT}/lib")

include_directories(${GTEST_INCLUDE_DIR})
link_directories(${GTEST_LIB_DIR})
include_directories(${CMAKE_SOURCE_DIR}/src)  # To find server.h

# Add the test source file and the server source file (elserver.cpp).
add_executable(server_tests server_test.cpp ../src/elserver.cpp)

# Link against GoogleTest libraries and pthread.
target_link_libraries(server_tests gtest gtest_main pthread)

# Define UNIT_TEST for testing builds.
target_compile_definitions(server_tests PRIVATE UNIT_TEST)
