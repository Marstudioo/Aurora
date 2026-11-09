/*
 * Aurora — HiFi music player for CardputerZero
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "subjects.h"
#include "base_model.h"

#include "lvgl.h"

#include <string>
#include <vector>

namespace viewmodel {

class BaseViewModel {
public:
    BaseViewModel();
    ~BaseViewModel();

    BaseViewModel(const BaseViewModel&) = delete;
    BaseViewModel& operator=(const BaseViewModel&) = delete;

    lv_subject_t* title_subject();
    lv_subject_t* greeting_subject();
    lv_subject_t* dark_mode_subject();
    lv_subject_t* current_page_subject();
    lv_subject_t* quit_requested_subject();

    // ── 曲库状态 ──
    lv_subject_t* library_rev_subject();   // 曲库内容版本号（扫描完成后 +1）
    lv_subject_t* selected_subject();      // 当前选中行
    lv_subject_t* track_no_subject();      // "3/27" 文案

    // ── 播放器状态 ──
    lv_subject_t* now_title_subject();     // 正在播放标题
    lv_subject_t* format_subject();        // "FLAC 24bit 96.0kHz 立体声"
    lv_subject_t* badge_subject();         // "位完美直通" / "重采样96k" / "有损"
    lv_subject_t* position_label_subject();// "01:23 / 04:56"
    lv_subject_t* progress_subject();      // 0..1000
    lv_subject_t* playing_subject();       // 1=播放中
    lv_subject_t* volume_label_subject();  // "音量 75%"
    lv_subject_t* status_subject();       // 底部状态行

    bool is_dark_mode() const;
    void set_dark_mode(bool enabled);
    void toggle_dark_mode();

    model::AppPage current_page() const;
    void show_library_page();
    void show_player_page();
    void toggle_page();
    void request_quit();

    // ── 曲库 ──
    void set_library(std::vector<std::string> paths);
    const std::vector<std::string>& library() const { return library_; }
    int selected() const;
    void select_next();
    void select_prev();
    void set_selected(int index);

    // ── 播放控制 ──
    void play_selected();     // 播放选中曲目并切到播放页
    void toggle_play();
    void next_track();
    void prev_track();
    void seek_relative(int delta_sec);
    void volume_up();
    void volume_down();
    void refresh_player_ui(); // 由轮询定时器调用：同步引擎状态到 subjects

private:
    void publish_all();
    void load_track(int index, bool autoplay);
    void update_track_badges();

    model::BaseModel model_;
    reactive::StringSubject<32> title_subject_;
    reactive::StringSubject<32> greeting_subject_;
    reactive::BoolSubject dark_mode_subject_;
    reactive::IntSubject current_page_subject_;
    reactive::BoolSubject quit_requested_subject_;

    reactive::IntSubject library_rev_subject_;
    reactive::IntSubject selected_subject_;
    reactive::StringSubject<16> track_no_subject_;

    reactive::StringSubject<96> now_title_subject_;
    reactive::StringSubject<64> format_subject_;
    reactive::StringSubject<32> badge_subject_;
    reactive::StringSubject<32> position_label_subject_;
    reactive::IntSubject progress_subject_;
    reactive::BoolSubject playing_subject_;
    reactive::StringSubject<24> volume_label_subject_;
    reactive::StringSubject<64> status_subject_;

    std::vector<std::string> library_;
    int pending_index_ = -1; // load_track 用
};

} // namespace viewmodel
