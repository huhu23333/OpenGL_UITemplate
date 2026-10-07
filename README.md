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

## 依赖

- CMake ≥ 3.16
- C++17 编译器
- OpenGL ES 3.0（`glesv2`）、EGL（无头模式）
- GLFW 3（`glfw3`）
- OpenCV 4（`opencv4`）

Ubuntu/Debian 安装：

```bash
sudo apt install cmake g++ libgles2-mesa-dev libegl1-mesa-dev libglfw3-dev libopencv-dev
```

## 构建

```bash
cmake -S . -B build
cmake --build build -j
```

产物：`build/bin/demo`、`build/lib/libui.a`。

回归测试（默认随构建一起编译，`-DUI_BUILD_TESTS=OFF` 可关闭）：

```bash
ctest --test-dir build --output-on-failure   # 离屏渲染/alpha/裁剪/输入/UTF-8/生命周期
./build/bin/ui_window_regression              # 窗口 letterbox/resize（需显示服务）
```

## 运行

```bash
# 窗口模式
./build/bin/demo

# 无头模式（离屏渲染，输出 headless_output.png）
./build/bin/demo --headless
```

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
| `ImageDisplay` | 显示 `cv::Mat`（Stretch/Contain/Cover），点击/拖拽输出 **yolo 格式归一化** (cx, cy, w, h) |
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
```

关键实现要点：

- **渲染后端**：GLES3（`<GLES3/gl3.h>`，全核心函数，无需 GLEW）。窗口模式用 GLFW 的 `GLFW_OPENGL_ES_API`，无头模式用 EGL surfaceless + PBuffer，两种模式共享同一套渲染代码与着色器（GLSL ES 3.00）。
- **纹理约定**：统一采用 GL 标准 `v=1=顶部`；`cv::Mat` 上传时自动垂直翻转，FBO 渲染目标天然一致，因此离屏纹理可直接 blit 到窗口或读回图像而不翻转。
- **虚拟画布重定向**：`CanvasGroup::Render` 保存/恢复当前 FBO 绑定与视口，把子组件渲染到自己的 FBO，再 `DrawTextureSub` 按滚动偏移合成回父画布。

## 已知限制（模板级简化，可按需扩展）

- 文本栅格化使用 OpenCV Hershey 字体（仅 ASCII），中文会显示为 `?`；可替换为 FreeType/stb_truetype。
- 无头模式依赖 Linux EGL surfaceless；其他平台需自行适配（如 OSMesa）。
- 下拉菜单收起不自动响应"点击外部"，需通过头部按钮切换。
