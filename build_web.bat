@echo off
setlocal enabledelayedexpansion

set SCRIPT_DIR=%~dp0
set ROOT_DIR=%SCRIPT_DIR%
set DEPS_DIR=%ROOT_DIR%\deps
set BIN_DIR=%ROOT_DIR%\bin\web

if not exist "%BIN_DIR%" mkdir "%BIN_DIR%"

if not exist "%DEPS_DIR%\sdl3" (
    echo SDL3 repository missing! Running fetch_deps.bat...
    call "%ROOT_DIR%\fetch_deps.bat"
)

where emcc >nul 2>nul
if %ERRORLEVEL% neq 0 (
    echo emcc not found in PATH! Searching for Emscripten SDK...
    if defined EMSDK (
        if exist "%EMSDK%\emsdk_env.bat" (
            echo Auto-activating Emscripten environment from %EMSDK%\emsdk_env.bat...
            call "%EMSDK%\emsdk_env.bat" >nul 2>nul
        )
    ) else if exist "C:\emsdk\emsdk_env.bat" (
        echo Auto-activating Emscripten environment from C:\emsdk\emsdk_env.bat...
        call "C:\emsdk\emsdk_env.bat" >nul 2>nul
    ) else if exist "%USERPROFILE%\emsdk\emsdk_env.bat" (
        echo Auto-activating Emscripten environment from %USERPROFILE%\emsdk\emsdk_env.bat...
        call "%USERPROFILE%\emsdk\emsdk_env.bat" >nul 2>nul
    )
)

where emcc >nul 2>nul
if %ERRORLEVEL% neq 0 (
    echo ==========================================================
    echo  ERROR: Emscripten (emcc) compiler not detected!
    echo ==========================================================
    echo  To install and activate the Emscripten SDK:
    echo    1. git clone https://github.com/emscripten-core/emsdk.git C:\emsdk
    echo    2. cd C:\emsdk ^&^& emsdk install latest ^&^& emsdk activate latest
    echo    3. emsdk_env.bat
    echo ==========================================================
    exit /b 1
)

echo === Building All Web Application Examples (Emscripten + emdawnwebgpu) ===

set EMCC_FLAGS=--use-port=emdawnwebgpu -s USE_SDL=3 -O2 -s WASM=1 -s ALLOW_MEMORY_GROWTH=1 --shell-file "%ROOT_DIR%\shell.html" -I"%DEPS_DIR%\sdl3\include"

echo Building Example 01: 01_triangle.html...
emcc "%ROOT_DIR%\01_triangle.c" -o "%BIN_DIR%\01_triangle.html" %EMCC_FLAGS%

echo Building Example 02: 02_triangle_instancing.html...
emcc "%ROOT_DIR%\02_triangle_instancing.c" -o "%BIN_DIR%\02_triangle_instancing.html" %EMCC_FLAGS%

echo Building Example 03: 03_bouncing_particles.html...
emcc "%ROOT_DIR%\03_bouncing_particles.c" -o "%BIN_DIR%\03_bouncing_particles.html" %EMCC_FLAGS%

echo Building Example 04: 04_threaded_triangle.html...
emcc "%ROOT_DIR%\04_threaded_triangle.c" -o "%BIN_DIR%\04_threaded_triangle.html" %EMCC_FLAGS% -pthread -s PTHREAD_POOL_SIZE=2

echo Building Example 05: 05_compute_threads.html...
emcc "%ROOT_DIR%\05_compute_threads.c" -o "%BIN_DIR%\05_compute_threads.html" %EMCC_FLAGS% -pthread -s PTHREAD_POOL_SIZE=2

copy /Y "%BIN_DIR%\01_triangle.html" "%BIN_DIR%\index.html"
copy /Y "%ROOT_DIR%\coi-serviceworker.js" "%BIN_DIR%\coi-serviceworker.js"

echo Web Builds Succeeded in %BIN_DIR%

REM ===========================================================================
REM itch.io bundle: standalone zip for Example 05 (compute + threads).
REM Built against a minimal shell (no coi-serviceworker, no nav, no sliders).
REM itch.io provides the cross-origin isolation required for pthreads; the page
REM is emitted as index.html so it is itch.io's required entry point.
REM ===========================================================================
set ITCH_DIR=%ROOT_DIR%bin\itch
set ITCH_EX=05_compute_threads
set ITCH_STAGE=%ITCH_DIR%\%ITCH_EX%
if exist "%ITCH_STAGE%" rmdir /s /q "%ITCH_STAGE%"
mkdir "%ITCH_STAGE%"

echo Building itch.io bundle: %ITCH_EX% (index.html)...
set ITCH_FLAGS=--use-port=emdawnwebgpu -s USE_SDL=3 -O2 -s WASM=1 -s ALLOW_MEMORY_GROWTH=1 --shell-file "%ROOT_DIR%\shell_itch.html" -I"%DEPS_DIR%\sdl3\include" -pthread -s PTHREAD_POOL_SIZE=2
emcc "%ROOT_DIR%\%ITCH_EX%.c" -o "%ITCH_STAGE%\index.html" %ITCH_FLAGS%
if %ERRORLEVEL% neq 0 exit /b 1

powershell -NoProfile -Command "Compress-Archive -Path '%ITCH_STAGE%\*' -DestinationPath '%ITCH_DIR%\%ITCH_EX%.zip' -Force"
if %ERRORLEVEL% neq 0 (
    echo NOTE: zip step failed; staged itch bundle is ready to zip manually at:
    echo   %ITCH_STAGE%  ^(must contain index.html at the archive root^)
) else (
    echo itch.io bundle created: %ITCH_DIR%\%ITCH_EX%.zip
    echo   Upload it at https://itch.io/game/new ^(Kind of game: HTML, embed as ZIP^).
)
