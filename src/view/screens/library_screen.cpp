/*
 * Aurora — HiFi music player for CardputerZero
 * SPDX-License-Identifier: MIT
 */

#include "library_screen.h"

#include "asset_manager.h"
#include "bindings.h"
#include "theme.h"

namespace screen {
namespace {

constexpr int32_t kRowHeight = 16;
constexpr int32_t kListTop = 18; // 顶部留给"曲库 3/27"标题行

void apply_row_style(lv_obj_t* row, bool selected, bool dark) {
    const auto colors = view::palette(dark);
    if (selected) {
        lv_obj_set_style_bg_color(row, colors.primary, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_30, 0);
        lv_obj_set_style_text_color(row, colors.primary, 0);
    } else {
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_color(row, colors.text, 0);
    }
}

} // namespace

LibraryScreen::LibraryScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : BaseScreen(view_model, assets) {
    init();
}

void LibraryScreen::build_content(lv_obj_t* content) {
    // 顶部行：左=曲库标题，右=3/27 计数
    auto* header = lv_obj_create(content);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, LV_PCT(100), 16);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    auto* title = lv_label_create(header);
    lv_label_set_text(title, "曲库");
    auto* title_font = assets().load_standard_font(14);
    lv_obj_set_style_text_font(title, title_font ? title_font : &lv_font_montserrat_14, 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 4, 0);
    reactive::bind_theme(title, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

    track_no_label_ = lv_label_create(header);
    lv_label_set_text(track_no_label_, "0/0");
    auto* count_font = assets().load_standard_font(12, app::StandardFontWeight::Regular);
    lv_obj_set_style_text_font(track_no_label_, count_font ? count_font : &lv_font_montserrat_12, 0);
    lv_obj_align(track_no_label_, LV_ALIGN_RIGHT_MID, -4, 0);
    reactive::bind_theme(track_no_label_, view_model().dark_mode_subject(), reactive::ThemeRole::Text);
    lv_label_bind_text(track_no_label_, view_model().track_no_subject(), nullptr);

    // 列表容器（可滚动）
    list_container_ = lv_obj_create(content);
    lv_obj_remove_style_all(list_container_);
    lv_obj_set_size(list_container_, LV_PCT(100), LV_PCT(100) - kListTop);
    lv_obj_align(list_container_, LV_ALIGN_TOP_MID, 0, kListTop);
    lv_obj_set_flex_flow(list_container_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list_container_,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(list_container_, 1, 0);
    lv_obj_set_scrollbar_mode(list_container_, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_scroll_dir(list_container_, LV_DIR_VER);
    reactive::bind_theme(list_container_, view_model().dark_mode_subject(), reactive::ThemeRole::Surface);

    // 空态提示
    empty_label_ = lv_label_create(content);
    lv_label_set_text(empty_label_, "把音乐文件拷入 ~/Music\n(FLAC / MP3 / WAV / OGG)");
    lv_label_set_long_mode(empty_label_, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(empty_label_, 260);
    lv_obj_set_style_text_align(empty_label_, LV_TEXT_ALIGN_CENTER, 0);
    auto* hint_font = assets().load_standard_font(12, app::StandardFontWeight::Regular);
    lv_obj_set_style_text_font(empty_label_, hint_font ? hint_font : &lv_font_montserrat_12, 0);
    lv_obj_align(empty_label_, LV_ALIGN_CENTER, 0, 8);
    reactive::bind_theme(empty_label_, view_model().dark_mode_subject(), reactive::ThemeRole::Text);
    lv_obj_set_style_text_opa(empty_label_, LV_OPA_70, 0);

    // 观察曲库版本号 → 重建列表
    lv_subject_add_observer_obj(view_model().library_rev_subject(),
                                library_rev_observer,
                                content,
                                this);
    lv_subject_add_observer_obj(view_model().selected_subject(),
                                selected_observer,
                                content,
                                this);

    rebuild_list();
    refresh_highlight();
}

void LibraryScreen::rebuild_list() {
    if (!list_container_) {
        return;
    }
    for (auto* row : rows_) {
        if (row && lv_obj_is_valid(row)) {
            lv_obj_delete(row);
        }
    }
    rows_.clear();

    const auto& library = view_model().library();
    if (empty_label_) {
        if (library.empty()) {
            lv_obj_remove_flag(empty_label_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(empty_label_, LV_OBJ_FLAG_HIDDEN);
        }
    }

    auto* row_font = assets().load_standard_font(12, app::StandardFontWeight::Regular);
    const bool dark = view_model().is_dark_mode();

    for (size_t i = 0; i < library.size(); ++i) {
        auto* row = lv_obj_create(list_container_);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, LV_PCT(100), kRowHeight);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(row, 3, 0);
        lv_obj_set_style_pad_hor(row, 6, 0);

        auto* label = lv_label_create(row);
        // 显示相对文件名（带扩展名便于区分格式）
        const std::string& name = library[i];
        const auto slash = name.find_last_of('/');
        lv_label_set_text(label, (slash == std::string::npos ? name : name.substr(slash + 1)).c_str());
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        lv_obj_set_width(label, LV_PCT(100) - 8);
        if (row_font) {
            lv_obj_set_style_text_font(label, row_font, 0);
        }
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);

        lv_obj_t* raw = row;
        lv_subject_add_observer_obj(view_model().dark_mode_subject(),
                                    [](lv_observer_t* observer, lv_subject_t*) {
                                        auto* self = static_cast<LibraryScreen*>(lv_observer_get_user_data(observer));
                                        if (self) {
                                            self->refresh_highlight();
                                        }
                                    },
                                    raw,
                                    this);
        rows_.push_back(row);
    }
}

void LibraryScreen::refresh_highlight() {
    if (!list_container_) {
        return;
    }
    const int selected = view_model().selected();
    const bool dark = view_model().is_dark_mode();
    const auto& library = view_model().library();

    lv_obj_t* selected_row = nullptr;
    for (size_t i = 0; i < rows_.size() && i < library.size(); ++i) {
        if (!rows_[i] || !lv_obj_is_valid(rows_[i])) {
            continue;
        }
        const bool is_sel = (static_cast<int>(i) == selected);
        apply_row_style(rows_[i], is_sel, dark);
        if (is_sel) {
            selected_row = rows_[i];
        }
        // 行内 label 颜色随行
        if (lv_obj_get_child_count(rows_[i]) > 0) {
            auto* label = lv_obj_get_child(rows_[i], 0);
            if (label) {
                const auto colors = view::palette(dark);
                lv_obj_set_style_text_color(label,
                                            is_sel ? colors.primary : colors.text,
                                            0);
                lv_obj_set_style_text_opa(label, is_sel ? LV_OPA_COVER : LV_OPA_80, 0);
            }
        }
    }
    // 滚动到选中行
    if (selected_row) {
        lv_obj_scroll_to_view(selected_row, LV_ANIM_OFF);
    }
}

void LibraryScreen::library_rev_observer(lv_observer_t* observer, lv_subject_t*) {
    auto* self = static_cast<LibraryScreen*>(lv_observer_get_user_data(observer));
    if (self) {
        self->rebuild_list();
        self->refresh_highlight();
    }
}

void LibraryScreen::selected_observer(lv_observer_t* observer, lv_subject_t*) {
    auto* self = static_cast<LibraryScreen*>(lv_observer_get_user_data(observer));
    if (self) {
        self->refresh_highlight();
    }
}

} // namespace screen
