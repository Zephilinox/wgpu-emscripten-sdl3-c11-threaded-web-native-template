#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$SCRIPT_DIR"
DEPS_DIR="$ROOT_DIR/deps"

mkdir -p "$DEPS_DIR"

WGPU_VERSION="v29.0.1.1"
SDL3_TAG="release-3.2.8"
OS="$(uname -s)"
ARCH="$(uname -m)"

echo "=== Fetching Dependencies for $OS ($ARCH) ==="

# 1. Fetch wgpu-native (Pinned Version: v29.0.1.1)
WGPU_DIR="$DEPS_DIR/wgpu-native"
if [ ! -d "$WGPU_DIR" ]; then
    echo "Fetching wgpu-native ($WGPU_VERSION)..."
    mkdir -p "$WGPU_DIR"

    case "$OS" in
        Linux*)
            WGPU_URL="https://github.com/gfx-rs/wgpu-native/releases/download/${WGPU_VERSION}/wgpu-linux-x86_64-release.zip"
            ;;
        Darwin*)
            if [ "$ARCH" = "arm64" ]; then
                WGPU_URL="https://github.com/gfx-rs/wgpu-native/releases/download/${WGPU_VERSION}/wgpu-macos-aarch64-release.zip"
            else
                WGPU_URL="https://github.com/gfx-rs/wgpu-native/releases/download/${WGPU_VERSION}/wgpu-macos-x86_64-release.zip"
            fi
            ;;
        *)
            echo "Unsupported OS for automatic wgpu-native bash fetch: $OS"
            exit 1
            ;;
    esac

    curl -L "$WGPU_URL" -o "$DEPS_DIR/wgpu.zip"
    unzip -q "$DEPS_DIR/wgpu.zip" -d "$WGPU_DIR"
    rm "$DEPS_DIR/wgpu.zip"
    echo "wgpu-native fetched successfully into $WGPU_DIR"
else
    echo "wgpu-native already exists in $WGPU_DIR"
fi

# 2. Fetch/Build SDL3 (Pinned Stable Tag: release-3.2.8)
SDL3_DIR="$DEPS_DIR/sdl3"
if [ ! -d "$SDL3_DIR" ]; then
    echo "Fetching SDL3 repository (Tag: $SDL3_TAG)..."
    git clone --depth 1 --branch "$SDL3_TAG" https://github.com/libsdl-org/SDL.git "$SDL3_DIR"
    
    echo "Building SDL3 (Native)..."
    mkdir -p "$SDL3_DIR/build"
    cd "$SDL3_DIR/build"
    cmake .. -DCMAKE_BUILD_TYPE=Release
    cmake --build . --parallel
    cd "$ROOT_DIR"
    echo "SDL3 built successfully!"
else
    echo "SDL3 already exists in $SDL3_DIR"
fi

echo "=== All dependencies ready in $DEPS_DIR ==="
