# CMakeLists.txt
cmake_minimum_required(VERSION 3.10)
project(redis_at_home)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Server executable
add_executable(server
    src/hashtable.cpp
    src/elserver.cpp
)

# Client executable
add_executable(client
    src/elclient.cpp
)

# Include directories
include_directories(${PROJECT_SOURCE_DIR}/src)
