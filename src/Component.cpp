#include "ui/Component.h"
#include "ui/Renderer2D.h"

namespace ui {

const InputState* Component::s_globalInput = nullptr;

Component::Component() = default;
Component::~Component() = default;

// ---------------------------------------------------------------------------
// 布局：根据"比例+偏移"计算实际像素位置/尺寸，并计算渲染/屏幕绝对矩形
// ---------------------------------------------------------------------------
void Component::Layout(const Vec2& parentSize, const Vec2& renderOrigin, const Rect& renderClip, const Vec2& screenOrigin) {
    // 实际像素 = 父尺寸 × 比例 + 绝对增减值
    m_localX = parentSize.x * m_posXRatio + m_posXOffset;
    m_localY = parentSize.y * m_posYRatio + m_posYOffset;
    m_width  = parentSize.x * m_wRatio + m_wOffset;
    m_height = parentSize.y * m_hRatio + m_hOffset;

    m_abs.x = renderOrigin.x + m_localX;
    m_abs.y = renderOrigin.y + m_localY;
    m_abs.w = m_width;
    m_abs.h = m_height;

    m_screenAbs.x = screenOrigin.x + m_localX;
    m_screenAbs.y = screenOrigin.y + m_localY;
    m_screenAbs.w = m_width;
    m_screenAbs.h = m_height;
    m_screenClip = Rect(renderClip.x + screenOrigin.x - renderOrigin.x,
                        renderClip.y + screenOrigin.y - renderOrigin.y, renderClip.w, renderClip.h);

    OnLayoutSelf();  // 子类更新依赖自身尺寸的状态（如虚拟画布尺寸）

    Vec2 childParentSize = ChildLayoutSize();
    Vec2 childRenderOrigin = ChildRenderOrigin();
    Rect childClip = ChildClip(renderClip);
    Vec2 childScreenOrigin = ChildScreenOrigin();
    for (auto& c : m_children) {
        c->Layout(childParentSize, childRenderOrigin, childClip, childScreenOrigin);
    }
}

// ---------------------------------------------------------------------------
// 默认渲染：自身（含自透明度）→ 子组件递归，scissor 裁剪到自身与父裁剪交集
// ---------------------------------------------------------------------------
void Component::Render(Renderer2D& r, const Rect& clip) {
    if (!m_visible) return;
    Rect myClip = m_abs.Intersect(clip);
    if (!myClip.IsEmpty()) {
        r.PushScissor(myClip.x, myClip.y, myClip.w, myClip.h);
        r.PushTranslate(m_abs.x, m_abs.y);
        r.SetOpacity(m_opacity);
        OnDraw(r);
        r.SetOpacity(1.0f);
        r.PopTranslate();
        r.PopScissor();
    }

    Rect childClip = ChildClip(clip);
    if (childClip.IsEmpty()) return;
    r.PushScissor(childClip.x, childClip.y, childClip.w, childClip.h);
    for (auto& c : m_children) {
        c->Render(r, childClip);
    }
    r.PopScissor();
}

// ---------------------------------------------------------------------------
// 命中测试：基于屏幕空间绝对矩形，后添加的子组件优先（在上层）
// ---------------------------------------------------------------------------
Component* Component::HitTest(float sx, float sy) {
    if (!m_visible) return nullptr;
    if (!HitBounds().Contains(sx, sy) || !m_screenClip.Contains(sx, sy)) return nullptr;
    for (auto it = m_children.rbegin(); it != m_children.rend(); ++it) {
        Component* hit = (*it)->HitTest(sx, sy);
        if (hit) return hit;
    }
    return this;
}

// ---------------------------------------------------------------------------
// 默认自绘：绘制边框（厚度>0 时）
// ---------------------------------------------------------------------------
void Component::OnDraw(Renderer2D& r) {
    if (m_borderThickness > 0.0f) {
        Vec2 s = SelfSize();
        r.StrokeRect(0.0f, 0.0f, s.x, s.y, m_borderThickness, m_borderColor);
    }
}

// ---------------------------------------------------------------------------
// 输入查询
// ---------------------------------------------------------------------------
Vec2 Component::GetMouseLocal() const {
    if (!s_globalInput) return {-1.0f, -1.0f};
    return {s_globalInput->mouseX - m_screenAbs.x, s_globalInput->mouseY - m_screenAbs.y};
}

bool Component::IsMouseInside() const {
    if (!s_globalInput || !s_globalInput->mouseInside) return false;
    float x = s_globalInput->mouseX, y = s_globalInput->mouseY;
    if (!m_screenAbs.Contains(x, y)) return false;
    for (auto* c = this; c; c = c->m_parent) {
        if (!c->m_visible || !c->HitBounds().Contains(x, y) || !c->m_screenClip.Contains(x, y)) return false;
    }
    return true;
}

bool Component::IsMouseDown(MouseButton b) const {
    return s_globalInput && s_globalInput->IsMouseDown(b);
}
bool Component::IsMouseClicked(MouseButton b) const {
    return s_globalInput && s_globalInput->IsMousePressed(b);
}
bool Component::IsMouseReleased(MouseButton b) const {
    return s_globalInput && s_globalInput->IsMouseReleased(b);
}
bool Component::IsKeyDown(int key) const {
    return s_globalInput && s_globalInput->IsKeyDown(key);
}
bool Component::IsKeyPressed(int key) const {
    return s_globalInput && s_globalInput->IsKeyPressed(key);
}
bool Component::IsKeyReleased(int key) const {
    return s_globalInput && s_globalInput->IsKeyReleased(key);
}
float Component::GetScrollY() const {
    return s_globalInput ? s_globalInput->scrollY : 0.0f;
}

} // namespace ui
