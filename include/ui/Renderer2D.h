#ifndef UI_RENDERER2D_H
#define UI_RENDERER2D_H

#include <GLES3/gl3.h>

#include "Types.h"
#include "Texture.h"

#include <vector>
#include <map>
#include <memory>
#include <string>
#include <tuple>

namespace ui {

// ---------------------------------------------------------------------------
// 2D 批量渲染器（GLES3 + 单个着色器）：
//   - 正交投影：原点在左上角，Y 向下，单位 = 当前逻辑像素。
//   - 支持纯色矩形、描边矩形、带纹理矩形（缩放/裁剪）。
//   - 支持 scissor 裁剪（参数为 UI 约定：x/y 左上角、Y 向下），用栈管理。
//   - 支持"当前透明度"：后续顶点的 alpha 会被乘以该系数（用于组件的自透明度）。
//
// 所有绘制先累积到顶点批次，在以下时机 flush 到 GL：
//   - EndFrame / 切换纹理 / 切换透明度 / Push/Pop 视口 / Push/Pop scissor
// 纹理约定：v=0=底部、v=1=顶部（与 Texture 一致）。
// ---------------------------------------------------------------------------
class Renderer2D {
public:
    Renderer2D() = default;
    ~Renderer2D();

    Renderer2D(const Renderer2D&) = delete;
    Renderer2D& operator=(const Renderer2D&) = delete;

    // 初始化着色器 / VAO / VBO / 白色纹理。width/height 为初始逻辑尺寸。
    bool Init(int width, int height);
    void Shutdown();

    // 每帧开始：设置视口与投影，重置批次。
    void BeginFrame(int width, int height);
    // 每帧结束：flush 剩余批次。
    void EndFrame();

    // ---- scissor 裁剪（UI 坐标：x/y 左上角，Y 向下）----
    void PushScissor(float x, float y, float w, float h);  // 保存当前并设置新的
    void PopScissor();                                     // 恢复上一层
    void DisableScissor();                                 // 禁用裁剪（保存当前后禁用）

    // ---- 平移（用于组件局部坐标系绘制）----
    void PushTranslate(float dx, float dy);  // 保存当前并设置新的平移
    void PopTranslate();                     // 恢复上一层

    // ---- 透明度：影响后续所有绘制顶点的 alpha（乘性）----
    void SetOpacity(float a);  // a ∈ [0,1]，=1 表示不透明

    // ---- 线宽缩放：所有 StrokeRect / DrawLine 的厚度乘以该因子（默认 1，用于分辨率自缩放）----
    void SetStrokeScale(float s) { m_strokeScale = std::isfinite(s) && s > 0.0f ? s : 1.0f; }
    float GetStrokeScale() const { return m_strokeScale; }

    // ---- 基本绘制 ----
    void FillRect(float x, float y, float w, float h, const Color& c);
    // 描边：在矩形内部绘制指定厚度的边框（厚度<=0 时不绘制）
    void StrokeRect(float x, float y, float w, float h, float thickness, const Color& c);
    // 线段（指定厚度，任意方向）
    void DrawLine(float x0, float y0, float x1, float y1, float thickness, const Color& c);

    // 绘制完整纹理到目标矩形（等比拉伸）
    void DrawTexture(const Texture* tex, float x, float y, float w, float h,
                     const Color& tint = Color::White());
    // 绘制纹理子区域（src 为纹理像素坐标，左上角原点）到目标矩形
    void DrawTextureSub(const Texture* tex,
                        float srcX, float srcY, float srcW, float srcH,
                        float dstX, float dstY, float dstW, float dstH,
                        const Color& tint = Color::White());

    // ---- 视口切换（用于虚拟画布 / 离屏 FBO 重定向）----
    // 切换后 scissor 与投影以新的逻辑尺寸为准；旧状态压栈。
    void PushViewport(int width, int height);
    void PopViewport();

    // 强制提交当前批次（切换 FBO 前调用）
    void Flush();

    using TextCacheMap = std::map<std::tuple<std::string, float, int, float>, std::unique_ptr<Texture>>;
    TextCacheMap& TextCache() { return m_textCache; }

    int ViewportWidth() const { return m_vpW; }
    int ViewportHeight() const { return m_vpH; }

private:
    struct Vertex {
        float x, y, u, v, r, g, b, a;
    };

    void PushQuad(const Vertex v[4]);
    void BindTexture(const Texture* tex);

    GLuint m_program = 0;
    GLuint m_vao = 0;
    GLuint m_vbo = 0;
    GLuint m_whiteTex = 0;
    GLint  m_uProj = -1;
    GLint  m_uTex = -1;
    GLint  m_uUseTex = -1;

    TextCacheMap m_textCache;
    std::vector<Vertex> m_verts;
    const Texture* m_boundTex = nullptr;
    float  m_opacity = 1.0f;
    float  m_strokeScale = 1.0f;

    float m_transX = 0.0f;
    float m_transY = 0.0f;
    std::vector<std::pair<float, float>> m_transStack;

    int m_vpW = 0;
    int m_vpH = 0;

    // scissor 栈（UI 坐标）
    struct ScissorState {
        bool enabled = false;
        float x = 0, y = 0, w = 0, h = 0;
    };
    std::vector<ScissorState> m_scissorStack;

    // 视口栈（保存宽高 + 当时的 scissor 状态 + 平移，供 Push/PopViewport 恢复）
    struct ViewportState {
        int w = 0, h = 0;
        std::vector<ScissorState> scissors;
        std::vector<std::pair<float, float>> translations;
        float opacity = 1.0f;
        float transX = 0.0f;
        float transY = 0.0f;
    };
    std::vector<ViewportState> m_vpStack;

    void ApplyScissor(const ScissorState& s);
};

} // namespace ui

#endif // UI_RENDERER2D_H
