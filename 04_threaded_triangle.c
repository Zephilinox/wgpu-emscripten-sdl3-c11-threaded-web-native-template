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

static inline WGPUStringView make_str_view(const char* s) {
    return (WGPUStringView){ .data = s, .length = s ? strlen(s) : 0 };
}

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

    WGPURenderPipeline triangleRenderPipeline;
    WGPUBuffer triangleVertexBuffer;
    Vertex triangleVertices[3];

    SDL_Thread* workerThread;
    SDL_Mutex* colorMutex;
    bool threadRunning;
    bool colorDirty;

#ifdef SDL_PLATFORM_MACOS
    SDL_MetalView metalView;
#endif
} AppState;

static int SDLCALL color_worker_thread(void* data) {
    AppState* app = (AppState*)data;
    printf("[WorkerThread] Background color thread started.\n");

    float time = 0.0f;
    while (app->threadRunning) {
        time += 0.05f;
        float r = (sinf(time) + 1.0f) * 0.5f;
        float g = (sinf(time + 2.094f) + 1.0f) * 0.5f;
        float b = (sinf(time + 4.188f) + 1.0f) * 0.5f;

        SDL_LockMutex(app->colorMutex);
        app->triangleVertices[0].color[0] = r;
        app->triangleVertices[0].color[1] = g;
        app->triangleVertices[0].color[2] = b;

        app->triangleVertices[1].color[0] = g;
        app->triangleVertices[1].color[1] = b;
        app->triangleVertices[1].color[2] = r;

        app->triangleVertices[2].color[0] = b;
        app->triangleVertices[2].color[1] = r;
        app->triangleVertices[2].color[2] = g;
        app->colorDirty = true;
        SDL_UnlockMutex(app->colorMutex);

        SDL_Delay(16); // Update at ~60 Hz in background
    }

    printf("[WorkerThread] Background color thread exiting.\n");
    return 0;
}

static void handle_adapter_request(WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message, void* userdata1, void* userdata2) {
    AppState* app = (AppState*)userdata1;
    if (status == WGPURequestAdapterStatus_Success) {
        app->adapter = adapter;
        printf("[WebGPU] Adapter acquired successfully.\n");
    }
}

static void handle_device_request(WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message, void* userdata1, void* userdata2) {
    AppState* app = (AppState*)userdata1;
    if (status == WGPURequestDeviceStatus_Success) {
        app->device = device;
        printf("[WebGPU] Device created successfully.\n");
    }
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[]) {
    printf("[AppInit] Example 04: Threaded WebGPU + SDL3 Triangle...\n");

    AppState* app = (AppState*)SDL_calloc(1, sizeof(AppState));
    if (!app) return SDL_APP_FAILURE;
    *appstate = app;

    app->width = 1280;
    app->height = 720;

    app->window = SDL_CreateWindow("Example 04: Threaded WebGPU Triangle", app->width, app->height, SDL_WINDOW_RESIZABLE);
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

        app->triangleVertices[0] = (Vertex){ { 0.0f,  0.5f }, { 1.0f, 0.0f, 0.0f } };
        app->triangleVertices[1] = (Vertex){ {-0.5f, -0.5f }, { 0.0f, 1.0f, 0.0f } };
        app->triangleVertices[2] = (Vertex){ { 0.5f, -0.5f }, { 0.0f, 0.0f, 1.0f } };

        WGPUBufferDescriptor triDesc = { .label = make_str_view("Vertex Buffer"), .usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst, .size = sizeof(app->triangleVertices) };
        app->triangleVertexBuffer = wgpuDeviceCreateBuffer(app->device, &triDesc);
        wgpuQueueWriteBuffer(app->queue, app->triangleVertexBuffer, 0, app->triangleVertices, sizeof(app->triangleVertices));

        const char* triCode = WGSL(
            struct VertexOutput {
                @builtin(position) position : vec4<f32>,
                @location(0) color : vec3<f32>,
            };

            @vertex
            fn vs_main(@location(0) inPos : vec2<f32>, @location(1) inColor : vec3<f32>) -> VertexOutput {
                var out : VertexOutput;
                out.position = vec4<f32>(inPos, 0.0, 1.0);
                out.color = inColor;
                return out;
            }

            @fragment
            fn fs_main(in : VertexOutput) -> @location(0) vec4<f32> {
                return vec4<f32>(in.color, 1.0);
            }
        );

        WGPUShaderSourceWGSL triSource = { .chain = { .sType = WGPUSType_ShaderSourceWGSL }, .code = make_str_view(triCode) };
        WGPUShaderModuleDescriptor triModDesc = { .nextInChain = (WGPUChainedStruct*)&triSource, .label = make_str_view("Triangle Shader Module") };
        WGPUShaderModule triModule = wgpuDeviceCreateShaderModule(app->device, &triModDesc);

        WGPUVertexAttribute vertAttrs[2] = {
            { .format = WGPUVertexFormat_Float32x2, .offset = offsetof(Vertex, position), .shaderLocation = 0 },
            { .format = WGPUVertexFormat_Float32x3, .offset = offsetof(Vertex, color), .shaderLocation = 1 },
        };
        WGPUVertexBufferLayout vertexBufferLayout = { .arrayStride = sizeof(Vertex), .stepMode = WGPUVertexStepMode_Vertex, .attributeCount = 2, .attributes = vertAttrs };
        WGPUColorTargetState colorTarget = { .format = app->surfaceFormat, .writeMask = WGPUColorWriteMask_All };
        WGPUFragmentState triangleFragmentState = { .module = triModule, .entryPoint = make_str_view("fs_main"), .targetCount = 1, .targets = &colorTarget };
        WGPUPipelineLayoutDescriptor triPlDesc = { .bindGroupLayoutCount = 0, .bindGroupLayouts = NULL };
        WGPUPipelineLayout trianglePipelineLayout = wgpuDeviceCreatePipelineLayout(app->device, &triPlDesc);

        WGPURenderPipelineDescriptor triPipelineDesc = {
            .layout = trianglePipelineLayout,
            .vertex = { .module = triModule, .entryPoint = make_str_view("vs_main"), .bufferCount = 1, .buffers = &vertexBufferLayout },
            .primitive = { .topology = WGPUPrimitiveTopology_TriangleList, .frontFace = WGPUFrontFace_CCW, .cullMode = WGPUCullMode_None },
            .multisample = { .count = 1, .mask = 0xFFFFFFFF },
            .fragment = &triangleFragmentState,
        };
        app->triangleRenderPipeline = wgpuDeviceCreateRenderPipeline(app->device, &triPipelineDesc);

        // Pipeline holds its own references — release intermediates
        wgpuShaderModuleRelease(triModule);
        wgpuPipelineLayoutRelease(trianglePipelineLayout);

        // Spawn Background Thread for Color Updates
        app->colorMutex = SDL_CreateMutex();
        app->threadRunning = true;
        app->workerThread = SDL_CreateThread(color_worker_thread, "ColorWorker", app);

        app->pipelinesInitialized = true;
        printf("[Init] Example 04 (Threaded Triangle) initialized.\n");
    }

    SDL_LockMutex(app->colorMutex);
    if (app->colorDirty) {
        wgpuQueueWriteBuffer(app->queue, app->triangleVertexBuffer, 0, app->triangleVertices, sizeof(app->triangleVertices));
        app->colorDirty = false;
    }
    SDL_UnlockMutex(app->colorMutex);

    WGPUSurfaceTexture surfaceTexture;
    wgpuSurfaceGetCurrentTexture(app->surface, &surfaceTexture);
    if (!surfaceTexture.texture) return SDL_APP_CONTINUE;

    WGPUTextureView renderView = wgpuTextureCreateView(surfaceTexture.texture, NULL);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(app->device, NULL);

    WGPURenderPassColorAttachment colorAttachment = {
        .view = renderView,
        .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
        .loadOp = WGPULoadOp_Clear,
        .storeOp = WGPUStoreOp_Store,
        .clearValue = (WGPUColor){ 0.08, 0.09, 0.12, 1.0 },
    };
    WGPURenderPassDescriptor renderPassDesc = { .colorAttachmentCount = 1, .colorAttachments = &colorAttachment };
    WGPURenderPassEncoder renderPass = wgpuCommandEncoderBeginRenderPass(encoder, &renderPassDesc);
    
    wgpuRenderPassEncoderSetPipeline(renderPass, app->triangleRenderPipeline);
    wgpuRenderPassEncoderSetVertexBuffer(renderPass, 0, app->triangleVertexBuffer, 0, 3 * sizeof(Vertex));
    wgpuRenderPassEncoderDraw(renderPass, 3, 1, 0, 0);

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
        printf("[AppQuit] Shutting down Example 04.\n");
        app->threadRunning = false;
        if (app->workerThread) {
            SDL_WaitThread(app->workerThread, NULL);
        }
        if (app->colorMutex) {
            SDL_DestroyMutex(app->colorMutex);
        }

        if (app->triangleVertexBuffer) wgpuBufferRelease(app->triangleVertexBuffer);
        if (app->triangleRenderPipeline) wgpuRenderPipelineRelease(app->triangleRenderPipeline);
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
