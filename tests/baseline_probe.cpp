// 改动前基线探针：只用公开 API + wall clock，可对 git HEAD 编译。
// 场景与 frame_bench.cpp 共享（bench_scene.h），用于前后对比。
#include "bench_scene.h"

#include <GLES3/gl3.h>
#include <opencv2/imgproc.hpp>

#include <chrono>
#include <cstdio>
#include <cstring>

using Clock = std::chrono::steady_clock;

static double Ms(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}
static void Report(const char* key, double value) {
    std::printf("%s=%.3f\n", key, value);
}

int main(int argc, char** argv) {
    bool window = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--window") == 0) window = true;
    }

    ui::UIApplication::Config cfg;
    cfg.renderWidth = 1920;
    cfg.renderHeight = 1080;
    cfg.showWindow = false;
    cfg.windowWidth = 1280;
    cfg.windowHeight = 720;
    ui::UIApplication app;
    if (!app.Init(cfg)) { std::printf("init failed\n"); return 2; }

    auto scene = bench::BuildScene(app, 1920, 1080);

    const int kWarmup = 10;
    const int kFrames = 60;
    const double avg = (double)kFrames;

    for (int i = 0; i < kWarmup; ++i) app.RunFrame();
    double runTotal = 0;
    for (int i = 0; i < kFrames; ++i) {
        auto t0 = Clock::now();
        app.RunFrame();
        runTotal += Ms(t0);
    }
    Report("runframe_ms", runTotal / avg);

    for (int i = 0; i < kWarmup; ++i) app.RenderOffscreen();
    double renderOnly = 0;
    for (int i = 0; i < kFrames; ++i) {
        auto t0 = Clock::now();
        app.RenderOffscreen();
        renderOnly += Ms(t0);
    }
    Report("render_only_ms", renderOnly / avg);

    for (int i = 0; i < kWarmup; ++i) { app.RenderOffscreen(); app.GetFrameBGRA(); }
    double renderCapture = 0;
    for (int i = 0; i < kFrames; ++i) {
        auto t0 = Clock::now();
        app.RenderOffscreen();
        app.GetFrameBGRA();
        renderCapture += Ms(t0);
    }
    Report("render_capture_ms", renderCapture / avg);

    app.RenderOffscreen();
    app.GetFrameBGRA();
    double cachedClone = 0;
    for (int i = 0; i < kFrames; ++i) {
        auto t0 = Clock::now();
        app.GetFrameBGRA();
        cachedClone += Ms(t0);
    }
    Report("cached_getframe_ms", cachedClone / avg);

    const int sizes[][2] = {{640, 480}, {1280, 720}, {1920, 1080}, {3840, 2160}};
    for (auto& s : sizes) {
        cv::Mat img = bench::MakeImage(s[0], s[1]);
        const int n = 10;
        auto t0 = Clock::now();
        for (int k = 0; k < n; ++k) scene.image->SetImage(img);
        char key[64];
        std::snprintf(key, sizeof(key), "setimage_%dx%d_ms", s[0], s[1]);
        Report(key, Ms(t0) / n);
    }

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

    const int nLayout = 200;
    double layoutOnce = 0;
    for (int i = 0; i < nLayout; ++i) {
        auto t0 = Clock::now();
        scene.root->Layout({1920.0f, 1080.0f}, {}, {0, 0, 1920, 1080}, {});
        layoutOnce += Ms(t0);
    }
    Report("layout_once_ms", layoutOnce / nLayout);

    ui::Texture tex;
    if (tex.CreateEmpty(1920, 1080)) {
        glFinish();
        auto t0 = Clock::now();
        const int n = 20;
        for (int i = 0; i < n; ++i) { tex.GenerateMipmaps(); glFinish(); }
        Report("mipmap_1080p_ms", Ms(t0) / n);
    }

    if (window) {
        ui::UIApplication::Config wcfg = cfg;
        wcfg.showWindow = true;
        ui::UIApplication wapp;
        if (wapp.Init(wcfg)) {
            bench::BuildScene(wapp, 1920, 1080);
            for (int i = 0; i < 20; ++i) wapp.RunFrame();
            double frameTotal = 0;
            for (int i = 0; i < 120; ++i) {
                auto t0 = Clock::now();
                wapp.RunFrame();
                frameTotal += Ms(t0);
            }
            Report("window_runframe_ms", frameTotal / 120.0);
        } else {
            std::printf("window_init_failed=1\n");
        }
    }

    app.Shutdown();
    return 0;
}
