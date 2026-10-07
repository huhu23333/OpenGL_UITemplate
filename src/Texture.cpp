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
}

Texture::Texture() = default;

Texture::~Texture() {
    Release();
}

Texture::Texture(Texture&& o) noexcept
    : m_id(o.m_id), m_width(o.m_width), m_height(o.m_height) {
    o.m_id = 0;
    o.m_width = o.m_height = 0;
}

Texture& Texture::operator=(Texture&& o) noexcept {
    if (this != &o) {
        Release();
        m_id = o.m_id;
        m_width = o.m_width;
        m_height = o.m_height;
        o.m_id = 0;
        o.m_width = o.m_height = 0;
    }
    return *this;
}

bool Texture::Upload(const cv::Mat& img) {
    if (img.empty() || img.dims != 2 || img.depth() != CV_8U) return false;

    GLint maxSize = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxSize);
    if (img.cols > maxSize || img.rows > maxSize) return false;
    cv::Mat rgba;
    switch (img.channels()) {
        case 1:  cv::cvtColor(img, rgba, cv::COLOR_GRAY2RGBA); break;
        case 3:  cv::cvtColor(img, rgba, cv::COLOR_BGR2RGBA);  break;
        case 4:  cv::cvtColor(img, rgba, cv::COLOR_BGRA2RGBA); break;
        default: return false;
    }
    return Upload(rgba.cols, rgba.rows, rgba.data);
}

bool Texture::Upload(int width, int height, const void* rgba8) {
    if (!rgba8 || width <= 0 || height <= 0) return false;
    GLint maxSize = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxSize);
    if (width > maxSize || height > maxSize) return false;

    if (m_id == 0) glGenTextures(1, &m_id);
    m_width = width;
    m_height = height;

    UploadState saved;
    glBindTexture(GL_TEXTURE_2D, m_id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    // 翻转行序：输入 row0=顶部 → 纹理 v=1=顶部（GL 约定 v=0=底部）
    const uint8_t* src = static_cast<const uint8_t*>(rgba8);
    std::vector<uint8_t> flipped(static_cast<size_t>(width) * height * 4);
    for (int r = 0; r < height; ++r) {
        std::memcpy(flipped.data() + static_cast<size_t>(r) * width * 4,
                    src + static_cast<size_t>(height - 1 - r) * width * 4,
                    static_cast<size_t>(width) * 4);
    }
    // 所有纹理与渲染目标统一使用预乘 alpha，避免离屏合成二次乘 alpha。
    for (size_t i = 0; i < flipped.size(); i += 4) {
        for (size_t c = 0; c < 3; ++c)
            flipped[i + c] = (uint8_t)((unsigned(flipped[i + c]) * flipped[i + 3] + 127) / 255);
    }
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, flipped.data());
    if (glGetError() != GL_NO_ERROR) {
        if (saved.texture == (GLint)m_id) saved.texture = 0;
        Release();
        return false;
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

bool Texture::CreateEmpty(int width, int height) {
    if (width <= 0 || height <= 0) return false;
    GLint maxSize = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxSize);
    if (width > maxSize || height > maxSize) return false;
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
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

void Texture::Release() {
    if (m_id != 0) {
        glDeleteTextures(1, &m_id);
        m_id = 0;
    }
    m_width = m_height = 0;
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
