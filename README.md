# Vibe Coded for Cakez

A cross-platform starter project demonstrating **WebGPU** and **SDL3** in pure C, compiling to both **Native Desktop** (Linux, macOS, Windows) and **WebAssembly** (WebGPU in browser).

---

## Prerequisites

Before building, ensure you have the following installed:

### Native Desktop (Linux / macOS / Windows)
- **C Compiler**: `gcc` or `clang` (Linux/macOS) or MSVC (`cl.exe` on Windows).
- **Build Tools**: `git`, `cmake`, `curl`, `unzip`.
- **Graphics Drivers**: GPU drivers supporting Vulkan (Linux/Windows), Metal (macOS), or Direct3D 12 (Windows).

### WebAssembly (Browser)
- **Emscripten SDK (`emcc`)**: Installed on your system (`~/emsdk`). The build script auto-detects and activates `emsdk_env.sh` automatically if `emcc` is not already in your `PATH`.
- **Python 3**: For hosting the local dev web server (`python3 -m http.server`).
- **WebGPU-Enabled Browser**: Chrome 113+, Edge 113+, or Firefox (`dom.webgpu.enabled = true` in `about:config`).

---

## Troubleshooting & FAQs

### `WebGPU API Not Available in this Browser`
- **Firefox**: Open `about:config` in the address bar, search for `dom.webgpu.enabled`, and toggle it to `true`.
- **Chrome / Edge**: Ensure you are using version 113 or newer. WebGPU is enabled by default.
- **Linux/Nvidia**: Run Chrome with `--enable-features=Vulkan,UseSkiaRenderer,WebGPU`.

---

## Quick Start

### 1. Fetch Dependencies
Downloads pinned `wgpu-native` binaries (`v29.0.1.1`) and clones/builds stable `SDL3` (`release-3.2.8`):
```bash
./fetch_deps.sh
# Windows: fetch_deps.bat
```

### 2. Build & Run Native Executables
Compiles all 4 C examples into `./bin/native/`:
```bash
./build_native.sh
# Windows: build_native.bat

# Run any example natively:
./bin/native/01_triangle
./bin/native/02_triangle_instancing
./bin/native/03_bouncing_particles
./bin/native/04_threaded_triangle
```

### 3. Build & Serve WebAssembly Web App
Compiles all 4 C examples into WebAssembly and starts a local web server (with COOP/COEP headers for pthreads):
```bash
./build_web.sh
./serve_web.sh
# Windows: build_web.bat / serve_web.bat
```
Open **`http://localhost:8000/`** in your browser to view the interactive application with navigation buttons for all 4 examples.
