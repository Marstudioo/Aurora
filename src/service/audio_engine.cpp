/*
 * Aurora — HiFi music player for CardputerZero
 * SPDX-License-Identifier: MIT
 */
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include "audio_engine.h"
#include "logger.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

namespace service {
namespace {

constexpr const char* kDeviceName = "hw:CARD=ES8389Audio,DEV=0";
constexpr const char* kDeviceNamePlug = "plughw:CARD=ES8389Audio,DEV=0";
// ES8389 硬件音量 0..255，其中 191 = 0.00dB（实测），255 = +32dB 会削波。
constexpr int kHardwareVolumeMax = 191;

std::string stem_of(const std::string& path) {
    std::filesystem::path p(path);
    return p.stem().string();
}

} // namespace

struct AudioEngine::Impl {
    ma_context context{};
    ma_device device{};
    ma_decoder decoder{};
    bool context_inited = false;
    bool device_inited = false;
    bool decoder_inited = false;
    bool hw_volume_ok = true; // amixer 可用时走硬件音量
    std::string device_name;
    ma_uint32 device_rate = 0;
    ma_format device_format = ma_format_unknown;
    ma_device_id device_id{}; // 必须比 ma_device 活得久（init 期间被引用）
};

AudioEngine& AudioEngine::instance() {
    static AudioEngine engine;
    return engine;
}

bool AudioEngine::init(std::string* error_out) {
    if (impl_ && impl_->context_inited) {
        return true;
    }
    if (!impl_) {
        impl_ = new Impl();
    }
    ma_backend backends[] = {ma_backend_alsa};
    ma_result result = ma_context_init(backends, 1, nullptr, &impl_->context);
    if (result != MA_SUCCESS) {
        if (error_out) {
            *error_out = "ALSA 上下文初始化失败";
        }
        return false;
    }
    impl_->context_inited = true;

    // 同步当前硬件音量到 UI
    const int raw = read_hardware_volume();
    if (raw < 0) {
        impl_->hw_volume_ok = false;
        LOG_WARN("amixer unavailable, volume falls back to software");
    } else {
        volume_.store(raw * 100 / kHardwareVolumeMax);
    }
    LOG_INFO("audio engine initialized (hw_volume={})", impl_->hw_volume_ok);
    return true;
}

void AudioEngine::shutdown() {
    if (!impl_) {
        return;
    }
    close_device();
    if (impl_->decoder_inited) {
        std::lock_guard<std::mutex> lock(decoder_mutex_);
        ma_decoder_uninit(&impl_->decoder);
        impl_->decoder_inited = false;
    }
    if (impl_->context_inited) {
        ma_context_uninit(&impl_->context);
        impl_->context_inited = false;
    }
    delete impl_;
    impl_ = nullptr;
    has_track_.store(false);
}

int AudioEngine::read_hardware_volume() const {
    FILE* pipe = popen("amixer -c1 sget DACL 2>/dev/null", "r");
    if (!pipe) {
        return -1;
    }
    char line[256] = {};
    int raw = -1;
    while (fgets(line, sizeof(line), pipe)) {
        // amixer 输出示例：
        //   Limits: Playback 0 - 255          ← 必须跳过，否则 atoi 得 0
        //   Mono: Playback 19 [7%] [-86.00dB] ← 真正值
        if (std::strstr(line, "Limits")) {
            continue;
        }
        const char* value = std::strstr(line, ": Playback");
        if (value) {
            raw = std::atoi(value + 10); // 跳过 ": Playback"
            break;
        }
    }
    pclose(pipe);
    return raw;
}

void AudioEngine::write_hardware_volume(int raw) const {
    if (raw < 0) {
        raw = 0;
    }
    if (raw > kHardwareVolumeMax) {
        raw = kHardwareVolumeMax; // 封顶 0dB，避免 +32dB 削波
    }
    char cmd[160];
    std::snprintf(cmd,
                  sizeof(cmd),
                  "sh -c 'amixer -q -c1 sset DACL %d >/dev/null 2>&1; "
                  "amixer -q -c1 sset DACR %d >/dev/null 2>&1'",
                  raw,
                  raw);
    FILE* pipe = popen(cmd, "r");
    if (pipe) {
        pclose(pipe);
    }
}

void AudioEngine::set_volume(int percent) {
    if (percent < 0) {
        percent = 0;
    }
    if (percent > 100) {
        percent = 100;
    }
    volume_.store(percent);
    if (impl_ && impl_->hw_volume_ok) {
        write_hardware_volume(percent * kHardwareVolumeMax / 100);
    }
    // 硬件音量不可用时，音量在数据回调中按 volume_ 软件施加
}

bool AudioEngine::open_device(std::string* error_out) {
    const char* names[] = {kDeviceName, kDeviceNamePlug, "default"};
    ma_result result = MA_FAILED_TO_OPEN_BACKEND_DEVICE;
    for (const char* name : names) {
        std::memset(&impl_->device_id, 0, sizeof(impl_->device_id));
        std::snprintf(impl_->device_id.alsa, sizeof(impl_->device_id.alsa), "%s", name);

        ma_device_config config = ma_device_config_init(ma_device_type_playback);
        config.playback.pDeviceID = &impl_->device_id;
        config.playback.format = impl_->decoder.outputFormat;
        config.playback.channels = impl_->decoder.outputChannels;
        config.sampleRate = impl_->decoder.outputSampleRate;
        config.dataCallback = [](ma_device* device, void* output, const void*, ma_uint32 frames) {
            auto* self = static_cast<AudioEngine*>(device->pUserData);
            if (!self || self->paused_.load() || !self->has_track_.load()) {
                return; // 缓冲区默认已预置静音
            }
            std::lock_guard<std::mutex> lock(self->decoder_mutex_);
            auto* impl = self->impl_;
            if (!impl || !impl->decoder_inited) {
                return;
            }
            ma_uint64 read = 0;
            ma_decoder_read_pcm_frames(&impl->decoder, output, frames, &read);
            self->frames_played_.fetch_add(static_cast<long long>(read));
            if (read < frames) {
                self->ended_.store(true);
            }
            // 软件音量兜底（仅在硬件音量不可用时）
            if (!impl->hw_volume_ok && self->volume_.load() < 100) {
                const float gain = self->volume_.load() / 100.0f;
                if (impl->decoder.outputFormat == ma_format_s16) {
                    auto* samples = static_cast<ma_int16*>(output);
                    const ma_uint32 count = frames * impl->decoder.outputChannels;
                    for (ma_uint32 i = 0; i < count; ++i) {
                        samples[i] = static_cast<ma_int16>(samples[i] * gain);
                    }
                } else if (impl->decoder.outputFormat == ma_format_s32) {
                    auto* samples = static_cast<ma_int32*>(output);
                    const ma_uint32 count = frames * impl->decoder.outputChannels;
                    for (ma_uint32 i = 0; i < count; ++i) {
                        samples[i] = static_cast<ma_int32>(samples[i] * gain);
                    }
                }
            }
        };
        config.pUserData = this;
        config.alsa.noMMap = MA_TRUE; // read/write 路径兼容性最好

        result = ma_device_init(&impl_->context, &config, &impl_->device);
        if (result == MA_SUCCESS) {
            impl_->device_name = name;
            impl_->device_rate = impl_->device.sampleRate;
            impl_->device_format = impl_->device.playback.format;
            result = ma_device_start(&impl_->device);
            if (result == MA_SUCCESS) {
                impl_->device_inited = true;
                device_running_.store(true);
                LOG_INFO("audio device opened: {} rate={} format={}",
                         name,
                         impl_->device.sampleRate,
                         static_cast<int>(impl_->device.playback.format));
                return true;
            }
            ma_device_uninit(&impl_->device);
        }
    }
    if (error_out) {
        *error_out = "音频设备打开失败（可能被其他应用占用）";
    }
    return false;
}

void AudioEngine::close_device() {
    if (!impl_ || !impl_->device_inited) {
        return;
    }
    ma_device_stop(&impl_->device);
    device_running_.store(false);
    ma_device_uninit(&impl_->device);
    impl_->device_inited = false;
}

bool AudioEngine::load(const std::string& path, bool auto_play, std::string* error_out) {
    if (!init(error_out)) {
        return false;
    }

    // 1) 探测源格式，决定解码输出参数
    AudioInfo info{};
    const bool probed = probe_audio_info(path, &info);
    if (probed && !info.supported) {
        if (error_out) {
            *error_out = "暂不支持此格式（如 OPUS/M4A/AAC）";
        }
        return false;
    }

    ma_format fmt = ma_format_s16;
    ma_uint32 req_rate = 0; // 0 = 跟随源
    if (probed && info.sample_rate > 0) {
        req_rate = rate_hw_supported(info.sample_rate)
                       ? static_cast<ma_uint32>(info.sample_rate)
                       : 96000; // 板级时钟上限 96kHz，超限源降采样
        fmt = info.bit_depth > 16 ? ma_format_s32 : ma_format_s16;
    }

    // 2) 先停设备，再换解码器（避免回调与 uninit 竞争）
    if (device_running_.load()) {
        ma_device_stop(&impl_->device);
        device_running_.store(false);
    }

    ma_decoder decoder{};
    ma_decoder_config decoder_config = ma_decoder_config_init(fmt, 2, req_rate);
    ma_result result = ma_decoder_init_file(path.c_str(), &decoder_config, &decoder);
    if (result != MA_SUCCESS) {
        // 兜底：跟随源参数再试一次
        decoder_config = ma_decoder_config_init(ma_format_unknown, 0, 0);
        result = ma_decoder_init_file(path.c_str(), &decoder_config, &decoder);
        if (result != MA_SUCCESS) {
            if (error_out) {
                *error_out = "无法解码该文件（格式损坏或不支持）";
            }
            return false;
        }
    }

    // 源采样率超板级上限时，要求解码器重采样到 96k
    if (decoder.outputSampleRate > 0 && !rate_hw_supported(static_cast<int>(decoder.outputSampleRate))) {
        ma_decoder_uninit(&decoder);
        decoder_config = ma_decoder_config_init(fmt, 2, 96000);
        result = ma_decoder_init_file(path.c_str(), &decoder_config, &decoder);
        if (result != MA_SUCCESS) {
            if (error_out) {
                *error_out = "无法解码该文件";
            }
            return false;
        }
    }

    {
        std::lock_guard<std::mutex> lock(decoder_mutex_);
        if (impl_->decoder_inited) {
            ma_decoder_uninit(&impl_->decoder);
        }
        impl_->decoder = decoder;
        impl_->decoder_inited = true;
    }

    ma_uint64 total_frames = 0;
    ma_decoder_get_length_in_pcm_frames(&impl_->decoder, &total_frames);
    const ma_uint32 rate = impl_->decoder.outputSampleRate;

    TrackState state;
    state.path = path;
    state.title = stem_of(path);
    state.info = info;
    state.device_rate = static_cast<int>(rate);
    state.device_bits = impl_->decoder.outputFormat == ma_format_s32 ? 32 : 16;
    state.direct = probed && info.sample_rate == static_cast<int>(rate) && info.channels == 2;
    state.duration_sec = rate > 0 ? static_cast<double>(total_frames) / rate : 0.0;

    // 3) 设备参数变化则重建设备
    const bool params_changed =
        !impl_->device_inited || impl_->device_rate != rate ||
        impl_->device_format != impl_->decoder.outputFormat;
    if (params_changed) {
        close_device();
        if (!open_device(error_out)) {
            std::lock_guard<std::mutex> lock(decoder_mutex_);
            ma_decoder_uninit(&impl_->decoder);
            impl_->decoder_inited = false;
            has_track_.store(false);
            return false;
        }
    }

    track_ = state;
    total_frames_.store(static_cast<long long>(total_frames));
    frames_played_.store(0);
    sample_rate_.store(static_cast<int>(rate));
    ended_.store(false);
    paused_.store(!auto_play);
    has_track_.store(true);

    if (!device_running_.load()) {
        ma_device_start(&impl_->device);
        device_running_.store(true);
    }

    LOG_INFO("loaded: {} rate={} bits={} direct={} dur={:.1f}s device={}",
             path,
             state.device_rate,
             state.device_bits,
             state.direct,
             state.duration_sec,
             impl_->device_name);
    return true;
}

void AudioEngine::play() {
    if (!has_track_.load() || !impl_ || !impl_->device_inited) {
        return;
    }
    if (ended_.load()) {
        seek(0.0);
        ended_.store(false);
    }
    paused_.store(false);
    if (!device_running_.load()) {
        ma_device_start(&impl_->device);
        device_running_.store(true);
    }
}

void AudioEngine::pause() {
    paused_.store(true);
}

void AudioEngine::stop() {
    paused_.store(true);
    seek(0.0);
    ended_.store(false);
}

void AudioEngine::seek(double sec) {
    if (!impl_ || !impl_->decoder_inited) {
        return;
    }
    if (sec < 0) {
        sec = 0;
    }
    const int rate = sample_rate_.load();
    if (rate <= 0) {
        return;
    }
    ma_uint64 target = static_cast<ma_uint64>(sec * rate);
    const long long total = total_frames_.load();
    if (total > 0 && target > static_cast<ma_uint64>(total)) {
        target = static_cast<ma_uint64>(total);
    }
    // 设备先停再 seek，避免回调与 seek 竞争；seek 后恢复
    const bool was_running = device_running_.load();
    if (was_running) {
        ma_device_stop(&impl_->device);
        device_running_.store(false);
    }
    {
        std::lock_guard<std::mutex> lock(decoder_mutex_);
        if (impl_->decoder_inited) {
            ma_decoder_seek_to_pcm_frame(&impl_->decoder, target);
        }
    }
    frames_played_.store(static_cast<long long>(target));
    if (was_running) {
        ma_device_start(&impl_->device);
        device_running_.store(true);
    }
}

bool AudioEngine::consume_ended() {
    return ended_.exchange(false);
}

double AudioEngine::position_sec() const {
    const int rate = sample_rate_.load();
    if (rate <= 0) {
        return 0.0;
    }
    return static_cast<double>(frames_played_.load()) / rate;
}

double AudioEngine::duration_sec() const {
    const int rate = sample_rate_.load();
    if (rate <= 0) {
        return 0.0;
    }
    return static_cast<double>(total_frames_.load()) / rate;
}

} // namespace service
