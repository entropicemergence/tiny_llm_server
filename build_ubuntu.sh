#!/bin/bash

# Simple build script for Ubuntu/WSL using CMake presets

echo "Building HTTP Server for Ubuntu/WSL..."

# # Remove build directory if it exists
# if [ -d "build_linux" ]; then
#     echo "Removing existing build_linux directory..."
#     rm -rf build_linux
# fi

# Check if required tools are installed
check_dependency() {
    if ! command -v $1 &> /dev/null; then
        echo "ERROR: $1 is not installed"
        echo "Please install it using: sudo apt update && sudo apt install $2"
        exit 1
    fi
}

echo "Checking dependencies..."
check_dependency "cmake" "cmake"
check_dependency "ninja" "ninja-build"
check_dependency "g++" "build-essential"

# Check CMake version
CMAKE_VERSION=$(cmake --version | head -n1 | cut -d' ' -f3)
CMAKE_MAJOR=$(echo $CMAKE_VERSION | cut -d'.' -f1)
CMAKE_MINOR=$(echo $CMAKE_VERSION | cut -d'.' -f2)

if [ "$CMAKE_MAJOR" -lt 3 ] || ([ "$CMAKE_MAJOR" -eq 3 ] && [ "$CMAKE_MINOR" -lt 20 ]); then
    echo "ERROR: CMake version $CMAKE_VERSION is too old. Minimum required: 3.20"
    echo "Please update CMake or install from https://cmake.org/download/"
    exit 1
fi

# Initialize git submodules if they don't exist
if [ ! -f "external/oatpp/CMakeLists.txt" ]; then
    echo "Initializing git submodules..."
    git submodule update --init --recursive
    if [ $? -ne 0 ]; then
        echo "ERROR: Failed to initialize git submodules"
        echo "Make sure you're in a git repository and have internet access"
        exit 1
    fi
fi

# Configure and build using CMake preset
echo "Configuring and building with CMake preset..."
cmake --preset linux-release -B build_linux
if [ $? -ne 0 ]; then
    echo "ERROR: Configuration failed"
    exit 1
fi

cmake --build build_linux
if [ $? -ne 0 ]; then
    echo "ERROR: Build failed"
    exit 1
fi

echo ""
echo "Build completed successfully!"
echo ""
echo "Executables are available in: build_linux/"
echo "  - server  (HTTP Server on port 8080)"
echo ""
echo "To run the server:"
echo "  ./build_linux/server"
echo ""
echo "To test the server:"
echo "  curl -X POST http://localhost:8080/process -H \"Content-Type: application/json\" -d '{\"message\":\"Hello World\"}'"
echo ""
echo "Press any key to continue..."
# read -n 1 -s


build_linux/inference