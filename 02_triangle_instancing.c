#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#ifdef SDL_PLATFORM_MACOS
#include <SDL3/SDL_metal.h>
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
#endif

#define WGSL(...) #__VA_ARGS__
#define INSTANCE_COUNT 100

static inline WGPUStringView make_str_view(const char* s) {
    return (WGPUStringView){ .data = s, .length = s ? strlen(s) : 0 };
}

typedef struct InstanceData {
    float pos[2];       // 0..8   : Center Position (X, Y)
    float rotation;     // 8..12  : Current Rotation Angle in Radians
    float rotSpeed;     // 12..16 : Rotation Angular Speed (rad/s)
    float color[4];     // 16..32 : RGBA Color
    float scale;        // 32..36 : Triangle Scale
    float pad[7];       // 36..64 : Padded to 64 bytes for WGSL alignment
} InstanceData;

typedef struct SimParams {
    float deltaTime;    // 0..4
    float aspect;       // 4..8
    float pad[6];       // 8..32
} SimParams;

typedef struct Vertex {
    float position[2];
    float color[3];
} Vertex;

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
    WGPUBuffer instanceBuffer;
    WGPUBuffer simParamsBuffer;

    WGPUBindGroupLayout renderBindGroupLayout;
    WGPUBindGroup renderBindGroup;
    WGPURenderPipeline renderPipeline;
    WGPUBuffer vertexBuffer;

    uint64_t lastTicks;

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
    printf("[AppInit] Example 02: 100 Rotating Instanced Triangles via Compute Shader...\n");

    AppState* app = (AppState*)SDL_calloc(1, sizeof(AppState));
    if (!app) return SDL_APP_FAILURE;
    *appstate = app;

    app->width = 1280;
    app->height = 720;
    app->lastTicks = SDL_GetTicks();

    app->window = SDL_CreateWindow("Example 02: 100 Rotating Instanced Triangles", app->width, app->height, SDL_WINDOW_RESIZABLE);
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

        // Populate 100 Instanced Triangles in a 10x10 Grid Layout
        InstanceData instances[INSTANCE_COUNT];
        for (int i = 0; i < INSTANCE_COUNT; i++) {
            int col = i % 10;
            int row = i / 10;
            instances[i].pos[0] = (col - 4.5f) * 0.28f;
            instances[i].pos[1] = (row - 4.5f) * 0.18f;
            instances[i].rotation = (i * 0.15f);
            instances[i].rotSpeed = ((i % 2 == 0) ? 1.0f : -1.0f) * (0.8f + 1.2f * ((i % 5) / 5.0f));

            float hue = i / (float)INSTANCE_COUNT;
            instances[i].color[0] = 0.5f + 0.5f * cosf(hue * 6.283f);
            instances[i].color[1] = 0.5f + 0.5f * sinf((hue + 0.33f) * 6.283f);
            instances[i].color[2] = 0.5f + 0.5f * cosf((hue + 0.66f) * 6.283f);
            instances[i].color[3] = 1.0f;
            instances[i].scale = 0.065f;
        }

        WGPUBufferDescriptor instBufDesc = { .label = make_str_view("Instance Storage Buffer"), .usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst, .size = sizeof(instances) };
        app->instanceBuffer = wgpuDeviceCreateBuffer(app->device, &instBufDesc);
        wgpuQueueWriteBuffer(app->queue, app->instanceBuffer, 0, instances, sizeof(instances));

        float aspect = (float)app->width / (float)app->height;
        SimParams params = { .deltaTime = 0.016f, .aspect = aspect };
        WGPUBufferDescriptor simBufDesc = { .label = make_str_view("Sim Params Uniform Buffer"), .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst, .size = sizeof(SimParams) };
        app->simParamsBuffer = wgpuDeviceCreateBuffer(app->device, &simBufDesc);
        wgpuQueueWriteBuffer(app->queue, app->simParamsBuffer, 0, &params, sizeof(SimParams));

        // Compute Pipeline: Updates Rotation Angles Over Time (No Bouncing)
        const char* csCode = WGSL(
            struct InstanceData {
                pos: vec2<f32>,
                rotation: f32,
                rotSpeed: f32,
                color: vec4<f32>,
                scale: f32,
                _p0: f32,
                _p1: vec2<f32>,
                _p2: vec4<f32>,
            };
            struct SimParams { deltaTime: f32, aspect: f32, _p: vec2<f32> };

            @group(0) @binding(0) var<storage, read_write> instances : array<InstanceData>;
            @group(0) @binding(1) var<uniform> params : SimParams;

            @compute @workgroup_size(64)
            fn cs_main(@builtin(global_invocation_id) id : vec3<u32>) {
                let idx = id.x;
                if (idx >= arrayLength(&instances)) { return; }
                var inst = instances[idx];
                // Smooth continuous rotation over time
                inst.rotation += inst.rotSpeed * params.deltaTime;
                instances[idx] = inst;
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
            { .binding = 0, .buffer = app->instanceBuffer, .offset = 0, .size = sizeof(instances) },
            { .binding = 1, .buffer = app->simParamsBuffer, .offset = 0, .size = sizeof(SimParams) }
        };
        WGPUBindGroupDescriptor csBgDesc = { .layout = app->computeBindGroupLayout, .entryCount = 2, .entries = csBgEntries };
        app->computeBindGroup = wgpuDeviceCreateBindGroup(app->device, &csBgDesc);

        // Render Pipeline: Draws 100 Rotating Instanced Triangles
        Vertex baseVertices[3] = {
            { { 0.0f,  1.0f }, { 1.0f, 1.0f, 1.0f } }, // Top
            { {-0.866f, -0.5f }, { 1.0f, 1.0f, 1.0f } }, // Bottom-Left
            { { 0.866f, -0.5f }, { 1.0f, 1.0f, 1.0f } }, // Bottom-Right
        };
        WGPUBufferDescriptor vBufDesc = { .label = make_str_view("Vertex Buffer"), .usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst, .size = sizeof(baseVertices) };
        app->vertexBuffer = wgpuDeviceCreateBuffer(app->device, &vBufDesc);
        wgpuQueueWriteBuffer(app->queue, app->vertexBuffer, 0, baseVertices, sizeof(baseVertices));

        const char* renderCode = WGSL(
            struct InstanceData {
                pos: vec2<f32>,
                rotation: f32,
                rotSpeed: f32,
                color: vec4<f32>,
                scale: f32,
                _p0: f32,
                _p1: vec2<f32>,
                _p2: vec4<f32>,
            };
            struct SimParams { deltaTime: f32, aspect: f32, _p: vec2<f32> };

            @group(0) @binding(0) var<storage, read> instances : array<InstanceData>;
            @group(0) @binding(1) var<uniform> params : SimParams;

            struct VertexOutput {
                @builtin(position) position : vec4<f32>,
                @location(0) color : vec4<f32>,
            };

            @vertex
            fn vs_instanced(@location(0) inPos : vec2<f32>, @builtin(instance_index) i_idx : u32) -> VertexOutput {
                let inst = instances[i_idx];

                // 2D Rotation Matrix Transformation
                let cosA = cos(inst.rotation);
                let sinA = sin(inst.rotation);
                let rotPos = vec2<f32>(
                    inPos.x * cosA - inPos.y * sinA,
                    inPos.x * sinA + inPos.y * cosA
                );

                // Scale, translate, and apply aspect ratio correction
                let scaledPos = rotPos * inst.scale;
                let worldPos = vec2<f32>((inst.pos.x + scaledPos.x) / params.aspect, inst.pos.y + scaledPos.y);

                var out : VertexOutput;
                out.position = vec4<f32>(worldPos, 0.0, 1.0);
                out.color = inst.color;
                return out;
            }

            @fragment
            fn fs_instanced(in : VertexOutput) -> @location(0) vec4<f32> {
                return in.color;
            }
        );

        WGPUShaderSourceWGSL rSource = { .chain = { .sType = WGPUSType_ShaderSourceWGSL }, .code = make_str_view(renderCode) };
        WGPUShaderModuleDescriptor rModDesc = { .nextInChain = (WGPUChainedStruct*)&rSource };
        WGPUShaderModule rModule = wgpuDeviceCreateShaderModule(app->device, &rModDesc);

        WGPUBindGroupLayoutEntry rEntries[2] = {
            { .binding = 0, .visibility = WGPUShaderStage_Vertex, .buffer = { .type = WGPUBufferBindingType_ReadOnlyStorage } },
            { .binding = 1, .visibility = WGPUShaderStage_Vertex, .buffer = { .type = WGPUBufferBindingType_Uniform } }
        };
        WGPUBindGroupLayoutDescriptor rBglDesc = { .entryCount = 2, .entries = rEntries };
        app->renderBindGroupLayout = wgpuDeviceCreateBindGroupLayout(app->device, &rBglDesc);

        WGPUPipelineLayoutDescriptor rPlDesc = { .bindGroupLayoutCount = 1, .bindGroupLayouts = &app->renderBindGroupLayout };
        WGPUPipelineLayout rPipelineLayout = wgpuDeviceCreatePipelineLayout(app->device, &rPlDesc);

        WGPUVertexAttribute vertAttrs[1] = {
            { .format = WGPUVertexFormat_Float32x2, .offset = offsetof(Vertex, position), .shaderLocation = 0 },
        };
        WGPUVertexBufferLayout vertexBufferLayout = { .arrayStride = sizeof(Vertex), .stepMode = WGPUVertexStepMode_Vertex, .attributeCount = 1, .attributes = vertAttrs };
        WGPUColorTargetState colorTarget = { .format = app->surfaceFormat, .writeMask = WGPUColorWriteMask_All };
        WGPUFragmentState rFragmentState = { .module = rModule, .entryPoint = make_str_view("fs_instanced"), .targetCount = 1, .targets = &colorTarget };

        WGPURenderPipelineDescriptor rPipelineDesc = {
            .layout = rPipelineLayout,
            .vertex = { .module = rModule, .entryPoint = make_str_view("vs_instanced"), .bufferCount = 1, .buffers = &vertexBufferLayout },
            .primitive = { .topology = WGPUPrimitiveTopology_TriangleList, .frontFace = WGPUFrontFace_CCW, .cullMode = WGPUCullMode_None },
            .multisample = { .count = 1, .mask = 0xFFFFFFFF },
            .fragment = &rFragmentState,
        };
        app->renderPipeline = wgpuDeviceCreateRenderPipeline(app->device, &rPipelineDesc);

        wgpuShaderModuleRelease(rModule);
        wgpuPipelineLayoutRelease(rPipelineLayout);

        WGPUBindGroupEntry rBgEntries[2] = {
            { .binding = 0, .buffer = app->instanceBuffer, .offset = 0, .size = sizeof(instances) },
            { .binding = 1, .buffer = app->simParamsBuffer, .offset = 0, .size = sizeof(SimParams) }
        };
        WGPUBindGroupDescriptor rBgDesc = { .layout = app->renderBindGroupLayout, .entryCount = 2, .entries = rBgEntries };
        app->renderBindGroup = wgpuDeviceCreateBindGroup(app->device, &rBgDesc);

        app->pipelinesInitialized = true;
        printf("[Init] Example 02 initialized (100 rotating instanced triangles).\n");
    }

    uint64_t currentTicks = SDL_GetTicks();
    float dt = (currentTicks - app->lastTicks) / 1000.0f;
    if (dt <= 0.0f) dt = 0.016f;
    app->lastTicks = currentTicks;

    float aspect = (float)app->width / (float)app->height;
    SimParams params = { .deltaTime = dt, .aspect = aspect };
    wgpuQueueWriteBuffer(app->queue, app->simParamsBuffer, 0, &params, sizeof(SimParams));

    WGPUSurfaceTexture surfaceTexture;
    wgpuSurfaceGetCurrentTexture(app->surface, &surfaceTexture);
    if (!surfaceTexture.texture) return SDL_APP_CONTINUE;

    WGPUTextureView renderView = wgpuTextureCreateView(surfaceTexture.texture, NULL);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(app->device, NULL);

    // 1. Dispatch Compute Pass (Updates 100 Triangle Rotation Angles)
    WGPUComputePassEncoder computePass = wgpuCommandEncoderBeginComputePass(encoder, NULL);
    wgpuComputePassEncoderSetPipeline(computePass, app->computePipeline);
    wgpuComputePassEncoderSetBindGroup(computePass, 0, app->computeBindGroup, 0, NULL);
    wgpuComputePassEncoderDispatchWorkgroups(computePass, (INSTANCE_COUNT + 63) / 64, 1, 1);
    wgpuComputePassEncoderEnd(computePass);
    wgpuComputePassEncoderRelease(computePass);

    // 2. Dispatch Render Pass (Draws 100 Rotating Instanced Triangles)
    WGPURenderPassColorAttachment colorAttachment = {
        .view = renderView,
        .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
        .loadOp = WGPULoadOp_Clear,
        .storeOp = WGPUStoreOp_Store,
        .clearValue = (WGPUColor){ 0.08, 0.09, 0.12, 1.0 },
    };
    WGPURenderPassDescriptor renderPassDesc = { .colorAttachmentCount = 1, .colorAttachments = &colorAttachment };
    WGPURenderPassEncoder renderPass = wgpuCommandEncoderBeginRenderPass(encoder, &renderPassDesc);

    wgpuRenderPassEncoderSetPipeline(renderPass, app->renderPipeline);
    wgpuRenderPassEncoderSetBindGroup(renderPass, 0, app->renderBindGroup, 0, NULL);
    wgpuRenderPassEncoderSetVertexBuffer(renderPass, 0, app->vertexBuffer, 0, 3 * sizeof(Vertex));
    wgpuRenderPassEncoderDraw(renderPass, 3, INSTANCE_COUNT, 0, 0);

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
        printf("[AppQuit] Shutting down Example 02.\n");

        if (app->computeBindGroup) wgpuBindGroupRelease(app->computeBindGroup);
        if (app->renderBindGroup) wgpuBindGroupRelease(app->renderBindGroup);
        if (app->computeBindGroupLayout) wgpuBindGroupLayoutRelease(app->computeBindGroupLayout);
        if (app->renderBindGroupLayout) wgpuBindGroupLayoutRelease(app->renderBindGroupLayout);
        if (app->computePipeline) wgpuComputePipelineRelease(app->computePipeline);
        if (app->renderPipeline) wgpuRenderPipelineRelease(app->renderPipeline);
        if (app->instanceBuffer) wgpuBufferRelease(app->instanceBuffer);
        if (app->simParamsBuffer) wgpuBufferRelease(app->simParamsBuffer);
        if (app->vertexBuffer) wgpuBufferRelease(app->vertexBuffer);
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
