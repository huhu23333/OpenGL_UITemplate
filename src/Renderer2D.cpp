#include "ui/Renderer2D.h"

#include <cstdio>
#include <cstring>

namespace ui {

// ---------------------------------------------------------------------------
// 着色器（GLSL ES 3.00）：单一着色器，始终采样纹理；纯色绘制绑定 1x1 白色纹理。
// ---------------------------------------------------------------------------
static const char* kVertSrc = R"(#version 300 es
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;
uniform mat4 uProj;
out vec2 vUV;
out vec4 vColor;
void main() {
    gl_Position = uProj * vec4(aPos, 0.0, 1.0);
    vUV = aUV;
    vColor = aColor;
}
)";

static const char* kFragSrc = R"(#version 300 es
precision mediump float;
in vec2 vUV;
in vec4 vColor;
uniform sampler2D uTex;
out vec4 fragColor;
void main() {
    fragColor = texture(uTex, vUV) * vec4(vColor.rgb * vColor.a, vColor.a);
}
)";

static GLuint CompileShader(GLenum type, const char* src) {
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, nullptr);
    glCompileShader(sh);
    GLint ok = GL_FALSE;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(sh, sizeof(log), nullptr, log);
        std::fprintf(stderr, "[Renderer2D] shader compile error: %s\n", log);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

Renderer2D::~Renderer2D() {
    Shutdown();
}

bool Renderer2D::Init(int width, int height) {
    Shutdown();
    if (width <= 0 || height <= 0) return false;
    // ---- 着色器 ----
    GLuint vs = CompileShader(GL_VERTEX_SHADER, kVertSrc);
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, kFragSrc);
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return false;
    }

    m_program = glCreateProgram();
    glAttachShader(m_program, vs);
    glAttachShader(m_program, fs);
    glLinkProgram(m_program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = GL_FALSE;
    glGetProgramiv(m_program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(m_program, sizeof(log), nullptr, log);
        std::fprintf(stderr, "[Renderer2D] program link error: %s\n", log);
        Shutdown();
        return false;
    }
    m_uProj = glGetUniformLocation(m_program, "uProj");
    m_uTex  = glGetUniformLocation(m_program, "uTex");

    // ---- VAO / VBO ----
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    const size_t stride = sizeof(Vertex);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, x));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, u));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, r));
    glBindVertexArray(0);

    // ---- 白色纹理 ----
    const uint8_t white[4] = {255, 255, 255, 255};
    glGenTextures(1, &m_whiteTex);
    glBindTexture(GL_TEXTURE_2D, m_whiteTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
    glBindTexture(GL_TEXTURE_2D, 0);

    // ---- 混合 ----
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    m_vpW = width;
    m_vpH = height;
    m_scissorStack.push_back({false, 0, 0, 0, 0});
    return true;
}

void Renderer2D::Shutdown() {
    if (m_program) { glDeleteProgram(m_program); m_program = 0; }
    if (m_vao) { glDeleteVertexArrays(1, &m_vao); m_vao = 0; }
    if (m_vbo) { glDeleteBuffers(1, &m_vbo); m_vbo = 0; }
    if (m_whiteTex) { glDeleteTextures(1, &m_whiteTex); m_whiteTex = 0; }
    m_verts.clear();
    m_boundTex = nullptr;
    m_appliedScissor.valid = false;
    m_projDirty = true;
    m_textCache.clear();
    m_transStack.clear();
    m_scissorStack.clear();
    m_vpStack.clear();
}

void Renderer2D::BeginFrame(int width, int height) {
    Flush();
    m_boundTex = nullptr;
    // 跨帧复用常用文本；仅在批次已提交后清理，避免无界增长及悬空纹理。
    if (m_textCache.size() > 256) m_textCache.clear();
    m_vpStack.clear();
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    // 程序 / VAO / VBO 在整个帧内保持不变：每帧绑定一次，draw call 内不再重复绑定
    glUseProgram(m_program);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    m_vpW = width;
    m_vpH = height;
    glViewport(0, 0, width, height);
    m_verts.clear();
    m_boundTex = nullptr;
    m_opacity = 1.0f;
    m_transX = 0.0f;
    m_transY = 0.0f;
    m_transStack.clear();
    m_scissorStack.clear();
    m_scissorStack.push_back({false, 0, 0, 0, 0});
    m_projDirty = true;
    m_appliedScissor.valid = false;  // 视口变化后 GL scissor 状态需重新确认
    ApplyScissor(m_scissorStack.back());
}

void Renderer2D::EndFrame() {
    Flush();
}

void Renderer2D::Flush() {
    if (m_verts.empty()) return;
    if (m_vpW <= 0 || m_vpH <= 0) { m_verts.clear(); return; }

    // BeginFrame 已绑定程序 / VAO / VBO，这里只更新顶点数据
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)(m_verts.size() * sizeof(Vertex)),
                 m_verts.data(), GL_STREAM_DRAW);

    if (m_projDirty) {
        // 正交投影（列主序）；仅视口变化时重传，避免每次 draw call 都上传 uniform
        float proj[16];
        std::memset(proj, 0, sizeof(proj));
        proj[0] = 2.0f / (float)m_vpW;
        proj[5] = -2.0f / (float)m_vpH;
        proj[10] = 1.0f;
        proj[12] = -1.0f;
        proj[13] = 1.0f;
        proj[15] = 1.0f;
        glUniformMatrix4fv(m_uProj, 1, GL_FALSE, proj);
        glUniform1i(m_uTex, 0);
        m_projDirty = false;
    }

    GLuint tex = m_boundTex ? m_boundTex->Id() : m_whiteTex;
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);

    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)m_verts.size());
    ++m_stats.drawCalls;
    m_stats.vertices += (long long)m_verts.size();

    m_verts.clear();  // VAO 保持绑定，下一帧 BeginFrame 重新绑定
}

void Renderer2D::BindTexture(const Texture* tex) {
    if (tex != m_boundTex) {
        Flush();
        m_boundTex = tex;
        ++m_stats.textureBinds;
    }
}

void Renderer2D::PushQuad(const Vertex v[4]) {
    // 逆时针两个三角形：0-1-2, 0-2-3
    ++m_stats.quads;
    const int idx[6] = {0, 1, 2, 0, 2, 3};
    for (int i : idx) {
        Vertex vv = v[i];
        vv.x += m_transX;
        vv.y += m_transY;
        vv.a *= m_opacity;
        m_verts.push_back(vv);
    }
}

// ---------------------------------------------------------------------------
// scissor
// ---------------------------------------------------------------------------
bool Renderer2D::ComputeScissor(const ScissorState& s, int& x, int& y, int& w, int& h) const {
    if (!s.enabled) return false;
    // 按像素中心确定覆盖范围，分别换算边界，避免浮点宽高截断导致漏裁剪。
    Rect clipped = Rect(s.x, s.y, s.w, s.h).Intersect(Rect(0, 0, (float)m_vpW, (float)m_vpH));
    if (clipped.IsEmpty() || !std::isfinite(clipped.x) || !std::isfinite(clipped.y) ||
        !std::isfinite(clipped.w) || !std::isfinite(clipped.h)) {
        x = y = w = h = 0;
        return true;
    }
    int x0 = (int)std::ceil(clipped.x - 0.5f);
    int y0 = (int)std::ceil(clipped.y - 0.5f);
    int x1 = (int)std::ceil(clipped.Right() - 0.5f);
    int y1 = (int)std::ceil(clipped.Bottom() - 0.5f);
    x = x0;
    y = m_vpH - y1;
    w = std::max(0, x1 - x0);
    h = std::max(0, y1 - y0);
    return true;
}

bool Renderer2D::ScissorIsCurrent(const ScissorState& s) const {
    int x = 0, y = 0, w = 0, h = 0;
    const bool enabled = ComputeScissor(s, x, y, w, h);
    if (!m_appliedScissor.valid) return false;
    if (m_appliedScissor.enabled != enabled) return false;
    if (!enabled) return true;
    return m_appliedScissor.x == x && m_appliedScissor.y == y &&
           m_appliedScissor.w == w && m_appliedScissor.h == h;
}

void Renderer2D::ApplyScissor(const ScissorState& s) {
    if (ScissorIsCurrent(s)) { ++m_stats.scissorSkips; return; }
    int x = 0, y = 0, w = 0, h = 0;
    const bool enabled = ComputeScissor(s, x, y, w, h);
    if (enabled) {
        glEnable(GL_SCISSOR_TEST);
        glScissor(x, y, w, h);
    } else {
        glDisable(GL_SCISSOR_TEST);
    }
    m_appliedScissor = {true, enabled, x, y, w, h};
    ++m_stats.scissorChanges;
}

void Renderer2D::PushScissor(float x, float y, float w, float h) {
    Rect clip(x, y, w, h);
    if (!m_scissorStack.empty() && m_scissorStack.back().enabled) {
        const auto& s = m_scissorStack.back();
        clip = clip.Intersect(Rect(s.x, s.y, s.w, s.h));
    }
    ScissorState st{true, clip.x, clip.y, clip.w, clip.h};
    if (!ScissorIsCurrent(st)) Flush();  // 仅状态真的变化时才提交当前批次
    m_scissorStack.push_back(st);
    ApplyScissor(st);
}

void Renderer2D::PopScissor() {
    if (m_scissorStack.size() > 1) {
        m_scissorStack.pop_back();
        const ScissorState& st = m_scissorStack.back();
        if (!ScissorIsCurrent(st)) Flush();
        ApplyScissor(st);
    }
}

void Renderer2D::DisableScissor() {
    ScissorState st{false, 0.0f, 0.0f, 0.0f, 0.0f};
    if (!ScissorIsCurrent(st)) Flush();
    m_scissorStack.push_back(st);
    ApplyScissor(st);
}

// ---------------------------------------------------------------------------
// 平移
// ---------------------------------------------------------------------------
void Renderer2D::PushTranslate(float dx, float dy) {
    m_transStack.emplace_back(m_transX, m_transY);
    m_transX = dx;
    m_transY = dy;
}

void Renderer2D::PopTranslate() {
    if (m_transStack.empty()) return;
    m_transX = m_transStack.back().first;
    m_transY = m_transStack.back().second;
    m_transStack.pop_back();
}

// ---------------------------------------------------------------------------
// 透明度
// ---------------------------------------------------------------------------
void Renderer2D::SetOpacity(float a) {
    m_opacity = !std::isfinite(a) ? 0.0f : std::max(0.0f, std::min(a, 1.0f));
}

// ---------------------------------------------------------------------------
// 基本绘制
// ---------------------------------------------------------------------------
void Renderer2D::FillRect(float x, float y, float w, float h, const Color& c) {
    if (w <= 0.0f || h <= 0.0f) return;
    BindTexture(nullptr);
    Vertex v[4] = {
        {x,     y,     0, 0, c.r, c.g, c.b, c.a},
        {x + w, y,     0, 0, c.r, c.g, c.b, c.a},
        {x + w, y + h, 0, 0, c.r, c.g, c.b, c.a},
        {x,     y + h, 0, 0, c.r, c.g, c.b, c.a},
    };
    PushQuad(v);
}

void Renderer2D::StrokeRect(float x, float y, float w, float h, float t, const Color& c) {
    if (!std::isfinite(t) || !std::isfinite(x) || !std::isfinite(y) ||
        !std::isfinite(w) || !std::isfinite(h) || t <= 0.0f || w <= 0.0f || h <= 0.0f) return;
    t *= m_strokeScale;  // 线宽随分辨率缩放
    t = std::min(t, std::min(w, h) * 0.5f);
    // 上、下、左、右四条边
    FillRect(x, y, w, t, c);                 // top
    FillRect(x, y + h - t, w, t, c);         // bottom
    FillRect(x, y + t, t, h - t * 2.0f, c);  // left
    FillRect(x + w - t, y + t, t, h - t * 2.0f, c); // right
}

void Renderer2D::DrawLine(float x0, float y0, float x1, float y1, float thickness, const Color& c) {
    if (!std::isfinite(thickness) || thickness <= 0.0f ||
        !std::isfinite(x0) || !std::isfinite(y0) || !std::isfinite(x1) || !std::isfinite(y1)) return;
    thickness *= m_strokeScale;  // 线宽随分辨率缩放
    if (!std::isfinite(thickness) || thickness <= 0.0f) return;
    float dx = x1 - x0, dy = y1 - y0;
    float len = std::hypot(dx, dy);
    if (!std::isfinite(len) || len < 1e-6f) return;
    // 单位法向量（垂直于线段方向）
    float nx = -dy / len * thickness * 0.5f;
    float ny = dx / len * thickness * 0.5f;
    BindTexture(nullptr);
    Vertex v[4] = {
        {x0 + nx, y0 + ny, 0, 0, c.r, c.g, c.b, c.a},
        {x1 + nx, y1 + ny, 0, 0, c.r, c.g, c.b, c.a},
        {x1 - nx, y1 - ny, 0, 0, c.r, c.g, c.b, c.a},
        {x0 - nx, y0 - ny, 0, 0, c.r, c.g, c.b, c.a},
    };
    for (const auto& vertex : v) {
        if (!std::isfinite(vertex.x) || !std::isfinite(vertex.y)) return;
    }
    PushQuad(v);
}

void Renderer2D::DrawTexture(const Texture* tex, float x, float y, float w, float h, const Color& tint) {
    if (!tex || !tex->Valid()) return;
    DrawTextureSub(tex, 0, 0, (float)tex->Width(), (float)tex->Height(), x, y, w, h, tint);
}

void Renderer2D::DrawTextureSub(const Texture* tex,
                                float srcX, float srcY, float srcW, float srcH,
                                float dstX, float dstY, float dstW, float dstH,
                                const Color& tint) {
    if (!tex || !tex->Valid() || tex->Width() <= 0 || tex->Height() <= 0 ||
        srcW <= 0 || srcH <= 0 || dstW <= 0 || dstH <= 0) return;
    BindTexture(tex);
    float tw = (float)tex->Width();
    float th = (float)tex->Height();
    float u0 = srcX / tw;
    float u1 = (srcX + srcW) / tw;
    // 上传纹理（TopDown）行 0 = 图像顶部；FBO 纹理（v=1=顶部）按 GL 约定采样。
    float vTop, vBot;
    if (tex->TopDown()) {
        vTop = srcY / th;
        vBot = (srcY + srcH) / th;
    } else {
        vTop = 1.0f - srcY / th;
        vBot = 1.0f - (srcY + srcH) / th;
    }

    Vertex v[4] = {
        {dstX,         dstY,         u0, vTop, tint.r, tint.g, tint.b, tint.a},
        {dstX + dstW,  dstY,         u1, vTop, tint.r, tint.g, tint.b, tint.a},
        {dstX + dstW,  dstY + dstH,  u1, vBot, tint.r, tint.g, tint.b, tint.a},
        {dstX,         dstY + dstH,  u0, vBot, tint.r, tint.g, tint.b, tint.a},
    };
    PushQuad(v);
}

// ---------------------------------------------------------------------------
// 视口切换
// ---------------------------------------------------------------------------
void Renderer2D::PushViewport(int width, int height) {
    Flush();
    ViewportState s;
    s.w = m_vpW;
    s.h = m_vpH;
    s.scissors = std::move(m_scissorStack);   // 移走而非复制，避免每帧分配
    s.translations = std::move(m_transStack);
    s.opacity = m_opacity;
    s.transX = m_transX;
    s.transY = m_transY;
    m_vpStack.push_back(std::move(s));

    m_vpW = width;
    m_vpH = height;
    glViewport(0, 0, width, height);
    m_projDirty = true;
    m_appliedScissor.valid = false;  // scissor 的 GL y 坐标依赖视口高度
    m_scissorStack.clear();
    m_scissorStack.push_back({false, 0, 0, 0, 0});
    ApplyScissor(m_scissorStack.back());
    m_transX = 0.0f;
    m_transY = 0.0f;
    m_transStack.clear();
    m_opacity = 1.0f;
}

void Renderer2D::PopViewport() {
    if (m_vpStack.empty()) return;
    Flush();
    ViewportState s = m_vpStack.back();
    m_vpStack.pop_back();
    m_vpW = s.w;
    m_vpH = s.h;
    glViewport(0, 0, s.w, s.h);
    m_projDirty = true;
    m_appliedScissor.valid = false;
    m_scissorStack.clear();
    m_scissorStack = std::move(s.scissors);
    m_transStack = std::move(s.translations);
    m_opacity = s.opacity;
    ApplyScissor(m_scissorStack.back());
    m_transX = s.transX;
    m_transY = s.transY;
}

} // namespace ui
