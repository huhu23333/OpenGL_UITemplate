#ifndef UI_UI_APPLICATION_H
#define UI_UI_APPLICATION_H

#include "Types.h"
#include "InputState.h"
#include "Component.h"
#include "Renderer2D.h"
#include "RenderContext.h"
#include "Texture.h"

#include <GLES3/gl3.h>
#include <opencv2/core.hpp>

#include <memory>
#include <string>
#include <vector>

namespace ui {

// ---------------------------------------------------------------------------
// UI 应用：把组件树离屏渲染到固定分辨率图像，并按比例缩放到窗口中心。
//   - 离屏渲染：FBO + 纹理（renderWidth x renderHeight，可配置）。
//   - 输出：glReadPixels → cv::Mat（显示/无头模式均可获取）。
//   - 显示模式：letterbox 按比例居中呈现，多余区域填充纯色。
//   - 无头模式：不创建窗口，仅离屏渲染（EGL surfaceless）。
// ---------------------------------------------------------------------------
class UIApplication {
public:
    struct Config {
        int  renderWidth  = 1280;   // 离屏渲染分辨率（宽）
        int  renderHeight = 720;    // 离屏渲染分辨率（高）
        bool showWindow   = true;   // false = 无头模式
        int  windowWidth  = 1280;   // 窗口初始宽（显示模式）
        int  windowHeight = 720;    // 窗口初始高（显示模式）
        std::string title = "OpenGL UI";
        Color clearColor  = {0.08f, 0.08f, 0.10f, 1.0f};  // 离屏图像背景
        Color letterboxColor = {0.03f, 0.03f, 0.05f, 1.0f}; // 窗口多余区域填充
    };

    UIApplication();
    ~UIApplication();

    UIApplication(const UIApplication&) = delete;
    UIApplication& operator=(const UIApplication&) = delete;

    // 初始化上下文、渲染器、离屏 FBO。失败返回 false。
    bool Init(const Config& cfg);
    void Shutdown();

    // ---- 根组件 ----
    void SetRoot(std::unique_ptr<Component> root);
    Component* GetRoot() const { return m_root.get(); }
    template <typename T, typename... Args>
    T* SetRoot(Args&&... args) {
        auto p = std::make_unique<T>(std::forward<Args>(args)...);
        T* raw = p.get();
        SetRoot(std::move(p));
        return raw;
    }

    // ---- 帧循环 ----
    void RunFrame();          // 输入 → 布局 → 离屏渲染 → 输出 → 呈现
    void RenderOffscreen();   // 仅布局 + 离屏渲染（不处理输入/呈现）
    bool ShouldClose() const;
    bool IsHeadless() const { return m_ctx ? m_ctx->IsHeadless() : true; }

    // ---- 输出 ----
    cv::Mat GetFrame() const;      // 返回 BGR 3 通道副本
    cv::Mat GetFrameBGRA() const;  // 返回 BGRA 4 通道副本

    // ---- 无头模式注入输入（图像坐标）----
    void InjectKey(int key, bool down);
    void InjectMouseButton(MouseButton b, bool down);
    void InjectMousePos(float x, float y);
    void InjectScroll(float dy);
    void InjectChar(unsigned int codepoint);

    // ---- 访问器 ----
    const Config& GetConfig() const { return m_cfg; }
    RenderContext* GetContext() { return m_ctx.get(); }
    Renderer2D* GetRenderer() { return m_renderer.get(); }
    const InputState& GetInput() const { return m_input; }

    // 组件树中当前键盘焦点组件
    Component* GetFocus() const { return IsActive(m_focus, m_focusLife) ? m_focus : nullptr; }
    void SetFocus(Component* c);

private:
    void BuildInput();       // 从 context.RawInput 构建 m_input（含坐标变换）
    void DispatchInput();    // 命中测试 + 事件分发
    void Present();          // letterbox 呈现（显示模式）

    // 窗口坐标 → 图像坐标（letterbox 逆变换）
    void WindowToImage(double wx, double wy, float& ix, float& iy, bool& inside);

    Config m_cfg;
    std::unique_ptr<RenderContext> m_ctx;
    std::unique_ptr<Renderer2D> m_renderer;
    std::unique_ptr<Component> m_root;

    GLuint m_fbo = 0;
    std::unique_ptr<Texture> m_offscreenTex;
    cv::Mat m_frameBGRA;     // 缓存最近一帧（BGRA）

    InputState m_input;

    Component* m_hover = nullptr;
    Component* m_captured = nullptr;
    Component* m_focus = nullptr;
    Component* m_pressed[3] = {nullptr, nullptr, nullptr};
    std::weak_ptr<void> m_hoverLife, m_captureLife, m_focusLife;
    std::weak_ptr<void> m_pressedLife[3];
    bool IsActive(Component* c, const std::weak_ptr<void>& life) const;
    void ValidateInputTargets();
};

} // namespace ui

#endif // UI_UI_APPLICATION_H
