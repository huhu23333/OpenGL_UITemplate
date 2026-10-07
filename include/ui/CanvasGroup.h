#ifndef UI_CANVAS_GROUP_H
#define UI_CANVAS_GROUP_H

#include "Component.h"
#include "Texture.h"
#include "Widgets.h"

#include <GLES3/gl3.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ui {

// ---------------------------------------------------------------------------
// 虚拟画布容器组件（需求 #6）：
//   - 在父组件中仍有位置与宽高（视口），且只在其对应范围内绘制。
//   - 自身与所有子组件渲染到一张独立虚拟画布（FBO，尺寸可配置）。
//   - 子组件在虚拟画布坐标系 (0,0,canvasW,canvasH) 内布局与裁剪。
//   - 子组件绘制完成后，按"合成规则"（默认为带滚动偏移的视口）应用回父画布。
//
// 可用于实现滚动菜单（ScrollView）、下拉菜单（Dropdown）等。
// ---------------------------------------------------------------------------
class CanvasGroup : public Component {
public:
    CanvasGroup();
    ~CanvasGroup() override;

    CanvasGroup(const CanvasGroup&) = delete;
    CanvasGroup& operator=(const CanvasGroup&) = delete;

    // 虚拟画布尺寸（配置），改变后自动重建 FBO
    void SetCanvasSize(float w, float h);
    void GetCanvasSize(float& w, float& h) const { w = m_canvasW; h = m_canvasH; }

    // 滚动偏移（视口在虚拟画布上的位置，像素）
    void SetScrollOffset(float dx, float dy);
    void GetScrollOffset(float& dx, float& dy) const { dx = m_scrollX; dy = m_scrollY; }
    void SetScrollX(float x) { m_scrollX = x; ClampScroll(); }
    void SetScrollY(float y) { m_scrollY = y; ClampScroll(); }
    float GetScrollX() const { return m_scrollX; }
    float GetScrollY() const { return m_scrollY; }

    // 虚拟画布背景色（合成前填充）
    void SetCanvasBackground(const Color& c) { m_canvasBg = c; }
    const Color& GetCanvasBackground() const { return m_canvasBg; }

    void OnLayoutSelf() override;

    // 确保 FBO 已分配（canvas 尺寸未知时延迟到首帧）
    void EnsureCanvas();

protected:
    // ---- 覆盖：子组件坐标空间指向虚拟画布 ----
    bool IsVirtualCanvas() const override { return true; }
    Vec2 ChildRenderOrigin() const override { return {0.0f, 0.0f}; }
    Vec2 ChildScreenOrigin() const override { return {m_screenAbs.x - m_scrollX, m_screenAbs.y - m_scrollY}; }
    Rect  ChildClip(const Rect& parentClip) const override {
        (void)parentClip;
        return Rect(0.0f, 0.0f, m_canvasW, m_canvasH);
    }
    Vec2 SelfSize() const override { return {m_canvasW, m_canvasH}; }
    // 子组件布局空间尺寸 = 虚拟画布尺寸
    Vec2 ChildLayoutSize() const override { return {m_canvasW, m_canvasH}; }

    // 自身内容绘制到虚拟画布（默认填充画布背景色 + 边框）
    void OnDraw(Renderer2D& r) override;

    // 重写渲染：子组件重定向到虚拟 FBO，再合成回父画布
    void Render(Renderer2D& r, const Rect& clip) override;

    // 合成规则：把虚拟画布按滚动偏移绘制回自身视口（可被子类覆盖）
    virtual void Composite(Renderer2D& r, const Rect& myClip);

    // 滚动边界裁剪
    void ClampScroll();

    float m_canvasW = 0.0f;
    float m_canvasH = 0.0f;
    float m_scrollX = 0.0f;
    float m_scrollY = 0.0f;
    Color m_canvasBg = Color::Transparent();

    GLuint m_fbo = 0;
    std::unique_ptr<Texture> m_canvasTex;   // 虚拟画布纹理（FBO 颜色附件）
    bool m_canvasDirty = true;
    float m_requestedCanvasW = 0.0f;
    float m_requestedCanvasH = 0.0f;
};

// ---------------------------------------------------------------------------
// 滚动菜单（垂直滚动容器）：虚拟画布为内容区，右侧绘制滚动条，支持滚轮与拖拽。
// ---------------------------------------------------------------------------
class ScrollView : public CanvasGroup {
public:
    ScrollView();
    Component* HitTest(float sx, float sy) override;

    void SetScrollbarWidth(float w) { m_barW = w; }
    void SetScrollbarColor(const Color& c) { m_barColor = c; }
    void SetScrollbarThumbColor(const Color& c) { m_thumbColor = c; }
    // 内容尺寸（即虚拟画布尺寸，需 >= 视口尺寸才能滚动）
    void SetContentSize(float w, float h) { SetCanvasSize(w, h); }

    // 滚动到指定 y（像素）
    void ScrollTo(float y) { SetScrollY(y); }

protected:
    void Render(Renderer2D& r, const Rect& clip) override;
    bool OnMouseDown(MouseButton b, float lx, float ly) override;
    void OnMouseUp(MouseButton b, float lx, float ly) override;
    void OnMouseMove(float lx, float ly) override;
    void OnScroll(float dy) override;

    void DrawScrollbar(Renderer2D& r, const Rect& myClip);
    bool IsOnScrollbar(float lx) const;
    float ThumbHeight() const;
    float ThumbY() const;

    float m_barW = 10.0f;
    Color m_barColor = {0.10f, 0.12f, 0.16f, 1.0f};
    Color m_thumbColor = {0.35f, 0.40f, 0.50f, 1.0f};
    bool m_draggingBar = false;
    float m_dragGrabOffset = 0.0f;
};

// ---------------------------------------------------------------------------
// 下拉菜单：一个头部按钮 + 下拉选项列表（虚拟画布弹出层）。
//   - 头部显示当前选中项；点击头部在下方展开/收起选项。
//   - 选项列表为虚拟画布，选中后回调并收起。
// ---------------------------------------------------------------------------
class Dropdown : public Component {
public:
    using Callback = std::function<void(Dropdown*, int index, const std::string& item)>;

    Dropdown();

    void SetItems(const std::vector<std::string>& items);
    const std::vector<std::string>& GetItems() const { return m_items; }
    void SetSelectedIndex(int i);
    int  GetSelectedIndex() const { return m_selected; }
    const std::string& GetSelectedText() const;

    void SetOnSelect(Callback cb) { m_onSelect = std::move(cb); }

    void SetOpen(bool open);
    bool IsOpen() const { return m_open; }

    // 头部按钮外观
    void SetColors(const Color& normal, const Color& hover, const Color& pressed);
    void SetItemHeight(float h) { m_itemHeight = h; }
    void SetFontScale(float s) { m_fontScale = s; }
    // 文字渲染细化倍数（超采样），会传播到头部按钮与选项
    void SetTextSupersample(float ss) { m_textSS = ss > 0.0f ? ss : 1.0f; }

protected:
    // 允许子组件（弹出层）绘制在头部矩形之外（下方区域）
    Rect ChildClip(const Rect& parentClip) const override;
    // 命中测试边界扩展到弹出层区域
    Rect HitBounds() const override;
    // 每帧布局中同步子组件尺寸到自身尺寸
    void OnLayoutSelf() override;

    std::vector<std::string> m_items;
    int m_selected = -1;
    Callback m_onSelect;
    bool m_open = false;
    float m_headerH = 36.0f;
    float m_itemHeight = 32.0f;
    float m_fontScale = 0.5f;
    float m_textSS = 1.0f;
    Color m_colorNormal = {0.25f, 0.28f, 0.36f, 1.0f};
    Color m_colorHover = {0.32f, 0.38f, 0.48f, 1.0f};
    Color m_colorPressed = {0.15f, 0.18f, 0.24f, 1.0f};

    // 内部子组件
    Button* m_headerButton = nullptr;
    CanvasGroup* m_popup = nullptr;
    void Rebuild();
};

} // namespace ui

#endif // UI_CANVAS_GROUP_H
