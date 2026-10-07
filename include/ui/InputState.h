#ifndef UI_INPUT_STATE_H
#define UI_INPUT_STATE_H

#include "Types.h"

#include <vector>
#include <cstdint>

namespace ui {

// ---------------------------------------------------------------------------
// 输入状态快照：由 UIApplication 每帧更新，所有组件共享同一份只读引用。
// 坐标均位于离屏渲染图像（固定分辨率）的像素坐标系内，原点为图像左上角。
// "Pressed/Released/Clicked" 为当帧边沿事件；"Down" 为持续状态。
// ---------------------------------------------------------------------------
struct InputState {
    // ---- 鼠标 ----
    float   mouseX = -1.0f;              // 图像坐标（相对离屏图像左上角）
    float   mouseY = -1.0f;
    bool    mouseInside = false;         // 鼠标是否位于图像范围内
    bool    mouseDown[3]   = {false, false, false};  // 持续按下
    bool    mousePressed[3]  = {false, false, false}; // 本帧按下边沿
    bool    mouseReleased[3] = {false, false, false}; // 本帧抬起边沿
    float   scrollY = 0.0f;              // 本帧滚轮增量（>0 上滚）

    // ---- 键盘 ----
    std::vector<int> keysDown;           // 持续按下的键（Key 枚举）
    std::vector<int> keysPressed;        // 本帧按下边沿
    std::vector<int> keysReleased;       // 本帧抬起边沿

    // ---- 文本输入（用于文本框，UTF-8 字节流）----
    std::vector<uint8_t> charInput;      // 本帧输入的字符（UTF-8 编码）

    // ---- 查询辅助 ----
    bool IsKeyDown(int key) const {
        for (int k : keysDown) if (k == key) return true;
        return false;
    }
    bool IsKeyPressed(int key) const {
        for (int k : keysPressed) if (k == key) return true;
        return false;
    }
    bool IsKeyReleased(int key) const {
        for (int k : keysReleased) if (k == key) return true;
        return false;
    }
    bool IsMouseDown(MouseButton b) const { int i = (int)b; return i >= 0 && i < 3 && mouseDown[i]; }
    bool IsMousePressed(MouseButton b) const { int i = (int)b; return i >= 0 && i < 3 && mousePressed[i]; }
    bool IsMouseReleased(MouseButton b) const { int i = (int)b; return i >= 0 && i < 3 && mouseReleased[i]; }

    // 清空边沿事件（在帧结束/开始时调用）
    void ResetEdges() {
        mousePressed[0] = mousePressed[1] = mousePressed[2] = false;
        mouseReleased[0] = mouseReleased[1] = mouseReleased[2] = false;
        keysPressed.clear();
        keysReleased.clear();
        charInput.clear();
        scrollY = 0.0f;
    }
};

} // namespace ui

#endif // UI_INPUT_STATE_H
