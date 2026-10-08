#include "ui/TextEngine.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H
#include <harfbuzz/hb.h>
#include <harfbuzz/hb-ft.h>
#include <fontconfig/fontconfig.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <map>
#include <mutex>
#include <tuple>
#include <vector>

namespace ui {

namespace {

// 字形实际边界之外再保留抗锯齿留白（逻辑像素）。
constexpr float kMarginLogical = 2.0f;
constexpr double kMaxPixels = 64.0 * 1024.0 * 1024.0;
// FreeType face / HarfBuzz font 的字号状态不能被并发修改。
std::mutex g_textMutex;

bool ValidSize(float sizePx) {
    return std::isfinite(sizePx) && sizePx >= 1.0f / 64.0f && sizePx <= 4096.0f;
}

// 字体路径和 TTC face index 均由 Fontconfig 解析，不依赖发行版目录布局。
bool FindFontViaFontconfig(FcConfig* config, const char* family,
                           std::string& path, int& faceIndex) {
    FcPattern* pat = FcPatternCreate();
    if (!pat) return false;
    bool ok = FcPatternAddString(pat, FC_FAMILY, (const FcChar8*)family) &&
              FcPatternAddString(pat, FC_LANG, (const FcChar8*)"zh-cn") &&
              FcPatternAddBool(pat, FC_SCALABLE, FcTrue) &&
              FcConfigSubstitute(config, pat, FcMatchPattern);
    if (!ok) { FcPatternDestroy(pat); return false; }
    FcDefaultSubstitute(pat);
    FcResult result = FcResultNoMatch;
    FcPattern* match = FcFontMatch(config, pat, &result);
    FcPatternDestroy(pat);
    if (!match || result != FcResultMatch) {
        if (match) FcPatternDestroy(match);
        return false;
    }
    FcChar8* file = nullptr;
    ok = FcPatternGetString(match, FC_FILE, 0, &file) == FcResultMatch && file;
    if (ok) {
        path = (const char*)file;
        faceIndex = 0;
        FcPatternGetInteger(match, FC_INDEX, 0, &faceIndex);
    }
    FcPatternDestroy(match);
    return ok;
}

} // namespace

TextEngine::TextEngine() {
    if (!Init()) {
        // 初始化失败：保持 m_face == nullptr，Available() 返回 false
    }
}

TextEngine::~TextEngine() {
    if (m_hbFont) { hb_font_destroy((hb_font_t*)m_hbFont); m_hbFont = nullptr; }
    if (m_face) { FT_Done_Face((FT_Face)m_face); m_face = nullptr; }
    if (m_lib) { FT_Done_FreeType((FT_Library)m_lib); m_lib = nullptr; }
}

TextEngine& TextEngine::Instance() {
    static TextEngine engine;
    return engine;
}

bool TextEngine::Init() {
    FT_Library lib = nullptr;
    if (FT_Init_FreeType(&lib) != 0) return false;

    FcConfig* config = FcInitLoadConfigAndFonts();
    if (!config) { FT_Done_FreeType(lib); return false; }
    FT_Face face = nullptr;
    int faceIndex = 0;
    for (const char* family : {"Noto Sans CJK SC", "sans-serif"}) {
        std::string path;
        if (!FindFontViaFontconfig(config, family, path, faceIndex)) continue;
        if (FT_New_Face(lib, path.c_str(), faceIndex, &face) != 0) continue;
        if (FT_IS_SCALABLE(face) && FT_Select_Charmap(face, FT_ENCODING_UNICODE) == 0) break;
        FT_Done_Face(face);
        face = nullptr;
    }
    FcConfigDestroy(config);
    if (!face) { FT_Done_FreeType(lib); return false; }

    hb_font_t* font = hb_ft_font_create_referenced(face);
    if (!font || font == hb_font_get_empty()) {
        if (font) hb_font_destroy(font);
        FT_Done_Face(face);
        FT_Done_FreeType(lib);
        return false;
    }
    // HB 的 advance 与轮廓栅格化使用相同的无 hinting 度量。
    hb_ft_font_set_load_flags(font, FT_LOAD_NO_BITMAP | FT_LOAD_NO_HINTING);
    m_lib = lib;
    m_face = face;
    m_faceIndex = faceIndex;
    m_hbFont = font;
    return true;
}

// ---------------------------------------------------------------------------
// 内部：设置字号并 shaping，返回字形 id / 位置 / 度量（栅格像素）
// ---------------------------------------------------------------------------
struct ShapeResult {
    std::vector<uint32_t> glyphIds;
    std::vector<float> penX;   // 每个字形原点 x（栅格像素，相对文本起点）
    std::vector<float> penY;
    std::vector<uint32_t> clusters;
    std::vector<float> advances;
    std::vector<float> cursors;
    bool rtl = false;
    float advance = 0.0f;      // 总推进（栅格像素）
    float ascent = 0.0f;       // 栅格像素（>0 向上）
    float descent = 0.0f;      // 栅格像素（<0 向下）
    float originX = 0.0f;
    float baselineY = 0.0f;
    TextMetrics metrics;
};

static bool Shape(FT_Face face, hb_font_t* hbFont, const std::string& utf8,
                  float rasterSizePx, ShapeResult& out) {
    if (utf8.empty() || utf8.size() > 1024 * 1024) return false;
    // 必须检查设置失败，否则会沿用上一次字号。随后通知 HB 刷新 scale/cache。
    if (FT_Set_Char_Size(face, 0, (FT_F26Dot6)std::lround(rasterSizePx * 64.0), 72, 72) != 0)
        return false;
    hb_ft_font_changed(hbFont);

    hb_buffer_t* buf = hb_buffer_create();
    if (!hb_buffer_allocation_successful(buf)) { hb_buffer_destroy(buf); return false; }
    hb_buffer_add_utf8(buf, utf8.data(), (int)utf8.size(), 0, (int)utf8.size());
    hb_buffer_guess_segment_properties(buf);
    hb_buffer_set_language(buf, hb_language_from_string("zh-cn", -1));
    hb_shape(hbFont, buf, nullptr, 0);
    if (!hb_buffer_allocation_successful(buf)) { hb_buffer_destroy(buf); return false; }

    unsigned count = 0;
    hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(buf, &count);
    hb_glyph_position_t* pos = hb_buffer_get_glyph_positions(buf, &count);

    out.glyphIds.clear();
    out.penX.clear();
    out.penY.clear();
    out.clusters.clear();
    out.advances.clear();
    out.cursors.clear();
    out.rtl = HB_DIRECTION_IS_BACKWARD(hb_buffer_get_direction(buf));
    float penX = 0.0f, penY = 0.0f;
    for (unsigned i = 0; i < count; ++i) {
        out.glyphIds.push_back(infos[i].codepoint);
        out.clusters.push_back(infos[i].cluster);
        out.advances.push_back(pos[i].x_advance / 64.0f);
        out.cursors.push_back(penX);
        out.penX.push_back(penX + pos[i].x_offset / 64.0f);
        out.penY.push_back(penY + pos[i].y_offset / 64.0f);
        penX += pos[i].x_advance / 64.0f;
        penY += pos[i].y_advance / 64.0f;
    }
    out.advance = penX;
    out.ascent = (float)face->size->metrics.ascender / 64.0f;
    out.descent = (float)face->size->metrics.descender / 64.0f;
    hb_buffer_destroy(buf);
    return true;
}

static FT_Pos EmboldenStrength(float sizePx, int thickness) {
    // 每档扩张 em/48；原 em/24 在 thickness=4 时会填死 CJK 内部留白。
    return thickness > 1 ? (FT_Pos)std::lround((double(thickness) - 1.0) * sizePx * 64.0 / 48.0) : 0;
}

// 只在逻辑字号排版；所有 ss 共享 advance、原点与基线，避免逐字号取整引起漂移。
static bool Layout(FT_Face face, hb_font_t* font, const std::string& utf8,
                   float sizePx, int thickness, ShapeResult& s) {
    if (!ValidSize(sizePx) || thickness > 1024 || !Shape(face, font, utf8, sizePx, s)) return false;
    float left = 0.0f, right = s.advance, top = s.ascent, bottom = s.descent;
    FT_Pos strength = EmboldenStrength(sizePx, thickness);
    for (size_t i = 0; i < s.glyphIds.size(); ++i) {
        if (FT_Load_Glyph(face, s.glyphIds[i], FT_LOAD_NO_BITMAP | FT_LOAD_NO_HINTING) != 0) return false;
        if (face->glyph->format != FT_GLYPH_FORMAT_OUTLINE) return false;
        if (strength && FT_Outline_Embolden(&face->glyph->outline, strength) != 0) return false;
        if (!face->glyph->outline.n_points) continue;
        FT_BBox box;
        FT_Outline_Get_CBox(&face->glyph->outline, &box);
        left = std::min(left, s.penX[i] + box.xMin / 64.0f);
        right = std::max(right, s.penX[i] + box.xMax / 64.0f);
        top = std::max(top, s.penY[i] + box.yMax / 64.0f);
        bottom = std::min(bottom, s.penY[i] + box.yMin / 64.0f);
    }
    float padX = kMarginLogical + std::max(-left, right - s.advance);
    float padY = kMarginLogical + std::max(top - s.ascent, s.descent - bottom);
    s.metrics.advance = s.advance;
    s.metrics.width = std::ceil(s.advance + 2.0f * padX);
    s.metrics.height = std::ceil(s.ascent - s.descent + 2.0f * padY);
    if (s.metrics.width <= 0 || s.metrics.height <= 0 ||
        double(s.metrics.width) * s.metrics.height > kMaxPixels) return false;
    // 对称留白使组件可由 (width - advance) / 2 找到真正的 pen 原点。
    s.originX = (s.metrics.width - s.advance) * 0.5f;
    s.baselineY = (s.metrics.height - (s.ascent - s.descent)) * 0.5f + s.ascent;
    return true;
}

TextMetrics TextEngine::Measure(const std::string& utf8, float sizePx, int thickness) const {
    sizePx *= m_resolutionScale;  // 分辨率自缩放
    if (!m_face || !ValidSize(sizePx) || utf8.empty()) return {};
    std::lock_guard<std::mutex> lock(g_textMutex);
    using Key = std::tuple<std::string, float, int>;
    // 有界缓存，避免每帧反复 shaping；超长动态文本不长期驻留。
    static std::map<Key, TextMetrics> cache;
    Key key{utf8, sizePx, thickness};
    auto found = cache.find(key);
    if (found != cache.end()) return found->second;
    ShapeResult s;
    if (!Layout((FT_Face)m_face, (hb_font_t*)m_hbFont, utf8, sizePx, thickness, s)) return {};
    if (utf8.size() <= 4096) {
        if (cache.size() >= 256) cache.clear();
        cache.emplace(std::move(key), s.metrics);
    }
    return s.metrics;
}

float TextEngine::CaretPosition(const std::string& utf8, size_t byteOffset, float sizePx, int thickness) const {
    sizePx *= m_resolutionScale;  // 分辨率自缩放
    if (!m_face || !ValidSize(sizePx) || utf8.empty()) return 0.0f;
    std::lock_guard<std::mutex> lock(g_textMutex);
    // TextBox 一次点击会查询多个字节边界；保留最后一次完整排版。
    static std::tuple<std::string, float, int> key;
    static ShapeResult s;
    auto requested = std::make_tuple(utf8, sizePx, thickness);
    if (requested != key) {
        ShapeResult next;
        if (!Layout((FT_Face)m_face, (hb_font_t*)m_hbFont, utf8, sizePx, thickness, next)) return 0.0f;
        s = std::move(next);
        key = std::move(requested);
    }
    if (byteOffset >= utf8.size()) return s.originX + (s.rtl ? 0.0f : s.advance);
    if (!byteOffset) return s.originX + (s.rtl ? s.advance : 0.0f);
    for (size_t i = 0; i < s.clusters.size();) {
        size_t j = i + 1;
        float advance = s.advances[i];
        while (j < s.clusters.size() && s.clusters[j] == s.clusters[i]) advance += s.advances[j++];
        size_t begin = s.clusters[i];
        size_t end = s.rtl ? (i ? s.clusters[i - 1] : utf8.size()) :
                            (j < s.clusters.size() ? s.clusters[j] : utf8.size());
        if (byteOffset >= begin && byteOffset < end) {
            // ligature/组合字符内部按码点插值；cluster 边界使用完整 HB 推进。
            size_t count = 0, before = 0;
            for (size_t b = begin; b < end; ++b) {
                if (((unsigned char)utf8[b] & 0xC0) != 0x80) { ++count; if (b < byteOffset) ++before; }
            }
            float fraction = count ? float(before) / count : 0.0f;
            if (s.rtl) fraction = 1.0f - fraction;
            return s.originX + s.cursors[i] + fraction * advance;
        }
        i = j;
    }
    return s.originX;
}

cv::Mat TextEngine::Rasterize(const std::string& utf8, float sizePx, int thickness, float ss) const {
    sizePx *= m_resolutionScale;  // 分辨率自缩放
    if (!m_face || utf8.empty() || !ValidSize(sizePx)) return cv::Mat();
    if (!std::isfinite(ss) || ss <= 0.0f) ss = 1.0f;
    ss = std::max(1.0f, std::min(ss, 8.0f));
    std::lock_guard<std::mutex> lock(g_textMutex);

    FT_Face face = (FT_Face)m_face;
    ShapeResult s;
    if (!Layout(face, (hb_font_t*)m_hbFont, utf8, sizePx, thickness, s)) return cv::Mat();
    double width = std::ceil(double(s.metrics.width) * ss);
    double height = std::ceil(double(s.metrics.height) * ss);
    // 转成 int 之前检查，不能用可能发生有符号溢出的 W * H。
    if (width <= 0 || height <= 0 || width * height > kMaxPixels ||
        width > std::numeric_limits<int>::max() || height > std::numeric_limits<int>::max()) return cv::Mat();
    int W = (int)width, H = (int)height;
    if (FT_Set_Char_Size(face, 0, (FT_F26Dot6)std::lround(sizePx * ss * 64.0), 72, 72) != 0)
        return cv::Mat();
    hb_ft_font_changed((hb_font_t*)m_hbFont);
    cv::Mat mask(H, W, CV_8UC1, cv::Scalar(0));
    FT_Pos embolden = EmboldenStrength(sizePx * ss, thickness);

    for (size_t i = 0; i < s.glyphIds.size(); ++i) {
        if (FT_Load_Glyph(face, s.glyphIds[i], FT_LOAD_NO_BITMAP | FT_LOAD_NO_HINTING) != 0) return cv::Mat();
        if (embolden > 0 && FT_Outline_Embolden(&face->glyph->outline, embolden) != 0) return cv::Mat();
        if (FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL) != 0) return cv::Mat();

        FT_Bitmap* bmp = &face->glyph->bitmap;
        if (!bmp->width || !bmp->rows) continue;  // 空格等只推进，不产生 coverage
        if (!bmp->buffer || bmp->pixel_mode != FT_PIXEL_MODE_GRAY) return cv::Mat();
        int bx = (int)std::lround((s.penX[i] + s.originX) * ss + face->glyph->bitmap_left);
        // HB offset 与 bitmap_top 都是 Y-up；目标矩阵的行号是 Y-down。
        int by = (int)std::lround((s.baselineY - s.penY[i]) * ss - face->glyph->bitmap_top);
        for (int row = 0; row < (int)bmp->rows; ++row) {
            int dy = by + row;
            if (dy < 0 || dy >= H) continue;
            // pitch 是向下一行的有符号偏移；使用 ptrdiff_t 避免乘法溢出。
            const unsigned char* src = bmp->buffer + std::ptrdiff_t(row) * bmp->pitch;
            unsigned char* dst = mask.ptr<unsigned char>(dy);
            for (int col = 0; col < (int)bmp->width; ++col) {
                int dx = bx + col;
                if (dx < 0 || dx >= W) continue;
                if (src[col] > dst[dx]) dst[dx] = src[col];
            }
        }
    }
    return mask;
}

} // namespace ui
