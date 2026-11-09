/*
 * Aurora — HiFi music player for CardputerZero
 * SPDX-License-Identifier: MIT
 *
 * 轻量文件头探测：FLAC STREAMINFO / WAV fmt / MP3 帧头 / OGG Vorbis ID 头。
 * 目的：给 UI 展示"格式徽章"（codec · 位深 · 采样率 · 声道），并据此选择
 * 解码输出位深，尽量让解码器输出与硬件原生格式一致（少一次转换）。
 */
#include "audio_info.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

namespace service {
namespace {

constexpr size_t kProbeBytes = 65536; // 足够覆盖 ID3 + 首帧

std::vector<unsigned char> read_head(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        return {};
    }
    std::vector<unsigned char> buf(kProbeBytes);
    f.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(buf.size()));
    buf.resize(static_cast<size_t>(f.gcount()));
    return buf;
}

uint32_t rd_u32be(const unsigned char* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

uint32_t rd_u32le(const unsigned char* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

uint16_t rd_u16le(const unsigned char* p) {
    return uint16_t(p[0]) | (uint16_t(p[1]) << 8);
}

bool probe_flac(const std::vector<unsigned char>& b, AudioInfo* out) {
    // "fLaC" + metadata block header(4) + STREAMINFO(34)
    if (b.size() < 42 || std::memcmp(b.data(), "fLaC", 4) != 0) {
        return false;
    }
    const unsigned char* s = b.data() + 8; // STREAMINFO 起点
    // 采样率 20bit / 声道-1 3bit / 位深-1 5bit / 总样本 36bit（大端位打包）
    out->sample_rate = int((uint32_t(s[10]) << 12) | (uint32_t(s[11]) << 4) | (uint32_t(s[12]) >> 4));
    out->channels = int(((s[12] >> 1) & 0x07) + 1);
    out->bit_depth = int((((s[12] & 0x01) << 4) | (s[13] >> 4)) + 1);
    out->codec = "FLAC";
    out->lossy = false;
    out->supported = true;
    return true;
}

bool probe_wav(const std::vector<unsigned char>& b, AudioInfo* out) {
    if (b.size() < 44 || std::memcmp(b.data(), "RIFF", 4) != 0 || std::memcmp(b.data() + 8, "WAVE", 4) != 0) {
        return false;
    }
    // 顺序扫描 chunk，定位 "fmt "
    size_t pos = 12;
    while (pos + 8 <= b.size()) {
        const unsigned char* c = b.data() + pos;
        const uint32_t size = rd_u32le(c + 4);
        if (std::memcmp(c, "fmt ", 4) == 0 && pos + 8 + 16 <= b.size()) {
            const unsigned char* fmt = c + 8;
            const uint16_t format_tag = rd_u16le(fmt);
            out->channels = rd_u16le(fmt + 2);
            out->sample_rate = int(rd_u32le(fmt + 4));
            out->bit_depth = rd_u16le(fmt + 14);
            out->codec = "WAV";
            // IEEE float(3) / 扩展格式(0xFFFE) 也交由 miniaudio 解码
            out->lossy = (format_tag == 3) ? false : false;
            out->supported = true;
            if (format_tag == 1 || format_tag == 3 || format_tag == 0xFFFE) {
                return true;
            }
            out->supported = false;
            return true;
        }
        pos += 8 + size + (size & 1);
    }
    return false;
}

// MP3 采样率表（按 version index / rate index）
const int kMp3Rates[2][4] = {
    {44100, 48000, 32000, 0}, // MPEG1
    {22050, 24000, 16000, 0}, // MPEG2/2.5（2.5 另有 11025/12000/8000，归并此处近似）
};

bool probe_mp3(const std::vector<unsigned char>& b, AudioInfo* out) {
    size_t pos = 0;
    // 跳过 ID3v2
    if (b.size() > 10 && std::memcmp(b.data(), "ID3", 3) == 0) {
        const size_t size = (size_t(b[6]) << 21) | (size_t(b[7]) << 14) | (size_t(b[8]) << 7) | size_t(b[9]);
        pos = 10 + size;
    }
    // 找帧同步 0xFFEx
    for (; pos + 4 <= b.size(); ++pos) {
        if (b[pos] != 0xFF || (b[pos + 1] & 0xE0) != 0xE0) {
            continue;
        }
        const unsigned char h1 = b[pos + 1];
        const unsigned char h2 = b[pos + 2];
        const int version = (h1 >> 3) & 0x03; // 3=MPEG1 2=MPEG2 0=MPEG2.5 1=reserved
        const int layer = (h1 >> 1) & 0x03;   // 1=Layer3
        const int rate_idx = (h2 >> 2) & 0x03;
        if (version == 1 || layer == 0 || rate_idx == 3) {
            continue;
        }
        int rate = 0;
        if (version == 3) {
            rate = kMp3Rates[0][rate_idx];
        } else {
            rate = kMp3Rates[1][rate_idx];
            if (version == 0) {
                rate /= 2; // MPEG2.5
            }
        }
        if (rate == 0) {
            continue;
        }
        out->codec = "MP3";
        out->sample_rate = rate;
        out->channels = ((h2 >> 6) & 0x03) == 3 ? 1 : 2;
        out->bit_depth = 16;
        out->lossy = true;
        out->supported = true;
        return true;
    }
    return false;
}

bool probe_ogg(const std::vector<unsigned char>& b, AudioInfo* out) {
    if (b.size() < 30 || std::memcmp(b.data(), "OggS", 4) != 0) {
        return false;
    }
    // 搜索 Vorbis ID 头（0x01 "vorbis"）或 Opus（"OpusHead"）
    for (size_t i = 0; i + 16 < b.size(); ++i) {
        if (b[i] == 0x01 && std::memcmp(b.data() + i + 1, "vorbis", 6) == 0) {
            out->codec = "OGG";
            out->channels = b[i + 11];
            out->sample_rate = int(rd_u32le(b.data() + i + 12));
            out->bit_depth = 16;
            out->lossy = true;
            out->supported = true; // stb_vorbis 内置
            return true;
        }
        if (std::memcmp(b.data() + i, "OpusHead", 8) == 0) {
            out->codec = "OPUS";
            out->channels = b[i + 9];
            out->sample_rate = 48000; // Opus 恒定 48k 解码
            out->bit_depth = 16;
            out->lossy = true;
            out->supported = false; // miniaudio 无 Opus 解码器
            return true;
        }
    }
    return false;
}

bool probe_aiff(const std::vector<unsigned char>& b, AudioInfo* out) {
    if (b.size() < 64 || std::memcmp(b.data(), "FORM", 4) != 0 || std::memcmp(b.data() + 8, "AIFF", 4) != 0) {
        return false;
    }
    // COMM chunk: channels(2) numSampleFrames(4) sampleSize(2) sampleRate(10, IEEE754 extended)
    size_t pos = 12;
    while (pos + 8 <= b.size()) {
        const unsigned char* c = b.data() + pos;
        const uint32_t size = (uint32_t(c[4]) << 24) | (uint32_t(c[5]) << 16) | (uint32_t(c[6]) << 8) | c[7];
        if (std::memcmp(c, "COMM", 4) == 0 && pos + 8 + 18 <= b.size()) {
            const unsigned char* comm = c + 8;
            out->channels = (comm[0] << 8) | comm[1];
            out->bit_depth = (comm[8] << 8) | comm[9];
            // 80bit extended 浮点：指数在前 2 字节，尾数 64bit 中高 32 位即有效整数部分
            const uint16_t exp = uint16_t((comm[10] << 8) | comm[11]);
            const uint32_t mant = rd_u32be(comm + 12);
            if (exp > 16383 && mant != 0) {
                // 80bit extended：rate = mant * 2^(exp-16383-31)
                int shift = int(exp) - 16383 - 31;
                double rate = double(mant);
                if (shift >= 0) {
                    rate *= double(uint64_t(1) << shift);
                } else if (shift > -60) {
                    rate /= double(uint64_t(1) << (-shift));
                }
                out->sample_rate = int(rate + 0.5);
            }
            out->codec = "AIFF";
            out->lossy = false;
            out->supported = true;
            return true;
        }
        pos += 8 + size + (size & 1);
    }
    return false;
}

} // namespace

bool probe_audio_info(const std::string& path, AudioInfo* out) {
    if (!out) {
        return false;
    }
    *out = AudioInfo{};
    const auto head = read_head(path);
    if (head.size() < 16) {
        return false;
    }
    if (probe_flac(head, out) || probe_wav(head, out) || probe_ogg(head, out) ||
        probe_aiff(head, out) || probe_mp3(head, out)) {
        return true;
    }
    return false;
}

bool rate_hw_supported(int sample_rate) {
    // 真机实测：ES8389 板级时钟支持 44.1/48/88.2/96k，176.4/192k 被驱动拒绝。
    switch (sample_rate) {
        case 8000:
        case 11025:
        case 12000:
        case 16000:
        case 22050:
        case 24000:
        case 32000:
        case 44100:
        case 48000:
        case 64000:
        case 88200:
        case 96000:
            return true;
        default:
            return false;
    }
}

} // namespace service
