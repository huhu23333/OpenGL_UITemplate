#ifndef UI_WIDGETS_H
#define UI_WIDGETS_H

#include "Component.h"

#include <opencv2/core.hpp>

#include <functional>
#include <string>

namespace ui {

class Texture;

// ---------------------------------------------------------------------------
// 按钮：可配置标签、三态颜色（普通/悬停/按下）、描边、点击回调。
// ---------------------------------------------------------------------------
class Button : public Component {
public:
    using Callback = std::function<void(Button*)>;

    Button();

    void SetLabel(const std::string& text) { m_label = text; }
    const std::string& GetLabel() const { return m_label; }
    void SetFontScale(float s) { m_fontScale = s; }
    void SetFontThickness(int t) { m_fontThickness = t; }
    // 文字渲染细化倍数（超采样）：以 scale*ss 栅格化再缩回逻辑尺寸，减弱文字模糊
    void SetTextSupersample(float ss) { m_textSS = ss > 0.0f ? ss : 1.0f; }

    void SetColors(const Color& normal, const Color& hover, const Color& pressed);
    void SetLabelColor(const Color& c) { m_labelColor = c; }
    void SetOnClick(Callback cb) { m_onClick = std::move(cb); }

    bool IsHovered() const { return m_hovered; }
    bool IsPressed() const { return m_pressed; }

protected:
    void OnDraw(Renderer2D& r) override;
    bool OnMouseDown(MouseButton b, float lx, float ly) override;
    void OnMouseUp(MouseButton b, float lx, float ly) override;
    void OnMouseCancel(MouseButton b) override { if (b == MouseButton::Left) m_pressed = false; }
    void OnMouseEnter() override;
    void OnMouseLeave() override;

    std::string m_label;
    float m_fontScale = 0.5f;
    int   m_fontThickness = 1;
    float m_textSS = 1.0f;
    Color m_colorNormal = {0.25f, 0.28f, 0.36f, 1.0f};
    Color m_colorHover  = {0.32f, 0.38f, 0.48f, 1.0f};
    Color m_colorPressed = {0.15f, 0.18f, 0.24f, 1.0f};
    Color m_labelColor = Color::White();
    Callback m_onClick;
    bool m_hovered = false;
    bool m_pressed = false;
};

// ---------------------------------------------------------------------------
// 滑块：水平拖动，值域 [min, max]，拖拽时捕获鼠标。
// ---------------------------------------------------------------------------
class Slider : public Component {
public:
    using Callback = std::function<void(Slider*, float value)>;

    Slider();

    void SetRange(float min, float max);
    void SetValue(float v);
    float GetValue() const;
    float GetNormalized() const;  // [0,1]
    void SetOnChange(Callback cb) { m_onChange = std::move(cb); }

    void SetTrackColor(const Color& c) { m_trackColor = c; }
    void SetFillColor(const Color& c) { m_fillColor = c; }
    void SetHandleColor(const Color& c) { m_handleColor = c; }
    void SetHandleWidth(float w) { m_handleWidth = w; }

    bool IsDragging() const { return m_dragging; }

protected:
    void OnDraw(Renderer2D& r) override;
    bool OnMouseDown(MouseButton b, float lx, float ly) override;
    void OnMouseUp(MouseButton b, float lx, float ly) override;
    void OnMouseMove(float lx, float ly) override;

    void SetValueFromX(float lx);

    float m_min = 0.0f;
    float m_max = 1.0f;
    float m_value = 0.0f;
    float m_handleWidth = 14.0f;
    Color m_trackColor = {0.15f, 0.17f, 0.22f, 1.0f};
    Color m_fillColor = {0.20f, 0.55f, 0.90f, 1.0f};
    Color m_handleColor = {0.85f, 0.88f, 0.92f, 1.0f};
    Callback m_onChange;
    bool m_dragging = false;
};

// ---------------------------------------------------------------------------
// 文字标签：可配置文本、字号、颜色、对齐、可选背景。
// ---------------------------------------------------------------------------
class Text : public Component {
public:
    enum class HAlign { Left, Center, Right };
    enum class VAlign { Top, Middle, Bottom };

    Text();

    void SetText(const std::string& t) { m_text = t; }
    const std::string& GetText() const { return m_text; }
    void SetFontScale(float s) { m_fontScale = s; }
    void SetFontThickness(int t) { m_fontThickness = t; }
    // 文字渲染细化倍数（超采样）：以 scale*ss 栅格化再缩回逻辑尺寸，减弱文字模糊
    void SetTextSupersample(float ss) { m_textSS = ss > 0.0f ? ss : 1.0f; }
    void SetColor(const Color& c) { m_color = c; }
    void SetAlignment(HAlign h, VAlign v) { m_halign = h; m_valign = v; }
    // 设置背景色（a>0 时先填充背景）
    void SetBackground(const Color& bg) { m_background = bg; }

protected:
    void OnDraw(Renderer2D& r) override;

    std::string m_text;
    float m_fontScale = 0.5f;
    int   m_fontThickness = 1;
    float m_textSS = 1.0f;
    Color m_color = Color::White();
    Color m_background = Color::Transparent();
    HAlign m_halign = HAlign::Left;
    VAlign m_valign = VAlign::Top;
};

// ---------------------------------------------------------------------------
// 可输入文本框：单行文本编辑，焦点管理、光标移动、退格/删除、回车提交。
// 文本以 UTF-8 存储与编辑（支持中文按字符操作）。
// ---------------------------------------------------------------------------
class TextBox : public Component {
public:
    using Callback = std::function<void(TextBox*)>;

    TextBox();

    void SetText(const std::string& t);
    const std::string& GetText() const { return m_text; }
    void SetPlaceholder(const std::string& p) { m_placeholder = p; }
    void SetFontScale(float s) { m_fontScale = s; }
    void SetFontThickness(int t) { m_fontThickness = t; }
    // 文字渲染细化倍数（超采样）：以 scale*ss 栅格化再缩回逻辑尺寸，减弱文字模糊
    void SetTextSupersample(float ss) { m_textSS = ss > 0.0f ? ss : 1.0f; }
    void SetTextColor(const Color& c) { m_textColor = c; }
    void SetPlaceholderColor(const Color& c) { m_placeholderColor = c; }
    void SetBackgroundColor(const Color& c) { m_bgColor = c; }
    void SetCursorColor(const Color& c) { m_cursorColor = c; }
    void SetMaxLength(int n) { m_maxLength = n; }

    void SetOnSubmit(Callback cb) { m_onSubmit = std::move(cb); }
    void SetOnChange(Callback cb) { m_onChange = std::move(cb); }

    void SetFocus(bool f) override { m_focused = f; }
    bool IsFocusable() const override { return true; }
    bool IsFocused() const { return m_focused; }

protected:
    void OnDraw(Renderer2D& r) override;
    bool OnMouseDown(MouseButton b, float lx, float ly) override;
    bool OnKeyDown(int key) override;
    bool OnChar(const std::string& utf8) override;

    // UTF-8 辅助
    static size_t Utf8Prev(const std::string& s, size_t pos);
    static size_t Utf8Next(const std::string& s, size_t pos);
    static void   Utf8EraseAt(std::string& s, size_t pos);  // 删除 pos 起始的一个字符

    std::string m_text;
    std::string m_placeholder;
    float m_fontScale = 0.5f;
    int   m_fontThickness = 1;
    float m_textSS = 1.0f;
    Color m_textColor = Color::White();
    Color m_placeholderColor = {0.5f, 0.5f, 0.5f, 1.0f};
    Color m_bgColor = {0.10f, 0.12f, 0.16f, 1.0f};
    Color m_cursorColor = Color::White();
    int   m_maxLength = 128;

    size_t m_cursor = 0;   // 字节位置
    bool   m_focused = false;
    Callback m_onSubmit;
    Callback m_onChange;
};

// ---------------------------------------------------------------------------
// 图像显示组件：
//   - 显示 cv::Mat（内部上传为纹理），支持 Stretch / Contain / Cover 三种适配。
//   - 点击可获取图像内容中的归一化位置（yolo 格式比例小数）。
//   - 支持拖拽框选，输出 (cx, cy, w, h) ∈ [0,1] 归一化到图像内容区域。
// ---------------------------------------------------------------------------
class ImageDisplay : public Component {
public:
    enum class FitMode { Stretch, Contain, Cover };
    using ClickCallback = std::function<void(ImageDisplay*, float cx, float cy)>;
    using BoxCallback   = std::function<void(ImageDisplay*, float cx, float cy, float w, float h)>;

    ImageDisplay();
    ~ImageDisplay() override;

    void SetImage(const cv::Mat& img);
    const cv::Mat& GetImage() const { return m_image; }
    bool HasImage() const { return m_tex != nullptr; }
    void SetFitMode(FitMode m) { m_fitMode = m; }
    void SetOnClick(ClickCallback cb) { m_onClick = std::move(cb); }
    void SetOnBoxDrawn(BoxCallback cb) { m_onBox = std::move(cb); }
    void SetBoxColor(const Color& c) { m_boxColor = c; }
    void SetShowBox(bool s) { m_showBox = s; }

    // 图像内容在组件内的显示矩形（考虑适配模式，组件局部坐标），无图像时为空
    Rect GetContentRect() const;

    // 将组件内局部坐标转换为图像内容归一化坐标 [0,1]（越界会截断到 [0,1]）
    void OnMouseCancel(MouseButton b) override {
        if (b == MouseButton::Left) { m_dragging = false; SetMouseCapture(false); }
    }
    Vec2 LocalToNormalized(float lx, float ly) const;

protected:
    void OnDraw(Renderer2D& r) override;
    bool OnMouseDown(MouseButton b, float lx, float ly) override;
    void OnMouseMove(float lx, float ly) override;
    void OnMouseUp(MouseButton b, float lx, float ly) override;

    cv::Mat m_image;
    std::unique_ptr<Texture> m_tex;
    FitMode m_fitMode = FitMode::Contain;
    ClickCallback m_onClick;
    BoxCallback m_onBox;
    Color m_boxColor = {0.0f, 1.0f, 0.5f, 1.0f};
    bool m_showBox = true;
    bool m_dragging = false;
    Vec2 m_dragStart;
    Vec2 m_dragCur;
};

} // namespace ui

#endif // UI_WIDGETS_H
