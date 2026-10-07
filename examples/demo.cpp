#include "ui/UIApplication.h"
#include "ui/Widgets.h"
#include "ui/CanvasGroup.h"
#include "common/FrameRateCounter.h"

#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>

#include <cstdio>
#include <cmath>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

using namespace ui;

// 纯色矩形（用于演示透明叠加、颜色背景等）
struct Fill : Component {
    Color color = Color::Red();
    void OnDraw(Renderer2D& r) override { r.FillRect(0, 0, m_width, m_height, color); }
};

// 鼠标捕获框：点击捕获鼠标，按 ESC 释放；捕获期间显示鼠标移动速度并用箭头可视化
struct MouseCaptureBox : Component {
    bool captured = false;
    float speed = 0.0f;       // 像素/秒
    float angle = 0.0f;       // 移动方向（弧度）
    Text* speedLabel = nullptr;
    Text* statusLabel = nullptr;

    void OnDraw(Renderer2D& r) override {
        Vec2 s = SelfSize();
        r.FillRect(0, 0, s.x, s.y, {0.09f, 0.11f, 0.15f, 1.0f});
        Component::OnDraw(r);  // 边框

        // 以中心为原点的速度箭头
        float cx = s.x * 0.5f;
        float cy = s.y * 0.5f;
        float maxLen = std::min(s.x, s.y) * 0.38f;
        float t = speed / 1500.0f;             // 归一化（1500 px/s 视为满长）
        if (t > 1.0f) t = 1.0f;
        float len = t * maxLen;
        r.FillRect(cx - 3.0f, cy - 3.0f, 6.0f, 6.0f, {1.0f, 1.0f, 1.0f, 1.0f});
        if (len > 3.0f) {
            float ex = cx + std::cos(angle) * len;
            float ey = cy + std::sin(angle) * len;
            Color arrow = {1.0f, 0.85f, 0.2f, 1.0f};
            r.DrawLine(cx, cy, ex, ey, 5.0f, arrow);
            float head = 16.0f;
            float a1 = angle + 2.6f;  // ~150°
            float a2 = angle - 2.6f;
            r.DrawLine(ex, ey, ex + std::cos(a1) * head, ey + std::sin(a1) * head, 4.0f, arrow);
            r.DrawLine(ex, ey, ex + std::cos(a2) * head, ey + std::sin(a2) * head, 4.0f, arrow);
        }
    }

    bool OnMouseDown(MouseButton b, float, float) override {
        if (b != MouseButton::Left) return false;
        captured = true;
        SetMouseCapture(true);
        const InputState* in = GetInput();
        lastX = in->mouseX;
        lastY = in->mouseY;
        lastTime = std::chrono::steady_clock::now();
        speed = 0.0f;
        if (statusLabel) statusLabel->SetText("captured — move mouse, press ESC to release");
        return true;
    }

    // 捕获框可聚焦：点击捕获后成为键盘焦点，从而接收 ESC 按键释放
    bool IsFocusable() const override { return true; }

    bool OnKeyDown(int key) override {
        if (key == KEY_ESCAPE && captured) {
            captured = false;
            SetMouseCapture(false);
            speed = 0.0f;
            if (statusLabel) statusLabel->SetText("click to capture mouse (ESC to release)");
            if (speedLabel) speedLabel->SetText("speed: 0 px/s");
            return true;
        }
        return false;
    }

    void OnMouseMove(float, float) override {
        const InputState* in = GetInput();
        auto now = std::chrono::steady_clock::now();
        double dt = std::chrono::duration<double>(now - lastTime).count();
        if (dt > 0.0) {
            double dx = in->mouseX - lastX;
            double dy = in->mouseY - lastY;
            double dist = std::sqrt(dx * dx + dy * dy);
            if (captured && dist > 0.5) {
                speed = (float)(dist / dt);
                angle = (float)std::atan2(dy, dx);
            }
            lastX = in->mouseX;
            lastY = in->mouseY;
            lastTime = now;
        }
        if (speedLabel) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "speed: %.0f px/s", speed);
            speedLabel->SetText(buf);
        }
    }

private:
    double lastX = 0.0, lastY = 0.0;
    std::chrono::steady_clock::time_point lastTime{};
};


static cv::Mat MakeTestImage() {
    cv::Mat img(400, 600, CV_8UC3);
    for (int y = 0; y < img.rows; ++y) {
        for (int x = 0; x < img.cols; ++x) {
            img.at<cv::Vec3b>(y, x) = cv::Vec3b(
                (uchar)(y * 255 / img.rows),
                (uchar)(x * 255 / img.cols),
                140);
        }
    }
    cv::putText(img, "OpenGL offscreen image", cv::Point(24, 60),
                cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(255, 255, 255), 2);
    cv::putText(img, "drag to draw a yolo box", cv::Point(24, 120),
                cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(220, 255, 220), 2);
    return img;
}

int main(int argc, char** argv) {
    bool headless = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--headless") headless = true;
    }

    UIApplication::Config cfg;
    cfg.renderWidth = 1920;
    cfg.renderHeight = 1080;
    cfg.showWindow = !headless;
    cfg.windowWidth = 1280;
    cfg.windowHeight = 720;
    cfg.title = "OpenGL UI Template Demo";

    UIApplication app;
    if (!app.Init(cfg)) {
        std::fprintf(stderr, "[demo] UIApplication::Init failed\n");
        return 1;
    }

    // ===== 根容器 =====
    auto root = std::make_unique<Component>();
    root->SetRect(0, 0, (float)cfg.renderWidth, (float)cfg.renderHeight);

    // 标题
    auto* title = root->AddChild<Text>();
    title->SetRect(40, 30, 1200, 40);
    title->SetText("OpenGL UI Template - component tree demo");
    title->SetFontScale(1.0f);
    title->SetFontThickness(2);
    title->SetColor(Color::White());

    // ===== 帧率显示（右上角）=====
    FrameRateCounter frc(60);
    auto* fpsText = root->AddChild<Text>();
    fpsText->SetRect(1720, 30, 170, 40);
    fpsText->SetText("FPS: -");
    fpsText->SetFontScale(0.7f);
    fpsText->SetFontThickness(2);
    fpsText->SetAlignment(Text::HAlign::Right, Text::VAlign::Middle);
    fpsText->SetColor({0.3f, 1.0f, 0.5f, 1.0f});

    // ===== 日志栏（带边框、可滚动的文字显示）=====
    const float logLineH = 24.0f;
    const int maxLogLines = 200;
    auto* logScroll = root->AddChild<ScrollView>();
    logScroll->SetRect(1000, 470, 460, 550);
    logScroll->SetBorder(2.0f, {0.40f, 0.50f, 0.60f, 1.0f});
    logScroll->SetCanvasSize(460.0f, 550.0f);
    std::vector<std::string> logLines;
    auto log = [&](const std::string& msg) {
        logLines.push_back(msg);
        while ((int)logLines.size() > maxLogLines) logLines.erase(logLines.begin());
        logScroll->GetChildren().clear();
        for (size_t i = 0; i < logLines.size(); ++i) {
            auto* line = logScroll->AddChild<Text>();
            line->SetRect(8.0f, (float)i * logLineH, 444.0f, logLineH);
            line->SetText(logLines[i]);
            line->SetFontScale(0.4f);
            line->SetColor({0.85f, 0.90f, 0.95f, 1.0f});
        }
        float totalH = (float)logLines.size() * logLineH;
        if (totalH < 550.0f) totalH = 550.0f;
        logScroll->SetCanvasSize(460.0f, totalH);
        float maxScroll = totalH - 550.0f;
        if (maxScroll < 0.0f) maxScroll = 0.0f;
        logScroll->SetScrollY(maxScroll);
    };
    log("demo started");

    // ===== 按钮 + 点击计数 =====
    int clickCount = 0;
    auto* btnLabel = root->AddChild<Text>();
    btnLabel->SetRect(260, 90, 220, 48);
    btnLabel->SetText("clicks: 0");
    btnLabel->SetFontScale(0.5f);
    btnLabel->SetAlignment(Text::HAlign::Left, Text::VAlign::Middle);
    btnLabel->SetColor(Color::White());

    auto* btn = root->AddChild<Button>();
    btn->SetRect(40, 90, 200, 48);
    btn->SetLabel("Click Me");
    btn->SetFontScale(0.6f);
    btn->SetOnClick([&](Button*) {
        ++clickCount;
        char buf[64];
        std::snprintf(buf, sizeof(buf), "clicks: %d", clickCount);
        btnLabel->SetText(buf);
        log("button clicked (" + std::to_string(clickCount) + ")");
    });

    // ===== 滑块 =====
    auto* sliderLabel = root->AddChild<Text>();
    sliderLabel->SetRect(360, 160, 200, 24);
    sliderLabel->SetText("value: 30.0");
    sliderLabel->SetFontScale(0.5f);
    sliderLabel->SetAlignment(Text::HAlign::Left, Text::VAlign::Middle);
    sliderLabel->SetColor(Color::White());

    auto* slider = root->AddChild<Slider>();
    slider->SetRect(40, 160, 300, 24);
    slider->SetRange(0.0f, 100.0f);
    slider->SetValue(30.0f);
    slider->SetOnChange([&](Slider*, float v) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "value: %.1f", v);
        sliderLabel->SetText(buf);
        log("slider -> " + std::string(buf));
    });

    // ===== 文本框 =====
    auto* textBox = root->AddChild<TextBox>();
    textBox->SetRect(40, 210, 300, 36);
    textBox->SetPlaceholder("type here...");
    textBox->SetFontScale(0.5f);
    textBox->SetOnSubmit([&](TextBox* tb) {
        std::printf("[demo] textbox submit: \"%s\"\n", tb->GetText().c_str());
        log("textbox submit: \"" + tb->GetText() + "\"");
    });

    // ===== 下拉菜单 =====
    auto* dropLabel = root->AddChild<Text>();
    dropLabel->SetRect(280, 270, 320, 36);
    dropLabel->SetFontScale(0.5f);
    dropLabel->SetAlignment(Text::HAlign::Left, Text::VAlign::Middle);
    dropLabel->SetColor(Color::White());

    // ===== 滚动菜单 =====
    auto* scroll = root->AddChild<ScrollView>();
    scroll->SetRect(680, 90, 260, 320);
    scroll->SetCanvasSize(260.0f, 900.0f);   // 内容高度 > 视口，可滚动
    for (int i = 0; i < 20; ++i) {
        auto* item = scroll->AddChild<Button>();
        item->SetRect(8.0f, (float)i * 44.0f, 244.0f, 40.0f);
        char buf[64];
        std::snprintf(buf, sizeof(buf), "Scroll item %02d", i);
        item->SetLabel(buf);
        item->SetFontScale(0.5f);
    }

    // ===== 图像显示（yolo 框选）=====
    auto* boxLabel = root->AddChild<Text>();
    boxLabel->SetRect(40, 680, 640, 26);
    boxLabel->SetFontScale(0.5f);
    boxLabel->SetColor({0.3f, 1.0f, 0.6f, 1.0f});
    boxLabel->SetText("drag inside image to draw a box (yolo normalized output)");

    auto* imageDisp = root->AddChild<ImageDisplay>();
    imageDisp->SetRect(40, 360, 400, 300);
    imageDisp->SetImage(MakeTestImage());
    imageDisp->SetOnBoxDrawn([&](ImageDisplay*, float cx, float cy, float w, float h) {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "yolo box: cx=%.3f cy=%.3f w=%.3f h=%.3f", cx, cy, w, h);
        boxLabel->SetText(buf);
        log(buf);
    });
    imageDisp->SetOnClick([&](ImageDisplay*, float cx, float cy) {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "click at: x=%.3f y=%.3f (normalized)", cx, cy);
        boxLabel->SetText(buf);
        log(buf);
    });

    // ===== 鼠标捕获框（点击捕获，ESC 释放，显示速度 + 箭头）=====
    auto* mouseBox = root->AddChild<MouseCaptureBox>();
    mouseBox->SetRect(1000, 100, 460, 340);
    mouseBox->SetBorder(3.0f, {0.40f, 0.50f, 0.65f, 1.0f});
    auto* mouseSpeedLabel = mouseBox->AddChild<Text>();
    mouseSpeedLabel->SetRect(16, 16, 428, 30);
    mouseSpeedLabel->SetText("speed: 0 px/s");
    mouseSpeedLabel->SetFontScale(0.5f);
    mouseSpeedLabel->SetColor(Color::White());
    auto* mouseStatusLabel = mouseBox->AddChild<Text>();
    mouseStatusLabel->SetRect(16, 52, 428, 24);
    mouseStatusLabel->SetText("click to capture mouse (ESC to release)");
    mouseStatusLabel->SetFontScale(0.4f);
    mouseStatusLabel->SetColor({0.65f, 0.75f, 0.85f, 1.0f});
    mouseBox->speedLabel = mouseSpeedLabel;
    mouseBox->statusLabel = mouseStatusLabel;

    // ===== 透明度示例（半透明面板）=====
    auto* translucent = root->AddChild<Text>();
    translucent->SetRect(480, 160, 160, 86);
    translucent->SetText("50% opacity");
    translucent->SetFontScale(0.45f);
    translucent->SetAlignment(Text::HAlign::Center, Text::VAlign::Middle);
    translucent->SetBackground({0.2f, 0.45f, 0.85f, 1.0f});
    translucent->SetOpacity(0.5f);

    // ===== 边框示例（纯 Component，默认绘制边框）=====
    auto* bordered = root->AddChild<Component>();
    bordered->SetRect(480, 270, 160, 60);
    bordered->SetBorder(3.0f, {1.0f, 0.6f, 0.2f, 1.0f});

    // 弹出菜单最后添加，避免后绘制的图像挡住选项 C/D 及其鼠标命中。
    auto* dropdown = root->AddChild<Dropdown>();
    dropdown->SetRect(40, 270, 220, 36);
    dropdown->SetItems({"Option A", "Option B", "Option C", "Option D"});
    dropdown->SetSelectedIndex(0);
    dropLabel->SetText("selected: Option A");
    dropdown->SetOnSelect([&](Dropdown*, int, const std::string& item) {
        dropLabel->SetText("selected: " + item);
        log("dropdown -> " + item);
    });

    app.SetRoot(std::move(root));

    if (headless) {
        // 注入一次点击，验证交互，然后离屏渲染并保存图像
        app.InjectMousePos(140.0f, 114.0f);      // 按钮中心
        app.InjectMouseButton(MouseButton::Left, true);
        app.RunFrame();
        app.InjectMouseButton(MouseButton::Left, false);
        app.RunFrame();
        app.RenderOffscreen();

        cv::Mat frame = app.GetFrame();
        bool ok = cv::imwrite("headless_output.png", frame);
        std::printf("[demo] headless render: %dx%d channels=%d saved=%s\n",
                    frame.cols, frame.rows, frame.channels(), ok ? "yes" : "no");
        std::printf("[demo] click count after injection: %d\n", clickCount);
        return 0;
    }

    // 显示模式主循环
    int lastFps = -1;
    while (!app.ShouldClose()) {
        app.RunFrame();
        frc.tick();
        int f = (int)std::lround(frc.fps());
        if (f != lastFps) {
            lastFps = f;
            char buf[32];
            std::snprintf(buf, sizeof(buf), "FPS: %d", f);
            fpsText->SetText(buf);
        }
    }
    return 0;
}
