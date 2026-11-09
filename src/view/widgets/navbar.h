/*
 * Aurora — HiFi music player for CardputerZero
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "base_widget.h"
#include "base_viewmodel.h"
#include "asset_manager.h"
#include "lvgl.h"

namespace app {
class AssetManager;
}

namespace view::widgets {

// 底部功能键 4-8：曲库 播放 上曲 下曲 主题
class NavBar : public BaseWidgets {
public:
    NavBar(lv_obj_t* parent, viewmodel::BaseViewModel& view_model, app::AssetManager& assets);
    ~NavBar() override;

    void build() override;
    void teardown();

private:
    static void button_clicked_cb(lv_event_t* event);
    static void update_icons_cb(lv_observer_t* observer, lv_subject_t* subject);

    void on_button(size_t index);
    void refresh();

    viewmodel::BaseViewModel& view_model_;
    app::AssetManager& assets_;
    lv_obj_t* buttons_[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};
    lv_obj_t* labels_[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};
    lv_observer_t* page_observer_ = nullptr;
    lv_observer_t* theme_observer_ = nullptr;
};

} // namespace view::widgets
