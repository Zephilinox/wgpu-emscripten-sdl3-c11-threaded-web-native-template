#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#ifdef SDL_PLATFORM_MACOS
#include <SDL3/SDL_metal.h>
#endif
#ifdef SDL_PLATFORM_WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <stddef.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#include <webgpu/webgpu.h>
#else
#include <webgpu/webgpu.h>
#include <webgpu/wgpu.h>
#endif

#define WGSL(...) #__VA_ARGS__

#define PARTICLE_COUNT 20000u
#define SIZE_SCALE 4.0f

static inline WGPUStringView make_str_view(const char* s) {
    return (WGPUStringView){ .data = s, .length = s ? strlen(s) : 0 };
}

typedef struct Particle {
    float pos[2];     // 0..8
    float vel[2];     // 8..16
    float radius;     // 16..20
    float pad0;       // 20..24
    float pad1[2];    // 24..32  (padded to 32 bytes for WGSL storage array stride)
} Particle;

typedef struct SimParams {
    float deltaTime;    // 0..4
    float aspect;       // 4..8
    float sizeScale;    // 8..12
    float speedScale;   // 12..16
    float boundsMin[2]; // 16..24
    float boundsMax[2]; // 24..32
} SimParams;

typedef struct AppState {
    SDL_Window* window;

    WGPUInstance instance;
    WGPUSurface surface;
    WGPUAdapter adapter;
    WGPUDevice device;
    WGPUQueue queue;

    bool deviceRequested;
    bool pipelinesInitialized;

    WGPUTextureFormat surfaceFormat;
    uint32_t width;
    uint32_t height;

    WGPUBindGroupLayout computeBindGroupLayout;
    WGPUBindGroup computeBindGroup;
    WGPUComputePipeline computePipeline;
    WGPUBuffer particleBuffer;
    WGPUBuffer simParamsBuffer;

    WGPUBindGroupLayout renderBindGroupLayout;
    WGPUBindGroup renderBindGroup;
    WGPURenderPipeline renderPipeline;
    WGPUBuffer paletteBuffer;

    // CPU worker thread that cycles the palette (example-04 style)
    SDL_Thread* workerThread;
    SDL_Mutex* colorMutex;
    bool threadRunning;
    bool colorDirty;
    float palette[3][4]; // 3 vec4 colors, cycled by the worker thread

    uint64_t lastTicks;
    uint64_t fpsTimerTicks;
    uint32_t frameCount;

#ifdef SDL_PLATFORM_MACOS
    SDL_MetalView metalView;
#endif
} AppState;

static void handle_adapter_request(WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message, void* userdata1, void* userdata2) {
    AppState* app = (AppState*)userdata1;
    if (status == WGPURequestAdapterStatus_Success) app->adapter = adapter;
}

static void handle_device_request(WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message, void* userdata1, void* userdata2) {
    AppState* app = (AppState*)userdata1;
    if (status == WGPURequestDeviceStatus_Success) app->device = device;
}

// Background worker thread: cycles a 3-color palette over time and flags it dirty.
// The main thread uploads the palette to the GPU when dirty (see SDL_AppIterate).
// This is the same pattern as Example 04, applied to the particle palette.
static int SDLCALL palette_worker_thread(void* data) {
    AppState* app = (AppState*)data;
    printf("[WorkerThread] Palette color thread started.\n");

    float time = 0.0f;
    while (app->threadRunning) {
        time += 0.05f;
        float r = (sinf(time) + 1.0f) * 0.5f;
        float g = (sinf(time + 2.094f) + 1.0f) * 0.5f;
        float b = (sinf(time + 4.188f) + 1.0f) * 0.5f;

        SDL_LockMutex(app->colorMutex);
        app->palette[0][0] = r; app->palette[0][1] = g; app->palette[0][2] = b; app->palette[0][3] = 0.85f;
        app->palette[1][0] = g; app->palette[1][1] = b; app->palette[1][2] = r; app->palette[1][3] = 0.85f;
        app->palette[2][0] = b; app->palette[2][1] = r; app->palette[2][2] = g; app->palette[2][3] = 0.85f;
        app->colorDirty = true;
        SDL_UnlockMutex(app->colorMutex);

        SDL_Delay(16); // ~60 Hz
    }

    printf("[WorkerThread] Palette color thread exiting.\n");
    return 0;
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[]) {
    printf("[AppInit] Example 05: Compute-Shader Particles + Threaded Palette (itch.io build)...\n");

    AppState* app = (AppState*)SDL_calloc(1, sizeof(AppState));
    if (!app) return SDL_APP_FAILURE;
    *appstate = app;

    app->width = 1280;
    app->height = 720;
    app->lastTicks = SDL_GetTicks();
    app->fpsTimerTicks = app->lastTicks;

    app->window = SDL_CreateWindow("Example 05: Compute + Threads", app->width, app->height, SDL_WINDOW_RESIZABLE);
    if (!app->window) return SDL_APP_FAILURE;

    app->instance = wgpuCreateInstance(NULL);
    if (!app->instance) return SDL_APP_FAILURE;

#ifdef __EMSCRIPTEN__
    WGPUEmscriptenSurfaceSourceCanvasHTMLSelector canvasDesc = {
        .chain = { .next = NULL, .sType = WGPUSType_EmscriptenSurfaceSourceCanvasHTMLSelector },
        .selector = make_str_view("#canvas")
    };
    WGPUSurfaceDescriptor surfaceDesc = { .nextInChain = (WGPUChainedStruct*)&canvasDesc, .label = make_str_view("Canvas Surface") };
    app->surface = wgpuInstanceCreateSurface(app->instance, &surfaceDesc);
#else
    SDL_PropertiesID props = SDL_GetWindowProperties(app->window);
#if defined(SDL_PLATFORM_WIN32)
    HWND hwnd = (HWND)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
    HINSTANCE hinstance = (HINSTANCE)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_INSTANCE_POINTER, NULL);
    WGPUSurfaceSourceWindowsHWND winDesc = { .chain = { .sType = WGPUSType_SurfaceSourceWindowsHWND }, .hinstance = hinstance, .hwnd = hwnd };
    WGPUSurfaceDescriptor surfaceDesc = { .nextInChain = (WGPUChainedStruct*)&winDesc };
    app->surface = wgpuInstanceCreateSurface(app->instance, &surfaceDesc);
#elif defined(SDL_PLATFORM_MACOS)
    app->metalView = SDL_Metal_CreateView(app->window);
    void* metalLayer = SDL_Metal_GetLayer(app->metalView);
    WGPUSurfaceSourceMetalLayer metalDesc = { .chain = { .sType = WGPUSType_SurfaceSourceMetalLayer }, .layer = metalLayer };
    WGPUSurfaceDescriptor surfaceDesc = { .nextInChain = (WGPUChainedStruct*)&metalDesc, .label = make_str_view("Canvas Surface") };
    app->surface = wgpuInstanceCreateSurface(app->instance, &surfaceDesc);
#else
    void* xdisplay = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, NULL);
    uint64_t xwindow = (uint64_t)SDL_GetNumberProperty(props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);
    if (xdisplay && xwindow) {
        WGPUSurfaceSourceXlibWindow x11Desc = { .chain = { .sType = WGPUSType_SurfaceSourceXlibWindow }, .display = xdisplay, .window = xwindow };
        WGPUSurfaceDescriptor surfaceDesc = { .nextInChain = (WGPUChainedStruct*)&x11Desc };
        app->surface = wgpuInstanceCreateSurface(app->instance, &surfaceDesc);
    } else {
        void* wldisplay = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, NULL);
        void* wlsurface = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, NULL);
        if (wldisplay && wlsurface) {
            WGPUSurfaceSourceWaylandSurface waylandDesc = { .chain = { .sType = WGPUSType_SurfaceSourceWaylandSurface }, .display = wldisplay, .surface = wlsurface };
            WGPUSurfaceDescriptor surfaceDesc = { .nextInChain = (WGPUChainedStruct*)&waylandDesc };
            app->surface = wgpuInstanceCreateSurface(app->instance, &surfaceDesc);
        }
    }
#endif
#endif

    if (!app->surface) return SDL_APP_FAILURE;

    WGPURequestAdapterOptions adapterOpts = { .compatibleSurface = app->surface, .powerPreference = WGPUPowerPreference_HighPerformance };
    WGPURequestAdapterCallbackInfo adapterCbInfo = { .mode = WGPUCallbackMode_AllowProcessEvents, .callback = handle_adapter_request, .userdata1 = app };
    wgpuInstanceRequestAdapter(app->instance, &adapterOpts, adapterCbInfo);

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
    AppState* app = (AppState*)appstate;
    if (event->type == SDL_EVENT_QUIT) {
        return SDL_APP_SUCCESS;
    } else if (event->type == SDL_EVENT_WINDOW_RESIZED || event->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
        if (event->window.data1 > 0 && event->window.data2 > 0) {
            app->width = (uint32_t)event->window.data1;
            app->height = (uint32_t)event->window.data2;
            if (app->pipelinesInitialized && app->surface && app->device) {
                WGPUSurfaceConfiguration config = { .device = app->device, .format = app->surfaceFormat, .usage = WGPUTextureUsage_RenderAttachment, .width = app->width, .height = app->height, .presentMode = WGPUPresentMode_Fifo };
                wgpuSurfaceConfigure(app->surface, &config);
            }
        }
    }
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate) {
    AppState* app = (AppState*)appstate;

    if (app->instance) wgpuInstanceProcessEvents(app->instance);
    if (!app->adapter) return SDL_APP_CONTINUE;

    if (!app->deviceRequested) {
        app->deviceRequested = true;
        WGPUDeviceDescriptor deviceDesc = { .label = make_str_view("Logical Device") };
        WGPURequestDeviceCallbackInfo deviceCbInfo = { .mode = WGPUCallbackMode_AllowProcessEvents, .callback = handle_device_request, .userdata1 = app };
        wgpuAdapterRequestDevice(app->adapter, &deviceDesc, deviceCbInfo);
        return SDL_APP_CONTINUE;
    }

    if (!app->device) return SDL_APP_CONTINUE;

    if (!app->pipelinesInitialized) {
        app->queue = wgpuDeviceGetQueue(app->device);
        app->surfaceFormat = WGPUTextureFormat_BGRA8Unorm;

        WGPUSurfaceConfiguration config = { .device = app->device, .format = app->surfaceFormat, .usage = WGPUTextureUsage_RenderAttachment, .width = app->width, .height = app->height, .presentMode = WGPUPresentMode_Fifo };
        wgpuSurfaceConfigure(app->surface, &config);

        float aspect = (float)app->width / (float)app->height;

        // Seed particles (positions/velocities/radii). Colors come from the threaded palette.
        srand(1337);
        Particle* particles = (Particle*)malloc(sizeof(Particle) * PARTICLE_COUNT);
        if (particles) {
            for (uint32_t i = 0; i < PARTICLE_COUNT; i++) {
                particles[i].pos[0] = (((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f) * aspect;
                particles[i].pos[1] = (((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f);
                float angle = ((float)rand() / (float)RAND_MAX) * 6.2831853f;
                float speed = 0.04f + 0.16f * ((float)rand() / (float)RAND_MAX);
                particles[i].vel[0] = cosf(angle) * speed;
                particles[i].vel[1] = sinf(angle) * speed;
                particles[i].radius = 0.0048f + 0.0032f * ((float)rand() / (float)RAND_MAX);
            }

            WGPUBufferDescriptor particleBufferDesc = { .label = make_str_view("Particle Storage Buffer"), .usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst, .size = sizeof(Particle) * PARTICLE_COUNT };
            app->particleBuffer = wgpuDeviceCreateBuffer(app->device, &particleBufferDesc);
            wgpuQueueWriteBuffer(app->queue, app->particleBuffer, 0, particles, sizeof(Particle) * PARTICLE_COUNT);
            free(particles);
        }

        // Uniform buffers: simulation params (refreshed every frame) and the color
        // palette (refreshed from the worker thread). The per-frame upload below
        // seeds simParams before the first dispatch, so no init-time write is needed.
        WGPUBufferDescriptor simBufferDesc = { .label = make_str_view("Sim Params Uniform Buffer"), .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst, .size = sizeof(SimParams) };
        app->simParamsBuffer = wgpuDeviceCreateBuffer(app->device, &simBufferDesc);

        // Palette uniform: 3 vec4 colors, refreshed from the worker thread.
        WGPUBufferDescriptor paletteBufferDesc = { .label = make_str_view("Palette Uniform Buffer"), .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst, .size = sizeof(app->palette) };
        app->paletteBuffer = wgpuDeviceCreateBuffer(app->device, &paletteBufferDesc);

        // ---- Compute pipeline (GPU particle physics) ----
        const char* csCode = WGSL(
            struct Particle { pos: vec2<f32>, vel: vec2<f32>, radius: f32, _pad0: f32, _pad1: vec2<f32> };
            struct SimParams { deltaTime: f32, aspect: f32, sizeScale: f32, speedScale: f32, boundsMin: vec2<f32>, boundsMax: vec2<f32> };

            @group(0) @binding(0) var<storage, read_write> particles : array<Particle>;
            @group(0) @binding(1) var<uniform> params : SimParams;

            @compute @workgroup_size(64)
            fn cs_main(@builtin(global_invocation_id) id : vec3<u32>) {
                let idx = id.x;
                if (idx >= arrayLength(&particles)) { return; }
                var p = particles[idx];
                p.pos += p.vel * (params.deltaTime * params.speedScale);

                let effX = params.boundsMax.x * params.aspect;
                let effR = p.radius * params.sizeScale;
                if (p.pos.x - effR < -effX) { p.pos.x = -effX + effR; p.vel.x = -p.vel.x; }
                if (p.pos.x + effR >  effX) { p.pos.x =  effX - effR; p.vel.x = -p.vel.x; }
                if (p.pos.y - effR < params.boundsMin.y) { p.pos.y = params.boundsMin.y + effR; p.vel.y = -p.vel.y; }
                if (p.pos.y + effR > params.boundsMax.y) { p.pos.y = params.boundsMax.y - effR; p.vel.y = -p.vel.y; }

                particles[idx] = p;
            }
        );

        WGPUShaderSourceWGSL csSource = { .chain = { .sType = WGPUSType_ShaderSourceWGSL }, .code = make_str_view(csCode) };
        WGPUShaderModuleDescriptor csModDesc = { .nextInChain = (WGPUChainedStruct*)&csSource, .label = make_str_view("Compute Module") };
        WGPUShaderModule csModule = wgpuDeviceCreateShaderModule(app->device, &csModDesc);

        WGPUBindGroupLayoutEntry csEntries[2] = {
            { .binding = 0, .visibility = WGPUShaderStage_Compute, .buffer = { .type = WGPUBufferBindingType_Storage } },
            { .binding = 1, .visibility = WGPUShaderStage_Compute, .buffer = { .type = WGPUBufferBindingType_Uniform } }
        };
        WGPUBindGroupLayoutDescriptor csBglDesc = { .label = make_str_view("Compute BGL"), .entryCount = 2, .entries = csEntries };
        app->computeBindGroupLayout = wgpuDeviceCreateBindGroupLayout(app->device, &csBglDesc);

        WGPUPipelineLayoutDescriptor csPlDesc = { .bindGroupLayoutCount = 1, .bindGroupLayouts = &app->computeBindGroupLayout };
        WGPUPipelineLayout csPipelineLayout = wgpuDeviceCreatePipelineLayout(app->device, &csPlDesc);

        WGPUComputePipelineDescriptor csPipelineDesc = { .layout = csPipelineLayout, .compute = { .module = csModule, .entryPoint = make_str_view("cs_main") } };
        app->computePipeline = wgpuDeviceCreateComputePipeline(app->device, &csPipelineDesc);

        wgpuShaderModuleRelease(csModule);
        wgpuPipelineLayoutRelease(csPipelineLayout);

        WGPUBindGroupEntry csBgEntries[2] = {
            { .binding = 0, .buffer = app->particleBuffer, .offset = 0, .size = sizeof(Particle) * PARTICLE_COUNT },
            { .binding = 1, .buffer = app->simParamsBuffer, .offset = 0, .size = sizeof(SimParams) }
        };
        WGPUBindGroupDescriptor csBgDesc = { .label = make_str_view("Compute BG"), .layout = app->computeBindGroupLayout, .entryCount = 2, .entries = csBgEntries };
        app->computeBindGroup = wgpuDeviceCreateBindGroup(app->device, &csBgDesc);

        // ---- Render pipeline (instanced circle quads tinted by the threaded palette) ----
        const char* renderCode = WGSL(
            struct Particle { pos: vec2<f32>, vel: vec2<f32>, radius: f32, _pad0: f32, _pad1: vec2<f32> };
            struct SimParams { deltaTime: f32, aspect: f32, sizeScale: f32, speedScale: f32, boundsMin: vec2<f32>, boundsMax: vec2<f32> };
            struct Palette { colors: array<vec4<f32>, 3> };

            @group(0) @binding(0) var<storage, read> particles : array<Particle>;
            @group(0) @binding(1) var<uniform> params : SimParams;
            @group(0) @binding(2) var<uniform> palette : Palette;

            struct VertexOutput { @builtin(position) position: vec4<f32>, @location(0) color: vec4<f32>, @location(1) uv: vec2<f32> };

            @vertex
            fn vs_particle(@builtin(vertex_index) v_idx: u32, @builtin(instance_index) i_idx: u32) -> VertexOutput {
                let p = particles[i_idx];
                var quad = array<vec2<f32>, 6>(vec2<f32>(-1.0,-1.0), vec2<f32>(1.0,-1.0), vec2<f32>(-1.0,1.0), vec2<f32>(-1.0,1.0), vec2<f32>(1.0,-1.0), vec2<f32>(1.0,1.0));
                let corner = quad[v_idx];
                let effR = p.radius * params.sizeScale;
                let worldPos = vec2<f32>(p.pos.x / params.aspect + (corner.x * effR) / params.aspect, p.pos.y + corner.y * effR);
                var out : VertexOutput;
                out.position = vec4<f32>(worldPos, 0.0, 1.0);
                out.color = palette.colors[i_idx % 3u];
                out.uv = corner;
                return out;
            }

            @fragment
            fn fs_particle(in: VertexOutput) -> @location(0) vec4<f32> {
                let dist = length(in.uv);
                if (dist > 1.0) { discard; }
                let alpha = 1.0 - smoothstep(0.6, 1.0, dist);
                return vec4<f32>(in.color.rgb, in.color.a * alpha);
            }
        );

        WGPUShaderSourceWGSL rSource = { .chain = { .sType = WGPUSType_ShaderSourceWGSL }, .code = make_str_view(renderCode) };
        WGPUShaderModuleDescriptor rModDesc = { .nextInChain = (WGPUChainedStruct*)&rSource, .label = make_str_view("Render Module") };
        WGPUShaderModule rModule = wgpuDeviceCreateShaderModule(app->device, &rModDesc);

        WGPUBindGroupLayoutEntry rEntries[3] = {
            { .binding = 0, .visibility = WGPUShaderStage_Vertex, .buffer = { .type = WGPUBufferBindingType_ReadOnlyStorage } },
            { .binding = 1, .visibility = WGPUShaderStage_Vertex, .buffer = { .type = WGPUBufferBindingType_Uniform } },
            { .binding = 2, .visibility = WGPUShaderStage_Vertex, .buffer = { .type = WGPUBufferBindingType_Uniform } }
        };
        WGPUBindGroupLayoutDescriptor rBglDesc = { .label = make_str_view("Render BGL"), .entryCount = 3, .entries = rEntries };
        app->renderBindGroupLayout = wgpuDeviceCreateBindGroupLayout(app->device, &rBglDesc);

        WGPUPipelineLayoutDescriptor rPlDesc = { .bindGroupLayoutCount = 1, .bindGroupLayouts = &app->renderBindGroupLayout };
        WGPUPipelineLayout rPipelineLayout = wgpuDeviceCreatePipelineLayout(app->device, &rPlDesc);

        WGPUBlendState alphaBlend = {
            .color = { .operation = WGPUBlendOperation_Add, .srcFactor = WGPUBlendFactor_SrcAlpha, .dstFactor = WGPUBlendFactor_OneMinusSrcAlpha },
            .alpha = { .operation = WGPUBlendOperation_Add, .srcFactor = WGPUBlendFactor_One, .dstFactor = WGPUBlendFactor_OneMinusSrcAlpha }
        };
        WGPUColorTargetState rColorTarget = { .format = app->surfaceFormat, .blend = &alphaBlend, .writeMask = WGPUColorWriteMask_All };
        WGPUFragmentState rFragmentState = { .module = rModule, .entryPoint = make_str_view("fs_particle"), .targetCount = 1, .targets = &rColorTarget };

        WGPURenderPipelineDescriptor rPipelineDesc = {
            .layout = rPipelineLayout,
            .vertex = { .module = rModule, .entryPoint = make_str_view("vs_particle"), .bufferCount = 0 },
            .primitive = { .topology = WGPUPrimitiveTopology_TriangleList, .frontFace = WGPUFrontFace_CCW, .cullMode = WGPUCullMode_None },
            .multisample = { .count = 1, .mask = 0xFFFFFFFF },
            .fragment = &rFragmentState,
        };
        app->renderPipeline = wgpuDeviceCreateRenderPipeline(app->device, &rPipelineDesc);

        wgpuShaderModuleRelease(rModule);
        wgpuPipelineLayoutRelease(rPipelineLayout);

        WGPUBindGroupEntry rBgEntries[3] = {
            { .binding = 0, .buffer = app->particleBuffer, .offset = 0, .size = sizeof(Particle) * PARTICLE_COUNT },
            { .binding = 1, .buffer = app->simParamsBuffer, .offset = 0, .size = sizeof(SimParams) },
            { .binding = 2, .buffer = app->paletteBuffer, .offset = 0, .size = sizeof(app->palette) }
        };
        WGPUBindGroupDescriptor rBgDesc = { .label = make_str_view("Render BG"), .layout = app->renderBindGroupLayout, .entryCount = 3, .entries = rBgEntries };
        app->renderBindGroup = wgpuDeviceCreateBindGroup(app->device, &rBgDesc);

        // Spawn the palette worker thread (example-04 pattern)
        app->colorMutex = SDL_CreateMutex();
        app->threadRunning = true;
        app->workerThread = SDL_CreateThread(palette_worker_thread, "PaletteWorker", app);

        app->pipelinesInitialized = true;
        printf("[Init] Example 05 initialized (%u particles, compute + threaded palette).\n", PARTICLE_COUNT);
    }

    uint64_t currentTicks = SDL_GetTicks();
    float dt = (currentTicks - app->lastTicks) / 1000.0f;
    if (dt <= 0.0f) dt = 0.016f;
    app->lastTicks = currentTicks;

    app->frameCount++;
    if (currentTicks - app->fpsTimerTicks >= 500) {
        float fps = (app->frameCount * 1000.0f) / (float)(currentTicks - app->fpsTimerTicks);
        char titleBuf[128];
        snprintf(titleBuf, sizeof(titleBuf), "Example 05: %u Particles - FPS: %.1f", PARTICLE_COUNT, fps);
        SDL_SetWindowTitle(app->window, titleBuf);
        app->frameCount = 0;
        app->fpsTimerTicks = currentTicks;
    }

    // Upload the threaded palette if the worker updated it (under mutex, like Example 04).
    SDL_LockMutex(app->colorMutex);
    if (app->colorDirty) {
        wgpuQueueWriteBuffer(app->queue, app->paletteBuffer, 0, app->palette, sizeof(app->palette));
        app->colorDirty = false;
    }
    SDL_UnlockMutex(app->colorMutex);

    float aspect = (float)app->width / (float)app->height;
    SimParams params = { .deltaTime = dt, .aspect = aspect, .sizeScale = SIZE_SCALE, .speedScale = 1.0f, .boundsMin = {-1.0f, -1.0f}, .boundsMax = {1.0f, 1.0f} };
    wgpuQueueWriteBuffer(app->queue, app->simParamsBuffer, 0, &params, sizeof(SimParams));

    WGPUSurfaceTexture surfaceTexture;
    wgpuSurfaceGetCurrentTexture(app->surface, &surfaceTexture);
    if (!surfaceTexture.texture) return SDL_APP_CONTINUE;

    WGPUTextureView renderView = wgpuTextureCreateView(surfaceTexture.texture, NULL);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(app->device, NULL);

    // 1. Compute pass: step particle physics on the GPU.
    WGPUComputePassEncoder computePass = wgpuCommandEncoderBeginComputePass(encoder, NULL);
    wgpuComputePassEncoderSetPipeline(computePass, app->computePipeline);
    wgpuComputePassEncoderSetBindGroup(computePass, 0, app->computeBindGroup, 0, NULL);
    wgpuComputePassEncoderDispatchWorkgroups(computePass, (PARTICLE_COUNT + 63) / 64, 1, 1);
    wgpuComputePassEncoderEnd(computePass);
    wgpuComputePassEncoderRelease(computePass);

    // 2. Render pass: draw instanced circle quads tinted by the threaded palette.
    WGPURenderPassColorAttachment colorAttachment = {
        .view = renderView,
        .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
        .loadOp = WGPULoadOp_Clear,
        .storeOp = WGPUStoreOp_Store,
        .clearValue = (WGPUColor){ 0.03, 0.04, 0.06, 1.0 },
    };
    WGPURenderPassDescriptor renderPassDesc = { .colorAttachmentCount = 1, .colorAttachments = &colorAttachment };
    WGPURenderPassEncoder renderPass = wgpuCommandEncoderBeginRenderPass(encoder, &renderPassDesc);

    wgpuRenderPassEncoderSetPipeline(renderPass, app->renderPipeline);
    wgpuRenderPassEncoderSetBindGroup(renderPass, 0, app->renderBindGroup, 0, NULL);
    wgpuRenderPassEncoderDraw(renderPass, 6, PARTICLE_COUNT, 0, 0);

    wgpuRenderPassEncoderEnd(renderPass);
    wgpuRenderPassEncoderRelease(renderPass);

    WGPUCommandBuffer command = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuQueueSubmit(app->queue, 1, &command);
    wgpuCommandBufferRelease(command);
    wgpuCommandEncoderRelease(encoder);

#ifndef __EMSCRIPTEN__
    wgpuSurfacePresent(app->surface);
#endif

    wgpuTextureViewRelease(renderView);
    wgpuTextureRelease(surfaceTexture.texture);

    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result) {
    if (appstate) {
        AppState* app = (AppState*)appstate;
        printf("[AppQuit] Shutting down Example 05.\n");

        app->threadRunning = false;
        if (app->workerThread) {
            SDL_WaitThread(app->workerThread, NULL);
        }
        if (app->colorMutex) {
            SDL_DestroyMutex(app->colorMutex);
        }

        if (app->computeBindGroup) wgpuBindGroupRelease(app->computeBindGroup);
        if (app->renderBindGroup) wgpuBindGroupRelease(app->renderBindGroup);
        if (app->computeBindGroupLayout) wgpuBindGroupLayoutRelease(app->computeBindGroupLayout);
        if (app->renderBindGroupLayout) wgpuBindGroupLayoutRelease(app->renderBindGroupLayout);
        if (app->computePipeline) wgpuComputePipelineRelease(app->computePipeline);
        if (app->renderPipeline) wgpuRenderPipelineRelease(app->renderPipeline);
        if (app->particleBuffer) wgpuBufferRelease(app->particleBuffer);
        if (app->simParamsBuffer) wgpuBufferRelease(app->simParamsBuffer);
        if (app->paletteBuffer) wgpuBufferRelease(app->paletteBuffer);
        if (app->queue) wgpuQueueRelease(app->queue);
        if (app->surface) {
            wgpuSurfaceUnconfigure(app->surface);
            wgpuSurfaceRelease(app->surface);
        }
        if (app->device) wgpuDeviceRelease(app->device);
        if (app->adapter) wgpuAdapterRelease(app->adapter);
        if (app->instance) wgpuInstanceRelease(app->instance);

#ifdef SDL_PLATFORM_MACOS
        if (app->metalView) SDL_Metal_DestroyView(app->metalView);
#endif
        if (app->window) SDL_DestroyWindow(app->window);
        SDL_free(app);
    }
}
