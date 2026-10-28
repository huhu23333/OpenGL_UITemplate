# OpenGL UI Template

一个用 **C++ / OpenGL ES 3.0** 编写的离屏渲染 UI 模版。所有组件渲染到一张**固定分辨率**的离屏图像上（`glReadPixels` → `cv::Mat`），显示模式下按比例缩放到窗口中心（letterbox，多余区域填充纯色、不拉伸），也支持**无头模式**（无图形界面环境，仅离屏渲染输出 OpenCV 图像）。

## 特性

- **纯 C++ / CMake**，无任何绝对路径，移动到任意位置后 `cmake && make` 即可编译。
- **离屏渲染**：FBO + 纹理（分辨率可配置，默认 1280×720），输出 `cv::Mat`（显示/无头模式均可获取）。
- **窗口模式**：GLFW 创建 GLES3 窗口，letterbox 按比例居中显示，不拉伸。
- **无头模式**：EGL surfaceless 创建 GLES3 上下文，**无需任何显示服务**（服务器/机器人等环境可用）。
- **矩形组件层级**：每个组件有相对父组件的像素位置与宽高；只绘制父组件范围内的像素（scissor 裁剪）；先自绘再按序递归子组件。
- **自透明度**：`SetOpacity` 只影响自身绘制，不影响子组件。
- **输入**：可查询鼠标在每个组件中的相对位置、点击状态、键盘按键状态，支持鼠标捕获。
- **内置组件**：按钮、滑块、文字、文本框、图像显示（yolo 格式归一化框选）、虚拟画布容器（→ 滚动菜单、下拉菜单）。
- **逐帧图像更新**：`ImageDisplay::UpdateImage()` 复用纹理与转换缓冲，可逐帧喂视频/相机画面；更新图像不会打断拖拽/框选。
- **按需帧读回**：只有调用 `GetFrame()/GetFrameBGRA()` 时才 `glReadPixels`；纯显示场景没有读回开销。
- **性能埋点**：`UIApplication::GetFrameStats()` 给出每帧各阶段耗时与 draw call / 纹理切换 / scissor 切换等计数；`tests/` 下的 `ui_frame_bench` 为可复现基准。

## 依赖

- CMake ≥ 3.16
- C++17 编译器
- OpenGL ES 3.0（`glesv2`）、EGL（无头模式）
- GLFW 3（`glfw3`）
- OpenCV 4（`opencv4`）
- FreeType 2（`freetype2`）、HarfBuzz（`harfbuzz`）、Fontconfig（`fontconfig`）——文本矢量渲染与中文字体发现

Ubuntu/Debian 安装：

```bash
sudo apt install cmake g++ libgles2-mesa-dev libegl1-mesa-dev libglfw3-dev libopencv-dev \
                 libfreetype6-dev libharfbuzz-dev libfontconfig1-dev
```

## 构建

```bash
cmake -S . -B build
cmake --build build -j
```

产物：`build/bin/demo`、`build/lib/libui.a`。

测试与性能基准集中在独立工程 `tests/` 中，**不随外层工程一起构建**：

```bash
# 独立配置/构建测试目录（复用同一份源码与依赖）
cmake -S tests -B build-tests
cmake --build build-tests -j
ctest --test-dir build-tests --output-on-failure   # 离屏渲染/alpha/裁剪/输入/UTF-8/生命周期
./build-tests/bin/ui_window_regression              # 窗口 letterbox/resize（需显示服务）
./build-tests/bin/ui_frame_bench                    # 性能基准（无头）
./build-tests/bin/ui_frame_bench --window-only      # 性能基准（窗口 present）
```

说明与前后对比数据见 `tests/README.md`。

## 运行

```bash
# 窗口模式
./build/bin/demo

# 无头模式（离屏渲染，输出 headless_output.png）
./build/bin/demo --headless
```

### 应用实例（demo）内容

`examples/demo.cpp` 演示了模板的各类能力：

- **帧率显示**：移植的 `common/FrameRateCounter`（滑动平均），在画布右上角以文字显示 FPS。
- **按钮 / 滑块 / 文本框 / 下拉菜单 / 滚动菜单**：基础交互组件。
- **图像显示 + yolo 框选 + 逐帧更新**：每帧用 `UpdateImage()` 更新图像（亮带扫动），拖拽框选过程中持续更新，输出归一化 (cx, cy, w, h)。
- **鼠标捕获框**：点击框体捕获鼠标，按 `ESC` 释放；捕获期间以文字显示鼠标移动速度，并在框内用**带长度的箭头**可视化方向与大小。
- **日志栏**：带边框、可滚动（滚轮）的文字栏，滚动显示按钮点击/滑块/下拉/图像框选等事件。
- **透明度叠加**：一个半透明蓝色矩形**半叠**在不透明橙色矩形上，其透明度由滑条实时驱动（0%–100%）。

## 快速上手

```cpp
#include "ui/UIApplication.h"
#include "ui/Widgets.h"
#include "ui/CanvasGroup.h"

using namespace ui;

int main() {
    UIApplication::Config cfg;
    cfg.renderWidth  = 1280;   // 离屏渲染分辨率（可配置）
    cfg.renderHeight = 720;
    cfg.showWindow   = true;   // false = 无头模式

    UIApplication app;
    if (!app.Init(cfg)) return 1;

    auto root = std::make_unique<Component>();
    root->SetRect(0, 0, 1280, 720);

    auto* btn = root->AddChild<Button>();
    btn->SetRect(40, 40, 200, 48);
    btn->SetLabel("Hello");
    btn->SetOnClick([](Button*) { printf("clicked!\n"); });

    app.SetRoot(std::move(root));
    while (!app.ShouldClose()) app.RunFrame();

    // 任意时刻获取离屏图像
    cv::Mat frame = app.GetFrame();      // BGR
    return 0;
}
```

### 逐帧更新图像（视频 / 相机）

```cpp
// 一次性设置：深拷贝，GetImage() 与调用方数据解耦
imageDisp->SetImage(firstFrame);

while (!app.ShouldClose()) {
    cv::Mat frame;                 // BGR，来自 VideoCapture / 相机
    cap >> frame;
    if (!frame.empty()) imageDisp->UpdateImage(frame);  // 复用纹理/缓冲，可逐帧更新
    app.RunFrame();
}
```

`UpdateImage` 不做深拷贝（`GetImage()` 返回共享缓冲），因此不要原地改写同一个 `cv::Mat`；尺寸变化时会自动重建纹理存储。

## 组件层级模型

- 根组件 = 整幅离屏图像（`SetRoot` 的组件，矩形 = 渲染分辨率）。
- 每个组件用 `SetRect(x, y, w, h)` 指定**相对父组件左上角**的像素位置与宽高。
- 渲染时用 scissor 裁剪到「自身矩形 ∩ 父裁剪」，超出部分不会被绘制。
- 绘制顺序：先 `OnDraw()`（默认绘制边框，厚度 0 = 不绘制、颜色纯白），再按子组件添加顺序递归。
- `SetOpacity(a)` 仅作用于自身绘制，子组件透明度独立。

### 内置组件

| 组件 | 说明 |
| --- | --- |
| `Button` | 三态背景色（普通/悬停/按下）、文字标签、点击回调 |
| `Slider` | 值域、拖拽（自动捕获鼠标）、值变化回调 |
| `Text` | 文本、字号、颜色、水平/垂直对齐、可选背景 |
| `TextBox` | 单行输入、焦点、光标移动、退格/删除、回车提交、UTF-8 编辑 |
| `ImageDisplay` | 显示 `cv::Mat`（Stretch/Contain/Cover），点击/拖拽输出 **yolo 格式归一化** (cx, cy, w, h)；`SetImage` 深拷贝、`UpdateImage` 逐帧流式更新（复用纹理，不打断框选） |
| `CanvasGroup` | 虚拟画布容器（见下） |
| `ScrollView` | 滚动菜单（虚拟画布 + 滚动条 + 滚轮/拖拽） |
| `Dropdown` | 下拉菜单（头部按钮 + 弹出选项列表） |

### 虚拟画布容器（CanvasGroup，需求 #6）

`CanvasGroup` 在父组件中仍有位置/宽高（视口）且只在其范围内绘制，但其**自身与所有子组件**先渲染到一张独立虚拟画布（FBO，尺寸可配置），全部子组件绘制完成后再按规则合成回父画布（默认 = 带滚动偏移的视口）。

```cpp
auto* scroll = root->AddChild<ScrollView>();
scroll->SetRect(100, 100, 260, 320);      // 视口
scroll->SetCanvasSize(260, 1200);         // 虚拟画布（内容）尺寸
for (int i = 0; i < 30; ++i) {
    auto* item = scroll->AddChild<Button>();   // 子组件在虚拟画布坐标系内布局
    item->SetRect(0, i * 44, 260, 40);
}
```

### 输入

- `GetMouseLocal()` / `IsMouseInside()`：鼠标相对本组件的像素位置 / 是否在组件内。
- `IsMouseDown/Clicked/Released(button)`：鼠标按钮状态与边沿。
- `IsKeyDown/Pressed/Released(key)`：键盘状态与边沿（`Key` 枚举沿用 GLFW key code）。
- `SetMouseCapture(true)`：捕获后即使光标移出范围仍持续收到鼠标事件（拖拽用）。
- 无头模式用 `app.InjectMousePos / InjectMouseButton / InjectKey / InjectChar / InjectScroll` 注入输入。

## 架构

```
include/ui/
  Types.h          颜色/点/矩形/键值枚举
  InputState.h     输入状态快照
  RenderContext.h  渲染上下文抽象（窗口/无头）
  Texture.h        GL 纹理 + cv::Mat 上传
  Renderer2D.h     2D 批量渲染器（着色器/scissor/透明度/平移/视口切换）
  Component.h      组件基类（层级/布局/裁剪/输入）
  Widgets.h        按钮/滑块/文字/文本框/图像显示
  CanvasGroup.h    虚拟画布/滚动菜单/下拉菜单
  UIApplication.h  应用（离屏 FBO/letterbox/cv 输出/输入分发）
src/               对应实现
examples/demo.cpp  应用实例
tests/             测试与性能基准（独立 CMake 工程，不随外层构建）
```

关键实现要点：

- **渲染后端**：GLES3（`<GLES3/gl3.h>`，全核心函数，无需 GLEW）。窗口模式用 GLFW 的 `GLFW_OPENGL_ES_API`，无头模式用 EGL surfaceless + PBuffer，两种模式共享同一套渲染代码与着色器（GLSL ES 3.00）。
- **纹理朝向**：`Texture::TopDown()` 标记纹理行 0 是否为图像顶部。`cv::Mat` 上传时**不做 CPU 翻转**（`TopDown=true`），由 `Renderer2D` 在采样时翻转 V；FBO 渲染目标保持 `v=1=顶部`（`TopDown=false`）。两类纹理可无缝混用。
- **逐帧上传快路径**：`Texture` 复用 RGBA 转换缓冲；尺寸不变时走 `glTexSubImage2D`；1/3 通道输入 alpha 恒为 255，跳过预乘；4 通道单遍原地预乘。
- **懒读回**：`RenderOffscreen()` 只渲染不读回；`GetFrame()/GetFrameBGRA()` 首次访问时才对最近的离屏帧做 `glReadPixels` + 翻转/通道交换（OpenCV SIMD）。
- **布局去重**：几何 setter / 子组件增删会自增布局代数，`UIApplication` 在同一帧内只做一次整树布局（自定义组件在 setter 之外改布局时调用 `InvalidateLayout()`）。滚动偏移改变子组件屏幕坐标，也会触发重新布局。
- **绘制批处理**：`Renderer2D` 按纹理切换与 scissor 状态切分批次；scissor 状态未变化时不提交批次；叶子组件与"自身/子组件同裁剪"的容器只切换一次 scissor。
- **虚拟画布重定向**：`CanvasGroup::Render` 保存/恢复当前 FBO 绑定与视口，把子组件渲染到自己的 FBO，再 `DrawTextureSub` 按滚动偏移合成回父画布。

## 已知限制（模板级简化，可按需扩展）

- 文本渲染使用 HarfBuzz + FreeType 矢量轮廓（通过 Fontconfig 发现字体，支持中文等任意 Unicode），不再用 OpenCV Hershey 位图。
- 文本/按钮标签各自持有独立纹理，跨组件无法合批（每帧约与文字组件数同量级的 draw call）；文本图集（atlas）是后续可做的优化。
- 需要 CPU 像素时（每帧调用 `GetFrame*`），`glReadPixels` + 格式转换约 1.4ms（1080p，软件渲染；真实 GPU 取决于驱动），属于读回本身的成本。
- 缩小呈现时每帧生成 mipmap（~1ms/帧，软件渲染下明显）；仅在 `scale < 1` 时进行，放大呈现不做。
- 无头模式依赖 Linux EGL surfaceless；其他平台需自行适配（如 OSMesa）。同一进程内混用 EGL 无头上下文与 GLX 窗口上下文在部分 Mesa 环境会冲突（基准程序用 `--window-only` 单独测窗口模式）。
- 下拉菜单收起不自动响应"点击外部"，需通过头部按钮切换。
