// 帧管线分阶段性能基准（当前版本，使用 UIApplication::GetFrameStats 埋点）。
//
// 用法：
//   ./build/bin/ui_frame_bench            # 无头模式的管线/上传/布局/读回/呈现(mipmap)测量
//   ./build/bin/ui_frame_bench --window   # 额外跑 120 帧窗口模式，测 present 阶段
#include "bench_scene.h"
#include "ui/Texture.h"

#include <GLES3/gl3.h>
#include <opencv2/imgproc.hpp>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>

using Clock = std::chrono::steady_clock;

static double Ms(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}
static void Report(const char* key, double value) {
    std::printf("%s=%.3f\n", key, value);
}

int main(int argc, char** argv) {
    bool window = false;
    bool windowOnly = false;
    bool resize = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--window") == 0) window = true;
        if (std::strcmp(argv[i], "--window-only") == 0) windowOnly = true;
        if (std::strcmp(argv[i], "--resize") == 0) resize = true;
    }

    // 窗口专用模式：进程内不要先创建 EGL 无头上下文（部分 Mesa 环境不允许与 GLX 混用）。
    if (windowOnly) {
        int winW = 1280, winH = 720;
        for (int i = 1; i < argc; ++i) {
            if (std::strcmp(argv[i], "--window-size") == 0 && i + 1 < argc) {
                std::sscanf(argv[i + 1], "%dx%d", &winW, &winH);
            }
        }
        ui::UIApplication::Config wcfg;
        wcfg.renderWidth = 1920;
        wcfg.renderHeight = 1080;
        wcfg.showWindow = true;
        wcfg.windowWidth = winW;
        wcfg.windowHeight = winH;
        ui::UIApplication wapp;
        if (!wapp.Init(wcfg)) { std::printf("window_init_failed=1\n"); return 2; }
        const GLubyte* r = glGetString(GL_RENDERER);
        std::printf("gl_renderer=%s\n", r ? reinterpret_cast<const char*>(r) : "?");
        bench::BuildScene(wapp, 1920, 1080);
        for (int i = 0; i < 20; ++i) wapp.RunFrame();
        const int n = 120;
        double total = 0, render = 0, present = 0;
        for (int i = 0; i < n; ++i) {
            auto t0 = Clock::now();
            wapp.RunFrame();
            total += Ms(t0);
            render += wapp.GetFrameStats().renderMs;
            present += wapp.GetFrameStats().presentMs;
        }
        std::printf("window_size=%dx%d\n", winW, winH);
        Report("window_runframe_ms", total / n);
        Report("window_render_ms", render / n);
        Report("window_present_ms", present / n);
        return 0;
    }

    ui::UIApplication::Config cfg;
    cfg.renderWidth = 1920;
    cfg.renderHeight = 1080;
    cfg.showWindow = false;
    cfg.windowWidth = 1280;
    cfg.windowHeight = 720;
    ui::UIApplication app;
    if (!app.Init(cfg)) { std::printf("init failed\n"); return 2; }
    const GLubyte* glRenderer = glGetString(GL_RENDERER);
    std::printf("gl_renderer=%s\n", glRenderer ? reinterpret_cast<const char*>(glRenderer) : "?");
    std::printf("render_size=%dx%d\n", cfg.renderWidth, cfg.renderHeight);

    auto scene = bench::BuildScene(app, 1920, 1080);

    const int kWarmup = 10;
    const int kFrames = 60;

    // ---------- 1. 帧管线分阶段 ----------
    for (int i = 0; i < kWarmup; ++i) app.RunFrame();
    double runTotal = 0, buildInput = 0, layout = 0, dispatch = 0, render = 0;
    double drawCalls = 0, quads = 0, texBinds = 0, scissorChanges = 0, scissorSkips = 0;
    double vertices = 0, layoutCalls = 0;
    for (int i = 0; i < kFrames; ++i) {
        auto t0 = Clock::now();
        app.RunFrame();
        runTotal += Ms(t0);
        const auto& s = app.GetFrameStats();
        buildInput += s.buildInputMs;
        layout += s.layoutMs;
        dispatch += s.dispatchMs;
        render += s.renderMs;
        layoutCalls += s.layoutCalls;
        drawCalls += s.drawCalls;
        quads += s.quads;
        texBinds += s.textureBinds;
        scissorChanges += s.scissorChanges;
        scissorSkips += s.scissorSkips;
        vertices += (double)s.vertices;
    }
    const double avg = (double)kFrames;
    Report("runframe_ms", runTotal / avg);
    Report("buildinput_ms", buildInput / avg);
    Report("layout_ms", layout / avg);
    Report("dispatch_ms", dispatch / avg);
    Report("render_ms", render / avg);
    Report("layout_calls", layoutCalls / avg);
    Report("drawcalls", drawCalls / avg);
    Report("quads", quads / avg);
    Report("texturebinds", texBinds / avg);
    Report("scissor_changes", scissorChanges / avg);
    Report("scissor_skips", scissorSkips / avg);
    Report("vertices", vertices / avg);

    // ---------- 2. 只渲染（不读回） ----------
    for (int i = 0; i < kWarmup; ++i) app.RenderOffscreen();
    double renderOnly = 0;
    for (int i = 0; i < kFrames; ++i) {
        auto t0 = Clock::now();
        app.RenderOffscreen();
        renderOnly += Ms(t0);
    }
    Report("render_only_ms", renderOnly / avg);
    Report("render_only_layout_calls", (double)app.GetFrameStats().layoutCalls);

    // ---------- 3. 渲染 + 读回（GetFrameBGRA 触发懒读回） ----------
    for (int i = 0; i < kWarmup; ++i) { app.RenderOffscreen(); app.GetFrameBGRA(); }
    double renderCapture = 0, captureSum = 0;
    for (int i = 0; i < kFrames; ++i) {
        auto t0 = Clock::now();
        app.RenderOffscreen();
        app.GetFrameBGRA();
        renderCapture += Ms(t0);
        captureSum += app.GetFrameStats().captureMs;
    }
    Report("render_capture_ms", renderCapture / avg);
    Report("capture_ms", captureSum / avg);

    // ---------- 4. 仅读回缓存（第二次 GetFrameBGRA，不重新 glReadPixels） ----------
    app.RenderOffscreen();
    app.GetFrameBGRA();
    double cachedClone = 0;
    for (int i = 0; i < kFrames; ++i) {
        auto t0 = Clock::now();
        app.GetFrameBGRA();
        cachedClone += Ms(t0);
    }
    Report("cached_getframe_ms", cachedClone / avg);

    // ---------- 5. 图像上传（SetImage / UpdateImage） ----------
    const int sizes[][2] = {{640, 480}, {1280, 720}, {1920, 1080}, {3840, 2160}};
    for (auto& s : sizes) {
        cv::Mat img = bench::MakeImage(s[0], s[1]);
        const int n = 10;
        auto t0 = Clock::now();
        for (int k = 0; k < n; ++k) scene.image->SetImage(img);
        char key[64];
        std::snprintf(key, sizeof(key), "setimage_%dx%d_ms", s[0], s[1]);
        Report(key, Ms(t0) / n);

        t0 = Clock::now();
        for (int k = 0; k < n; ++k) scene.image->UpdateImage(img);
        std::snprintf(key, sizeof(key), "updateimage_%dx%d_ms", s[0], s[1]);
        Report(key, Ms(t0) / n);
    }

    // ---------- 6. 流式帧：逐帧更新图像 + RunFrame ----------
    for (int i = 0; i < kWarmup; ++i) { scene.image->SetImage(scene.streamFrame); app.RunFrame(); }
    double streamFrame = 0;
    for (int i = 0; i < kFrames; ++i) {
        scene.streamFrame.setTo(cv::Scalar(i % 2 ? 40 : 200, 120, 90));
        auto t0 = Clock::now();
        scene.image->SetImage(scene.streamFrame);
        app.RunFrame();
        streamFrame += Ms(t0);
    }
    Report("stream_setimage_frame_ms", streamFrame / avg);

    for (int i = 0; i < kWarmup; ++i) { scene.image->UpdateImage(scene.streamFrame); app.RunFrame(); }
    double streamUpdate = 0;
    for (int i = 0; i < kFrames; ++i) {
        scene.streamFrame.setTo(cv::Scalar(i % 2 ? 40 : 200, 120, 90));
        auto t0 = Clock::now();
        scene.image->UpdateImage(scene.streamFrame);
        app.RunFrame();
        streamUpdate += Ms(t0);
    }
    Report("stream_updateimage_frame_ms", streamUpdate / avg);

    // ---------- 7. 直接布局成本（整棵树一次） ----------
    const int nLayout = 200;
    double layoutOnce = 0;
    for (int i = 0; i < nLayout; ++i) {
        auto t0 = Clock::now();
        scene.root->Layout({1920.0f, 1080.0f}, {}, {0, 0, 1920, 1080}, {});
        layoutOnce += Ms(t0);
    }
    Report("layout_once_ms", layoutOnce / nLayout);

    // ---------- 8. mipmap 生成成本（Present 缩小呈现时每帧会做） ----------
    ui::Texture tex;
    if (tex.CreateEmpty(1920, 1080)) {
        glFinish();
        auto t0 = Clock::now();
        const int n = 20;
        for (int i = 0; i < n; ++i) {
            tex.GenerateMipmaps();
            glFinish();
        }
        Report("mipmap_1080p_ms", Ms(t0) / n);
    }

    // ---------- 9. 可选：窗口模式 present ----------
    if (window) {
        ui::UIApplication::Config wcfg = cfg;
        wcfg.showWindow = true;
        ui::UIApplication wapp;
        if (wapp.Init(wcfg)) {
            bench::BuildScene(wapp, 1920, 1080);
            for (int i = 0; i < 20; ++i) wapp.RunFrame();
            double present = 0, frameTotal = 0;
            for (int i = 0; i < 120; ++i) {
                auto t0 = Clock::now();
                wapp.RunFrame();
                frameTotal += Ms(t0);
                present += wapp.GetFrameStats().presentMs;
            }
            Report("window_runframe_ms", frameTotal / 120.0);
            Report("present_ms", present / 120.0);
        } else {
            std::printf("window_init_failed=1\n");
        }
    }

    // ---------- 10. 可选：画布尺寸变化（FBO 重建） ----------
    if (resize) {
        const int n = 60;
        auto t0 = Clock::now();
        for (int i = 0; i < n; ++i) {
            // 交替改画布尺寸，强制重建两个列表的 FBO
            auto* s0 = scene.root->GetChild(8);
            if (auto* sv = dynamic_cast<ui::ScrollView*>(s0)) {
                sv->SetCanvasSize(260.0f, 880.0f + (i % 2) * 40.0f);
            }
            app.RunFrame();
        }
        Report("canvas_resize_frame_ms", Ms(t0) / n);
    }

    app.Shutdown();
    return 0;
}
