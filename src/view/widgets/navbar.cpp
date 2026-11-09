/*
 * Aurora — HiFi music player for CardputerZero
 * SPDX-License-Identifier: MIT
 */
#include "navbar.h"

#include "linux_input.h"
#include "bindings.h"
#include "theme.h"

namespace view::widgets {
namespace {
// 官方底部功能图标坐标：中心 x=49/105/161/217/273，y=153，导航条 30px
constexpr int32_t kCentersX[5] = {49, 105, 161, 217, 273};
constexpr const char* kLabels[5] = {"曲库", "播放", "上曲", "下曲", "主题"};
constexpr const char* kKeys[5] = {"4", "5", "6", "7", "8"};
constexpr int32_t kButtonW = 56;
constexpr int32_t kBarH = 30;
} // namespace

NavBar::NavBar(lv_obj_t* parent, viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : BaseWidgets(parent), view_model_(view_model), assets_(assets) {}

NavBar::~NavBar() {
    teardown();
}

void NavBar::teardown() {
    for (int i = 0; i < 5; i++) {
        if (buttons_[i] && lv_obj_is_valid(buttons_[i])) {
            platform::unregister_nav_button(i, buttons_[i]);
        }
        buttons_[i] = nullptr;
        labels_[i] = nullptr;
    }
    if (page_observer_) {
        lv_observer_remove(page_observer_);
        page_observer_ = nullptr;
    }
    if (theme_observer_) {
        lv_observer_remove(theme_observer_);
        theme_observer_ = nullptr;
    }
}

void NavBar::build() {
    core_obj_ = lv_obj_create(parent_);
    lv_obj_remove_style_all(core_obj_);
    lv_obj_set_size(core_obj_, 320, kBarH);
    lv_obj_align(core_obj_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_clear_flag(core_obj_, LV_OBJ_FLAG_SCROLLABLE);
    reactive::bind_theme(core_obj_, view_model_.dark_mode_subject(), reactive::ThemeRole::Bar);

    auto* label_font = assets_.load_standard_font(12, app::StandardFontWeight::Bold);
    auto* key_font = assets_.load_standard_font(11, app::StandardFontWeight::Regular);

    for (int i = 0; i < 5; i++) {
        auto* btn = lv_obj_create(core_obj_);
        lv_obj_remove_style_all(btn);
        lv_obj_set_size(btn, kButtonW, kBarH);
        lv_obj_set_pos(btn, kCentersX[i] - kButtonW / 2, 0);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(btn, button_clicked_cb, LV_EVENT_CLICKED, this);
        buttons_[i] = btn;

        auto* label = lv_label_create(btn);
        lv_label_set_text(label, kLabels[i]);
        if (label_font) {
            lv_obj_set_style_text_font(label, label_font, 0);
        }
        lv_obj_align(label, LV_ALIGN_CENTER, 0, -5);
        reactive::bind_theme(label, view_model_.dark_mode_subject(), reactive::ThemeRole::Text);
        labels_[i] = label;

        auto* key = lv_label_create(btn);
        lv_label_set_text(key, kKeys[i]);
        if (key_font) {
            lv_obj_set_style_text_font(key, key_font, 0);
        }
        lv_obj_align(key, LV_ALIGN_CENTER, 0, 8);
        reactive::bind_theme(key, view_model_.dark_mode_subject(), reactive::ThemeRole::Text);
        lv_obj_set_style_text_opa(key, LV_OPA_50, 0);

        platform::register_nav_button(i, btn);
    }

    page_observer_ = reactive::observe_obj(core_obj_,
                                           view_model_.current_page_subject(),
                                           update_icons_cb,
                                           this);
    theme_observer_ = reactive::observe_obj(core_obj_,
                                            view_model_.dark_mode_subject(),
                                            update_icons_cb,
                                            this);
    refresh();
}

void NavBar::button_clicked_cb(lv_event_t* event) {
    auto* self = static_cast<NavBar*>(lv_event_get_user_data(event));
    auto* btn = static_cast<lv_obj_t*>(lv_event_get_current_target(event));
    if (!self || !btn) {
        return;
    }
    for (size_t i = 0; i < 5; i++) {
        if (self->buttons_[i] == btn) {
            self->on_button(i);
            return;
        }
    }
}

void NavBar::on_button(size_t index) {
    switch (index) {
        case 0: // 曲库
            view_model_.show_library_page();
            break;
        case 1: // 播放/暂停
            view_model_.toggle_play();
            break;
        case 2: // 上曲
            view_model_.prev_track();
            break;
        case 3: // 下曲
            view_model_.next_track();
            break;
        case 4: // 主题
            view_model_.toggle_dark_mode();
            break;
        default:
            break;
    }
}

void NavBar::update_icons_cb(lv_observer_t* observer, lv_subject_t*) {
    auto* self = static_cast<NavBar*>(lv_observer_get_user_data(observer));
    if (self) {
        self->refresh();
    }
}

void NavBar::refresh() {
    // 高亮规则：曲库页高亮"曲库"，播放页高亮"播放"
    const bool dark = view_model_.is_dark_mode();
    const auto colors = view::palette(dark);
    const bool on_player = view_model_.current_page() == model::AppPage::Player;
    for (int i = 0; i < 5; i++) {
        if (!labels_[i] || !lv_obj_is_valid(labels_[i])) {
            continue;
        }
        const bool active = (i == 0 && !on_player) || (i == 1 && on_player);
        lv_obj_set_style_text_opa(labels_[i], active ? LV_OPA_COVER : LV_OPA_50, 0);
        lv_obj_set_style_text_color(labels_[i],
                                    active ? colors.primary : colors.text,
                                    0);
    }
}

} // namespace view::widgets
