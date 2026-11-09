/*
 * Aurora — HiFi music player for CardputerZero
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <string>

namespace service {

// 文件头探测结果：给 UI 显示"格式徽章"，并决定解码输出位深。
struct AudioInfo {
    std::string codec;      // "FLAC" "MP3" "WAV" "OGG" "AIFF"（大写扩展语义）
    int sample_rate = 0;    // 源采样率 Hz（探测失败为 0）
    int channels = 0;       // 声道数
    int bit_depth = 0;      // 源位深（MP3 等有损固定 16）
    bool lossy = false;
    bool supported = false; // miniaudio 是否能解码
};

// 尽力探测文件头。返回 false 表示不是可识别的音频文件。
bool probe_audio_info(const std::string& path, AudioInfo* out);

// 源采样率是否被 ES8389 硬件直通（<= 96kHz 视为直通，176.4/192k 需重采样）
bool rate_hw_supported(int sample_rate);

} // namespace service
