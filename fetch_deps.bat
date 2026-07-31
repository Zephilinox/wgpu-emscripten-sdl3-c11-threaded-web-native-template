@echo off
setlocal

set SCRIPT_DIR=%~dp0
set ROOT_DIR=%SCRIPT_DIR%
set DEPS_DIR=%ROOT_DIR%deps
set WGPU_VERSION=v29.0.1.1
set SDL3_TAG=release-3.2.8
set WGPU_DIR=%DEPS_DIR%\wgpu-native
set SDL3_DIR=%DEPS_DIR%\sdl3

if not exist "%DEPS_DIR%" mkdir "%DEPS_DIR%"

echo === Fetching Dependencies for Windows ===

if not exist "%WGPU_DIR%" (
    echo Fetching wgpu-native %WGPU_VERSION%...
    mkdir "%WGPU_DIR%"
    powershell -Command "Invoke-WebRequest -Uri 'https://github.com/gfx-rs/wgpu-native/releases/download/%WGPU_VERSION%/wgpu-windows-x86_64-msvc-release.zip' -OutFile '%DEPS_DIR%\wgpu.zip'"
    if errorlevel 1 (
        echo Failed to download wgpu-native. Aborting.
        rmdir /s /q "%WGPU_DIR%"
        exit /b 1
    )
    powershell -Command "Expand-Archive -Path '%DEPS_DIR%\wgpu.zip' -DestinationPath '%WGPU_DIR%'"
    if errorlevel 1 (
        echo Failed to extract wgpu-native. Aborting.
        rmdir /s /q "%WGPU_DIR%"
        exit /b 1
    )
    del "%DEPS_DIR%\wgpu.zip"
    echo wgpu-native fetched successfully!
) else (
    echo wgpu-native already exists in %WGPU_DIR%
)

if not exist "%SDL3_DIR%" (
    echo Fetching SDL3 repository - Tag %SDL3_TAG%...
    git clone --depth 1 --branch %SDL3_TAG% https://github.com/libsdl-org/SDL.git "%SDL3_DIR%"
    
    echo Building SDL3 Native...
    mkdir "%SDL3_DIR%\build"
    cd "%SDL3_DIR%\build"
    cmake .. -DCMAKE_BUILD_TYPE=Release
    cmake --build . --config Release --parallel
    cd "%ROOT_DIR%"
    echo SDL3 built successfully!
) else (
    echo SDL3 already exists in %SDL3_DIR%
)

echo === All dependencies ready in %DEPS_DIR% ===
