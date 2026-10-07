#ifndef UI_TEXTURE_H
#define UI_TEXTURE_H

#include <GLES3/gl3.h>
#include <opencv2/core.hpp>

#include <cstdint>

namespace ui {

// ---------------------------------------------------------------------------
// OpenGL 纹理封装：负责从 cv::Mat 或原始 RGBA 数据上传，或创建空的渲染目标。
// 纹理坐标约定：采用 GL 标准 —— v = 0 表示纹理底部，v = 1 表示顶部。
//   - Upload(cv::Mat)：内部自动垂直翻转，使图像顶部对应 v=1。
//   - CreateEmpty（作为 FBO 渲染目标）：渲染内容顶部自然对应 v=1，无需翻转。
// 渲染器(Renderer2D)统一按 "v=1=顶部" 生成 UV，因此两类纹理可无缝混用。
// ---------------------------------------------------------------------------
class Texture {
public:
    Texture();
    ~Texture();

    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;
    Texture(Texture&&) noexcept;
    Texture& operator=(Texture&&) noexcept;

    // 从 cv::Mat 上传（支持 CV_8UC3 / CV_8UC4），内部统一转为 RGBA
    bool Upload(const cv::Mat& img);

    // 从顶部行优先、非预乘 RGBA8 数据上传；内部翻转并预乘 alpha
    bool Upload(int width, int height, const void* rgba8);

    // 创建空的渲染目标纹理（未初始化内容），用于离屏 FBO
    bool CreateEmpty(int width, int height);

    // 释放 GL 资源
    void Release();

    void Bind(int unit = 0) const;
    static void Unbind(int unit = 0);

    bool Valid() const { return m_id != 0; }
    GLuint Id() const { return m_id; }
    int Width() const { return m_width; }
    int Height() const { return m_height; }

private:
    GLuint m_id = 0;
    int    m_width = 0;
    int    m_height = 0;
};

} // namespace ui

#endif // UI_TEXTURE_H
