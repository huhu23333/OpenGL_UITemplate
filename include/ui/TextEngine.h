#ifndef UI_TEXT_ENGINE_H
#define UI_TEXT_ENGINE_H

#include <opencv2/core.hpp>

#include <cmath>
#include <string>

namespace ui {

// ---------------------------------------------------------------------------
// 文本度量（逻辑像素）
// ---------------------------------------------------------------------------
struct TextMetrics {
    float width = 0.0f;   // 纹理逻辑宽度（含 margin）
    float height = 0.0f;  // 纹理逻辑高度（含 margin）
    float advance = 0.0f; // 排版推进宽度（不含 margin，用于光标定位）
};

// ---------------------------------------------------------------------------
// HarfBuzz + FreeType 文本引擎：排版（shaping）+ 矢量轮廓栅格化为灰度 coverage。
//   - 支持中文等任意 Unicode（取决于所选字体的覆盖）。
//   - 通过 Fontconfig 发现字体（无硬编码绝对路径），以 sans-serif 家族兜底。
// ---------------------------------------------------------------------------
class TextEngine {
public:
    static TextEngine& Instance();

    // 引擎是否已成功初始化（找到可用字体）
    bool Available() const { return m_face != nullptr; }

    // 分辨率缩放因子：所有字号与合成加粗随画布分辨率等比缩放（默认 1）。
    // 例如画布 3840 宽、参考 1920 宽时设为 2，文字随布局一起放大两倍。
    // 非有限或非正因子回退为 1。
    void SetResolutionScale(float s) { m_resolutionScale = std::isfinite(s) && s > 0.0f ? s : 1.0f; }
    float GetResolutionScale() const { return m_resolutionScale; }

    // sizePx：逻辑字号（乘分辨率因子后须在 1/64..4096 像素范围内）；thickness：<=1 常规，2..1024 合成加粗。
    // 超出支持范围返回空结果；ss 归一化到 [1,8]，非有限值回退为 1。
    TextMetrics Measure(const std::string& utf8, float sizePx, int thickness) const;

    // 栅格化：返回灰度 coverage 位图（CV_8UC1），尺寸 = ceil(逻辑纹理尺寸 × ss)。
    cv::Mat Rasterize(const std::string& utf8, float sizePx, int thickness, float ss) const;

private:
    friend class TextBox;
    // 完整文本的 shaping cluster 光标位置（含纹理原点）；避免单独排版前缀丢失 kerning。
    float CaretPosition(const std::string& utf8, size_t byteOffset, float sizePx, int thickness) const;
    TextEngine();
    ~TextEngine();
    TextEngine(const TextEngine&) = delete;
    TextEngine& operator=(const TextEngine&) = delete;
    bool Init();

    void* m_lib = nullptr;     // FT_Library
    void* m_face = nullptr;    // FT_Face
    void* m_hbFont = nullptr;  // hb_font_t
    int m_faceIndex = 0;
    float m_resolutionScale = 1.0f;
};

} // namespace ui

#endif // UI_TEXT_ENGINE_H
