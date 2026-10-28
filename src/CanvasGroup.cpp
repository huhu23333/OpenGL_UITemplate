#include "ui/CanvasGroup.h"
#include "ui/Renderer2D.h"

#include <cmath>
#include <cstdio>

namespace ui {

// ===========================================================================
// CanvasGroup
// ===========================================================================
CanvasGroup::CanvasGroup() = default;

CanvasGroup::~CanvasGroup() {
    if (m_fbo) {
        glDeleteFramebuffers(1, &m_fbo);
        m_fbo = 0;
    }
}

void CanvasGroup::SetCanvasSize(float w, float h) {
    m_requestedCanvasW = std::isfinite(w) && w > 0.0f ? w : 0.0f;
    m_requestedCanvasH = std::isfinite(h) && h > 0.0f ? h : 0.0f;
    float cw = std::ceil(m_requestedCanvasW > 0 ? m_requestedCanvasW : std::max(0.0f, m_width));
    float ch = std::ceil(m_requestedCanvasH > 0 ? m_requestedCanvasH : std::max(0.0f, m_height));
    if (cw != m_canvasW || ch != m_canvasH) {
        m_canvasDirty = true;
        InvalidateLayout();  // 画布尺寸决定子组件布局空间
    }
    m_canvasW = cw;
    m_canvasH = ch;
    ClampScroll();
}

void CanvasGroup::SetScrollOffset(float dx, float dy) {
    m_scrollX = dx;
    m_scrollY = dy;
    ClampScroll();
    InvalidateLayout();  // 滚动偏移影响子组件屏幕坐标
}

void CanvasGroup::ClampScroll() {
    if (!std::isfinite(m_scrollX)) m_scrollX = 0.0f;
    if (!std::isfinite(m_scrollY)) m_scrollY = 0.0f;
    float maxX = m_canvasW - m_width;
    float maxY = m_canvasH - m_height;
    if (maxX < 0.0f) maxX = 0.0f;
    if (maxY < 0.0f) maxY = 0.0f;
    if (m_scrollX < 0.0f) m_scrollX = 0.0f;
    if (m_scrollX > maxX) m_scrollX = maxX;
    if (m_scrollY < 0.0f) m_scrollY = 0.0f;
    if (m_scrollY > maxY) m_scrollY = maxY;
}

void CanvasGroup::OnLayoutSelf() {
    SetCanvasSize(m_requestedCanvasW, m_requestedCanvasH);
}

void CanvasGroup::EnsureCanvas() {
    if (!m_canvasDirty) return;
    const GLint maxSize = Texture::MaxSize();
    if (m_canvasW <= 0 || m_canvasH <= 0 || m_canvasW > maxSize || m_canvasH > maxSize) return;

    GLint previousDraw = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousDraw);
    auto texture = std::make_unique<Texture>();
    if (!texture->CreateEmpty((int)m_canvasW, (int)m_canvasH)) return;
    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture->Id(), 0);
    bool complete = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)previousDraw);
    if (!complete) {
        glDeleteFramebuffers(1, &fbo);
        std::fprintf(stderr, "[CanvasGroup] incomplete framebuffer\n");
        return;
    }
    if (m_fbo) glDeleteFramebuffers(1, &m_fbo);
    m_fbo = fbo;
    m_canvasTex = std::move(texture);
    m_canvasDirty = false;
}

void CanvasGroup::OnDraw(Renderer2D& r) {
    if (m_canvasBg.a > 0.0f) {
        r.FillRect(0.0f, 0.0f, m_canvasW, m_canvasH, m_canvasBg);
    }
    Component::OnDraw(r);  // 画布范围内绘制边框
}

void CanvasGroup::Render(Renderer2D& r, const Rect& clip) {
    if (!m_visible) return;
    Rect myClip = m_abs.Intersect(clip);
    if (myClip.IsEmpty()) return;

    r.Flush();  // 重建纹理前必须提交引用旧纹理的批次。
    EnsureCanvas();
    if (m_canvasDirty || !m_fbo || !m_canvasTex) return;

    // ---- 1. 渲染自身与子组件到虚拟画布 ----
    GLint prevFbo = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_fbo);

    int cw = (int)(m_canvasW + 0.5f);
    int ch = (int)(m_canvasH + 0.5f);
    r.PushViewport(cw, ch);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    r.PushScissor(0.0f, 0.0f, m_canvasW, m_canvasH);
    r.PushTranslate(0.0f, 0.0f);
    r.SetOpacity(m_opacity);
    OnDraw(r);
    r.SetOpacity(1.0f);
    r.PopTranslate();
    Rect canvasClip(0.0f, 0.0f, m_canvasW, m_canvasH);
    for (auto& c : m_children) {
        c->Render(r, canvasClip);
    }
    r.PopScissor();

    r.PopViewport();
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)prevFbo);

    // ---- 2. 合成回父画布 ----
    r.PushScissor(myClip.x, myClip.y, myClip.w, myClip.h);
    Composite(r, myClip);
    r.PopScissor();
}

void CanvasGroup::Composite(Renderer2D& r, const Rect& myClip) {
    (void)myClip;
    if (!m_canvasTex) return;
    float vw = m_width;
    float vh = m_height;
    if (m_scrollX + vw > m_canvasW) vw = m_canvasW - m_scrollX;
    if (m_scrollY + vh > m_canvasH) vh = m_canvasH - m_scrollY;
    if (vw <= 0.0f || vh <= 0.0f) return;

    r.DrawTextureSub(m_canvasTex.get(), m_scrollX, m_scrollY, vw, vh,
                     m_abs.x, m_abs.y, vw, vh);
}

// ===========================================================================
// ScrollView
// ===========================================================================
ScrollView::ScrollView() {
    SetCanvasBackground({0.11f, 0.12f, 0.15f, 1.0f});
    SetBorder(1.0f, {0.35f, 0.40f, 0.50f, 1.0f});
}

void ScrollView::Render(Renderer2D& r, const Rect& clip) {
    CanvasGroup::Render(r, clip);
    if (!m_visible) return;
    Rect myClip = m_abs.Intersect(clip);
    if (myClip.IsEmpty()) return;
    DrawScrollbar(r, myClip);
}

float ScrollView::ThumbHeight() const {
    float maxScroll = m_canvasH - m_height;
    if (maxScroll <= 0.0f) return m_height;
    float h = m_height * (m_height / m_canvasH);
    return std::min(m_height, std::max(h, m_barW));
}

float ScrollView::ThumbY() const {
    float maxScroll = m_canvasH - m_height;
    if (maxScroll <= 0.0f) return 0.0f;
    float track = m_height - ThumbHeight();
    if (track <= 0.0f) return 0.0f;
    return (m_scrollY / maxScroll) * track;
}

bool ScrollView::IsOnScrollbar(float lx) const {
    return m_canvasH > m_height && m_barW > 0 && lx >= m_width - m_barW && lx < m_width;
}

Component* ScrollView::HitTest(float sx, float sy) {
    if (!m_visible || !m_screenAbs.Contains(sx, sy) || !m_screenClip.Contains(sx, sy)) return nullptr;
    // 滚动条最后绘制，也必须优先于内容命中。
    if (IsOnScrollbar(sx - m_screenAbs.x)) return this;
    return Component::HitTest(sx, sy);
}

void ScrollView::DrawScrollbar(Renderer2D& r, const Rect& myClip) {
    if (m_canvasH <= m_height) return;  // 内容未超出视口，无需滚动条
    r.PushScissor(myClip.x, myClip.y, myClip.w, myClip.h);
    r.SetOpacity(m_opacity);
    float bx = m_abs.x + m_width - m_barW;
    r.FillRect(bx, m_abs.y, m_barW, m_height, m_barColor);
    r.FillRect(bx, m_abs.y + ThumbY(), m_barW, ThumbHeight(), m_thumbColor);
    r.SetOpacity(1.0f);
    r.PopScissor();
}

bool ScrollView::OnMouseDown(MouseButton b, float lx, float ly) {
    if (b != MouseButton::Left) return false;
    if (!IsOnScrollbar(lx)) return false;
    m_draggingBar = true;
    SetMouseCapture(true);
    float th = ThumbHeight();
    float ty = ThumbY();
    if (ly < ty || ly > ty + th) {
        m_dragGrabOffset = th * 0.5f;  // 点击滚动条空白处：跳到该处
    } else {
        m_dragGrabOffset = ly - ty;
    }
    // 应用拖拽定位
    float track = m_height - th;
    float maxScroll = m_canvasH - m_height;
    if (track > 0.0f && maxScroll > 0.0f) {
        float t = (ly - m_dragGrabOffset) / track;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        SetScrollY(t * maxScroll);
    }
    return true;
}

void ScrollView::OnMouseMove(float lx, float ly) {
    (void)lx;
    if (!m_draggingBar) return;
    float track = m_height - ThumbHeight();
    float maxScroll = m_canvasH - m_height;
    if (track <= 0.0f || maxScroll <= 0.0f) return;
    float t = (ly - m_dragGrabOffset) / track;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    SetScrollY(t * maxScroll);
}

void ScrollView::OnMouseUp(MouseButton b, float lx, float ly) {
    (void)lx; (void)ly;
    if (b != MouseButton::Left) return;
    if (m_draggingBar) {
        m_draggingBar = false;
        SetMouseCapture(false);
    }
}

void ScrollView::OnScroll(float dy) {
    SetScrollY(m_scrollY - dy * 40.0f);
}

// ===========================================================================
// Dropdown
// ===========================================================================
Dropdown::Dropdown() {
    Rebuild();
}

void Dropdown::SetColors(const Color& normal, const Color& hover, const Color& pressed) {
    m_colorNormal = normal;
    m_colorHover = hover;
    m_colorPressed = pressed;
    if (m_headerButton) m_headerButton->SetColors(normal, hover, pressed);
}

void Dropdown::SetItems(const std::vector<std::string>& items) {
    m_items = items;
    if (m_selected >= (int)m_items.size()) {
        m_selected = m_items.empty() ? -1 : 0;
    }
    Rebuild();
}

void Dropdown::SetSelectedIndex(int i) {
    m_selected = (i >= 0 && i < (int)m_items.size()) ? i : -1;
    if (m_headerButton) m_headerButton->SetLabel(GetSelectedText());
}

const std::string& Dropdown::GetSelectedText() const {
    static const std::string kEmpty;
    if (m_selected >= 0 && m_selected < (int)m_items.size()) return m_items[m_selected];
    return kEmpty;
}

void Dropdown::SetOpen(bool open) {
    m_open = open;
    if (m_popup) m_popup->SetVisible(open);
}

Rect Dropdown::ChildClip(const Rect& parentClip) const {
    float totalH = m_headerH + (m_open ? (float)m_items.size() * m_itemHeight : 0.0f);
    Rect full(m_abs.x, m_abs.y, m_width, totalH);
    return full.Intersect(parentClip);
}

Rect Dropdown::HitBounds() const {
    float totalH = m_headerH + (m_open ? (float)m_items.size() * m_itemHeight : 0.0f);
    return Rect(m_screenAbs.x, m_screenAbs.y, m_width, totalH);
}

void Dropdown::OnLayoutSelf() {
    // 同步子组件尺寸到自身尺寸（此时 m_width/m_height 已由布局算出）
    m_headerH = std::max(0.0f, m_height);
    m_itemHeight = std::isfinite(m_itemHeight) ? std::max(0.0f, m_itemHeight) : 0.0f;
    if (m_headerButton) {
        m_headerButton->SetSize(m_width, m_headerH);
        m_headerButton->SetFontScale(m_fontScale);
        m_headerButton->SetTextSupersample(m_textSS);
    }
    if (m_popup) {
        float popupH = (float)m_items.size() * m_itemHeight;
        m_popup->SetRect(0, m_headerH, m_width, popupH);
        m_popup->SetCanvasSize(m_width, popupH);
        for (size_t i = 0; i < m_popup->GetChildCount(); ++i) {
            auto* b = static_cast<Button*>(m_popup->GetChild(i));
            b->SetRect(0, (float)i * m_itemHeight, m_width, m_itemHeight);
            b->SetFontScale(m_fontScale);
            b->SetTextSupersample(m_textSS);
        }
    }
}

void Dropdown::Rebuild() {
    m_children.clear();
    m_headerButton = nullptr;
    m_popup = nullptr;

    m_headerButton = AddChild<Button>();
    m_headerButton->SetRect(0.0f, 0.0f, m_width, m_headerH);
    m_headerButton->SetLabel(GetSelectedText());
    m_headerButton->SetFontScale(m_fontScale);
    m_headerButton->SetColors(m_colorNormal, m_colorHover, m_colorPressed);
    m_headerButton->SetOnClick([this](Button*) { SetOpen(!m_open); });

    m_popup = AddChild<CanvasGroup>();
    float popupH = (float)m_items.size() * m_itemHeight;
    m_popup->SetRect(0.0f, m_headerH, m_width, popupH);
    m_popup->SetCanvasSize(m_width, popupH);
    m_popup->SetCanvasBackground({0.09f, 0.10f, 0.13f, 1.0f});
    m_popup->SetVisible(m_open);

    for (size_t i = 0; i < m_items.size(); ++i) {
        Button* b = m_popup->AddChild<Button>();
        b->SetRect(0.0f, (float)i * m_itemHeight, m_width, m_itemHeight);
        b->SetLabel(m_items[i]);
        b->SetFontScale(m_fontScale);
        b->SetColors({0.12f, 0.14f, 0.18f, 1.0f},
                     {0.22f, 0.26f, 0.33f, 1.0f},
                     {0.30f, 0.36f, 0.44f, 1.0f});
        b->SetOnClick([this, i](Button*) {
            m_selected = (int)i;
            if (m_headerButton) m_headerButton->SetLabel(GetSelectedText());
            SetOpen(false);
            auto callback = m_onSelect;
            std::string item = m_items[i];
            if (callback) callback(this, (int)i, item);
        });
    }
}

} // namespace ui
