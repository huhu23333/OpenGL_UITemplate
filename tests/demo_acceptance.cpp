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
