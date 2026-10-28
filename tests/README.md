# 测试与性能基准

本目录集中存放所有测试、性能基准与基线探针，是一个**独立的 CMake 工程**，不随外层工程一起构建。

| 文件 | 说明 |
| --- | --- |
| `review_regression.cpp` | 离屏渲染/alpha/裁剪/输入/UTF-8/生命周期/图像与框选回归 |
| `demo_acceptance.cpp` | demo 场景验收回归 |
| `window_regression.cpp` | 窗口 letterbox/resize（需显示服务） |
| `bench_scene.h` | 基准共享场景（面板/文本/滚动列表/图像） |
| `frame_bench.cpp` | 帧管线分阶段基准（使用 `GetFrameStats` 埋点） |
| `baseline_probe.cpp` | 改动前基线探针（只用公开 API，可对旧版本手工编译） |

## 构建与运行

```bash
# 外层工程：只构建 ui 库 + demo，不构建测试
cmake -S . -B build && cmake --build build -j

# 测试/基准：独立工程与独立构建目录
cmake -S tests -B build-tests
cmake --build build-tests -j
ctest --test-dir build-tests --output-on-failure

# 窗口回归（需显示服务）
./build-tests/bin/ui_window_regression

# 性能基准
./build-tests/bin/ui_frame_bench                    # 无头模式：各阶段耗时 + 上传/布局/读回/mipmap
./build-tests/bin/ui_frame_bench --window-only      # 窗口模式 present（单独进程）
./build-tests/bin/ui_frame_bench --window-only --window-size 1920x1080
./build-tests/bin/ui_baseline_probe                 # 基线探针在当前代码上的结果
```

输出为 `key=value` 行，便于脚本比较。核心字段：

| 字段 | 含义 |
| --- | --- |
| `runframe_ms` | 一次 `RunFrame()` 总耗时 |
| `buildinput_ms` / `layout_ms` / `dispatch_ms` / `render_ms` | 帧内各阶段耗时 |
| `layout_calls` | 每帧整树布局次数（布局去重后应为 1） |
| `drawcalls` / `quads` / `texturebinds` / `scissor_changes` / `vertices` | 绘制批次数 |
| `render_only_ms` | 只渲染、不读回 |
| `render_capture_ms` / `capture_ms` | 渲染 + 请求帧像素（懒读回）的总耗时 / 读回本身耗时 |
| `setimage_*` / `updateimage_*` | 不同分辨率图像上传的单次耗时 |
| `stream_*_frame_ms` | 逐帧更新图像 + `RunFrame()` 的整帧耗时 |
| `layout_once_ms` | 整棵树一次布局的耗时 |
| `mipmap_1080p_ms` | 1080p 纹理生成 mipmap 的耗时 |

## 测量环境

- Linux + Mesa **llvmpipe**（LLVM 15，软件光栅化），绝对耗时受 CPU 限制；比例与阶段占比可参考。
- 离屏分辨率 1920×1080，无头 EGL；场景见 `tests/bench_scene.h`：8 个带边框面板 + 6 条文本 + 2 个滚动列表（各 20 按钮）+ 1 个 960×540 图像。
- 预热 10 帧、采样 60 帧；下表取 3 次运行的中位数。
- llvmpipe 会吃满 CPU，绝对耗时对机器负载很敏感（高负载下同代码可慢 1.5–2×），前后对比请在同一台机器、相近负载下进行。

## 结果（1920×1080，单位 ms）

| 项目 | 改动前 | 改动后 | 变化 |
| --- | ---: | ---: | ---: |
| `RunFrame` 整帧 | 3.45 | 1.95 | **-43%** |
| 离屏渲染（改动前每帧内含读回） | 3.48 | — | — |
| 离屏渲染（纯绘制，无读回） | — | 1.95 | — |
| 帧读回（`GetFrame*`） | 每帧强制约 1.5 | 懒触发 1.42 | 无需像素时省 ~1.4/帧 |
| `SetImage` 1280×720 | 3.42 | 0.56 | **-84%** |
| `SetImage` 1920×1080 | 8.56 | 1.03 | **-88%** |
| `SetImage` 3840×2160 | 36.6 | 6.56 | **-82%** |
| `UpdateImage` 1280×720 / 1080p / 4K | 不存在 | 0.20 / 0.44 / 2.56 | 新增流式接口 |
| 流式帧 `SetImage`+ `RunFrame` | 6.98 | 2.66 | **-62%** |
| 流式帧 `UpdateImage`+ `RunFrame` | — | 2.58 | — |
| 整树布局一次 | 0.001 | 0.001 | 去重收益可忽略 |
| 每帧布局次数 | 4（RunFrame 1 + RenderOffscreen 1 + hit 2） | 1 | 语义更明确 |
| 1080p mipmap 生成 | 0.98 | 0.98 | 仅在缩小呈现时生成 |
| 组件 scissor 状态切换 | 241 | 129 | **-46%** |

### 优化后单帧分阶段拆解

| 阶段 | 耗时 (ms) | 备注 |
| --- | ---: | --- |
| 事件轮询 + 输入构建 | 0.003 | |
| 布局（1 次/帧） | 0.003 | 整树约 1µs |
| 输入分发 | 0.001 | |
| 离屏渲染 | 1.94 | 102 draw calls / 307 quads / 91 纹理切换 / 129 scissor 切换 / 1842 顶点 |
| 帧读回 + 转换（按需） | 1.42 | `glReadPixels` + OpenCV SIMD 通道交换与翻转 |
| 呈现（llvmpipe，1280×720 窗口） | ~3.3–3.6 | 含 SwapBuffers；软件光栅化下偏慢 |

> 说明：llvmpipe 下渲染本身受填充率限制（307 个 quad 覆盖约 1080p 一层），进一步减少 draw call 的收益有限；真实 GPU 上批次数与状态切换的权重会更高。

## 优化点

1. **修复交互**：`ImageDisplay::SetImage` 不再重置 `m_dragging` / 鼠标捕获；新增 `UpdateImage` 逐帧流式更新（复用纹理、不深拷贝）。
2. **懒读回**：`RenderOffscreen()` 不再无条件 `glReadPixels`；只有 `GetFrame()/GetFrameBGRA()` 需要像素时才读，且用 OpenCV SIMD 做 RGBA→BGRA + 翻转（1080p：1.34ms → 0.65ms），顺便去掉渲染路径上的 PACK 状态保存/恢复。
3. **图像上传**：复用同一 `Texture`；尺寸不变走 `glTexSubImage2D`；去掉 CPU 翻转（改由 `Texture::TopDown()` 让渲染器翻 UV）；1/3 通道输入跳过 alpha 预乘；4 通道单遍预乘；复用转换缓冲；缓存 `GL_MAX_TEXTURE_SIZE`。
4. **绘制路径**：叶子组件不再做空子组件的 scissor push/pop；自身与子组件裁剪相同时只切一次 scissor（241→129 次切换）；scissor 状态不变时跳过切换与批次提交；投影矩阵/程序/VAO/VBO 每帧只绑定一次。
5. **布局**：用布局代数（几何 setter / 子组件增删自动自增）做帧内去重，避免 `hit()` 每次重复整树布局；每帧仍至少完整布局一次以兼容自定义 `OnLayoutSelf`。
6. **呈现**：仅在缩小呈现（`scale < 1`）时生成 mipmap；放大时跳过。
7. **杂项**：`PushViewport` 的 scissor/translate 栈改为移动而非复制；去掉 `CanvasGroup` 中重复的 `Flush`。

## 仍存在的开销（未处理，原因）

- **102 draw calls / 91 纹理切换**：每个文本/按钮标签各有一张纹理，跨组件无法合批。文本图集（atlas）可显著减少，但需要重做文本纹理缓存与 UV 管理，改动较大。
- **读回 1.42ms**：`glReadPixels` + 格式转换基本是 CPU 读回下限；PBO 双缓冲可重叠但增加延迟/复杂度。
- **缩小呈现时 mipmap ~1.0ms/帧**：软件驱动下明显（真实 GPU 很小）。可改成自适应过滤或缓存 mipmap 层级，但会影响缩放质量。
- **窗口模式 `--window` 与无头上下文同进程混用**：当前 Mesa 环境会因 EGL/GLX 冲突报 BadAccess，所以窗口测量请用 `--window-only`。

## 复现“改动前”基线

基线程序只使用公开 API，可对任意历史版本编译（用当前工作区里的探针源码即可）：

```bash
git archive <改动前的提交> | tar -x -C build/baseline-src
cmake -S build/baseline-src -B build/baseline-build
cmake --build build/baseline-build -j --target ui

g++ -std=c++17 -O2 -Ibuild/baseline-src/include -Itests \
    $(pkg-config --cflags glfw3 glesv2 egl freetype2 harfbuzz fontconfig opencv4) \
    tests/baseline_probe.cpp build/baseline-build/lib/libui.a \
    $(pkg-config --libs glfw3 glesv2 egl freetype2 harfbuzz fontconfig opencv4) \
    -o build/baseline_probe
./build/baseline_probe
```

注意：旧版本的 CMakeLists 可能默认构建测试，`--target ui` 只构建静态库即可。