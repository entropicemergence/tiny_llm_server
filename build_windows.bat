@echo off
REM Simple build script using CMake presets

echo Building HTTP Server...

REM Remove build directory if it exists
if exist build_windows (
    echo Removing existing build_windows directory...
    rmdir /s /q build_windows
)

REM Check if we're in a Visual Studio Developer Command Prompt
where cl >nul 2>nul
if errorlevel 1 (
    echo ERROR: This script must be run from a Visual Studio Developer Command Prompt
    echo Please run from: "Visual Studio 2022 Developer Command Prompt"
    pause
    exit /b 1
)

REM Configure and build using CMake preset
echo Configuring and building with CMake preset...
cmake --preset windows-release -B build_windows
if errorlevel 1 (
    echo ERROR: Configuration failed
    pause
    exit /b 1
)

cmake --build build_windows
if errorlevel 1 (
    echo ERROR: Build failed
    pause
    exit /b 1
)

echo.
echo Build completed successfully!
echo.
echo Executables are available in: build_windows\
echo   - server.exe  (HTTP Server on port 8080)
echo.
echo To test the server:
echo   curl -X POST http://localhost:8080/process -H "Content-Type: application/json" -d "{\"message\":\"Hello World\"}"
echo.
pause
