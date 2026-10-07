#include "ui/TextEngine.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H
#include <harfbuzz/hb.h>
#include <harfbuzz/hb-ft.h>
#include <fontconfig/fontconfig.h>

#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace ui {

namespace {

// 纹理四周的留白（逻辑像素），覆盖字形负 bearing、重音等越界
constexpr float kMarginLogical = 2.0f;

// 通过 Fontconfig 查找 CJK 字体
bool FindFontViaFontconfig(std::string& path, int& faceIndex) {
    if (!FcInit()) return false;
    FcPattern* pat = FcPatternCreate();
    FcPatternAddString(pat, FC_FAMILY, (const FcChar8*)"Noto Sans CJK SC");
    FcPatternAddString(pat, FC_LANG, (const FcChar8*)"zh-cn");
    FcConfigSubstitute(nullptr, pat, FcMatchPattern);
    FcDefaultSubstitute(pat);
    FcResult result = FcResultNoMatch;
    FcPattern* match = FcFontMatch(nullptr, pat, &result);
    FcPatternDestroy(pat);
    if (!match || result == FcResultNoMatch) {
        if (match) FcPatternDestroy(match);
        return false;
    }
    FcChar8* file = nullptr;
    bool ok = FcPatternGetString(match, FC_FILE, 0, &file) == FcResultMatch;
    if (ok) {
        path = (const char*)file;
        int idx = 0;
        FcPatternGetInteger(match, FC_INDEX, 0, &idx);
        faceIndex = idx;
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

    std::string path;
    int faceIndex = 0;
    bool found = FindFontViaFontconfig(path, faceIndex);
    if (!found) {
        // Fontconfig 不可用或找不到时，用常见 CJK 字体候选兜底
        static const char* kCandidates[] = {
            "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
            "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf",
            "/usr/share/fonts/truetype/arphic/uming.ttc",
            "/usr/share/fonts/Windows/SourceHanSansJP-Normal.otf",
        };
        for (const char* c : kCandidates) {
            if (::access(c, R_OK) == 0) { path = c; faceIndex = 0; found = true; break; }
        }
    }
    if (!found) { FT_Done_FreeType(lib); return false; }

    FT_Face face = nullptr;
    if (FT_New_Face(lib, path.c_str(), faceIndex, &face) != 0) {
        FT_Done_FreeType(lib);
        return false;
    }
    if (FT_Select_Charmap(face, FT_ENCODING_UNICODE) != 0) {
        FT_Done_Face(face);
        FT_Done_FreeType(lib);
        return false;
    }

    m_lib = lib;
    m_face = face;
    m_faceIndex = faceIndex;
    m_hbFont = hb_ft_font_create_referenced(face);
    return true;
}

// ---------------------------------------------------------------------------
// 内部：设置字号并 shaping，返回字形 id / 位置 / 度量（栅格像素）
// ---------------------------------------------------------------------------
struct ShapeResult {
    std::vector<uint32_t> glyphIds;
    std::vector<float> penX;   // 每个字形原点 x（栅格像素，相对文本起点）
    std::vector<float> penY;
    float advance = 0.0f;      // 总推进（栅格像素）
    float ascent = 0.0f;       // 栅格像素（>0 向上）
    float descent = 0.0f;      // 栅格像素（<0 向下）
};

static bool Shape(FT_Face face, hb_font_t* hbFont, const std::string& utf8,
                  float rasterSizePx, ShapeResult& out) {
    if (utf8.empty()) return false;
    // 设置字号（26.6 定点）
    FT_Set_Char_Size(face, 0, (FT_F26Dot6)std::lround(rasterSizePx * 64.0), 72, 72);
    hb_ft_font_changed(hbFont);

    hb_buffer_t* buf = hb_buffer_create();
    hb_buffer_add_utf8(buf, utf8.c_str(), (int)utf8.size(), 0, (int)utf8.size());
    hb_buffer_guess_segment_properties(buf);
    hb_buffer_set_language(buf, hb_language_from_string("zh-cn", -1));
    hb_shape(hbFont, buf, nullptr, 0);

    unsigned count = 0;
    hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(buf, &count);
    hb_glyph_position_t* pos = hb_buffer_get_glyph_positions(buf, &count);

    out.glyphIds.clear();
    out.penX.clear();
    out.penY.clear();
    float penX = 0.0f, penY = 0.0f;
    for (unsigned i = 0; i < count; ++i) {
        out.glyphIds.push_back(infos[i].codepoint);
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

TextMetrics TextEngine::Measure(const std::string& utf8, float sizePx, int thickness) const {
    (void)thickness;
    TextMetrics m;
    if (!m_face) return m;
    ShapeResult s;
    if (!Shape((FT_Face)m_face, (hb_font_t*)m_hbFont, utf8, sizePx, s)) return m;
    float lineHeight = s.ascent - s.descent;
    m.advance = s.advance;
    m.width = s.advance + kMarginLogical * 2.0f;
    m.height = lineHeight + kMarginLogical * 2.0f;
    return m;
}

cv::Mat TextEngine::Rasterize(const std::string& utf8, float sizePx, int thickness, float ss) const {
    if (!m_face || utf8.empty()) return cv::Mat();
    if (!std::isfinite(ss) || ss <= 0.0f) ss = 1.0f;
    if (ss > 8.0f) ss = 8.0f;  // 限制超采样上限，避免位图爆炸

    FT_Face face = (FT_Face)m_face;
    float rasterSize = sizePx * ss;
    ShapeResult s;
    if (!Shape(face, (hb_font_t*)m_hbFont, utf8, rasterSize, s)) return cv::Mat();

    float lineHeight = s.ascent - s.descent;
    float margin = kMarginLogical * ss;
    int W = (int)std::ceil(s.advance + margin * 2.0f);
    int H = (int)std::ceil(lineHeight + margin * 2.0f);
    // 面积上限，防止超长文本 / 超大字号导致内存失控
    if (W <= 0 || H <= 0 || W * H > 64 * 1024 * 1024) return cv::Mat();

    cv::Mat mask(H, W, CV_8UC1, cv::Scalar(0));
    float baselineY = margin + s.ascent;

    // 合成加粗强度（26.6 定点）
    FT_Pos embolden = 0;
    if (thickness > 1) {
        embolden = (FT_Pos)std::lround((double)(thickness - 1) * rasterSize * 64.0 / 24.0);
        if (embolden < 0) embolden = 0;
    }

    for (size_t i = 0; i < s.glyphIds.size(); ++i) {
        if (FT_Load_Glyph(face, s.glyphIds[i], FT_LOAD_NO_BITMAP | FT_LOAD_NO_HINTING) != 0) continue;
        if (embolden > 0) FT_Outline_Embolden(&face->glyph->outline, embolden);
        if (FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL) != 0) continue;

        FT_Bitmap* bmp = &face->glyph->bitmap;
        int bx = (int)std::lround(s.penX[i] + margin + face->glyph->bitmap_left);
        int by = (int)std::lround(baselineY - face->glyph->bitmap_top);
        for (int row = 0; row < (int)bmp->rows; ++row) {
            int dy = by + row;
            if (dy < 0 || dy >= H) continue;
            const unsigned char* src = bmp->buffer + row * (int)bmp->pitch;
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
