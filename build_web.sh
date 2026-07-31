#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$SCRIPT_DIR"
DEPS_DIR="$ROOT_DIR/deps"
BIN_DIR="$ROOT_DIR/bin/web"

mkdir -p "$BIN_DIR"

if [ ! -d "$DEPS_DIR/sdl3" ]; then
    echo "SDL3 repository missing! Running fetch_deps.sh..."
    "$ROOT_DIR/fetch_deps.sh"
fi

# Check and auto-activate Emscripten SDK if not present in PATH
if ! command -v emcc &> /dev/null; then
    echo "emcc not found in PATH! Searching for Emscripten SDK..."
    
    EMSDK_PATH=""
    if [ -n "$EMSDK" ] && [ -f "$EMSDK/emsdk_env.sh" ]; then
        EMSDK_PATH="$EMSDK/emsdk_env.sh"
    elif [ -f "$HOME/emsdk/emsdk_env.sh" ]; then
        EMSDK_PATH="$HOME/emsdk/emsdk_env.sh"
    elif [ -f "$HOME/repos/emsdk/emsdk_env.sh" ]; then
        EMSDK_PATH="$HOME/repos/emsdk/emsdk_env.sh"
    fi

    if [ -n "$EMSDK_PATH" ]; then
        echo "Auto-activating Emscripten environment from: $EMSDK_PATH"
        source "$EMSDK_PATH" > /dev/null 2>&1 || true
    fi
fi

if ! command -v emcc &> /dev/null; then
    echo "=========================================================="
    echo " ERROR: Emscripten (emcc) compiler not detected!"
    echo "=========================================================="
    echo " To install and activate the Emscripten SDK:"
    echo "   1. git clone https://github.com/emscripten-core/emsdk.git ~/emsdk"
    echo "   2. cd ~/emsdk && ./emsdk install latest && ./emsdk activate latest"
    echo "   3. source ~/emsdk/emsdk_env.sh"
    echo "=========================================================="
    exit 1
fi

echo "=== Building All Web Application Examples (Emscripten + emdawnwebgpu) ==="

EMCC_FLAGS="--use-port=emdawnwebgpu -s USE_SDL=3 -O2 -s WASM=1 -s ALLOW_MEMORY_GROWTH=1 --shell-file $ROOT_DIR/shell.html -I$DEPS_DIR/sdl3/include"

echo "Building Example 01: 01_triangle.html..."
emcc "$ROOT_DIR/01_triangle.c" -o "$BIN_DIR/01_triangle.html" $EMCC_FLAGS

echo "Building Example 02: 02_triangle_instancing.html..."
emcc "$ROOT_DIR/02_triangle_instancing.c" -o "$BIN_DIR/02_triangle_instancing.html" $EMCC_FLAGS

echo "Building Example 03: 03_bouncing_particles.html..."
emcc "$ROOT_DIR/03_bouncing_particles.c" -o "$BIN_DIR/03_bouncing_particles.html" $EMCC_FLAGS

echo "Building Example 04: 04_threaded_triangle.html..."
emcc "$ROOT_DIR/04_threaded_triangle.c" -o "$BIN_DIR/04_threaded_triangle.html" $EMCC_FLAGS -pthread -s PTHREAD_POOL_SIZE=2

# Default root webpage hosts Example 01 (01_triangle.html) directly
cp "$BIN_DIR/01_triangle.html" "$BIN_DIR/index.html"
cp "$BIN_DIR/01_triangle.js" "$BIN_DIR/index.js"
cp "$BIN_DIR/01_triangle.wasm" "$BIN_DIR/index.wasm"
cp "$ROOT_DIR/coi-serviceworker.js" "$BIN_DIR/coi-serviceworker.js"

echo "Web Builds Succeeded:"
echo "  - Root Default Page: http://localhost:8000/ (Example 01)"
echo "  - Example 01:        http://localhost:8000/01_triangle.html"
echo "  - Example 02:        http://localhost:8000/02_triangle_instancing.html"
echo "  - Example 03:        http://localhost:8000/03_bouncing_particles.html"
echo "  - Example 04:        http://localhost:8000/04_threaded_triangle.html"
