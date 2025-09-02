@echo off
REM Simple build script using CMake presets

echo Building HTTP Server...

REM Remove build directory if it exists
if exist build (
    echo Removing existing build directory...
    rmdir /s /q build
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
cmake --preset windows-release
if errorlevel 1 (
    echo ERROR: Configuration failed
    pause
    exit /b 1
)

cmake --build --preset windows-release
if errorlevel 1 (
    echo ERROR: Build failed
    pause
    exit /b 1
)

echo.
echo Build completed successfully!
echo.
echo Executables are available in: build\
echo   - server.exe  (HTTP Server on port 8080)
echo.
echo To test the server:
echo   curl -X POST http://localhost:8080/process -H "Content-Type: application/json" -d "{\"message\":\"Hello World\"}"
echo.
pause
