// FrameRateCounter.cpp
#include "common/FrameRateCounter.h"

FrameRateCounter::FrameRateCounter(std::size_t window_size)
    : window_size_(window_size) {}

void FrameRateCounter::tick() {
    using namespace std::chrono;
    auto now = steady_clock::now();
    
    // 首次调用时不计算帧时间
    if (last_frame_time_.time_since_epoch().count() == 0) {
        last_frame_time_ = now;
        return;
    }
    
    // 计算当前帧耗时（单位：秒）
    double frame_time = duration_cast<duration<double>>(now - last_frame_time_).count();
    last_frame_time_ = now;
    // 时钟分辨率内的连续调用不产生有效样本，避免零耗时导致 FPS 除零。
    if (frame_time <= 0.0 || window_size_ == 0) return;
    
    // 更新滑动窗口
    frame_times_.push_back(frame_time);
    time_sum_ += frame_time;
    
    // 移除超过窗口大小的旧数据
    while (frame_times_.size() > window_size_) {
        time_sum_ -= frame_times_.front();
        frame_times_.pop_front();
    }
}

double FrameRateCounter::fps() const {
    if (frame_times_.empty() || time_sum_ <= 0.0) return 0.0;
    return static_cast<double>(frame_times_.size()) / time_sum_;
}

double FrameRateCounter::avg_frame_time() const {
    if (frame_times_.empty() || time_sum_ <= 0.0) return 0.0;
    return time_sum_ / static_cast<double>(frame_times_.size());
}

void FrameRateCounter::reset() {
    frame_times_.clear();
    time_sum_ = 0.0;
    last_frame_time_ = {};
}

std::size_t FrameRateCounter::window_size() const {
    return window_size_;
}

void FrameRateCounter::set_window_size(std::size_t new_size) {
    window_size_ = new_size;
    if (new_size == 0) {
        frame_times_.clear();
        time_sum_ = 0.0;
        return;
    }
    // 移除超出新窗口大小的旧数据
    while (frame_times_.size() > window_size_) {
        time_sum_ -= frame_times_.front();
        frame_times_.pop_front();
    }
}