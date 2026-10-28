#include "ui/Texture.h"

#include <opencv2/imgproc.hpp>

#include <cstring>
#include <vector>

namespace ui {

namespace {
struct UploadState {
    GLint texture, alignment, rowLength, skipRows, skipPixels, buffer;
    UploadState() {
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
        glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment);
        glGetIntegerv(GL_UNPACK_ROW_LENGTH, &rowLength);
        glGetIntegerv(GL_UNPACK_SKIP_ROWS, &skipRows);
        glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &skipPixels);
        glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &buffer);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    }
    ~UploadState() {
        glBindTexture(GL_TEXTURE_2D, texture);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, buffer);
        glPixelStorei(GL_UNPACK_ALIGNMENT, alignment);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, rowLength);
        glPixelStorei(GL_UNPACK_SKIP_ROWS, skipRows);
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, skipPixels);
    }
};

inline uint8_t Premul(uint8_t c, unsigned a) {
    return (uint8_t)((unsigned(c) * a + 127u) / 255u);
}
} // namespace

GLint Texture::MaxSize() {
    static GLint cached = 0;
    if (cached > 0) return cached;
    GLint v = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &v);
    if (v > 0) cached = v;
    return v;
}

Texture::Texture() = default;

Texture::~Texture() {
    Release();
}

Texture::Texture(Texture&& o) noexcept
    : m_id(o.m_id), m_width(o.m_width), m_height(o.m_height),
      m_allocW(o.m_allocW), m_allocH(o.m_allocH), m_topDown(o.m_topDown),
      m_conv(std::move(o.m_conv)), m_staging(std::move(o.m_staging)) {
    o.m_id = 0;
    o.m_width = o.m_height = 0;
    o.m_allocW = o.m_allocH = 0;
    o.m_topDown = false;
}

Texture& Texture::operator=(Texture&& o) noexcept {
    if (this != &o) {
        Release();
        m_id = o.m_id;
        m_width = o.m_width;
        m_height = o.m_height;
        m_allocW = o.m_allocW;
        m_allocH = o.m_allocH;
        m_topDown = o.m_topDown;
        m_conv = std::move(o.m_conv);
        m_staging = std::move(o.m_staging);
        o.m_id = 0;
        o.m_width = o.m_height = 0;
        o.m_allocW = o.m_allocH = 0;
        o.m_topDown = false;
    }
    return *this;
}

bool Texture::Upload(const cv::Mat& img) {
    if (img.empty() || img.dims != 2 || img.depth() != CV_8U) return false;
    if (img.cols <= 0 || img.rows <= 0) return false;
    const GLint maxSize = MaxSize();
    if (maxSize <= 0 || img.cols > maxSize || img.rows > maxSize) return false;

    const int channels = img.channels();
    int code = 0;
    switch (channels) {
        case 1:  code = cv::COLOR_GRAY2RGBA; break;
        case 3:  code = cv::COLOR_BGR2RGBA;  break;
        case 4:  code = cv::COLOR_BGRA2RGBA; break;
        default: return false;
    }
    // 复用转换缓冲：同尺寸连续更新时不再分配
    cv::cvtColor(img, m_conv, code);

    // 预乘 alpha：1/3 通道输入转换后 alpha 恒为 255，预乘是恒等变换，直接跳过；
    // 4 通道输入逐像素预乘（原地，单遍）。
    if (channels == 4) {
        for (int y = 0; y < m_conv.rows; ++y) {
            uint8_t* row = m_conv.ptr<uint8_t>(y);
            for (int x = 0; x < m_conv.cols; ++x, row += 4) {
                const unsigned a = row[3];
                row[0] = Premul(row[0], a);
                row[1] = Premul(row[1], a);
                row[2] = Premul(row[2], a);
            }
        }
    }
    // 直接上传（不翻转）：渲染器按 TopDown() 翻转 UV。
    return UploadTopDown(m_conv.ptr<uint8_t>(), m_conv.cols, m_conv.rows,
                         (int)(m_conv.step / 4));
}

bool Texture::Upload(int width, int height, const void* rgba8) {
    if (!rgba8 || width <= 0 || height <= 0) return false;
    const GLint maxSize = MaxSize();
    if (maxSize <= 0 || width > maxSize || height > maxSize) return false;

    // 兼容入口：输入顶部行优先、非预乘 RGBA8。融合"拷贝 + 预乘"为单遍，
    // 不再像旧实现那样先翻转再预乘（翻转由渲染器采样时完成）。
    const size_t rowBytes = (size_t)width * 4;
    const size_t bytes = rowBytes * (size_t)height;
    if (m_staging.size() < bytes) m_staging.resize(bytes);
    const uint8_t* src = static_cast<const uint8_t*>(rgba8);
    uint8_t* dst = m_staging.data();
    for (int y = 0; y < height; ++y) {
        const uint8_t* s = src + (size_t)y * rowBytes;
        uint8_t* d = dst + (size_t)y * rowBytes;
        for (int x = 0; x < width; ++x, s += 4, d += 4) {
            const uint8_t a = s[3];
            d[0] = Premul(s[0], a);
            d[1] = Premul(s[1], a);
            d[2] = Premul(s[2], a);
            d[3] = a;
        }
    }
    return UploadTopDown(dst, width, height, width);
}

bool Texture::UploadTopDown(const void* data, int width, int height, int rowStridePixels) {
    if (!data || width <= 0 || height <= 0) return false;
    if (rowStridePixels < width) rowStridePixels = width;

    if (m_id == 0) glGenTextures(1, &m_id);
    UploadState saved;
    glBindTexture(GL_TEXTURE_2D, m_id);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, rowStridePixels);

    if (m_allocW != width || m_allocH != height) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, data);
        if (glGetError() != GL_NO_ERROR) {
            if (saved.texture == (GLint)m_id) saved.texture = 0;
            Release();
            return false;
        }
        m_allocW = width;
        m_allocH = height;
    } else {
        // 尺寸不变：原地更新存储，避免每帧重新分配纹理
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height,
                        GL_RGBA, GL_UNSIGNED_BYTE, data);
        if (glGetError() != GL_NO_ERROR) {
            if (saved.texture == (GLint)m_id) saved.texture = 0;
            Release();
            return false;
        }
    }
    m_width = width;
    m_height = height;
    m_topDown = true;
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

bool Texture::CreateEmpty(int width, int height) {
    if (width <= 0 || height <= 0) return false;
    const GLint maxSize = MaxSize();
    if (maxSize <= 0 || width > maxSize || height > maxSize) return false;
    if (m_id == 0) glGenTextures(1, &m_id);
    m_width = width;
    m_height = height;

    UploadState saved;
    glBindTexture(GL_TEXTURE_2D, m_id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    if (glGetError() != GL_NO_ERROR) {
        if (saved.texture == (GLint)m_id) saved.texture = 0;
        Release();
        return false;
    }
    m_allocW = width;
    m_allocH = height;
    m_topDown = false;   // FBO 渲染目标：内容天然 v=1=顶部
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

void Texture::GenerateMipmaps() {
    if (m_id == 0) return;
    glBindTexture(GL_TEXTURE_2D, m_id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Texture::Release() {
    if (m_id != 0) {
        glDeleteTextures(1, &m_id);
        m_id = 0;
    }
    m_width = m_height = 0;
    m_allocW = m_allocH = 0;
    m_topDown = false;
    m_conv.release();
    m_staging.clear();
}

void Texture::Bind(int unit) const {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, m_id);
}

void Texture::Unbind(int unit) {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, 0);
}

} // namespace ui
