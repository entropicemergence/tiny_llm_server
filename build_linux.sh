#!/bin/bash

# Simple build script for Ubuntu/WSL using CMake presets

echo "Building HTTP Server for Ubuntu/WSL..."

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


# Configure and build using CMake preset
echo "Configuring and building with CMake preset..."
cmake --preset linux-release -B build
if [ $? -ne 0 ]; then
    echo "ERROR: Configuration failed"
    exit 1
fi

cmake --build build
if [ $? -ne 0 ]; then
    echo "ERROR: Build failed"
    exit 1
fi

echo ""
echo "Build completed successfully!"
echo ""
echo "Executables are available in: build/"
echo "  - server  (HTTP Server on port 8080)"
echo ""
echo "To run the server:"
echo "  ./build/server"
echo ""
echo "To test the server:"
echo "  curl -X POST http://localhost:8080/process -H \"Content-Type: application/json\" -d '{\"message\":\"Hello World\"}'"
echo ""
echo "Press any key to continue..."
read -n 1 -s


# build/inference
# build/server