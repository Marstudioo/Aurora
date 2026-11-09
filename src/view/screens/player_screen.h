/*
 * Aurora — HiFi music player for CardputerZero
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"

namespace screen {

// 播放页：标题 + 格式徽章 + 进度条 + 状态行
class PlayerScreen : public BaseScreen {
public:
    PlayerScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);

protected:
    void build_content(lv_obj_t* content) override;
};

} // namespace screen
