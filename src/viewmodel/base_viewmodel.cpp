/*
 * Aurora — HiFi music player for CardputerZero
 * SPDX-License-Identifier: MIT
 */

#include "base_viewmodel.h"

#include "audio_engine.h"
#include "logger.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>

namespace viewmodel {
namespace {

int page_to_int(model::AppPage page) {
    return static_cast<int>(page);
}

std::string fmt_time(double sec) {
    if (sec < 0) {
        sec = 0;
    }
    const int total = static_cast<int>(sec);
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", total / 60, total % 60);
    return buf;
}

std::string title_of(const std::string& path) {
    return std::filesystem::path(path).stem().string();
}

} // namespace

BaseViewModel::BaseViewModel()
    : title_subject_(model_.app_title()),
      greeting_subject_(model_.greeting()),
      dark_mode_subject_(model_.dark_mode()),
      current_page_subject_(page_to_int(model_.current_page())),
      quit_requested_subject_(false),
      library_rev_subject_(0),
      selected_subject_(0),
      track_no_subject_("0/0"),
      now_title_subject_("未播放"),
      format_subject_(""),
      badge_subject_(""),
      position_label_subject_("00:00 / 00:00"),
      progress_subject_(0),
      playing_subject_(false),
      volume_label_subject_("音量 100%"),
      status_subject_("曲库为空") {}

BaseViewModel::~BaseViewModel() = default;

lv_subject_t* BaseViewModel::title_subject() { return title_subject_.native(); }
lv_subject_t* BaseViewModel::greeting_subject() { return greeting_subject_.native(); }
lv_subject_t* BaseViewModel::dark_mode_subject() { return dark_mode_subject_.native(); }
lv_subject_t* BaseViewModel::current_page_subject() { return current_page_subject_.native(); }
lv_subject_t* BaseViewModel::quit_requested_subject() { return quit_requested_subject_.native(); }
lv_subject_t* BaseViewModel::library_rev_subject() { return library_rev_subject_.native(); }
lv_subject_t* BaseViewModel::selected_subject() { return selected_subject_.native(); }
lv_subject_t* BaseViewModel::track_no_subject() { return track_no_subject_.native(); }
lv_subject_t* BaseViewModel::now_title_subject() { return now_title_subject_.native(); }
lv_subject_t* BaseViewModel::format_subject() { return format_subject_.native(); }
lv_subject_t* BaseViewModel::badge_subject() { return badge_subject_.native(); }
lv_subject_t* BaseViewModel::position_label_subject() { return position_label_subject_.native(); }
lv_subject_t* BaseViewModel::progress_subject() { return progress_subject_.native(); }
lv_subject_t* BaseViewModel::playing_subject() { return playing_subject_.native(); }
lv_subject_t* BaseViewModel::volume_label_subject() { return volume_label_subject_.native(); }
lv_subject_t* BaseViewModel::status_subject() { return status_subject_.native(); }

bool BaseViewModel::is_dark_mode() const { return model_.dark_mode(); }

void BaseViewModel::set_dark_mode(bool enabled) {
    model_.set_dark_mode(enabled);
    publish_all();
}

void BaseViewModel::toggle_dark_mode() {
    model_.toggle_dark_mode();
    publish_all();
}

model::AppPage BaseViewModel::current_page() const { return model_.current_page(); }

void BaseViewModel::show_library_page() {
    model_.set_current_page(model::AppPage::Library);
    publish_all();
}

void BaseViewModel::show_player_page() {
    if (!service::AudioEngine::instance().has_track()) {
        return; // 没有曲目不进播放页
    }
    model_.set_current_page(model::AppPage::Player);
    publish_all();
}

void BaseViewModel::toggle_page() {
    if (model_.current_page() == model::AppPage::Library) {
        show_player_page();
    } else {
        show_library_page();
    }
}

void BaseViewModel::request_quit() {
    quit_requested_subject_.set(true);
}

// ── 曲库 ──

void BaseViewModel::set_library(std::vector<std::string> paths) {
    library_ = std::move(paths);
    if (library_.empty()) {
        selected_subject_.set(0);
        track_no_subject_.set("0/0");
        status_subject_.set("曲库为空 — 把音乐拷进 ~/Music");
    } else {
        if (selected_subject_.value() >= static_cast<int>(library_.size())) {
            selected_subject_.set(0);
        }
        char buf[24];
        std::snprintf(buf,
                      sizeof(buf),
                      "%d/%d",
                      selected_subject_.value() + 1,
                      static_cast<int>(library_.size()));
        track_no_subject_.set(buf);
        status_subject_.set("Enter 播放 · ↑↓ 选择");
    }
    library_rev_subject_.set(library_rev_subject_.value() + 1);
}

int BaseViewModel::selected() const {
    return selected_subject_.value();
}

void BaseViewModel::set_selected(int index) {
    if (library_.empty()) {
        return;
    }
    if (index < 0) {
        index = static_cast<int>(library_.size()) - 1;
    }
    if (index >= static_cast<int>(library_.size())) {
        index = 0;
    }
    selected_subject_.set(index);
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%d/%d", index + 1, static_cast<int>(library_.size()));
    track_no_subject_.set(buf);
}

void BaseViewModel::select_next() { set_selected(selected() + 1); }
void BaseViewModel::select_prev() { set_selected(selected() - 1); }

// ── 播放控制 ──

void BaseViewModel::load_track(int index, bool autoplay) {
    if (index < 0 || index >= static_cast<int>(library_.size())) {
        return;
    }
    auto& engine = service::AudioEngine::instance();
    std::string error;
    if (!engine.load(library_[index], autoplay, &error)) {
        status_subject_.set(error.empty() ? "播放失败" : error.c_str());
        LOG_WARN("load failed: {} ({})", library_[index], error);
        return;
    }
    pending_index_ = index;
    set_selected(index);
    now_title_subject_.set(title_of(library_[index]).c_str());
    update_track_badges();
    refresh_player_ui();
    status_subject_.set(autoplay ? "播放中" : "已暂停");
}

void BaseViewModel::update_track_badges() {
    auto& engine = service::AudioEngine::instance();
    const auto& track = engine.track();
    char format[64];
    const char* codec = track.info.codec.empty() ? "AUDIO" : track.info.codec.c_str();
    if (track.info.bit_depth > 0 && track.info.sample_rate > 0) {
        std::snprintf(format,
                      sizeof(format),
                      "%s · %dbit · %d.%dkHz · %s",
                      codec,
                      track.info.bit_depth,
                      track.info.sample_rate / 1000,
                      (track.info.sample_rate % 1000) / 100,
                      track.info.channels <= 1 ? "单声道" : "立体声");
    } else {
        std::snprintf(format, sizeof(format), "%s", codec);
    }
    format_subject_.set(format);

    const char* badge = "";
    if (track.info.lossy) {
        badge = "有损源 · 直通解码";
    } else if (track.direct) {
        badge = "★ 位完美直通";
    } else if (track.device_rate > 0 && track.info.sample_rate > track.device_rate) {
        badge = "重采样 → 96kHz";
    } else {
        badge = "直通解码";
    }
    badge_subject_.set(badge);
}

void BaseViewModel::play_selected() {
    if (library_.empty()) {
        status_subject_.set("曲库为空");
        return;
    }
    load_track(selected(), true);
    show_player_page();
}

void BaseViewModel::toggle_play() {
    auto& engine = service::AudioEngine::instance();
    if (!engine.has_track()) {
        play_selected();
        return;
    }
    if (engine.is_playing()) {
        engine.pause();
        status_subject_.set("已暂停");
    } else {
        engine.play();
        status_subject_.set("播放中");
    }
    refresh_player_ui();
}

void BaseViewModel::next_track() {
    if (library_.empty()) {
        return;
    }
    const bool was_playing = service::AudioEngine::instance().is_playing() ||
                             service::AudioEngine::instance().ended();
    load_track((selected() + 1) % static_cast<int>(library_.size()), was_playing);
}

void BaseViewModel::prev_track() {
    if (library_.empty()) {
        return;
    }
    const bool was_playing = service::AudioEngine::instance().is_playing() ||
                             service::AudioEngine::instance().ended();
    int index = selected() - 1;
    if (index < 0) {
        index = static_cast<int>(library_.size()) - 1;
    }
    load_track(index, was_playing);
}

void BaseViewModel::seek_relative(int delta_sec) {
    auto& engine = service::AudioEngine::instance();
    if (!engine.has_track()) {
        return;
    }
    engine.seek(engine.position_sec() + delta_sec);
    refresh_player_ui();
}

void BaseViewModel::volume_up() {
    auto& engine = service::AudioEngine::instance();
    engine.set_volume(engine.volume() + 5);
    char buf[24];
    std::snprintf(buf, sizeof(buf), "音量 %d%%", engine.volume());
    volume_label_subject_.set(buf);
}

void BaseViewModel::volume_down() {
    auto& engine = service::AudioEngine::instance();
    engine.set_volume(engine.volume() - 5);
    char buf[24];
    std::snprintf(buf, sizeof(buf), "音量 %d%%", engine.volume());
    volume_label_subject_.set(buf);
}

void BaseViewModel::refresh_player_ui() {
    auto& engine = service::AudioEngine::instance();
    if (!engine.has_track()) {
        playing_subject_.set(false);
        return;
    }
    playing_subject_.set(engine.is_playing());

    const double pos = engine.position_sec();
    const double dur = engine.duration_sec();
    char label[32];
    std::snprintf(label,
                  sizeof(label),
                  "%s / %s",
                  fmt_time(pos).c_str(),
                  fmt_time(dur).c_str());
    position_label_subject_.set(label);

    int permille = 0;
    if (dur > 0.5) {
        permille = static_cast<int>(pos * 1000.0 / dur);
        if (permille > 1000) {
            permille = 1000;
        }
    }
    progress_subject_.set(permille);

    char vol[24];
    std::snprintf(vol, sizeof(vol), "音量 %d%%", engine.volume());
    volume_label_subject_.set(vol);
}

void BaseViewModel::publish_all() {
    title_subject_.set(model_.app_title());
    greeting_subject_.set(model_.greeting());
    dark_mode_subject_.set(model_.dark_mode());
    current_page_subject_.set(page_to_int(model_.current_page()));
}

} // namespace viewmodel
