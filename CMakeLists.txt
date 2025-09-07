cmake_minimum_required(VERSION 3.20)
project(ServerMockInference)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(Threads REQUIRED)

# Add oatpp from submodule
# add_subdirectory(external/oatpp)

# Include directories
include_directories(src)

# Create IPC library (shared between server and worker)
add_library(ipc_lib
    src/ipc/ipc_utils.cpp
)

# Create tokenizer library
add_library(inference_lib
    src/llm/simple_tokenizer.cpp
    src/llm/tensor.cpp
    src/llm/transformer.cpp
    src/llm/tiny_llm_inference.cpp
)


# Create server dispatcher library
add_library(server_lib
    src/server/task_dispatcher.cpp
    src/server/worker_manager.cpp
    src/utils/http_utils.cpp
)

# Create server executable
add_executable(server
    src/main_server.cpp
)

# Create worker executable
add_executable(worker
    src/worker/worker_main.cpp
)

add_executable(tok
    src/llm/simple_tokenizer.cpp
)
target_link_libraries(tok
    inference_lib
)
target_compile_definitions(tok PRIVATE TOKENIZER_DEBUG)

add_executable(inference
    src/llm/tiny_llm_inference.cpp
)
target_link_libraries(inference
    inference_lib
)
target_compile_definitions(inference PRIVATE INFERENCE_LOOP)


# Link libraries for server
target_link_libraries(server 
    server_lib
    ipc_lib
    # oatpp
    Threads::Threads
)
#target_compile_definitions(server PRIVATE DEBUG_PRINT)

# Link libraries for worker
target_link_libraries(worker
    inference_lib
    ipc_lib
    Threads::Threads
)

# Add POSIX real-time library for semaphores (Linux/Unix)
if(UNIX AND NOT APPLE)
    target_link_libraries(server rt)
    target_link_libraries(worker rt)
    target_link_libraries(tok rt)
    target_link_libraries(inference rt)
endif()

# Set output directory
set_target_properties(server worker tok inference PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)



