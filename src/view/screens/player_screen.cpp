/*
 * Aurora — HiFi music player for CardputerZero
 * SPDX-License-Identifier: MIT
 */

#include "player_screen.h"

#include "asset_manager.h"
#include "bindings.h"
#include "theme.h"

namespace screen {
namespace {

constexpr int32_t kPadX = 8;

lv_obj_t* make_label(lv_obj_t* parent,
                     const char* text,
                     const lv_font_t* font,
                     lv_align_t align,
                     int32_t x,
                     int32_t y,
                     viewmodel::BaseViewModel& vm) {
    auto* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    if (font) {
        lv_obj_set_style_text_font(label, font, 0);
    }
    lv_obj_align(label, align, x, y);
    reactive::bind_theme(label, vm.dark_mode_subject(), reactive::ThemeRole::Text);
    return label;
}

} // namespace

PlayerScreen::PlayerScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : BaseScreen(view_model, assets) {
    init();
}

void PlayerScreen::build_content(lv_obj_t* content) {
    auto* title_font = assets().load_standard_font(16);
    auto* badge_font = assets().load_standard_font(12, app::StandardFontWeight::Bold);
    auto* body_font = assets().load_standard_font(12, app::StandardFontWeight::Regular);
    auto* time_font = assets().load_standard_font(11, app::StandardFontWeight::Regular);

    // 1. 标题（长文件名滚动）
    auto* title = lv_label_create(content);
    lv_label_set_text(title, "未播放");
    if (title_font) {
        lv_obj_set_style_text_font(title, title_font, 0);
    }
    lv_label_set_long_mode(title, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(title, LV_PCT(100) - kPadX * 2);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, kPadX, 2);
    reactive::bind_theme(title, view_model().dark_mode_subject(), reactive::ThemeRole::Text);
    lv_label_bind_text(title, view_model().now_title_subject(), nullptr);

    // 2. 格式行：codec/bit/rate/channels
    auto* format = make_label(content, "", body_font, LV_ALIGN_TOP_LEFT, kPadX, 24, view_model());
    lv_obj_set_width(format, LV_PCT(100) - kPadX * 2);
    lv_label_set_long_mode(format, LV_LABEL_LONG_DOT);
    lv_label_bind_text(format, view_model().format_subject(), nullptr);
    lv_obj_set_style_text_opa(format, LV_OPA_80, 0);

    // 3. 位完美徽章（右上角，主色高亮）
    auto* badge = make_label(content, "", badge_font, LV_ALIGN_TOP_RIGHT, -kPadX, 24, view_model());
    lv_label_bind_text(badge, view_model().badge_subject(), nullptr);
    {
        const auto colors = view::palette(view_model().is_dark_mode());
        lv_obj_set_style_text_color(badge, colors.primary, 0);
    }
    lv_subject_add_observer_obj(view_model().dark_mode_subject(),
                                [](lv_observer_t* observer, lv_subject_t*) {
                                    auto* label = lv_observer_get_target_obj(observer);
                                    auto* vm = static_cast<viewmodel::BaseViewModel*>(
                                        lv_observer_get_user_data(observer));
                                    if (label && vm) {
                                        lv_obj_set_style_text_color(
                                            label, view::palette(vm->is_dark_mode()).primary, 0);
                                    }
                                },
                                badge,
                                &view_model());

    // 4. 进度条
    auto* bar = lv_bar_create(content);
    lv_obj_set_size(bar, LV_PCT(100) - kPadX * 2, 8);
    lv_obj_align(bar, LV_ALIGN_TOP_LEFT, kPadX, 52);
    lv_bar_set_range(bar, 0, 1000);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);
    const auto colors = view::palette(view_model().is_dark_mode());
    lv_obj_set_style_bg_color(bar, colors.border, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(bar, colors.primary, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 4, LV_PART_INDICATOR);
    lv_subject_add_observer_obj(view_model().progress_subject(),
                                [](lv_observer_t* observer, lv_subject_t* subject) {
                                    auto* bar_obj = lv_observer_get_target_obj(observer);
                                    if (bar_obj) {
                                        lv_bar_set_value(bar_obj,
                                                         lv_subject_get_int(subject),
                                                         LV_ANIM_OFF);
                                    }
                                },
                                bar,
                                nullptr);
    lv_subject_add_observer_obj(view_model().dark_mode_subject(),
                                [](lv_observer_t* observer, lv_subject_t*) {
                                    auto* bar_obj = lv_observer_get_target_obj(observer);
                                    auto* vm = static_cast<viewmodel::BaseViewModel*>(
                                        lv_observer_get_user_data(observer));
                                    if (bar_obj && vm) {
                                        const auto c = view::palette(vm->is_dark_mode());
                                        lv_obj_set_style_bg_color(bar_obj, c.border, LV_PART_INDICATOR);
                                        lv_obj_set_style_bg_color(bar_obj, c.primary, LV_PART_MAIN);
                                    }
                                },
                                bar,
                                &view_model());

    // 5. 时间行
    auto* time = make_label(content, "00:00 / 00:00", time_font, LV_ALIGN_TOP_LEFT, kPadX, 66, view_model());
    lv_label_bind_text(time, view_model().position_label_subject(), nullptr);

    // 6. 音量（右对齐）
    auto* volume = make_label(content, "音量 100%", time_font, LV_ALIGN_TOP_RIGHT, -kPadX, 66, view_model());
    lv_label_bind_text(volume, view_model().volume_label_subject(), nullptr);
    lv_obj_set_style_text_opa(volume, LV_OPA_70, 0);

    // 7. 底部状态行 + 播放指示
    auto* status = make_label(content, "", body_font, LV_ALIGN_BOTTOM_LEFT, kPadX, -2, view_model());
    lv_obj_set_width(status, LV_PCT(100) - kPadX * 2 - 40);
    lv_label_set_long_mode(status, LV_LABEL_LONG_DOT);
    lv_label_bind_text(status, view_model().status_subject(), nullptr);
    lv_obj_set_style_text_opa(status, LV_OPA_70, 0);

    auto* playing = make_label(content, "■", badge_font, LV_ALIGN_BOTTOM_RIGHT, -kPadX, -2, view_model());
    lv_subject_add_observer_obj(view_model().playing_subject(),
                                [](lv_observer_t* observer, lv_subject_t* subject) {
                                    auto* label = lv_observer_get_target_obj(observer);
                                    if (label) {
                                        lv_label_set_text(label,
                                                          lv_subject_get_int(subject) ? "▶" : "■");
                                    }
                                },
                                playing,
                                nullptr);
    {
        const auto c = view::palette(view_model().is_dark_mode());
        lv_obj_set_style_text_color(playing, c.primary, 0);
    }
}

} // namespace screen
