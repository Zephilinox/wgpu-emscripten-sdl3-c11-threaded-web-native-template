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

echo "=== Building All Native Examples (Linux / macOS) ==="

CFLAGS="-I$DEPS_DIR/wgpu-native/include -I$DEPS_DIR/sdl3/include -O2 -g"
LDFLAGS="-L$DEPS_DIR/wgpu-native/lib -L$DEPS_DIR/sdl3/build -lwgpu_native -lSDL3 -lm -ldl -lpthread"

if [ "$(uname -s)" = "Darwin" ]; then
    LDFLAGS="$LDFLAGS -framework Metal -framework CoreVideo -framework Cocoa -framework IOKit -framework QuartzCore"
    RPATH="-Wl,-rpath,@executable_path -Wl,-rpath,$DEPS_DIR/wgpu-native/lib:$DEPS_DIR/sdl3/build"
else
    RPATH="-Wl,-rpath,\$ORIGIN -Wl,-rpath,$DEPS_DIR/wgpu-native/lib:$DEPS_DIR/sdl3/build"
fi

# Compiler choice: gcc or clang
CC=${CC:-gcc}

echo "Building Example 01: 01_triangle..."
$CC "$ROOT_DIR/01_triangle.c" -o "$BIN_DIR/01_triangle" $CFLAGS $LDFLAGS $RPATH

echo "Building Example 02: 02_triangle_instancing..."
$CC "$ROOT_DIR/02_triangle_instancing.c" -o "$BIN_DIR/02_triangle_instancing" $CFLAGS $LDFLAGS $RPATH

echo "Building Example 03: 03_bouncing_particles..."
$CC "$ROOT_DIR/03_bouncing_particles.c" -o "$BIN_DIR/03_bouncing_particles" $CFLAGS $LDFLAGS $RPATH

echo "Building Example 04: 04_threaded_triangle..."
$CC "$ROOT_DIR/04_threaded_triangle.c" -o "$BIN_DIR/04_threaded_triangle" $CFLAGS $LDFLAGS $RPATH

cp "$BIN_DIR/03_bouncing_particles" "$ROOT_DIR/bin/native_app"

# Copy dynamic libraries into bin/native so release bundles are self-contained
if [ "$(uname -s)" = "Darwin" ]; then
    cp -f "$DEPS_DIR"/wgpu-native/lib/*.dylib "$BIN_DIR/" 2>/dev/null || true
    cp -f "$DEPS_DIR"/sdl3/build/*.dylib "$BIN_DIR/" 2>/dev/null || true
else
    cp -f "$DEPS_DIR"/wgpu-native/lib/*.so* "$BIN_DIR/" 2>/dev/null || true
    cp -f "$DEPS_DIR"/sdl3/build/*.so* "$BIN_DIR/" 2>/dev/null || true
fi

echo "Native Builds Succeeded:"
echo "  - $BIN_DIR/01_triangle"
echo "  - $BIN_DIR/02_triangle_instancing"
echo "  - $BIN_DIR/03_bouncing_particles"
echo "  - $BIN_DIR/04_threaded_triangle"
