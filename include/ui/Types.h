#ifndef UI_TYPES_H
#define UI_TYPES_H

#include <cmath>
#include <algorithm>
#include <cstdint>

namespace ui {

// ---------------------------------------------------------------------------
// 颜色：RGBA，各分量范围 [0, 1]
// ---------------------------------------------------------------------------
struct Color {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;

    Color() = default;
    Color(float r_, float g_, float b_, float a_ = 1.0f)
        : r(r_), g(g_), b(b_), a(a_) {}

    // 静态常用色
    static Color White()  { return {1.0f, 1.0f, 1.0f, 1.0f}; }
    static Color Black()  { return {0.0f, 0.0f, 0.0f, 1.0f}; }
    static Color Red()    { return {1.0f, 0.0f, 0.0f, 1.0f}; }
    static Color Green()  { return {0.0f, 1.0f, 0.0f, 1.0f}; }
    static Color Blue()   { return {0.0f, 0.0f, 1.0f, 1.0f}; }
    static Color Transparent() { return {0.0f, 0.0f, 0.0f, 0.0f}; }

    // 调制 alpha（RGB 保持非预乘，预乘由渲染器处理）
    Color WithAlpha(float na) const { return {r, g, b, a * na}; }
};

// ---------------------------------------------------------------------------
// 二维点
// ---------------------------------------------------------------------------
struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
    Vec2() = default;
    Vec2(float x_, float y_) : x(x_), y(y_) {}
};

// ---------------------------------------------------------------------------
// 矩形：x/y 为左上角（UI 约定：Y 向下），w/h 为宽高（像素）
// ---------------------------------------------------------------------------
struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;

    Rect() = default;
    Rect(float x_, float y_, float w_, float h_) : x(x_), y(y_), w(w_), h(h_) {}

    float Right()  const { return x + w; }
    float Bottom() const { return y + h; }

    bool Contains(float px, float py) const {
        return px >= x && px < x + w && py >= y && py < y + h;
    }

    bool IsEmpty() const { return w <= 0.0f || h <= 0.0f; }

    // 求两个矩形交集；无交集返回空矩形
    Rect Intersect(const Rect& o) const {
        float x0 = std::max(x, o.x);
        float y0 = std::max(y, o.y);
        float x1 = std::min(Right(), o.Right());
        float y1 = std::min(Bottom(), o.Bottom());
        if (x1 <= x0 || y1 <= y0) return Rect(0, 0, 0, 0);
        return Rect(x0, y0, x1 - x0, y1 - y0);
    }
};

// ---------------------------------------------------------------------------
// 鼠标按键编号
// ---------------------------------------------------------------------------
enum class MouseButton : int {
    Left   = 0,
    Right  = 1,
    Middle = 2,
    Count  = 3,
};

// ---------------------------------------------------------------------------
// 键盘按键编号（沿用 GLFW 的 key code 约定，便于与 GLFW 对接）
// 无头模式下同样使用该约定注入按键。
// ---------------------------------------------------------------------------
enum Key : int {
    KEY_UNKNOWN       = -1,
    KEY_SPACE         = 32,
    KEY_APOSTROPHE    = 39,
    KEY_COMMA         = 44,
    KEY_MINUS         = 45,
    KEY_PERIOD        = 46,
    KEY_SLASH         = 47,
    KEY_0             = 48,
    KEY_1             = 49,
    KEY_2             = 50,
    KEY_3             = 51,
    KEY_4             = 52,
    KEY_5             = 53,
    KEY_6             = 54,
    KEY_7             = 55,
    KEY_8             = 56,
    KEY_9             = 57,
    KEY_SEMICOLON     = 59,
    KEY_EQUAL         = 61,
    KEY_A             = 65,
    KEY_B             = 66,
    KEY_C             = 67,
    KEY_D             = 68,
    KEY_E             = 69,
    KEY_F             = 70,
    KEY_G             = 71,
    KEY_H             = 72,
    KEY_I             = 73,
    KEY_J             = 74,
    KEY_K             = 75,
    KEY_L             = 76,
    KEY_M             = 77,
    KEY_N             = 78,
    KEY_O             = 79,
    KEY_P             = 80,
    KEY_Q             = 81,
    KEY_R             = 82,
    KEY_S             = 83,
    KEY_T             = 84,
    KEY_U             = 85,
    KEY_V             = 86,
    KEY_W             = 87,
    KEY_X             = 88,
    KEY_Y             = 89,
    KEY_Z             = 90,
    KEY_LEFT_BRACKET  = 91,
    KEY_BACKSLASH     = 92,
    KEY_RIGHT_BRACKET = 93,
    KEY_GRAVE_ACCENT  = 96,
    KEY_ESCAPE        = 256,
    KEY_ENTER         = 257,
    KEY_TAB           = 258,
    KEY_BACKSPACE     = 259,
    KEY_INSERT        = 260,
    KEY_DELETE        = 261,
    KEY_RIGHT         = 262,
    KEY_LEFT          = 263,
    KEY_DOWN          = 264,
    KEY_UP            = 265,
    KEY_PAGE_UP       = 266,
    KEY_PAGE_DOWN     = 267,
    KEY_HOME          = 268,
    KEY_END           = 269,
    KEY_CAPS_LOCK     = 280,
    KEY_LEFT_SHIFT    = 340,
    KEY_LEFT_CONTROL  = 341,
    KEY_LEFT_ALT      = 342,
    KEY_RIGHT_SHIFT   = 344,
    KEY_RIGHT_CONTROL = 345,
    KEY_RIGHT_ALT     = 346,
};

} // namespace ui

#endif // UI_TYPES_H
