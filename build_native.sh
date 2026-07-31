#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$SCRIPT_DIR"
DEPS_DIR="$ROOT_DIR/deps"
BIN_DIR="$ROOT_DIR/bin/native"

mkdir -p "$BIN_DIR"

if [ ! -d "$DEPS_DIR/wgpu-native" ] || [ ! -d "$DEPS_DIR/sdl3" ]; then
    echo "Dependencies missing! Running fetch_deps.sh..."
    "$ROOT_DIR/fetch_deps.sh"
fi

echo "=== Building All Native Examples (Linux / macOS / Windows-MinGW) ==="

CFLAGS="-I$DEPS_DIR/wgpu-native/include -I$DEPS_DIR/sdl3/include -O2 -g"
LDFLAGS="-L$DEPS_DIR/wgpu-native/lib -L$DEPS_DIR/sdl3/build -lwgpu_native -lSDL3 -lm -lpthread"

EXE=""
OS="$(uname -s)"
case "$OS" in
    Darwin*)
        LDFLAGS="$LDFLAGS -ldl -framework Metal -framework CoreVideo -framework Cocoa -framework IOKit -framework QuartzCore"
        RPATH="-Wl,-rpath,@executable_path -Wl,-rpath,$DEPS_DIR/wgpu-native/lib:$DEPS_DIR/sdl3/build"
        ;;
    MINGW*|MSYS*|CYGWIN*)
        # MinGW: gcc toolchain, no rpath (Windows resolves DLLs beside the exe).
        EXE=".exe"
        RPATH=""
        ;;
    *)
        LDFLAGS="$LDFLAGS -ldl"
        RPATH="-Wl,-rpath,\$ORIGIN -Wl,-rpath,$DEPS_DIR/wgpu-native/lib:$DEPS_DIR/sdl3/build"
        ;;
esac

# Compiler choice: gcc or clang
CC=${CC:-gcc}

echo "Building Example 01: 01_triangle..."
$CC "$ROOT_DIR/01_triangle.c" -o "$BIN_DIR/01_triangle$EXE" $CFLAGS $LDFLAGS $RPATH

echo "Building Example 02: 02_triangle_instancing..."
$CC "$ROOT_DIR/02_triangle_instancing.c" -o "$BIN_DIR/02_triangle_instancing$EXE" $CFLAGS $LDFLAGS $RPATH

echo "Building Example 03: 03_bouncing_particles..."
$CC "$ROOT_DIR/03_bouncing_particles.c" -o "$BIN_DIR/03_bouncing_particles$EXE" $CFLAGS $LDFLAGS $RPATH

echo "Building Example 04: 04_threaded_triangle..."
$CC "$ROOT_DIR/04_threaded_triangle.c" -o "$BIN_DIR/04_threaded_triangle$EXE" $CFLAGS $LDFLAGS $RPATH

echo "Building Example 05: 05_compute_threads..."
$CC "$ROOT_DIR/05_compute_threads.c" -o "$BIN_DIR/05_compute_threads$EXE" $CFLAGS $LDFLAGS $RPATH

cp "$BIN_DIR/03_bouncing_particles$EXE" "$ROOT_DIR/bin/native_app$EXE"

# Copy dynamic libraries into bin/native so release bundles are self-contained
case "$OS" in
    Darwin*)
        cp -f "$DEPS_DIR"/wgpu-native/lib/*.dylib "$BIN_DIR/" 2>/dev/null || true
        cp -f "$DEPS_DIR"/sdl3/build/*.dylib "$BIN_DIR/" 2>/dev/null || true
        ;;
    MINGW*|MSYS*|CYGWIN*)
        cp -f "$DEPS_DIR"/wgpu-native/lib/*.dll "$BIN_DIR/" 2>/dev/null || true
        cp -f "$DEPS_DIR"/sdl3/build/*.dll "$BIN_DIR/" 2>/dev/null || true
        ;;
    *)
        cp -f "$DEPS_DIR"/wgpu-native/lib/*.so* "$BIN_DIR/" 2>/dev/null || true
        cp -f "$DEPS_DIR"/sdl3/build/*.so* "$BIN_DIR/" 2>/dev/null || true
        ;;
esac

echo "Native Builds Succeeded:"
echo "  - $BIN_DIR/01_triangle$EXE"
echo "  - $BIN_DIR/02_triangle_instancing$EXE"
echo "  - $BIN_DIR/03_bouncing_particles$EXE"
echo "  - $BIN_DIR/04_threaded_triangle$EXE"
echo "  - $BIN_DIR/05_compute_threads$EXE"
