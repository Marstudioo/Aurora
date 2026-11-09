/*
 * Aurora — HiFi music player for CardputerZero
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"

#include <vector>

namespace screen {

// 曲库页：文件列表 + 选中高亮 + Enter 播放
class LibraryScreen : public BaseScreen {
public:
    LibraryScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);

protected:
    void build_content(lv_obj_t* content) override;

private:
    void rebuild_list();
    void refresh_highlight();

    static void library_rev_observer(lv_observer_t* observer, lv_subject_t* subject);
    static void selected_observer(lv_observer_t* observer, lv_subject_t* subject);

    lv_obj_t* list_container_{nullptr};
    lv_obj_t* empty_label_{nullptr};
    lv_obj_t* track_no_label_{nullptr};
    std::vector<lv_obj_t*> rows_;
};

} // namespace screen
