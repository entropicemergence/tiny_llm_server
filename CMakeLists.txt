cmake_minimum_required(VERSION 3.20)
project(ServerMockInference)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(Threads REQUIRED)

# Add oatpp from submodule
add_subdirectory(external/oatpp)

# Include directories
include_directories(src)

# Create server executable
add_executable(server
    src/main_server.cpp
)

# Link libraries for server
target_link_libraries(server 
    oatpp
    Threads::Threads
)

# Set output directory
set_target_properties(server PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)