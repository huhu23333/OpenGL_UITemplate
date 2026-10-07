#ifndef UI_COMPONENT_H
#define UI_COMPONENT_H

#include "Types.h"
#include "InputState.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ui {

class Renderer2D;

// ---------------------------------------------------------------------------
// 组件基类：矩形组件层级。
//
// 关键约定：
//   - 坐标系：UI 约定为左上角原点、Y 向下，单位 = 逻辑像素。
//   - 每个组件有"相对父组件的像素位置"(local) 与 "宽高"，绝对矩形在布局时算出。
//   - 每个组件只绘制在其父组件范围内的像素（渲染时用 scissor 裁剪）。
//   - 绘制顺序：先绘制自身（OnDraw，默认描边），再按子组件顺序递归。
//   - 透明度(opacity)只影响自身绘制，不影响子组件。
//   - 命中测试 / 鼠标相对位置基于"屏幕空间绝对矩形"(m_screenAbs)，
//     渲染基于"渲染空间绝对矩形"(m_abs)（虚拟画布子组件二者不同）。
// ---------------------------------------------------------------------------
class Component {
public:
    Component();
    virtual ~Component();

    Component(const Component&) = delete;
    Component& operator=(const Component&) = delete;

    // ---- 层级 ----
    // 以当前组件为父，创建子组件（类型 T，构造参数转发），返回裸指针便于配置。
    template <typename T, typename... Args>
    T* AddChild(Args&&... args) {
        auto ptr = std::make_unique<T>(std::forward<Args>(args)...);
        T* raw = ptr.get();
        raw->m_parent = this;
        m_children.push_back(std::move(ptr));
        return raw;
    }
    Component* GetParent() const { return m_parent; }
    const std::vector<std::unique_ptr<Component>>& GetChildren() const { return m_children; }
    std::vector<std::unique_ptr<Component>>& GetChildren() { return m_children; }
    Component* GetChild(size_t i) const { return i < m_children.size() ? m_children[i].get() : nullptr; }
    size_t GetChildCount() const { return m_children.size(); }

    // ---- 几何（相对父组件尺寸的比例 + 绝对增减值，左上角原点，Y 向下）----
    // 实际像素 = 父组件相应尺寸 × 比例 + 绝对增减值。
    // 父组件尺寸变化时，比例部分自动缩放，绝对增减值部分保持不变。
    void SetPositionRatio(float xr, float yr) { m_posXRatio = xr; m_posYRatio = yr; }
    void SetPositionOffset(float x, float y) { m_posXOffset = x; m_posYOffset = y; }
    void SetSizeRatio(float wr, float hr) { m_wRatio = wr; m_hRatio = hr; }
    void SetSizeOffset(float w, float h) { m_wOffset = w; m_hOffset = h; }
    // 便捷：位置与尺寸均按父组件尺寸的比例设置（绝对增减值 = 0）
    void SetRectRatio(float xr, float yr, float wr, float hr) {
        SetPositionRatio(xr, yr);
        SetSizeRatio(wr, hr);
    }
    // 便捷：纯绝对像素设置（比例 = 0，向后兼容）
    void SetPosition(float x, float y) { m_posXRatio = 0; m_posYRatio = 0; m_posXOffset = x; m_posYOffset = y; }
    void SetSize(float w, float h) { m_wRatio = 0; m_hRatio = 0; m_wOffset = w; m_hOffset = h; }
    void SetRect(float x, float y, float w, float h) { SetPosition(x, y); SetSize(w, h); }
    // 查询布局后计算出的实际像素位置 / 尺寸
    void GetPosition(float& x, float& y) const { x = m_localX; y = m_localY; }
    void GetSize(float& w, float& h) const { w = m_width; h = m_height; }

    // 屏幕空间绝对矩形（输入/命中测试用），布局后有效
    const Rect& GetScreenRect() const { return m_screenAbs; }

    // ---- 可见性 ----
    void SetVisible(bool v) { m_visible = v; }
    bool IsVisible() const { return m_visible; }

    // ---- 透明度（仅影响自身绘制）----
    void SetOpacity(float a) { m_opacity = a < 0 ? 0 : (a > 1 ? 1 : a); }
    float GetOpacity() const { return m_opacity; }

    // ---- 默认描边（厚度=0 不绘制，颜色默认纯白）----
    void SetBorder(float thickness, const Color& color) { m_borderThickness = thickness; m_borderColor = color; }
    void SetBorderThickness(float t) { m_borderThickness = t; }
    void SetBorderColor(const Color& c) { m_borderColor = c; }
    float GetBorderThickness() const { return m_borderThickness; }
    const Color& GetBorderColor() const { return m_borderColor; }

    // ---- 鼠标捕获：捕获后即使光标移出范围仍持续收到鼠标事件（滑块拖拽等）----
    void SetMouseCapture(bool capture) { m_captureMouse = capture; }
    bool WantsMouseCapture() const { return m_captureMouse; }

    // ---- 键盘焦点 ----
    virtual bool IsFocusable() const { return false; }
    virtual void SetFocus(bool focused) { (void)focused; }

    // ======================= 输入查询（依赖全局 InputState） =======================
    static void SetGlobalInput(const InputState* s) { s_globalInput = s; }
    static const InputState* GlobalInput() { return s_globalInput; }
    const InputState* GetInput() const { return s_globalInput; }

    // 当前鼠标在本组件内的相对位置（相对自身左上角，像素，Y 向下）；
    // 鼠标不在本组件内时返回其屏幕坐标相对值（仍可查询，但 IsMouseInside() 为 false）
    Vec2 GetMouseLocal() const;
    // 鼠标是否位于本组件屏幕矩形内
    bool IsMouseInside() const;
    // 鼠标按钮状态 / 边沿
    bool IsMouseDown(MouseButton b) const;
    bool IsMouseClicked(MouseButton b) const;
    bool IsMouseReleased(MouseButton b) const;
    // 键盘状态 / 边沿
    bool IsKeyDown(int key) const;
    bool IsKeyPressed(int key) const;
    bool IsKeyReleased(int key) const;
    // 本帧滚轮增量
    float GetScrollY() const;

    // ======================= 内部：由 UIApplication 调用 =======================
    // 布局：根据"比例+偏移"计算实际像素位置/尺寸，并算出 m_abs（渲染空间）
    // 与 m_screenAbs（屏幕空间）。parentSize 为父组件提供的布局空间尺寸。
    // renderOrigin/renderClip：父组件渲染空间的坐标原点与裁剪矩形；
    // screenOrigin：父组件屏幕空间坐标原点。
    virtual void Layout(const Vec2& parentSize, const Vec2& renderOrigin, const Rect& renderClip, const Vec2& screenOrigin);

    // 递归渲染：自身 + 子组件（含 scissor 裁剪、自身透明度）。
    // 虚拟画布容器会重写此方法以重定向子组件到独立 FBO。
    virtual void Render(Renderer2D& r, const Rect& clip);

    // 命中测试：返回屏幕上最深命中组件（sx/sy 为屏幕空间坐标）
    virtual Component* HitTest(float sx, float sy);
    std::weak_ptr<void> Lifetime() const { return m_lifetime; }

protected:
    // ---- 本组件自绘（不含子组件）。默认绘制边框（厚度>0 时）----
    virtual void OnDraw(Renderer2D& r);

    // ---- 子组件坐标空间（供布局 / 渲染 / 命中测试）----
    // 子组件渲染空间原点（默认 = 本组件渲染矩形左上角）
    virtual Vec2 ChildRenderOrigin() const { return {m_abs.x, m_abs.y}; }
    // 子组件屏幕空间原点（默认 = 本组件屏幕矩形左上角）
    virtual Vec2 ChildScreenOrigin() const { return {m_screenAbs.x, m_screenAbs.y}; }
    // 子组件裁剪矩形（默认 = 本组件渲染矩形 ∩ 父裁剪）
    virtual Rect ChildClip(const Rect& parentClip) const { return m_abs.Intersect(parentClip); }
    // 子组件布局空间尺寸（默认 = 本组件自身宽高；虚拟画布容器返回画布尺寸）
    virtual Vec2 ChildLayoutSize() const { return {m_width, m_height}; }
    // 在布局中算完自身实际像素尺寸后、布局子组件前调用（子类可在此更新依赖自身尺寸的状态，如画布尺寸）
    virtual void OnLayoutSelf() {}
    // 是否为虚拟画布容器（子组件渲染到独立 FBO，需重定向）
    virtual bool IsVirtualCanvas() const { return false; }
    // 自绘时使用的自身尺寸（虚拟画布容器返回画布尺寸）
    virtual Vec2 SelfSize() const { return {m_width, m_height}; }
    // 命中测试的边界（默认 = 屏幕矩形；下拉菜单等子组件外溢的容器可扩大）
    virtual Rect HitBounds() const { return m_screenAbs; }

    // ---- 事件回调（返回 true 表示已消费 / 请求焦点等）----
    virtual bool OnMouseDown(MouseButton b, float lx, float ly) { (void)b; (void)lx; (void)ly; return false; }
    virtual void OnMouseUp(MouseButton b, float lx, float ly) { (void)b; (void)lx; (void)ly; }
    virtual void OnMouseCancel(MouseButton b) { OnMouseUp(b, -1.0f, -1.0f); }
    virtual void OnMouseMove(float lx, float ly) { (void)lx; (void)ly; }
    virtual void OnMouseEnter() {}
    virtual void OnMouseLeave() {}
    virtual bool OnKeyDown(int key) { (void)key; return false; }
    virtual bool OnKeyUp(int key) { (void)key; return false; }
    // UTF-8 单字符输入（用于文本框）
    virtual bool OnChar(const std::string& utf8) { (void)utf8; return false; }
    // 滚轮（dy>0 上滚），分发给悬停组件
    virtual void OnScroll(float dy) { (void)dy; }

    // 供子类访问
    Component* m_parent = nullptr;
    std::vector<std::unique_ptr<Component>> m_children;

    // 几何规格：比例（相对父组件尺寸）+ 绝对增减值
    float m_posXRatio = 0.0f;
    float m_posYRatio = 0.0f;
    float m_posXOffset = 0.0f;
    float m_posYOffset = 0.0f;
    float m_wRatio = 0.0f;
    float m_hRatio = 0.0f;
    float m_wOffset = 0.0f;
    float m_hOffset = 0.0f;

    // 布局后计算出的实际像素值（= 父尺寸 × 比例 + 偏移）
    float m_localX = 0.0f;
    float m_localY = 0.0f;
    float m_width = 0.0f;
    float m_height = 0.0f;

    Rect  m_abs;         // 渲染空间绝对矩形
    Rect  m_screenAbs;   // 屏幕空间绝对矩形
    Rect  m_screenClip;  // 当前渲染目标及祖先裁剪对应的屏幕区域
    std::shared_ptr<void> m_lifetime = std::make_shared<int>(0);

    bool  m_visible = true;
    float m_opacity = 1.0f;

    float m_borderThickness = 0.0f;
    Color m_borderColor = Color::White();

    bool  m_captureMouse = false;

    static const InputState* s_globalInput;

    // 让 UIApplication 直接访问受保护事件回调进行分发
    friend class UIApplication;
};

} // namespace ui

#endif // UI_COMPONENT_H
