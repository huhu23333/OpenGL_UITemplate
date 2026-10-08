// Exercise the actual demo component without adding another library/public API.
#define main DemoMain
#include "../examples/demo.cpp"
#undef main

#include <GLFW/glfw3.h>
#include <iostream>
#include <stdexcept>

static void Check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

int main(int argc, char** argv) {
    try {
        // Nested demo layout must keep the same fractions at 720p, 1080p and 4K.
        for (float factor : {2.0f / 3.0f, 1.0f, 2.0f}) {
            Component root; root.SetSize(REF_W * factor, REF_H * factor);
            auto* scroll = root.AddChild<RatioScrollView>();
            place(scroll, 680, 90, 260, 320); scroll->contentHeightRatio = 900.0f / 320.0f;
            auto* item = scroll->AddChild<Button>(); place(item, 8, 44, 244, 40, 260, 900);
            auto* box = root.AddChild<MouseCaptureBox>(); place(box, 1000, 100, 460, 340);
            auto* label = box->AddChild<Text>(); place(label, 16, 16, 428, 30, 460, 340);
            auto* dropdown = root.AddChild<RatioDropdown>(); place(dropdown, 40, 270, 220, 36);
            dropdown->SetItems({"A", "B"}); dropdown->SetOpen(true);
            root.Layout({REF_W * factor, REF_H * factor}, {}, {0, 0, REF_W * factor, REF_H * factor}, {});
            auto near = [](float a, float b) { return std::fabs(a - b) < .1f; };
            float w, h; scroll->GetCanvasSize(w, h);
            Check(near(w, std::ceil(260 * factor)) && near(h, 900 * factor), "demo canvas scales with viewport");
            Check(near(item->GetScreenRect().y, 134 * factor) && near(item->GetScreenRect().h, 40 * factor),
                  "scroll items retain relative vertical layout");
            Check(near(label->GetScreenRect().x, 1016 * factor) && near(label->GetScreenRect().w, 428 * factor),
                  "mouse labels retain parent-relative layout");
            Check(near(dropdown->GetChild(1)->GetScreenRect().h, 64 * factor), "dropdown rows scale with header");
            scroll->scrollToEnd = true;
            root.Layout({REF_W * factor, REF_H * factor}, {}, {0, 0, REF_W * factor, REF_H * factor}, {});
            Check(near(scroll->GetScrollY(), 580 * factor), "demo deferred scroll-to-end uses resolved size");
        }
        const bool windowed = argc > 1 && std::string(argv[1]) == "--window";
        UIApplication app;
        UIApplication::Config cfg;
        cfg.showWindow = windowed;
        cfg.renderWidth = cfg.windowWidth = 640;
        cfg.renderHeight = cfg.windowHeight = 480;
        Check(app.Init(cfg), "initialize demo acceptance context");
        GLFWwindow* window = windowed ? glfwGetCurrentContext() : nullptr;
        GLFWmousebuttonfun mouseCallback = nullptr;
        GLFWkeyfun keyCallback = nullptr;
        if (window) {
            glfwFocusWindow(window);
            mouseCallback = glfwSetMouseButtonCallback(window, nullptr);
            glfwSetMouseButtonCallback(window, mouseCallback);
            keyCallback = glfwSetKeyCallback(window, nullptr);
            glfwSetKeyCallback(window, keyCallback);
        }
        auto position = [&](float x, float y) {
            if (window) glfwSetCursorPos(window, x, y);
            else app.InjectMousePos(x, y);
        };
        auto button = [&](bool down) {
            if (window) mouseCallback(window, GLFW_MOUSE_BUTTON_LEFT, down ? GLFW_PRESS : GLFW_RELEASE, 0);
            else app.InjectMouseButton(MouseButton::Left, down);
        };
        auto escape = [&](bool down) {
            if (window) keyCallback(window, GLFW_KEY_ESCAPE, 0, down ? GLFW_PRESS : GLFW_RELEASE, 0);
            else app.InjectKey(KEY_ESCAPE, down);
        };
        auto* box = app.GetRoot()->AddChild<MouseCaptureBox>();
        box->SetRect(10, 10, 460, 340);
        box->speedLabel = box->AddChild<Text>();
        box->speedLabel->SetRect(16, 16, 428, 30);
        box->statusLabel = box->AddChild<Text>();
        box->onSystemCapture = [&](bool captured) { app.GetContext()->SetMouseCaptured(captured); };
        auto arrowTip = [&](float speed) {
            box->speed = speed; box->angle = 0; app.RenderOffscreen();
            int tip = -1;
            const cv::Mat& frame = app.GetFrameBGRA();
            for (int x = 240; x < 470; ++x) {
                auto pixel = frame.at<cv::Vec4b>(180, x);
                if (pixel[0] < 100 && pixel[1] > 160 && pixel[2] > 220) tip = x;
            }
            return tip;
        };
        int quarterTip = arrowTip(15000), fullTip = arrowTip(60000), saturatedTip = arrowTip(120000);
        Check(std::abs(quarterTip - 272) <= 2 && std::abs(fullTip - 369) <= 2 && saturatedTip == fullTip,
              "60000 px/s arrow normalization: quarter, full and saturation");
        Check(arrowTip(0) == -1, "stationary speed draws no arrow");
        auto capture = [&]() {
            if (window) {
                glfwFocusWindow(window);
                // Window-manager focus changes are asynchronous on a shared desktop.
                for (int i = 0; i < 20 && !glfwGetWindowAttrib(window, GLFW_FOCUSED); ++i)
                    glfwWaitEventsTimeout(0.01);
                Check(glfwGetWindowAttrib(window, GLFW_FOCUSED), "test window must own OS focus");
            }
            position(200, 200); app.RunFrame();
            button(true); app.RunFrame(); button(false); app.RunFrame();
            Check(box->captured && box->WantsMouseCapture() && app.GetContext()->IsMouseCaptured(), "persistent system capture");
            if (window) Check(glfwGetInputMode(window, GLFW_CURSOR) == GLFW_CURSOR_DISABLED, "GLFW cursor disabled");
        };
        auto released = [&]() {
            Check(!box->captured && !box->WantsMouseCapture() && !app.GetContext()->IsMouseCaptured(), "release logical and system capture");
            Check(box->speed == 0 && box->speedLabel->GetText() == "speed: 0 px/s", "release clears speed and label");
            if (window) Check(glfwGetInputMode(window, GLFW_CURSOR) == GLFW_CURSOR_NORMAL, "GLFW cursor normal");
        };
        capture();
        // A shared desktop can produce real mouse motion during PollEvents.
        // Verify stationary-frame semantics with deterministic headless input.
        if (!window) {
            position(300, 250); app.RunFrame();
            Check(box->speed > 0, "motion updates actual demo speed");
            app.RunFrame();
            Check(box->speed == 0 && box->speedLabel->GetText() == "speed: 0 px/s", "stationary frame clears arrow and label");
            position(900, 900); app.RunFrame();
            Check(box->speed > 0 && box->captured, "virtual cursor outside viewport still moves capture");
            app.RunFrame(); Check(box->speed == 0, "outside viewport still updates stationary speed");
        }
        escape(true); app.RunFrame(); released(); escape(false); app.RunFrame();
        capture(); box->OnMouseCancel(MouseButton::Left); released(); app.RunFrame();
        capture(); app.SetFocus(nullptr); released(); app.RunFrame();
        capture(); box->SetVisible(false); app.RunFrame(); released();
        Check(glGetError() == GL_NO_ERROR, "demo capture rendering GL errors");
        std::cout << "PASS: actual demo capture, ESC, cancel, focus loss, hiding ("
                  << (windowed ? "GLFW cursor modes" : "headless, including motion and stationary speed") << ")\n";
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
