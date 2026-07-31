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

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#include <webgpu/webgpu.h>
#else
#include <webgpu/webgpu.h>
#include <webgpu/wgpu.h>
#define EMSCRIPTEN_KEEPALIVE
#endif

#define WGSL(...) #__VA_ARGS__
#define MAX_PARTICLE_COUNT 250000

static uint32_t g_activeParticleCount = 10000;
static float g_particleSizeScale = 1.0f;
static float g_particleSpeedScale = 1.0f;

EMSCRIPTEN_KEEPALIVE void set_active_particle_count(int count) {
    if (count >= 1000 && count <= MAX_PARTICLE_COUNT) {
        g_activeParticleCount = (uint32_t)count;
    }
}

EMSCRIPTEN_KEEPALIVE void set_particle_size_scale(float scale) {
    if (scale >= 0.05f && scale <= 200.0f) {
        g_particleSizeScale = scale;
    }
}

EMSCRIPTEN_KEEPALIVE void set_particle_speed_scale(float scale) {
    if (scale >= 0.05f && scale <= 20.0f) {
        g_particleSpeedScale = scale;
    }
}

static inline WGPUStringView make_str_view(const char* s) {
    return (WGPUStringView){ .data = s, .length = s ? strlen(s) : 0 };
}

typedef struct Particle {
    float pos[2];     // 0..8
    float vel[2];     // 8..16
    float color[4];   // 16..32
    float radius;     // 32..36
    float pad[7];     // 36..64 (Padded to 64 bytes for WGSL alignment)
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

    WGPUBindGroupLayout particleRenderBindGroupLayout;
    WGPUBindGroup particleRenderBindGroup;
    WGPURenderPipeline particleRenderPipeline;

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

SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[]) {
    printf("[AppInit] Example 03: Interactive Bouncing Particles (Up to 250,000)...\n");

    AppState* app = (AppState*)SDL_calloc(1, sizeof(AppState));
    if (!app) return SDL_APP_FAILURE;
    *appstate = app;

    app->width = 1280;
    app->height = 720;
    app->lastTicks = SDL_GetTicks();
    app->fpsTimerTicks = app->lastTicks;

    app->window = SDL_CreateWindow("Example 03: Up to 250,000 Particles", app->width, app->height, SDL_WINDOW_RESIZABLE);
    if (!app->window) return SDL_APP_FAILURE;

    app->instance = wgpuCreateInstance(NULL);
    if (!app->instance) return SDL_APP_FAILURE;

#ifdef __EMSCRIPTEN__
    WGPUEmscriptenSurfaceSourceCanvasHTMLSelector canvasDesc = {
        .chain = { .next = NULL, .sType = WGPUSType_EmscriptenSurfaceSourceCanvasHTMLSelector },
        .selector = make_str_view("#canvas")
    };
    WGPUSurfaceDescriptor surfaceDesc = { .nextInChain = (WGPUChainedStruct*)&canvasDesc };
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
    WGPUSurfaceDescriptor surfaceDesc = { .nextInChain = (WGPUChainedStruct*)&metalDesc };
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
    if (event->type == SDL_EVENT_QUIT) return SDL_APP_SUCCESS;
    else if (event->type == SDL_EVENT_WINDOW_RESIZED || event->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
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

        // Allocate Storage for 250,000 Particles (16MB VRAM)
        srand(1337);
        Particle* particles = (Particle*)malloc(sizeof(Particle) * MAX_PARTICLE_COUNT);
        if (particles) {
            for (int i = 0; i < MAX_PARTICLE_COUNT; i++) {
                float rx = (((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f) * aspect;
                float ry = (((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f);

                float angle = ((float)rand() / (float)RAND_MAX) * 6.2831853f;
                float speed = 0.04f + 0.16f * ((float)rand() / (float)RAND_MAX);
                
                // Doubled Base Circle Radius (0.0048 .. 0.0080)
                float radius = 0.0048f + 0.0032f * ((float)rand() / (float)RAND_MAX);

                particles[i].pos[0] = rx;
                particles[i].pos[1] = ry;
                particles[i].vel[0] = cosf(angle) * speed;
                particles[i].vel[1] = sinf(angle) * speed;

                // Ultra-Simple 3-Line Vibrant Color Generator (100% Saturation & Brightness)
                int p = rand() % 3;
                particles[i].color[p] = 1.0f;
                particles[i].color[(p + 1) % 3] = (float)rand() / (float)RAND_MAX;
                particles[i].color[(p + 2) % 3] = 0.0f;
                particles[i].color[3] = 0.85f;
                particles[i].radius = radius;
            }

            WGPUBufferDescriptor particleBufferDesc = { .label = make_str_view("Particle Storage Buffer"), .usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst, .size = sizeof(Particle) * MAX_PARTICLE_COUNT };
            app->particleBuffer = wgpuDeviceCreateBuffer(app->device, &particleBufferDesc);
            wgpuQueueWriteBuffer(app->queue, app->particleBuffer, 0, particles, sizeof(Particle) * MAX_PARTICLE_COUNT);
            free(particles);
        }

        WGPUBufferDescriptor simBufferDesc = { .label = make_str_view("Sim Params Uniform Buffer"), .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst, .size = sizeof(SimParams) };
        app->simParamsBuffer = wgpuDeviceCreateBuffer(app->device, &simBufferDesc);

        // Compute Pipeline (Particle Physics Simulation)
        const char* csCode = WGSL(
            struct Particle { pos: vec2<f32>, vel: vec2<f32>, color: vec4<f32>, radius: f32, _p0: f32, _p1: vec2<f32>, _p2: vec4<f32> };
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
        WGPUShaderModuleDescriptor csModDesc = { .nextInChain = (WGPUChainedStruct*)&csSource };
        WGPUShaderModule csModule = wgpuDeviceCreateShaderModule(app->device, &csModDesc);

        WGPUBindGroupLayoutEntry csEntries[2] = {
            { .binding = 0, .visibility = WGPUShaderStage_Compute, .buffer = { .type = WGPUBufferBindingType_Storage } },
            { .binding = 1, .visibility = WGPUShaderStage_Compute, .buffer = { .type = WGPUBufferBindingType_Uniform } }
        };
        WGPUBindGroupLayoutDescriptor csBglDesc = { .entryCount = 2, .entries = csEntries };
        app->computeBindGroupLayout = wgpuDeviceCreateBindGroupLayout(app->device, &csBglDesc);

        WGPUPipelineLayoutDescriptor csPlDesc = { .bindGroupLayoutCount = 1, .bindGroupLayouts = &app->computeBindGroupLayout };
        WGPUPipelineLayout csPipelineLayout = wgpuDeviceCreatePipelineLayout(app->device, &csPlDesc);

        WGPUComputePipelineDescriptor csPipelineDesc = { .layout = csPipelineLayout, .compute = { .module = csModule, .entryPoint = make_str_view("cs_main") } };
        app->computePipeline = wgpuDeviceCreateComputePipeline(app->device, &csPipelineDesc);

        wgpuShaderModuleRelease(csModule);
        wgpuPipelineLayoutRelease(csPipelineLayout);

        WGPUBindGroupEntry csBgEntries[2] = {
            { .binding = 0, .buffer = app->particleBuffer, .offset = 0, .size = sizeof(Particle) * MAX_PARTICLE_COUNT },
            { .binding = 1, .buffer = app->simParamsBuffer, .offset = 0, .size = sizeof(SimParams) }
        };
        WGPUBindGroupDescriptor csBgDesc = { .layout = app->computeBindGroupLayout, .entryCount = 2, .entries = csBgEntries };
        app->computeBindGroup = wgpuDeviceCreateBindGroup(app->device, &csBgDesc);

        // Render Pipeline (Particle Circles)
        const char* particleRenderCode = WGSL(
            struct Particle { pos: vec2<f32>, vel: vec2<f32>, color: vec4<f32>, radius: f32, _p0: f32, _p1: vec2<f32>, _p2: vec4<f32> };
            struct SimParams { deltaTime: f32, aspect: f32, sizeScale: f32, speedScale: f32, boundsMin: vec2<f32>, boundsMax: vec2<f32> };

            @group(0) @binding(0) var<storage, read> particles : array<Particle>;
            @group(0) @binding(1) var<uniform> params : SimParams;

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
                out.color = p.color;
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

        WGPUShaderSourceWGSL prSource = { .chain = { .sType = WGPUSType_ShaderSourceWGSL }, .code = make_str_view(particleRenderCode) };
        WGPUShaderModuleDescriptor prModDesc = { .nextInChain = (WGPUChainedStruct*)&prSource };
        WGPUShaderModule prModule = wgpuDeviceCreateShaderModule(app->device, &prModDesc);

        WGPUBindGroupLayoutEntry prEntries[2] = {
            { .binding = 0, .visibility = WGPUShaderStage_Vertex, .buffer = { .type = WGPUBufferBindingType_ReadOnlyStorage } },
            { .binding = 1, .visibility = WGPUShaderStage_Vertex, .buffer = { .type = WGPUBufferBindingType_Uniform } }
        };
        WGPUBindGroupLayoutDescriptor prBglDesc = { .entryCount = 2, .entries = prEntries };
        app->particleRenderBindGroupLayout = wgpuDeviceCreateBindGroupLayout(app->device, &prBglDesc);

        WGPUPipelineLayoutDescriptor prPlDesc = { .bindGroupLayoutCount = 1, .bindGroupLayouts = &app->particleRenderBindGroupLayout };
        WGPUPipelineLayout prPipelineLayout = wgpuDeviceCreatePipelineLayout(app->device, &prPlDesc);

        WGPUBlendState alphaBlend = {
            .color = { .operation = WGPUBlendOperation_Add, .srcFactor = WGPUBlendFactor_SrcAlpha, .dstFactor = WGPUBlendFactor_OneMinusSrcAlpha },
            .alpha = { .operation = WGPUBlendOperation_Add, .srcFactor = WGPUBlendFactor_One, .dstFactor = WGPUBlendFactor_OneMinusSrcAlpha }
        };
        WGPUColorTargetState prColorTarget = { .format = app->surfaceFormat, .blend = &alphaBlend, .writeMask = WGPUColorWriteMask_All };
        WGPUFragmentState prFragmentState = { .module = prModule, .entryPoint = make_str_view("fs_particle"), .targetCount = 1, .targets = &prColorTarget };

        WGPURenderPipelineDescriptor prPipelineDesc = {
            .layout = prPipelineLayout,
            .vertex = { .module = prModule, .entryPoint = make_str_view("vs_particle"), .bufferCount = 0 },
            .primitive = { .topology = WGPUPrimitiveTopology_TriangleList, .frontFace = WGPUFrontFace_CCW, .cullMode = WGPUCullMode_None },
            .multisample = { .count = 1, .mask = 0xFFFFFFFF },
            .fragment = &prFragmentState,
        };
        app->particleRenderPipeline = wgpuDeviceCreateRenderPipeline(app->device, &prPipelineDesc);

        wgpuShaderModuleRelease(prModule);
        wgpuPipelineLayoutRelease(prPipelineLayout);

        WGPUBindGroupEntry prBgEntries[2] = {
            { .binding = 0, .buffer = app->particleBuffer, .offset = 0, .size = sizeof(Particle) * MAX_PARTICLE_COUNT },
            { .binding = 1, .buffer = app->simParamsBuffer, .offset = 0, .size = sizeof(SimParams) }
        };
        WGPUBindGroupDescriptor prBgDesc = { .layout = app->particleRenderBindGroupLayout, .entryCount = 2, .entries = prBgEntries };
        app->particleRenderBindGroup = wgpuDeviceCreateBindGroup(app->device, &prBgDesc);

        app->pipelinesInitialized = true;
        printf("[Init] Example 03 initialized (Up to 250,000 particles supported).\n");
    }

    uint64_t currentTicks = SDL_GetTicks();
    float dt = (currentTicks - app->lastTicks) / 1000.0f;
    if (dt <= 0.0f) dt = 0.016f;
    app->lastTicks = currentTicks;

    uint32_t currentParticleCount = g_activeParticleCount;

    app->frameCount++;
    if (currentTicks - app->fpsTimerTicks >= 500) {
        float fps = (app->frameCount * 1000.0f) / (float)(currentTicks - app->fpsTimerTicks);
        char titleBuf[128];
        snprintf(titleBuf, sizeof(titleBuf), "Example 03: %u Particles - FPS: %.1f", currentParticleCount, fps);
        SDL_SetWindowTitle(app->window, titleBuf);

#ifdef __EMSCRIPTEN__
        EM_ASM_({
            var statusEl = document.getElementById('status');
            if (statusEl) {
                statusEl.innerText = $0.toLocaleString() + ' Particles | Running at ' + $1.toFixed(1) + ' FPS';
            }
        }, currentParticleCount, fps);
#endif

        app->frameCount = 0;
        app->fpsTimerTicks = currentTicks;
    }

    float aspect = (float)app->width / (float)app->height;
    SimParams params = { .deltaTime = dt, .aspect = aspect, .sizeScale = g_particleSizeScale, .speedScale = g_particleSpeedScale, .boundsMin = {-1.0f, -1.0f}, .boundsMax = {1.0f, 1.0f} };
    wgpuQueueWriteBuffer(app->queue, app->simParamsBuffer, 0, &params, sizeof(SimParams));

    WGPUSurfaceTexture surfaceTexture;
    wgpuSurfaceGetCurrentTexture(app->surface, &surfaceTexture);
    if (!surfaceTexture.texture) return SDL_APP_CONTINUE;

    WGPUTextureView renderView = wgpuTextureCreateView(surfaceTexture.texture, NULL);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(app->device, NULL);

    // 1. Dispatch Compute Pass (Simulates active particles)
    WGPUComputePassEncoder computePass = wgpuCommandEncoderBeginComputePass(encoder, NULL);
    wgpuComputePassEncoderSetPipeline(computePass, app->computePipeline);
    wgpuComputePassEncoderSetBindGroup(computePass, 0, app->computeBindGroup, 0, NULL);
    wgpuComputePassEncoderDispatchWorkgroups(computePass, (currentParticleCount + 63) / 64, 1, 1);
    wgpuComputePassEncoderEnd(computePass);
    wgpuComputePassEncoderRelease(computePass);

    // 2. Dispatch Render Pass (Draws active particle circles)
    WGPURenderPassColorAttachment colorAttachment = {
        .view = renderView,
        .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
        .loadOp = WGPULoadOp_Clear,
        .storeOp = WGPUStoreOp_Store,
        .clearValue = (WGPUColor){ 0.03, 0.04, 0.06, 1.0 },
    };
    WGPURenderPassDescriptor renderPassDesc = { .colorAttachmentCount = 1, .colorAttachments = &colorAttachment };
    WGPURenderPassEncoder renderPass = wgpuCommandEncoderBeginRenderPass(encoder, &renderPassDesc);
    
    wgpuRenderPassEncoderSetPipeline(renderPass, app->particleRenderPipeline);
    wgpuRenderPassEncoderSetBindGroup(renderPass, 0, app->particleRenderBindGroup, 0, NULL);
    wgpuRenderPassEncoderDraw(renderPass, 6, currentParticleCount, 0, 0);

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
        printf("[AppQuit] Shutting down Example 03.\n");

        if (app->computeBindGroup) wgpuBindGroupRelease(app->computeBindGroup);
        if (app->particleRenderBindGroup) wgpuBindGroupRelease(app->particleRenderBindGroup);
        if (app->computeBindGroupLayout) wgpuBindGroupLayoutRelease(app->computeBindGroupLayout);
        if (app->particleRenderBindGroupLayout) wgpuBindGroupLayoutRelease(app->particleRenderBindGroupLayout);
        if (app->computePipeline) wgpuComputePipelineRelease(app->computePipeline);
        if (app->particleRenderPipeline) wgpuRenderPipelineRelease(app->particleRenderPipeline);
        if (app->particleBuffer) wgpuBufferRelease(app->particleBuffer);
        if (app->simParamsBuffer) wgpuBufferRelease(app->simParamsBuffer);
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
