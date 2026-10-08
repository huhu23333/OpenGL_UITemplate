#include "ui/Widgets.h"
#include "ui/Renderer2D.h"
#include "ui/Texture.h"
#include "ui/TextEngine.h"

#include <opencv2/imgproc.hpp>

#include <map>
#include <memory>
#include <cmath>
#include <limits>

namespace ui {

// ===========================================================================
// 文本栅格化缓存（HarfBuzz + FreeType 矢量轮廓 → 灰度 coverage 纹理）
// 支持中文等任意 Unicode，覆盖范围取决于所选字体（Noto Sans CJK SC 等）。
// ===========================================================================
namespace {

using TextKey = std::tuple<std::string, float, int, float>;  // (text, scale, thickness, supersample)

// 逻辑字号映射：Hershey 的 scale 近似换算为像素字号（scale=1 → 24px）
constexpr float kBaseFontPx = 24.0f;

struct TextSize {
    float w = 0.0f;
    float h = 0.0f;
};

// ss 为文字渲染细化倍数（超采样）：以 scale*ss 的字号栅格化轮廓，绘制时缩回 scale 的逻辑尺寸。
Texture* GetTextTexture(Renderer2D& renderer, const std::string& text, float scale, int thickness, float ss) {
    if (text.empty() || !std::isfinite(scale) || scale <= 0.0f) return nullptr;
    if (!std::isfinite(ss) || ss <= 0.0f) ss = 1.0f;
    ss = std::max(1.0f, std::min(ss, 8.0f));  // 与 TextEngine 的范围一致，避免缓存键发散
    TextEngine& eng = TextEngine::Instance();
    const float effectiveScale = scale * eng.GetResolutionScale();
    if (!std::isfinite(effectiveScale) || effectiveScale <= 0.0f) return nullptr;
    TextKey key{text, effectiveScale, thickness, ss};
    auto& cache = renderer.TextCache();
    auto it = cache.find(key);
    if (it != cache.end()) return it->second.get();

    if (!eng.Available()) return nullptr;
    cv::Mat mask = eng.Rasterize(text, scale * kBaseFontPx, thickness, ss);
    if (mask.empty()) return nullptr;

    // 白 RGB + coverage alpha；Texture::Upload 会预乘为 coverage 全通道
    cv::Mat white(mask.rows, mask.cols, CV_8UC1, cv::Scalar(255)), img;
    cv::merge(std::vector<cv::Mat>{white, white, white, mask}, img);

    auto tex = std::make_unique<Texture>();
    if (!tex->Upload(img)) return nullptr;
    Texture* raw = tex.get();
    cache.emplace(std::move(key), std::move(tex));
    return raw;
}

TextSize MeasureText(const std::string& text, float scale, int thickness) {
    if (text.empty()) return {0.0f, 0.0f};
    TextEngine& eng = TextEngine::Instance();
    if (!eng.Available()) return {0.0f, 0.0f};
    ui::TextMetrics m = eng.Measure(text, scale * kBaseFontPx, thickness);
    return {m.width, m.height};
}

} // namespace

// ===========================================================================
// Button
// ===========================================================================
Button::Button() {
    SetBorder(1.0f, {0.45f, 0.50f, 0.60f, 1.0f});
}

void Button::SetColors(const Color& normal, const Color& hover, const Color& pressed) {
    m_colorNormal = normal;
    m_colorHover = hover;
    m_colorPressed = pressed;
}

void Button::OnDraw(Renderer2D& r) {
    Vec2 s = SelfSize();
    Color bg = m_pressed ? m_colorPressed : (m_hovered ? m_colorHover : m_colorNormal);
    r.FillRect(0.0f, 0.0f, s.x, s.y, bg);
    Component::OnDraw(r);  // 边框

    if (!m_label.empty()) {
        Texture* tex = GetTextTexture(r, m_label, m_fontScale, m_fontThickness, m_textSS);
        if (tex) {
            TextSize m = MeasureText(m_label, m_fontScale, m_fontThickness);
            r.DrawTexture(tex, (s.x - m.w) * 0.5f, (s.y - m.h) * 0.5f, m.w, m.h, m_labelColor);
        }
    }
}

bool Button::OnMouseDown(MouseButton b, float lx, float ly) {
    (void)lx; (void)ly;
    if (b != MouseButton::Left) return false;
    m_pressed = true;
    return true;
}

void Button::OnMouseUp(MouseButton b, float lx, float ly) {
    (void)lx; (void)ly;
    if (b != MouseButton::Left) return;
    bool wasPressed = m_pressed;
    m_pressed = false;
    auto callback = m_onClick;
    if (wasPressed && IsMouseInside() && callback) callback(this);
}

void Button::OnMouseEnter() { m_hovered = true; }
void Button::OnMouseLeave() { m_hovered = false; }

// ===========================================================================
// Slider
// ===========================================================================
Slider::Slider() {
    SetBorder(1.0f, {0.35f, 0.40f, 0.50f, 1.0f});
}

void Slider::SetRange(float min, float max) {
    if (!std::isfinite(min) || !std::isfinite(max)) return;
    m_min = min;
    m_max = std::max(min, max);
    SetValue(m_value);
}

void Slider::SetValue(float v) {
    if (!std::isfinite(v)) return;
    m_value = v < m_min ? m_min : (v > m_max ? m_max : v);
}

float Slider::GetValue() const { return m_value; }

float Slider::GetNormalized() const {
    if (m_max <= m_min) return 0.0f;
    return (float)((double(m_value) - m_min) / (double(m_max) - m_min));
}

void Slider::SetValueFromX(float lx) {
    float s = m_width;
    float norm = s > 0.0f ? lx / s : 0.0f;
    norm = norm < 0.0f ? 0.0f : (norm > 1.0f ? 1.0f : norm);
    float v = (float)(double(m_min) + (double(m_max) - m_min) * norm);
    if (v != m_value) {
        m_value = v;
        auto callback = m_onChange;
        if (callback) callback(this, m_value);
    }
}

void Slider::OnDraw(Renderer2D& r) {
    Vec2 s = SelfSize();
    float trackH = s.y * 0.32f;
    if (trackH < 4.0f) trackH = 4.0f;
    float trackY = (s.y - trackH) * 0.5f;
    float norm = GetNormalized();
    float handleCx = norm * s.x;

    r.FillRect(0.0f, trackY, s.x, trackH, m_trackColor);
    if (handleCx > 0.0f) r.FillRect(0.0f, trackY, handleCx, trackH, m_fillColor);
    r.FillRect(handleCx - m_handleWidth * 0.5f, 0.0f, m_handleWidth, s.y, m_handleColor);

    Component::OnDraw(r);
}

bool Slider::OnMouseDown(MouseButton b, float lx, float ly) {
    (void)ly;
    if (b != MouseButton::Left) return false;
    m_dragging = true;
    SetMouseCapture(true);
    SetValueFromX(lx);
    return true;
}

void Slider::OnMouseUp(MouseButton b, float lx, float ly) {
    (void)lx; (void)ly;
    if (b != MouseButton::Left) return;
    if (m_dragging) {
        m_dragging = false;
        SetMouseCapture(false);
    }
}

void Slider::OnMouseMove(float lx, float ly) {
    (void)ly;
    if (m_dragging) SetValueFromX(lx);
}

// ===========================================================================
// Text
// ===========================================================================
Text::Text() = default;

void Text::OnDraw(Renderer2D& r) {
    Vec2 s = SelfSize();
    if (m_background.a > 0.0f) r.FillRect(0.0f, 0.0f, s.x, s.y, m_background);
    Component::OnDraw(r);

    if (m_text.empty()) return;
    Texture* tex = GetTextTexture(r, m_text, m_fontScale, m_fontThickness, m_textSS);
    if (!tex) return;
    TextSize m = MeasureText(m_text, m_fontScale, m_fontThickness);

    float x = 0.0f;
    switch (m_halign) {
        case HAlign::Center: x = (s.x - m.w) * 0.5f; break;
        case HAlign::Right:  x = s.x - m.w; break;
        default: x = 0.0f;
    }
    float y = 0.0f;
    switch (m_valign) {
        case VAlign::Middle: y = (s.y - m.h) * 0.5f; break;
        case VAlign::Bottom: y = s.y - m.h; break;
        default: y = 0.0f;
    }
    r.DrawTexture(tex, x, y, m.w, m.h, m_color);
}

// ===========================================================================
// TextBox
// ===========================================================================
TextBox::TextBox() {
    SetBorder(1.0f, {0.40f, 0.50f, 0.65f, 1.0f});
}

void TextBox::SetText(const std::string& t) {
    m_text = t;
    if (m_cursor > m_text.size()) m_cursor = m_text.size();
    while (m_cursor > 0 && m_cursor < m_text.size() &&
           ((unsigned char)m_text[m_cursor] & 0xC0) == 0x80) --m_cursor;
}

void TextBox::OnDraw(Renderer2D& r) {
    Vec2 s = SelfSize();
    r.FillRect(0.0f, 0.0f, s.x, s.y, m_bgColor);
    Component::OnDraw(r);

    const std::string& shown = m_text.empty() ? m_placeholder : m_text;
    Color col = m_text.empty() ? m_placeholderColor : m_textColor;

    float pad = 4.0f;
    if (!shown.empty()) {
        Texture* tex = GetTextTexture(r, shown, m_fontScale, m_fontThickness, m_textSS);
        if (tex) {
            TextSize m = MeasureText(shown, m_fontScale, m_fontThickness);
            r.DrawTexture(tex, pad, (s.y - m.h) * 0.5f, m.w, m.h, col);
        }
    }

    // 光标
    if (m_focused) {
        float cursorX = pad + TextEngine::Instance().CaretPosition(m_text, m_cursor, m_fontScale * kBaseFontPx, m_fontThickness);
        float cursorH = s.y * 0.6f;
        float cursorY = (s.y - cursorH) * 0.5f;
        r.FillRect(cursorX, cursorY, 2.0f, cursorH, m_cursorColor);
    }
}

bool TextBox::OnMouseDown(MouseButton b, float lx, float ly) {
    (void)ly;
    if (b != MouseButton::Left) return false;
    m_focused = true;

    // 点击和绘制共享完整文本 shaping 的 cluster 推进与纹理原点。
    float target = lx - 4.0f;
    auto position = [&](size_t offset) {
        return TextEngine::Instance().CaretPosition(m_text, offset, m_fontScale * kBaseFontPx, m_fontThickness);
    };
    size_t best = 0;
    float bestDist = std::fabs(position(0) - target);
    for (size_t i = Utf8Next(m_text, 0); i > 0 && i <= m_text.size(); i = Utf8Next(m_text, i)) {
        float w = position(i);
        float d = std::fabs(w - target);
        if (d < bestDist) { bestDist = d; best = i; }
        if (i == m_text.size()) break;
    }
    m_cursor = best;
    return true;
}

bool TextBox::OnKeyDown(int key) {
    if (!m_focused) return false;
    switch (key) {
        case KEY_BACKSPACE:
            if (m_cursor > 0) {
                size_t p = Utf8Prev(m_text, m_cursor);
                m_text.erase(p, m_cursor - p);
                m_cursor = p;
                auto callback = m_onChange;
                if (callback) callback(this);
            }
            return true;
        case KEY_DELETE:
            if (m_cursor < m_text.size()) {
                Utf8EraseAt(m_text, m_cursor);
                auto callback = m_onChange;
                if (callback) callback(this);
            }
            return true;
        case KEY_LEFT:
            m_cursor = Utf8Prev(m_text, m_cursor);
            return true;
        case KEY_RIGHT:
            m_cursor = Utf8Next(m_text, m_cursor);
            return true;
        case KEY_HOME:
            m_cursor = 0;
            return true;
        case KEY_END:
            m_cursor = m_text.size();
            return true;
        case KEY_ENTER: {
            auto callback = m_onSubmit;
            if (callback) callback(this);
            return true;
        }
        default:
            return false;
    }
}

bool TextBox::OnChar(const std::string& utf8) {
    if (!m_focused) return false;
    if (utf8.empty() || (unsigned char)utf8[0] < 0x20 || utf8[0] == 0x7f) return false;
    size_t count = 0;
    for (size_t i = 0; i < m_text.size(); i = Utf8Next(m_text, i)) ++count;
    if (m_maxLength <= 0 || count >= (size_t)m_maxLength) return false;
    m_text.insert(m_cursor, utf8);
    m_cursor += utf8.size();
    auto callback = m_onChange;
    if (callback) callback(this);
    return true;
}

size_t TextBox::Utf8Prev(const std::string& s, size_t pos) {
    if (pos == 0) return 0;
    --pos;
    while (pos > 0 && ((unsigned char)s[pos] & 0xC0) == 0x80) --pos;
    return pos;
}

size_t TextBox::Utf8Next(const std::string& s, size_t pos) {
    if (pos >= s.size()) return s.size();
    ++pos;
    while (pos < s.size() && ((unsigned char)s[pos] & 0xC0) == 0x80) ++pos;
    return pos;
}

void TextBox::Utf8EraseAt(std::string& s, size_t pos) {
    if (pos >= s.size()) return;
    size_t end = Utf8Next(s, pos);
    s.erase(pos, end - pos);
}

// ===========================================================================
// ImageDisplay
// ===========================================================================
ImageDisplay::ImageDisplay() {
    SetBorder(1.0f, {0.30f, 0.35f, 0.45f, 1.0f});
}

ImageDisplay::~ImageDisplay() = default;

void ImageDisplay::SetImage(const cv::Mat& img) {
    m_dragging = false;
    SetMouseCapture(false);
    auto tex = std::make_unique<Texture>();
    if (!tex->Upload(img)) {
        m_image.release();
        m_tex.reset();
        return;
    }
    m_image = img.clone();
    m_tex = std::move(tex);
}

Rect ImageDisplay::GetContentRect() const {
    if (!m_tex || m_image.empty()) return Rect(0.0f, 0.0f, 0.0f, 0.0f);
    float iw = (float)m_image.cols;
    float ih = (float)m_image.rows;
    if (iw <= 0.0f || ih <= 0.0f || m_width <= 0 || m_height <= 0) return Rect(0.0f, 0.0f, 0.0f, 0.0f);

    float w = m_width, h = m_height;
    float sx = w / iw;
    float sy = h / ih;
    float scale;
    switch (m_fitMode) {
        case FitMode::Stretch: return Rect(0.0f, 0.0f, w, h);
        case FitMode::Contain: scale = sx < sy ? sx : sy; break;
        case FitMode::Cover:   scale = sx > sy ? sx : sy; break;
        default: scale = 1.0f;
    }
    float cw = iw * scale;
    float ch = ih * scale;
    return Rect((w - cw) * 0.5f, (h - ch) * 0.5f, cw, ch);
}

Vec2 ImageDisplay::LocalToNormalized(float lx, float ly) const {
    Rect cr = GetContentRect();
    if (cr.IsEmpty()) return {0.0f, 0.0f};
    float nx = (lx - cr.x) / cr.w;
    float ny = (ly - cr.y) / cr.h;
    if (nx < 0.0f) nx = 0.0f;
    if (nx > 1.0f) nx = 1.0f;
    if (ny < 0.0f) ny = 0.0f;
    if (ny > 1.0f) ny = 1.0f;
    return {nx, ny};
}

void ImageDisplay::OnDraw(Renderer2D& r) {
    Rect cr = GetContentRect();
    if (!m_tex || cr.IsEmpty()) { Component::OnDraw(r); return; }
    r.DrawTexture(m_tex.get(), cr.x, cr.y, cr.w, cr.h);

    if (m_dragging && m_showBox) {
        float x0 = m_dragStart.x < m_dragCur.x ? m_dragStart.x : m_dragCur.x;
        float y0 = m_dragStart.y < m_dragCur.y ? m_dragStart.y : m_dragCur.y;
        float x1 = m_dragStart.x > m_dragCur.x ? m_dragStart.x : m_dragCur.x;
        float y1 = m_dragStart.y > m_dragCur.y ? m_dragStart.y : m_dragCur.y;
        r.FillRect(x0, y0, x1 - x0, y1 - y0, m_boxColor.WithAlpha(0.15f));
        r.StrokeRect(x0, y0, x1 - x0, y1 - y0, 2.0f, m_boxColor);
    }
    Component::OnDraw(r);  // Stretch/Cover 纹理不能覆盖自身边框。
}

bool ImageDisplay::OnMouseDown(MouseButton b, float lx, float ly) {
    if (b != MouseButton::Left || !GetContentRect().Contains(lx, ly)) return false;
    m_dragStart = {lx, ly};
    m_dragCur = {lx, ly};
    m_dragging = true;
    SetMouseCapture(true);
    return true;
}

void ImageDisplay::OnMouseMove(float lx, float ly) {
    if (m_dragging) m_dragCur = {lx, ly};
}

void ImageDisplay::OnMouseUp(MouseButton b, float lx, float ly) {
    if (b != MouseButton::Left || !m_dragging) return;
    m_dragging = false;
    SetMouseCapture(false);
    m_dragCur = {lx, ly};

    float x0 = m_dragStart.x < m_dragCur.x ? m_dragStart.x : m_dragCur.x;
    float y0 = m_dragStart.y < m_dragCur.y ? m_dragStart.y : m_dragCur.y;
    float x1 = m_dragStart.x > m_dragCur.x ? m_dragStart.x : m_dragCur.x;
    float y1 = m_dragStart.y > m_dragCur.y ? m_dragStart.y : m_dragCur.y;

    Vec2 n0 = LocalToNormalized(x0, y0);
    Vec2 n1 = LocalToNormalized(x1, y1);
    float bw = n1.x - n0.x;
    float bh = n1.y - n0.y;

    if (bw < 0.005f && bh < 0.005f) {
        // 视为点击
        Vec2 n = LocalToNormalized(lx, ly);
        auto callback = m_onClick;
        if (callback) callback(this, n.x, n.y);
    } else {
        auto callback = m_onBox;
        if (callback) callback(this, (n0.x + n1.x) * 0.5f, (n0.y + n1.y) * 0.5f, bw, bh);
    }
}

} // namespace ui
