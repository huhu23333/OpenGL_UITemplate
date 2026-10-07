// Capture the real default framebuffer immediately before GLFW's buffer swap.
// Rendering, sizing, mapping and the real swap still run through GLFW/GLES.
#include "ui/UIApplication.h"
#include <GLFW/glfw3.h>
#include <dlfcn.h>
#include <opencv2/imgproc.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>

static cv::Mat presented;
extern "C" void glfwSwapBuffers(GLFWwindow* window) {
    int w, h; glfwGetFramebufferSize(window, &w, &h);
    if (w <= 0 || h <= 0) return;
    cv::Mat pixels(h, w, CV_8UC4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data);
    cv::flip(pixels, presented, 0);
    using Swap = void (*)(GLFWwindow*);
    static auto realSwap = reinterpret_cast<Swap>(dlsym(RTLD_NEXT, "glfwSwapBuffers"));
    if (!realSwap) throw std::runtime_error("resolve GLFW swap");
    realSwap(window);
}
struct Fill : ui::Component {
    void OnDraw(ui::Renderer2D& renderer) override {
        renderer.FillRect(0, 0, m_width, m_height / 2, ui::Color::Red());
        renderer.FillRect(0, m_height / 2, m_width, m_height / 2, ui::Color::Blue());
    }
};
static void Check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
static void CheckFrame(ui::UIApplication& app) {
    auto image = app.GetFrame();
    Check(image.rows == 120 && image.cols == 160 && image.channels() == 3, "fixed cv::Mat output");
    Check(image.at<cv::Vec3b>(10, 80) == cv::Vec3b(0, 0, 255), "offscreen top row orientation");
    Check(!presented.empty(), "presentation captured");
    int w = presented.cols, h = presented.rows;
    float scale = std::min(w / 160.0f, h / 120.0f);
    int ox = (int)((w - 160 * scale) / 2), oy = (int)((h - 120 * scale) / 2);
    Check(presented.at<cv::Vec4b>(oy + (int)(10 * scale), ox + (int)(80 * scale)) == cv::Vec4b(255, 0, 0, 255), "present top red");
    Check(presented.at<cv::Vec4b>(oy + (int)(110 * scale), ox + (int)(80 * scale)) == cv::Vec4b(0, 0, 255, 255), "present bottom blue");
    if (ox > 1 || oy > 1) {
        auto actual = presented.at<cv::Vec4b>(0, 0);
        if (actual != cv::Vec4b(0, 255, 0, 255)) std::cerr << "window " << w << 'x' << h << " letterbox pixel " << actual << '\n';
        Check(actual == cv::Vec4b(0, 255, 0, 255), "letterbox color");
    }
}
int main() {
    try {
        ui::UIApplication app;
        ui::UIApplication::Config cfg;
        cfg.renderWidth = 160; cfg.renderHeight = 120;
        cfg.windowWidth = 240; cfg.windowHeight = 240;
        cfg.title = "UI review regression";
        cfg.letterboxColor = ui::Color::Green();
        Check(app.Init(cfg), "window initialization (requires a working DISPLAY)");
        app.SetRoot<Fill>();
        auto* window = glfwGetCurrentContext();
        glfwSwapInterval(0);
        glfwFocusWindow(window);
        app.RunFrame(); CheckFrame(app);
        for (const auto& size : {std::pair<int,int>{320, 160}, {200, 300}}) {
            glfwSetWindowSize(window, size.first, size.second);
            app.RunFrame(); app.RunFrame(); CheckFrame(app);
            int w, h; glfwGetWindowSize(window, &w, &h);
            glfwSetCursorPos(window, w / 2.0, h / 2.0);
            app.RunFrame();
            Check(std::fabs(app.GetInput().mouseX - 80) < 1 && std::fabs(app.GetInput().mouseY - 60) < 1, "center input mapping after resize");
            glfwSetCursorPos(window, 0, 0); app.RunFrame();
            Check(!app.GetInput().mouseInside, "letterbox excludes input");
        }
        {
            ui::UIApplication second; Check(second.Init(cfg), "second GLFW context"); second.RunFrame();
        }
        app.RunFrame(); CheckFrame(app);
        Check(glGetError() == GL_NO_ERROR, "window GL errors");
        std::cout << "PASS: window FBO output, letterbox pixels, resize mapping, multiple GLFW contexts\n";
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
