#ifndef UI_TEXT_ENGINE_H
#define UI_TEXT_ENGINE_H

#include <opencv2/core.hpp>

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
//   - 通过 Fontconfig 发现字体（无硬编码绝对路径），必要时用候选路径兜底。
// ---------------------------------------------------------------------------
class TextEngine {
public:
    static TextEngine& Instance();

    // 引擎是否已成功初始化（找到可用字体）
    bool Available() const { return m_face != nullptr; }

    // sizePx：逻辑字号（像素）；thickness：1=常规，2=合成加粗，更大=更强加粗；ss：超采样倍数
    TextMetrics Measure(const std::string& utf8, float sizePx, int thickness) const;

    // 栅格化：返回灰度 coverage 位图（CV_8UC1），尺寸 = 逻辑纹理尺寸 × ss
    cv::Mat Rasterize(const std::string& utf8, float sizePx, int thickness, float ss) const;

private:
    TextEngine();
    ~TextEngine();
    TextEngine(const TextEngine&) = delete;
    TextEngine& operator=(const TextEngine&) = delete;
    bool Init();

    void* m_lib = nullptr;     // FT_Library
    void* m_face = nullptr;    // FT_Face
    void* m_hbFont = nullptr;  // hb_font_t
    int m_faceIndex = 0;
};

} // namespace ui

#endif // UI_TEXT_ENGINE_H
