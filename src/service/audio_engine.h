/*
 * Aurora — HiFi music player for CardputerZero
 * SPDX-License-Identifier: MIT
 *
 * 音频引擎：miniaudio 解码 + ALSA 硬件直通。
 * 位完美（bit-perfect）设计：
 *   - 直接打开 hw:CARD=ES8389Audio,DEV=0，绕过 PulseAudio/PipeWire 混音器
 *   - 解码输出格式/采样率与硬件一致时，SoC→DAC 的 I2S 码流与源文件逐位相同
 *   - 音量走 ES8389 硬件音量寄存器（DACL/DACR），不触碰数字码流
 *   - 176.4/192kHz 源（板级时钟上限 96kHz）降采样到 96k，UI 如实标注
 */
#pragma once

#include "audio_info.h"

#include <atomic>
#include <mutex>
#include <string>

namespace service {

struct TrackState {
    std::string path;
    std::string title;      // 文件名（去扩展名）
    AudioInfo info;         // 源格式探测
    int device_rate = 0;    // 实际设备采样率
    int device_bits = 0;    // 实际设备位深（16/32）
    bool direct = false;    // 是否位完美直通（无重采样/无格式转换）
    double duration_sec = 0;
};

class AudioEngine {
public:
    static AudioEngine& instance();

    // 初始化引擎（幂等）。error_out 可为 null。
    bool init(std::string* error_out = nullptr);
    void shutdown();

    // 加载曲目并（auto_play 时）开始播放。返回 false 时 error_out 说明原因。
    bool load(const std::string& path, bool auto_play, std::string* error_out = nullptr);

    void play();
    void pause();
    void stop(); // 停止但保留曲目（回到 0）
    void seek(double sec);
    void set_volume(int percent); // 0..100 → 硬件 0..191(0dB)
    int volume() const { return volume_.load(); }

    bool has_track() const { return has_track_.load(); }
    bool is_playing() const { return has_track_.load() && !paused_.load() && !ended_.load(); }
    bool ended() const { return ended_.load(); }
    bool consume_ended(); // UI 轮询用：读后清除
    double position_sec() const;
    double duration_sec() const;

    const TrackState& track() const { return track_; }

private:
    AudioEngine() = default;

    bool open_device(std::string* error_out);
    void close_device();
    int read_hardware_volume() const;
    void write_hardware_volume(int raw) const;

    mutable std::mutex decoder_mutex_; // 保护 decoder 与 track_
    TrackState track_;

    std::atomic<bool> has_track_{false};
    std::atomic<bool> paused_{false};
    std::atomic<bool> ended_{false};
    std::atomic<bool> device_running_{false};
    std::atomic<int> volume_{100};
    std::atomic<long long> frames_played_{0};
    std::atomic<long long> total_frames_{0};
    std::atomic<int> sample_rate_{44100};

    // miniaudio 上下文/设备（C 结构，定义在 .cpp 内的 Impl 中，避免头文件依赖）
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace service
