/*
 * Aurora — HiFi music player for CardputerZero
 * SPDX-License-Identifier: MIT
 */

#pragma once

namespace model {

enum class AppPage {
    Library = 0, // 曲库（文件列表）
    Player  = 1, // 正在播放
};

class BaseModel {
public:
    const char* app_title() const;
    const char* greeting() const;

    bool dark_mode() const;
    void set_dark_mode(bool enabled);
    void toggle_dark_mode();

    AppPage current_page() const;
    void set_current_page(AppPage page);
    void toggle_page();

private:
    bool dark_mode_ = false;
    AppPage current_page_ = AppPage::Player; // 播放页是根：启动即主页
};

} // namespace model
