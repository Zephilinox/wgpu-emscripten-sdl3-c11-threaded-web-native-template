@echo off
setlocal

set SCRIPT_DIR=%~dp0
set ROOT_DIR=%SCRIPT_DIR%
set DEPS_DIR=%ROOT_DIR%deps
set BIN_DIR=%ROOT_DIR%bin\native

if not exist "%BIN_DIR%" mkdir "%BIN_DIR%"

if not exist "%DEPS_DIR%\wgpu-native" (
    echo Dependencies missing! Running fetch_deps.bat...
    call "%ROOT_DIR%fetch_deps.bat"
)

echo === Building All Native Examples (Windows) ===

set CFLAGS=/I"%DEPS_DIR%\wgpu-native\include" /I"%DEPS_DIR%\sdl3\include" /std:c11 /O2 /W3
set LDFLAGS=/LIBPATH:"%DEPS_DIR%\wgpu-native\lib" /LIBPATH:"%DEPS_DIR%\sdl3\build" /LIBPATH:"%DEPS_DIR%\sdl3\build\Release" wgpu_native.dll.lib SDL3.lib user32.lib gdi32.lib shell32.lib

cl.exe "%ROOT_DIR%01_triangle.c" /Fe:"%BIN_DIR%\01_triangle.exe" %CFLAGS% /link %LDFLAGS%
if errorlevel 1 exit /b 1

cl.exe "%ROOT_DIR%02_triangle_instancing.c" /Fe:"%BIN_DIR%\02_triangle_instancing.exe" %CFLAGS% /link %LDFLAGS%
if errorlevel 1 exit /b 1

cl.exe "%ROOT_DIR%03_bouncing_particles.c" /Fe:"%BIN_DIR%\03_bouncing_particles.exe" %CFLAGS% /link %LDFLAGS%
if errorlevel 1 exit /b 1

cl.exe "%ROOT_DIR%04_threaded_triangle.c" /Fe:"%BIN_DIR%\04_threaded_triangle.exe" %CFLAGS% /link %LDFLAGS%
if errorlevel 1 exit /b 1

if not exist "%ROOT_DIR%bin" mkdir "%ROOT_DIR%bin"
copy /Y "%BIN_DIR%\03_bouncing_particles.exe" "%ROOT_DIR%bin\native_app.exe"

copy /Y "%DEPS_DIR%\wgpu-native\lib\wgpu_native.dll" "%BIN_DIR%\" >nul 2>nul
if exist "%DEPS_DIR%\sdl3\build\Release\SDL3.dll" copy /Y "%DEPS_DIR%\sdl3\build\Release\SDL3.dll" "%BIN_DIR%\" >nul 2>nul
if exist "%DEPS_DIR%\sdl3\build\SDL3.dll" copy /Y "%DEPS_DIR%\sdl3\build\SDL3.dll" "%BIN_DIR%\" >nul 2>nul

echo Native Builds Succeeded in %BIN_DIR%
