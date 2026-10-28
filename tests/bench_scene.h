// 性能基准共享场景：与 demo 结构相近（面板/边框 + 文本 + 滚动列表 + 图像）。
// 该头文件只使用公开 API，既能对当前版本编译，也能对改动前的基线版本编译。
#ifndef UI_BENCH_SCENE_H
#define UI_BENCH_SCENE_H

#include "ui/UIApplication.h"
#include "ui/Widgets.h"
#include "ui/CanvasGroup.h"

#include <opencv2/imgproc.hpp>

#include <string>

namespace bench {

struct Scene {
    ui::Component* root = nullptr;
    ui::ImageDisplay* image = nullptr;
    cv::Mat streamFrame;   // 逐帧更新的图像（1280x720）
};

struct Panel : ui::Component {
    ui::Color color = {0.16f, 0.18f, 0.22f, 1.0f};
    void OnDraw(ui::Renderer2D& r) override {
        r.FillRect(0, 0, m_width, m_height, color);
        ui::Component::OnDraw(r);
    }
};

inline cv::Mat MakeImage(int w, int h) {
    cv::Mat img(h, w, CV_8UC3);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            img.at<cv::Vec3b>(y, x) = cv::Vec3b((uchar)(x * 255 / w),
                                                (uchar)(y * 255 / h), (uchar)140);
        }
    }
    return img;
}

inline Scene BuildScene(ui::UIApplication& app, int W, int H) {
    Scene scene;
    scene.root = app.SetRoot<ui::Component>();
    scene.root->SetRect(0, 0, (float)W, (float)H);

    // 8 个带边框面板（纯色填充 + scissor 裁剪）
    for (int i = 0; i < 8; ++i) {
        auto* p = scene.root->AddChild<Panel>();
        p->SetRect(40.0f + (i % 4) * 220.0f, 40.0f + (i / 4) * 160.0f, 200.0f, 140.0f);
        p->SetBorder(2.0f, {0.30f, 0.40f, 0.50f, 1.0f});
    }
    // 6 条文本（每条一张文字纹理）
    for (int i = 0; i < 6; ++i) {
        auto* t = scene.root->AddChild<ui::Text>();
        t->SetRect(40.0f, 400.0f + i * 30.0f, 500.0f, 26.0f);
        t->SetText("benchmark label " + std::to_string(i));
        t->SetFontScale(1.0f);
    }
    // 两个滚动列表（虚拟画布 + 独立 FBO 重定向），每个 20 个按钮
    for (int s = 0; s < 2; ++s) {
        auto* scroll = scene.root->AddChild<ui::ScrollView>();
        scroll->SetRect(920.0f + s * 300.0f, 40.0f, 260.0f, 320.0f);
        scroll->SetCanvasSize(260.0f, 900.0f);
        for (int i = 0; i < 20; ++i) {
            auto* b = scroll->AddChild<ui::Button>();
            b->SetRect(8.0f, (float)i * 44.0f, 244.0f, 40.0f);
            b->SetLabel("Scroll item " + std::to_string(i));
            b->SetFontScale(1.0f);
        }
    }
    // 图像显示（960x540 BGR 上传）
    scene.image = scene.root->AddChild<ui::ImageDisplay>();
    scene.image->SetRect(40.0f, 620.0f, 640.0f, 360.0f);
    scene.image->SetImage(MakeImage(960, 540));

    scene.streamFrame = MakeImage(1280, 720);
    return scene;
}

} // namespace bench

#endif // UI_BENCH_SCENE_H
