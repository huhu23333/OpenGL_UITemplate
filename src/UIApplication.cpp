#include "ui/UIApplication.h"
#include "ui/CanvasGroup.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>

namespace ui {

static double MsSince(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

UIApplication::UIApplication() = default;

UIApplication::~UIApplication() {
    Shutdown();
}

bool UIApplication::Init(const Config& cfg) {
    Shutdown();
    if (cfg.renderWidth <= 0 || cfg.renderHeight <= 0 ||
        (cfg.showWindow && (cfg.windowWidth <= 0 || cfg.windowHeight <= 0))) return false;
    m_cfg = cfg;

    if (cfg.showWindow) {
        RenderContext::Options opt;
        opt.width = cfg.windowWidth;
        opt.height = cfg.windowHeight;
        opt.title = cfg.title;
        opt.visible = true;
        m_ctx = RenderContext::CreateWindowed(opt);
    } else {
        m_ctx = RenderContext::CreateHeadless();
    }
    if (!m_ctx) return false;
    m_ctx->MakeCurrent();

    m_renderer = std::make_unique<Renderer2D>();
    if (!m_renderer->Init(cfg.renderWidth, cfg.renderHeight)) { Shutdown(); return false; }

    // 离屏 FBO
    m_offscreenTex = std::make_unique<Texture>();
    if (!m_offscreenTex->CreateEmpty(cfg.renderWidth, cfg.renderHeight)) { Shutdown(); return false; }
    glGenFramebuffers(1, &m_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           m_offscreenTex->Id(), 0);
    bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!complete) { Shutdown(); return false; }
    // 离屏纹理启用 mipmap + 三线性过滤，使呈现时缩小方向也正确抗锯齿
    m_offscreenTex->GenerateMipmaps();
    SetRoot(std::make_unique<Component>());
    m_frameDirty = false;
    m_stats = FrameStats{};
    m_layoutValid = false;
    return true;
}

void UIApplication::Shutdown() {
    if (m_ctx) {
        m_ctx->MakeCurrent();
        if (m_ctx->IsMouseCaptured()) m_ctx->SetMouseCaptured(false);
    }
    m_root.reset();
    if (m_fbo) { glDeleteFramebuffers(1, &m_fbo); m_fbo = 0; }
    m_offscreenTex.reset();
    m_renderer.reset();
    m_ctx.reset();
    m_frameBGRA.release();
    m_readback.release();
    m_frameDirty = false;
    m_stats = FrameStats{};
    m_layoutValid = false;
    if (Component::GlobalInput() == &m_input) Component::SetGlobalInput(nullptr);
    m_input = InputState{};
    m_hover = m_captured = m_focus = nullptr;
    for (int i = 0; i < 3; ++i) { m_pressed[i] = nullptr; m_pressedLife[i].reset(); }
    m_hoverLife.reset(); m_captureLife.reset(); m_focusLife.reset();
}

void UIApplication::SetRoot(std::unique_ptr<Component> root) {
    if (m_ctx) {
        m_ctx->MakeCurrent();
        if (m_ctx->IsMouseCaptured()) m_ctx->SetMouseCaptured(false);
    }
    m_hover = m_captured = m_focus = nullptr;
    for (int i = 0; i < 3; ++i) { m_pressed[i] = nullptr; m_pressedLife[i].reset(); }
    m_hoverLife.reset(); m_captureLife.reset(); m_focusLife.reset();
    m_root = std::move(root);
    if (m_root) m_root->SetRect(0, 0, (float)m_cfg.renderWidth, (float)m_cfg.renderHeight);
}

bool UIApplication::IsActive(Component* c, const std::weak_ptr<void>& life) const {
    if (!c || life.expired()) return false;
    auto contains = [&](auto&& self, Component* node) -> bool {
        if (!node || !node->IsVisible()) return false;
        if (node == c) return true;
        for (const auto& child : node->GetChildren()) if (self(self, child.get())) return true;
        return false;
    };
    return contains(contains, m_root.get());
}

void UIApplication::SetFocus(Component* c) {
    if (c && (!c->IsFocusable() || !IsActive(c, c->Lifetime()))) c = nullptr;
    if (m_focus == c && !m_focusLife.expired()) return;
    Component* previous = m_focus;
    auto previousLife = m_focusLife;
    m_focus = c;
    m_focusLife = c ? c->Lifetime() : std::weak_ptr<void>{};
    if (previous && !previousLife.expired()) previous->SetFocus(false);
    if (m_focus && !m_focusLife.expired()) m_focus->SetFocus(true);
}

void UIApplication::ValidateInputTargets() {
    if (!IsActive(m_hover, m_hoverLife)) {
        auto* old = m_hover;
        auto life = m_hoverLife;
        m_hover = nullptr;
        if (old && !life.expired()) old->OnMouseLeave();
    }
    if (!IsActive(m_focus, m_focusLife)) SetFocus(nullptr);
    for (int b = 0; b < 3; ++b) {
        if (!IsActive(m_pressed[b], m_pressedLife[b])) {
            auto* old = m_pressed[b];
            auto life = m_pressedLife[b];
            m_pressed[b] = nullptr;
            if (old && !life.expired()) old->OnMouseCancel((MouseButton)b);
        }
    }
    if (m_captured && (!IsActive(m_captured, m_captureLife) || !m_captured->WantsMouseCapture())) {
        auto* old = m_captured;
        auto life = m_captureLife;
        m_captured = nullptr;
        m_captureLife.reset();
        // 持久捕获可能已没有按下所有者；隐藏/移除时仍须释放系统光标。
        if (m_ctx && m_ctx->IsMouseCaptured()) m_ctx->SetMouseCaptured(false);
        if (!life.expired() && old->WantsMouseCapture()) {
            old->SetMouseCapture(false);
            old->OnMouseCancel(MouseButton::Left);
        }
    }
}

bool UIApplication::ShouldClose() const {
    return m_ctx ? m_ctx->ShouldClose() : true;
}

// ---------------------------------------------------------------------------
// 帧循环
// ---------------------------------------------------------------------------
void UIApplication::ResetStepStats() {
    m_stats = FrameStats{};
}

void UIApplication::PullRendererStats() {
    if (!m_renderer) return;
    const auto& rs = m_renderer->GetStats();
    m_stats.drawCalls = rs.drawCalls;
    m_stats.quads = rs.quads;
    m_stats.textureBinds = rs.textureBinds;
    m_stats.scissorChanges = rs.scissorChanges;
    m_stats.scissorSkips = rs.scissorSkips;
    m_stats.vertices = rs.vertices;
}

void UIApplication::EnsureLayout() {
    if (!m_root) return;
    if (m_layoutValid && m_layoutEpoch == Component::LayoutEpoch()) return;
    auto t0 = std::chrono::steady_clock::now();
    Vec2 origin(0.0f, 0.0f);
    Rect full(0.0f, 0.0f, (float)m_cfg.renderWidth, (float)m_cfg.renderHeight);
    m_root->SetRect(0, 0, full.w, full.h);
    m_root->Layout(Vec2(full.w, full.h), origin, full, origin);
    m_layoutValid = true;
    m_layoutEpoch = Component::LayoutEpoch();
    m_stats.layoutMs += MsSince(t0);
    ++m_stats.layoutCalls;
}

void UIApplication::RunFrame() {
    if (!m_ctx) return;
    ResetStepStats();

    auto t0 = std::chrono::steady_clock::now();
    m_ctx->PollEvents();
    m_ctx->MakeCurrent();
    BuildInput();
    m_stats.buildInputMs = MsSince(t0);

    // 每帧至少完整布局一次：自定义 OnLayoutSelf 可能依赖组件外部状态。
    // 帧内后续布局（输入分发 / 渲染前）由布局代数去重，通常不再发生。
    m_layoutValid = false;
    EnsureLayout();

    auto t1 = std::chrono::steady_clock::now();
    DispatchInput();
    m_stats.dispatchMs = MsSince(t1);

    RenderOffscreenImpl();
    Present();
    if (m_ctx) m_ctx->ClearInputEdges();
}

void UIApplication::RenderOffscreen() {
    if (!m_ctx || !m_renderer) return;
    ResetStepStats();
    m_ctx->MakeCurrent();
    EnsureLayout();
    RenderOffscreenImpl();
}

void UIApplication::RenderOffscreenImpl() {
    if (!m_ctx || !m_renderer) return;
    auto t0 = std::chrono::steady_clock::now();
    m_ctx->MakeCurrent();
    Component::SetGlobalInput(&m_input);
    EnsureLayout();

    GLint draw, read, viewport[4], scissor[4];
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
    glGetIntegerv(GL_VIEWPORT, viewport);
    glGetIntegerv(GL_SCISSOR_BOX, scissor);
    GLboolean scissorEnabled = glIsEnabled(GL_SCISSOR_TEST);

    m_renderer->ResetStats();
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    m_renderer->BeginFrame(m_cfg.renderWidth, m_cfg.renderHeight);
    const Color& c = m_cfg.clearColor;
    glClearColor(c.r * c.a, c.g * c.a, c.b * c.a, c.a);
    glClear(GL_COLOR_BUFFER_BIT);
    if (m_root) {
        Rect full(0.0f, 0.0f, (float)m_cfg.renderWidth, (float)m_cfg.renderHeight);
        m_root->Render(*m_renderer, full);
    }
    m_renderer->EndFrame();

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)draw);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)read);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    glScissor(scissor[0], scissor[1], scissor[2], scissor[3]);
    if (scissorEnabled) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);

    PullRendererStats();
    m_frameDirty = true;  // 内容已更新，但读回推迟到 GetFrame/GetFrameBGRA
    m_stats.renderMs += MsSince(t0);
}

// 懒读回：只有真正需要像素时才 glReadPixels，并把翻转 / RGBA→BGRA / 去预乘合并为一次遍历。
void UIApplication::CaptureFrame() const {
    if (!m_frameDirty || !m_ctx || !m_renderer) return;
    auto t0 = std::chrono::steady_clock::now();
    m_ctx->MakeCurrent();
    const int w = m_cfg.renderWidth;
    const int h = m_cfg.renderHeight;
    if (m_readback.rows != h || m_readback.cols != w || m_readback.type() != CV_8UC4)
        m_readback.create(h, w, CV_8UC4);

    GLint readFbo = 0, packBuffer = 0, alignment = 4, rowLength = 0, skipRows = 0, skipPixels = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFbo);
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &packBuffer);
    glGetIntegerv(GL_PACK_ALIGNMENT, &alignment);
    glGetIntegerv(GL_PACK_ROW_LENGTH, &rowLength);
    glGetIntegerv(GL_PACK_SKIP_ROWS, &skipRows);
    glGetIntegerv(GL_PACK_SKIP_PIXELS, &skipPixels);

    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_fbo);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    glPixelStorei(GL_PACK_SKIP_ROWS, 0);
    glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, m_readback.data);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)readFbo);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, (GLuint)packBuffer);
    glPixelStorei(GL_PACK_ALIGNMENT, alignment);
    glPixelStorei(GL_PACK_ROW_LENGTH, rowLength);
    glPixelStorei(GL_PACK_SKIP_ROWS, skipRows);
    glPixelStorei(GL_PACK_SKIP_PIXELS, skipPixels);

    if (m_frameBGRA.rows != h || m_frameBGRA.cols != w || m_frameBGRA.type() != CV_8UC4)
        m_frameBGRA.create(h, w, CV_8UC4);
    // 去预乘仅在画布背景半透明时需要；不透明背景下 alpha 恒为 1，可直接跳过。
    const bool unpremultiply = m_cfg.clearColor.a < 1.0f;
    if (!unpremultiply) {
        // OpenCV 的 SIMD 通道交换与翻转明显快于手写融合循环（实测 1080p 约 0.65ms vs 1.34ms）。
        cv::cvtColor(m_readback, m_frameBGRA, cv::COLOR_RGBA2BGRA);
        cv::flip(m_frameBGRA, m_frameBGRA, 0);  // 支持原地翻转
    } else {
        for (int y = 0; y < h; ++y) {
            const uint8_t* src = m_readback.ptr<uint8_t>(h - 1 - y);
            uint8_t* dst = m_frameBGRA.ptr<uint8_t>(y);
            for (int x = 0; x < w; ++x, src += 4, dst += 4) {
                const unsigned a = src[3];
                dst[0] = a ? (uint8_t)std::min(255u, (unsigned(src[2]) * 255u + a / 2) / a) : 0;
                dst[1] = a ? (uint8_t)std::min(255u, (unsigned(src[1]) * 255u + a / 2) / a) : 0;
                dst[2] = a ? (uint8_t)std::min(255u, (unsigned(src[0]) * 255u + a / 2) / a) : 0;
                dst[3] = src[3];
            }
        }
    }
    m_frameDirty = false;
    m_stats.captureMs += MsSince(t0);
}

void UIApplication::Present() {
    if (!m_ctx || m_ctx->IsHeadless() || !m_offscreenTex) return;
    auto t0 = std::chrono::steady_clock::now();

    int ww = 0, wh = 0;
    m_ctx->GetFramebufferSize(ww, wh);
    if (ww <= 0 || wh <= 0) return;

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    m_renderer->BeginFrame(ww, wh);
    glClearColor(m_cfg.letterboxColor.r, m_cfg.letterboxColor.g,
                 m_cfg.letterboxColor.b, m_cfg.letterboxColor.a);
    glClear(GL_COLOR_BUFFER_BIT);

    float scale = std::min((float)ww / m_cfg.renderWidth, (float)wh / m_cfg.renderHeight);
    float dw = m_cfg.renderWidth * scale;
    float dh = m_cfg.renderHeight * scale;
    float ox = ((float)ww - dw) * 0.5f;
    float oy = ((float)wh - dh) * 0.5f;
    // 仅在缩小呈现时需要 mipmap（避免欠采样锯齿）；放大时 MAG_FILTER 生效，
    // 逐帧重建 mipmap 纯属浪费。
    if (scale < 1.0f) m_offscreenTex->GenerateMipmaps();
    m_renderer->DrawTexture(m_offscreenTex.get(), ox, oy, dw, dh);
    m_renderer->EndFrame();

    m_ctx->SwapBuffers();
    m_stats.presentMs += MsSince(t0);
}

// ---------------------------------------------------------------------------
// 输出
// ---------------------------------------------------------------------------
cv::Mat UIApplication::GetFrame() const {
    CaptureFrame();
    cv::Mat bgr;
    if (!m_frameBGRA.empty()) cv::cvtColor(m_frameBGRA, bgr, cv::COLOR_BGRA2BGR);
    return bgr;
}

cv::Mat UIApplication::GetFrameBGRA() const {
    CaptureFrame();
    return m_frameBGRA.clone();
}

// ---------------------------------------------------------------------------
// 输入注入（无头模式）
// ---------------------------------------------------------------------------
void UIApplication::InjectKey(int key, bool down) { if (m_ctx) m_ctx->InjectKey(key, down); }
void UIApplication::InjectMouseButton(MouseButton b, bool down) { if (m_ctx) m_ctx->InjectMouseButton(b, down); }
void UIApplication::InjectMousePos(float x, float y) { if (m_ctx) m_ctx->InjectCursorPos(x, y); }
void UIApplication::InjectScroll(float dy) { if (m_ctx) m_ctx->InjectScroll(dy); }
void UIApplication::InjectChar(unsigned int codepoint) { if (m_ctx) m_ctx->InjectChar(codepoint); }

// ---------------------------------------------------------------------------
// 坐标变换
// ---------------------------------------------------------------------------
void UIApplication::WindowToImage(double wx, double wy, float& ix, float& iy, bool& inside) {
    int ww = 0, wh = 0;
    m_ctx->GetFramebufferSize(ww, wh);
    if (ww <= 0 || wh <= 0) { inside = false; ix = iy = -1.0f; return; }

    float scale = std::min((float)ww / m_cfg.renderWidth, (float)wh / m_cfg.renderHeight);
    float dw = m_cfg.renderWidth * scale;
    float dh = m_cfg.renderHeight * scale;
    float ox = ((float)ww - dw) * 0.5f;
    float oy = ((float)wh - dh) * 0.5f;
    ix = (float)((wx - ox) / scale);
    iy = (float)((wy - oy) / scale);
    inside = ix >= 0.0f && iy >= 0.0f && ix < (float)m_cfg.renderWidth && iy < (float)m_cfg.renderHeight;
}

// ---------------------------------------------------------------------------
// 输入构建
// ---------------------------------------------------------------------------
void UIApplication::BuildInput() {
    const RawInput& raw = m_ctx->GetRawInput();

    if (m_ctx->IsHeadless()) {
        m_input.mouseX = (float)raw.mouseX;
        m_input.mouseY = (float)raw.mouseY;
        m_input.mouseInside = raw.mouseInside && m_input.mouseX >= 0 && m_input.mouseY >= 0 &&
                              m_input.mouseX < m_cfg.renderWidth && m_input.mouseY < m_cfg.renderHeight;
    } else {
        bool inside = false;
        WindowToImage(raw.mouseX, raw.mouseY, m_input.mouseX, m_input.mouseY, inside);
        m_input.mouseInside = inside && raw.mouseInside;
    }

    for (int i = 0; i < 3; ++i) {
        m_input.mouseDown[i] = raw.mouseDown[i];
        m_input.mousePressed[i] = raw.mousePressed[i];
        m_input.mouseReleased[i] = raw.mouseReleased[i];
    }
    m_input.keysDown = raw.keysDown;
    m_input.keysPressed = raw.keysPressed;
    m_input.keysReleased = raw.keysReleased;
    m_input.charInput = raw.charInput;
    m_input.scrollY = raw.scrollY;

    Component::SetGlobalInput(&m_input);
}

// ---------------------------------------------------------------------------
// 事件分发
// ---------------------------------------------------------------------------
void UIApplication::DispatchInput() {
    ValidateInputTargets();
    auto hit = [&]() -> Component* {
        if (!m_root || !m_input.mouseInside) return nullptr;
        // 布局在本帧开始时已完成；只有几何 setter 改过布局代数才会补算。
        EnsureLayout();
        return m_root->HitTest(m_input.mouseX, m_input.mouseY);
    };
    auto updateHover = [&]() {
        ValidateInputTargets();
        Component* target = hit();
        if (target == m_hover) return;
        auto* old = m_hover;
        m_hover = nullptr;
        if (old && !m_hoverLife.expired()) old->OnMouseLeave();
        target = hit();
        m_hover = target;
        m_hoverLife = target ? target->Lifetime() : std::weak_ptr<void>{};
        if (target) target->OnMouseEnter();
    };
    updateHover();

    // 先分发移动，保证释放帧的最终拖动位置不丢失。
    Component* moving = m_captured ? m_captured : hit();
    if (moving) {
        Vec2 lp = moving->GetMouseLocal();
        moving->OnMouseMove(lp.x, lp.y);
    }
    ValidateInputTargets();

    for (int b = 0; b < 3; ++b) {
        if (m_input.mousePressed[b]) {
            Component* t = m_captured ? m_captured : hit();
            while (t) {
                auto life = t->Lifetime();
                Vec2 lp = t->GetMouseLocal();
                bool consumed = t->OnMouseDown((MouseButton)b, lp.x, lp.y);
                if (!IsActive(t, life)) { t = nullptr; break; }
                if (consumed || t->WantsMouseCapture()) {
                    m_pressed[b] = t;
                    m_pressedLife[b] = life;
                    if (t->WantsMouseCapture()) { m_captured = t; m_captureLife = life; }
                    break;
                }
                t = t->GetParent();
            }
            if (b == (int)MouseButton::Left) SetFocus(t && t->IsFocusable() ? t : nullptr);
        }
        ValidateInputTargets();
        if (m_input.mouseReleased[b]) {
            // 释放发送给对应按下所有者，而不是当前悬停组件。
            Component* t = m_pressed[b];
            auto life = m_pressedLife[b];
            m_pressed[b] = nullptr;
            m_pressedLife[b].reset();
            if (IsActive(t, life)) {
                Vec2 lp = t->GetMouseLocal();
                t->OnMouseUp((MouseButton)b, lp.x, lp.y);
            }
            // 捕获由组件的请求状态决定，与其他鼠标键是否仍按下无关；
            // 拖拽组件在主键抬起时撤销请求，持久捕获组件则可继续持有。
            ValidateInputTargets();
        }
    }

    // 滚轮向父链传播，遇到最近滚动容器停止，避免嵌套容器同时滚动。
    if (m_input.scrollY != 0.0f) {
        for (Component* t = hit(); t;) {
            auto life = t->Lifetime();
            bool scrollView = dynamic_cast<ScrollView*>(t) != nullptr;
            t->OnScroll(m_input.scrollY);
            if (!IsActive(t, life) || scrollView) break;
            t = t->GetParent();
        }
    }
    const auto pressedKeys = m_input.keysPressed;
    const auto releasedKeys = m_input.keysReleased;
    for (int k : pressedKeys) {
        ValidateInputTargets();
        if (m_focus) m_focus->OnKeyDown(k);
    }
    for (int k : releasedKeys) {
        ValidateInputTargets();
        if (m_focus) m_focus->OnKeyUp(k);
    }
    const auto chars = m_input.charInput;
    for (size_t i = 0; i < chars.size();) {
        uint8_t b0 = chars[i];
        size_t len = b0 < 0x80 ? 1 : b0 >= 0xC2 && b0 <= 0xDF ? 2 :
                     b0 >= 0xE0 && b0 <= 0xEF ? 3 : b0 >= 0xF0 && b0 <= 0xF4 ? 4 : 0;
        if (!len || i + len > chars.size()) { ++i; continue; }
        unsigned cp = b0 & (len == 1 ? 0x7f : (1u << (7 - len)) - 1);
        bool valid = true;
        for (size_t j = 1; j < len; ++j) {
            if ((chars[i + j] & 0xC0) != 0x80) valid = false;
            cp = (cp << 6) | (chars[i + j] & 0x3f);
        }
        if ((len == 2 && cp < 0x80) || (len == 3 && cp < 0x800) ||
            (len == 4 && cp < 0x10000) || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) valid = false;
        if (!valid) { ++i; continue; }
        ValidateInputTargets();
        if (m_focus) m_focus->OnChar(std::string(chars.begin() + i, chars.begin() + i + len));
        i += len;
    }
    updateHover();
}

} // namespace ui
