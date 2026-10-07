#include "ui/RenderContext.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <GLES3/gl3.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <cstdio>
#include <chrono>
#include <map>

namespace ui {

namespace {

int glfwUsers = 0;
std::map<EGLDisplay, int> eglUsers;

// ---- UTF-8 编码辅助 ----
void PushUtf8(std::vector<uint8_t>& out, unsigned int cp) {
    if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return;
    if (cp < 0x80) {
        out.push_back((uint8_t)cp);
    } else if (cp < 0x800) {
        out.push_back((uint8_t)(0xC0 | (cp >> 6)));
        out.push_back((uint8_t)(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back((uint8_t)(0xE0 | (cp >> 12)));
        out.push_back((uint8_t)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((uint8_t)(0x80 | (cp & 0x3F)));
    } else {
        out.push_back((uint8_t)(0xF0 | (cp >> 18)));
        out.push_back((uint8_t)(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back((uint8_t)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((uint8_t)(0x80 | (cp & 0x3F)));
    }
}

void AddKeyDown(std::vector<int>& v, int key) {
    for (int k : v) if (k == key) return;
    v.push_back(key);
}
void RemoveKeyDown(std::vector<int>& v, int key) {
    for (size_t i = 0; i < v.size(); ++i) {
        if (v[i] == key) { v.erase(v.begin() + i); return; }
    }
}

// ===========================================================================
// 窗口模式（GLFW + GLES3）
// ===========================================================================
class GlfwContext : public RenderContext {
public:
    GlfwContext() = default;
    ~GlfwContext() override {
        if (m_window) glfwDestroyWindow(m_window);
        if (m_initialized && --glfwUsers == 0) glfwTerminate();
    }

    bool Init(const Options& opt) {
        if (glfwUsers == 0 && !glfwInit()) {
            std::fprintf(stderr, "[GlfwContext] glfwInit failed\n");
            return false;
        }
        ++glfwUsers;
        m_initialized = true;
        glfwDefaultWindowHints();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
        glfwWindowHint(GLFW_VISIBLE, opt.visible ? GLFW_TRUE : GLFW_FALSE);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

        m_window = glfwCreateWindow(opt.width, opt.height, opt.title.c_str(), nullptr, nullptr);
        if (!m_window) {
            const char* desc = nullptr;
            glfwGetError(&desc);
            std::fprintf(stderr, "[GlfwContext] window create failed: %s\n", desc ? desc : "unknown");
            return false;
        }
        glfwMakeContextCurrent(m_window);
        glfwSwapInterval(1);

        glfwSetWindowUserPointer(m_window, this);
        glfwSetKeyCallback(m_window, &GlfwContext::KeyCB);
        glfwSetCharCallback(m_window, &GlfwContext::CharCB);
        glfwSetMouseButtonCallback(m_window, &GlfwContext::MouseBtnCB);
        glfwSetCursorPosCallback(m_window, &GlfwContext::CursorPosCB);
        glfwSetScrollCallback(m_window, &GlfwContext::ScrollCB);
        return true;
    }

    void MakeCurrent() override { glfwMakeContextCurrent(m_window); }
    void SwapBuffers() override { glfwSwapBuffers(m_window); }
    void GetFramebufferSize(int& w, int& h) override { glfwGetFramebufferSize(m_window, &w, &h); }
    bool ShouldClose() const override { return glfwWindowShouldClose(m_window); }

    void PollEvents() override {
        glfwPollEvents();
        // 更新 mouseInside
        int w, h, fw, fh;
        glfwGetWindowSize(m_window, &w, &h);
        glfwGetFramebufferSize(m_window, &fw, &fh);
        double x, y;
        glfwGetCursorPos(m_window, &x, &y);
        // GLFW 光标是窗口逻辑坐标，呈现/letterbox 使用 framebuffer 像素。
        m_raw.mouseX = w > 0 ? x * fw / w : -1;
        m_raw.mouseY = h > 0 ? y * fh / h : -1;
        m_raw.mouseInside = glfwGetWindowAttrib(m_window, GLFW_FOCUSED) &&
                            x >= 0 && y >= 0 && x < w && y < h;
    }

    const RawInput& GetRawInput() const override { return m_raw; }
    void ClearInputEdges() override { m_raw.ClearEdges(); }
    double GetTime() const override { return glfwGetTime(); }

    void SetCursorVisible(bool v) override {
        glfwSetInputMode(m_window, GLFW_CURSOR, v ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_HIDDEN);
    }
    void SetMouseCaptured(bool cap) override {
        if (cap) {
            glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        } else {
            glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        }
    }
    bool IsMouseCaptured() const override {
        return glfwGetInputMode(m_window, GLFW_CURSOR) == GLFW_CURSOR_DISABLED;
    }
    bool IsHeadless() const override { return false; }

    RawInput m_raw;

private:
    static GlfwContext* Self(GLFWwindow* w) {
        return static_cast<GlfwContext*>(glfwGetWindowUserPointer(w));
    }
    static void KeyCB(GLFWwindow* w, int key, int, int action, int) {
        if (key < 0) return;
        auto* s = Self(w);
        if (action == GLFW_PRESS || action == GLFW_REPEAT) {
            AddKeyDown(s->m_raw.keysDown, key);
            s->m_raw.keysPressed.push_back(key);
        } else if (action == GLFW_RELEASE) {
            RemoveKeyDown(s->m_raw.keysDown, key);
            s->m_raw.keysReleased.push_back(key);
        }
    }
    static void CharCB(GLFWwindow* w, unsigned int cp) {
        PushUtf8(Self(w)->m_raw.charInput, cp);
    }
    static void MouseBtnCB(GLFWwindow* w, int button, int action, int) {
        int b = -1;
        if (button == GLFW_MOUSE_BUTTON_LEFT) b = 0;
        else if (button == GLFW_MOUSE_BUTTON_RIGHT) b = 1;
        else if (button == GLFW_MOUSE_BUTTON_MIDDLE) b = 2;
        if (b < 0) return;
        auto* s = Self(w);
        if (action == GLFW_PRESS) {
            s->m_raw.mouseDown[b] = true;
            s->m_raw.mousePressed[b] = true;
        } else {
            s->m_raw.mouseDown[b] = false;
            s->m_raw.mouseReleased[b] = true;
        }
    }
    static void CursorPosCB(GLFWwindow* w, double x, double y) {
        auto* s = Self(w);
        s->m_raw.mouseX = x;
        s->m_raw.mouseY = y;
    }
    static void ScrollCB(GLFWwindow* w, double, double yoff) {
        Self(w)->m_raw.scrollY += (float)yoff;
    }

    GLFWwindow* m_window = nullptr;
    bool m_initialized = false;
};

// ===========================================================================
// 无头模式（EGL surfaceless + GLES3）
// ===========================================================================
class EglHeadlessContext : public RenderContext {
public:
    EglHeadlessContext() = default;
    ~EglHeadlessContext() override {
        if (m_dpy != EGL_NO_DISPLAY) {
            if (m_ctx != EGL_NO_CONTEXT && eglGetCurrentContext() == m_ctx)
                eglMakeCurrent(m_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            if (m_surf != EGL_NO_SURFACE) eglDestroySurface(m_dpy, m_surf);
            if (m_ctx != EGL_NO_CONTEXT) eglDestroyContext(m_dpy, m_ctx);
            if (m_initialized && --eglUsers[m_dpy] == 0) {
                eglTerminate(m_dpy);
                eglUsers.erase(m_dpy);
            }
        }
    }

    bool Init() {
        m_dpy = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
        if (m_dpy == EGL_NO_DISPLAY) {
            m_dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        }
        if (m_dpy == EGL_NO_DISPLAY) {
            std::fprintf(stderr, "[EglHeadlessContext] no EGL display available\n");
            return false;
        }
        EGLint maj = 0, min = 0;
        if (!eglInitialize(m_dpy, &maj, &min)) {
            std::fprintf(stderr, "[EglHeadlessContext] eglInitialize failed (0x%x)\n", eglGetError());
            return false;
        }
        ++eglUsers[m_dpy];
        m_initialized = true;
        if (!eglBindAPI(EGL_OPENGL_ES_API)) return false;

        const EGLint cfgAttr[] = {
            EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
            EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
            EGL_NONE
        };
        EGLint n = 0;
        if (!eglChooseConfig(m_dpy, cfgAttr, &m_cfg, 1, &n) || n == 0) {
            std::fprintf(stderr, "[EglHeadlessContext] eglChooseConfig failed (0x%x)\n", eglGetError());
            return false;
        }
        const EGLint ctxAttr[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
        m_ctx = eglCreateContext(m_dpy, m_cfg, EGL_NO_CONTEXT, ctxAttr);
        if (m_ctx == EGL_NO_CONTEXT) {
            std::fprintf(stderr, "[EglHeadlessContext] eglCreateContext failed (0x%x)\n", eglGetError());
            return false;
        }
        const EGLint pbAttr[] = { EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE };
        m_surf = eglCreatePbufferSurface(m_dpy, m_cfg, pbAttr);
        if (m_surf == EGL_NO_SURFACE) {
            std::fprintf(stderr, "[EglHeadlessContext] eglCreatePbufferSurface failed (0x%x)\n", eglGetError());
            return false;
        }
        if (!eglMakeCurrent(m_dpy, m_surf, m_surf, m_ctx)) {
            std::fprintf(stderr, "[EglHeadlessContext] eglMakeCurrent failed (0x%x)\n", eglGetError());
            return false;
        }
        return true;
    }

    void MakeCurrent() override { eglMakeCurrent(m_dpy, m_surf, m_surf, m_ctx); }
    void SwapBuffers() override {}
    void GetFramebufferSize(int& w, int& h) override {
        EGLint pw = 0, ph = 0;
        eglQuerySurface(m_dpy, m_surf, EGL_WIDTH, &pw);
        eglQuerySurface(m_dpy, m_surf, EGL_HEIGHT, &ph);
        w = pw; h = ph;
    }
    bool ShouldClose() const override { return false; }
    void PollEvents() override { /* 输入来自 Inject */ }
    const RawInput& GetRawInput() const override { return m_raw; }
    void ClearInputEdges() override { m_raw.ClearEdges(); }
    double GetTime() const override {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - m_start).count();
    }
    void SetCursorVisible(bool) override {}
    void SetMouseCaptured(bool captured) override { m_captured = captured; }
    bool IsMouseCaptured() const override { return m_captured; }
    bool IsHeadless() const override { return true; }

    // ---- 注入输入 ----
    void InjectKey(int key, bool down) override {
        if (key < 0) return;
        bool held = std::find(m_raw.keysDown.begin(), m_raw.keysDown.end(), key) != m_raw.keysDown.end();
        if (held == down) return;
        if (down) {
            AddKeyDown(m_raw.keysDown, key);
            m_raw.keysPressed.push_back(key);
        } else {
            RemoveKeyDown(m_raw.keysDown, key);
            m_raw.keysReleased.push_back(key);
        }
    }
    void InjectMouseButton(MouseButton b, bool down) override {
        int i = (int)b;
        if (i < 0 || i >= 3 || m_raw.mouseDown[i] == down) return;
        if (down) { m_raw.mouseDown[i] = true; m_raw.mousePressed[i] = true; }
        else { m_raw.mouseDown[i] = false; m_raw.mouseReleased[i] = true; }
    }
    void InjectCursorPos(double x, double y) override { m_raw.mouseX = x; m_raw.mouseY = y; }
    void InjectScroll(double dy) override { m_raw.scrollY += (float)dy; }
    void InjectChar(unsigned int cp) override { PushUtf8(m_raw.charInput, cp); }

    RawInput m_raw;

private:
    bool m_initialized = false;
    bool m_captured = false;
    std::chrono::steady_clock::time_point m_start = std::chrono::steady_clock::now();
    EGLDisplay m_dpy = EGL_NO_DISPLAY;
    EGLConfig  m_cfg = nullptr;
    EGLContext m_ctx = EGL_NO_CONTEXT;
    EGLSurface m_surf = EGL_NO_SURFACE;
};

} // namespace

std::unique_ptr<RenderContext> RenderContext::CreateWindowed(const Options& opt) {
    auto ctx = std::make_unique<GlfwContext>();
    if (!ctx->Init(opt)) return nullptr;
    return ctx;
}

std::unique_ptr<RenderContext> RenderContext::CreateHeadless() {
    auto ctx = std::make_unique<EglHeadlessContext>();
    if (!ctx->Init()) return nullptr;
    return ctx;
}

} // namespace ui
