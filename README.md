
# Vibe Coded for [Cakez](https://www.twitch.tv/cakez77) [![GitHub license](https://img.shields.io/github/license/Zephilinox/wgpu-emscriptem-sdl3-c11-threaded-web-native-template.svg)](https://github.com/Zephilinox/wgpu-emscriptem-sdl3-c11-threaded-web-native-template/blob/main/LICENSE) [![Website ricardoheath.co.uk/wgpu-emscriptem-sdl3-c11-threaded-web-native-template/](https://img.shields.io/website-up-down-green-red/https/ricardoheath.co.uk/wgpu-emscriptem-sdl3-c11-threaded-web-native-template.svg)](https://ricardoheath.co.uk/wgpu-emscriptem-sdl3-c11-threaded-web-native-template)

[Try in the browser](https://ricardoheath.co.uk/wgpu-emscriptem-sdl3-c11-threaded-web-native-template)

[Try Compute + Threads on itch.io](https://zephilinox.itch.io/wgpu-sdl3-compute-shader-and-threads)

[Download examples](https://github.com/Zephilinox/wgpu-emscriptem-sdl3-c11-threaded-web-native-template/releases)

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

> **Windows note:** `fetch_deps.bat` downloads the **MSVC** build of `wgpu-native` (`wgpu-windows-x86_64-msvc-release.zip`) since `build_native.bat` compiles with `cl.exe`. If you build with a different toolchain (e.g. MinGW / `gcc`), edit `fetch_deps.bat` to fetch `wgpu-windows-x86_64-gnu-release.zip` instead. The MSVC binaries will not link against GNU binaries.

#### Providing Your Own Dependencies (Skip the Fetch)

Both `fetch_deps.sh` and `fetch_deps.bat` skip any dependency whose folder already exists under `deps/`. You can drop in your own pre-built `wgpu-native` and/or `SDL3` to bypass downloading (and the SDL3 cmake build) entirely. Either dependency can be supplied independently; only the one you provide needs to match the layout below.

The build scripts reference these exact paths, so populate them accordingly:

```
deps/
├── wgpu-native/
│   ├── include/
│   │   └── webgpu/
│   │       ├── webgpu.h
│   │       └── wgpu.h
│   └── lib/
│       ├── wgpu_native.dll.lib       # Windows (MSVC import lib; links against wgpu_native.dll)
│       ├── wgpu_native.dll            # Windows
│       ├── libwgpu_native.so*         # Linux
│       └── libwgpu_native.dylib       # macOS
└── sdl3/
    ├── include/                       # SDL3 headers (e.g. include/SDL3/SDL.h)
    └── build/
        ├── SDL3.lib / SDL3.dll        # Windows (build/ or build/Release/)
        ├── libSDL3.so*                # Linux
        └── libSDL3.dylib              # macOS
```

Notes:
- **wgpu-native**: any compatible `wgpu.h` / `webgpu.h` (matching the `webgpu/webgpu.h` include used by these examples) works. Headers must sit under `deps/wgpu-native/include/webgpu/`.
- **SDL3**: if you already have SDL3 installed system-wide, the simplest route is to create the expected `deps/sdl3/` tree with symlinks pointing at your installed prefix.

Runtime library resolution (the build scripts already copy the fetched libraries next to the executables in `bin/native/`):
- **Windows**: place `wgpu_native.dll` and `SDL3.dll` in `bin/native/` (or add their folder to `PATH`).
- **Linux**: place `libwgpu_native.so*` and `libSDL3.so*` in `bin/native/`. The binaries are built with an `rpath` of `$ORIGIN`, so this is enough. Otherwise set `LD_LIBRARY_PATH`.
- **macOS**: place `libwgpu_native.dylib` and `libSDL3.dylib` in `bin/native/`. The binaries are built with an `rpath` of `@executable_path`, so this is enough. Otherwise set `DYLD_LIBRARY_PATH`.

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
./bin/native/05_compute_threads
```

### 3. Build & Serve WebAssembly Web App
Compiles all 4 C examples into WebAssembly and starts a local web server (with COOP/COEP headers for pthreads):
```bash
./build_web.sh
./serve_web.sh
# Windows: build_web.bat / serve_web.bat
```
Open **`http://localhost:8000/`** in your browser to view the interactive application with navigation buttons for all 4 examples.
