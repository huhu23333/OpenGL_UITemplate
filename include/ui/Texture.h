#ifndef UI_TEXTURE_H
#define UI_TEXTURE_H

#include <GLES3/gl3.h>
#include <opencv2/core.hpp>

#include <cstdint>
#include <vector>

namespace ui {

// ---------------------------------------------------------------------------
// OpenGL 纹理封装：负责从 cv::Mat 或原始 RGBA 数据上传，或创建空的渲染目标。
//
// 朝向约定（渲染器通过 TopDown() 统一处理）：
//   - Upload(cv::Mat)：按"顶部行优先"原样上传，不做 CPU 翻转，TopDown()==true。
//   - Upload(w,h,rgba8)：输入顶部行优先、非预乘 RGBA8，融合预乘后上传，TopDown()==true。
//   - CreateEmpty（FBO 渲染目标）：内容天然 v=1=顶部，TopDown()==false。
//   Renderer2D 依据 TopDown() 生成 UV，两类纹理可无缝混用。
//
// 性能要点：复用转换/暂存缓冲；尺寸不变时用 glTexSubImage2D 原地更新，
// 避免逐帧重建纹理与 GL 存储（逐帧更新图像时必须走这条路径）。
// ---------------------------------------------------------------------------
class Texture {
public:
    Texture();
    ~Texture();

    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;
    Texture(Texture&&) noexcept;
    Texture& operator=(Texture&&) noexcept;

    // 从 cv::Mat 上传（支持 CV_8UC1 / CV_8UC3 / CV_8UC4），内部统一转为 RGBA。
    // 1/3 通道输入 alpha 恒为 255，跳过预乘；4 通道输入逐像素预乘 alpha。
    bool Upload(const cv::Mat& img);

    // 从顶部行优先、非预乘 RGBA8 数据上传；内部预乘 alpha，不翻转（TopDown=true）。
    bool Upload(int width, int height, const void* rgba8);

    // 创建空的渲染目标纹理（未初始化内容），用于离屏 FBO
    bool CreateEmpty(int width, int height);

    // 生成 mipmap 链，并将缩小过滤切换为三线性（GL_LINEAR_MIPMAP_LINEAR）。
    // 用于离屏渲染目标纹理在按比例缩小呈现时的正确抗锯齿（需在每次内容更新后重新生成）。
    void GenerateMipmaps();

    // 释放 GL 资源
    void Release();

    void Bind(int unit = 0) const;
    static void Unbind(int unit = 0);

    bool Valid() const { return m_id != 0; }
    GLuint Id() const { return m_id; }
    int Width() const { return m_width; }
    int Height() const { return m_height; }
    // 纹理行 0 是否为图像顶部（true = 渲染采样时需要翻转 V）
    bool TopDown() const { return m_topDown; }

    // 缓存的 GL_MAX_TEXTURE_SIZE（每次查询有驱动开销，且进程内稳定）
    static GLint MaxSize();

private:
    bool UploadTopDown(const void* data, int width, int height, int rowStridePixels);

    GLuint m_id = 0;
    int    m_width = 0;
    int    m_height = 0;
    int    m_allocW = 0;   // 当前 GL 存储尺寸（决定能否走 SubImage 快路径）
    int    m_allocH = 0;
    bool   m_topDown = false;
    cv::Mat m_conv;                     // Upload(cv::Mat) 的 RGBA 转换缓冲（复用）
    std::vector<uint8_t> m_staging;     // 原始 RGBA 上传的预乘暂存（复用）
};

} // namespace ui

#endif // UI_TEXTURE_H
