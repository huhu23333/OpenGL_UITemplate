#ifndef UI_RENDER_CONTEXT_H
#define UI_RENDER_CONTEXT_H

#include "Types.h"

#include <memory>
#include <string>
#include <vector>

namespace ui {

// ---------------------------------------------------------------------------
// 原始输入快照（窗口坐标，左上角原点；无头模式下即图像坐标）。
// 由 RenderContext 在 PollEvents 中累积，帧末调用 ClearInputEdges 清零边沿。
// ---------------------------------------------------------------------------
struct RawInput {
    double mouseX = -1.0;
    double mouseY = -1.0;
    bool   mouseInside = true;
    bool   mouseDown[3]    = {false, false, false};  // 当前按下
    bool   mousePressed[3] = {false, false, false};  // 本帧按下边沿
    bool   mouseReleased[3] = {false, false, false}; // 本帧抬起边沿
    std::vector<int> keysDown;                       // 当前按下
    std::vector<int> keysPressed;                    // 本帧按下边沿
    std::vector<int> keysReleased;                   // 本帧抬起边沿
    std::vector<uint8_t> charInput;                  // 本帧 UTF-8 字符输入
    float scrollY = 0.0f;                            // 本帧滚轮（>0 上滚）

    void ClearEdges() {
        mousePressed[0] = mousePressed[1] = mousePressed[2] = false;
        mouseReleased[0] = mouseReleased[1] = mouseReleased[2] = false;
        keysPressed.clear();
        keysReleased.clear();
        charInput.clear();
        scrollY = 0.0f;
    }
};

// ---------------------------------------------------------------------------
// 渲染上下文抽象：屏蔽"窗口模式"与"无头模式"的差异。
//   - 窗口模式：GLFW 创建 GLES3 窗口 + 输入。
//   - 无头模式：EGL surfaceless 创建 GLES3 上下文（无需任何显示服务）。
// 上层渲染代码（OpenGL 调用）在两种模式下完全一致。
// ---------------------------------------------------------------------------
class RenderContext {
public:
    struct Options {
        int  width  = 1280;     // 窗口宽度（无头模式忽略）
        int  height = 720;      // 窗口高度（无头模式忽略）
        bool visible = true;    // 是否显示窗口（false 则创建隐藏窗口，仍需显示服务）
        std::string title = "OpenGL UI";
    };

    virtual ~RenderContext() = default;

    // ---- 工厂 ----
    static std::unique_ptr<RenderContext> CreateWindowed(const Options& opt);
    static std::unique_ptr<RenderContext> CreateHeadless();

    // ---- 帧控制 ----
    virtual void MakeCurrent() = 0;
    virtual void SwapBuffers() = 0;          // 无头模式下为 no-op
    virtual void GetFramebufferSize(int& w, int& h) = 0;
    virtual bool ShouldClose() const = 0;
    virtual void PollEvents() = 0;           // 处理窗口系统事件，累积输入到 RawInput
    virtual const RawInput& GetRawInput() const = 0;
    virtual void ClearInputEdges() = 0;      // 帧末清零边沿/字符/滚轮
    virtual double GetTime() const = 0;      // 秒

    // ---- 光标 / 鼠标捕获 ----
    virtual void SetCursorVisible(bool visible) = 0;
    virtual void SetMouseCaptured(bool captured) = 0;  // 禁用系统光标（拖拽等）
    virtual bool IsMouseCaptured() const = 0;

    // ---- 无头模式注入输入（窗口模式下忽略）----
    virtual void InjectKey(int key, bool down) { (void)key; (void)down; }
    virtual void InjectMouseButton(MouseButton b, bool down) { (void)b; (void)down; }
    virtual void InjectCursorPos(double x, double y) { (void)x; (void)y; }
    virtual void InjectScroll(double dy) { (void)dy; }
    virtual void InjectChar(unsigned int codepoint) { (void)codepoint; }

    virtual bool IsHeadless() const = 0;
};

} // namespace ui

#endif // UI_RENDER_CONTEXT_H
