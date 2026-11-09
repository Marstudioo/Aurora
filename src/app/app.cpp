/*
 * Aurora — HiFi music player for CardputerZero
 * SPDX-License-Identifier: MIT
 */

#include "app.h"

#include "application_config.h"
#include "asset_manager.h"
#include "audio_engine.h"
#include "help_popup.h"
#include "logger.h"
#include "linux_input.h"
#include "screenshot_service.h"
#include "screen_manager.h"
#include "base_viewmodel.h"
#include "theme.h"
#include "toast.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#if USE_DESKTOP
#include "desktop_simulator_frame.h"
#endif

#if !USE_DESKTOP
#if APP_USE_DRM
#include "src/drivers/display/drm/lv_linux_drm.h"
#else
#include "src/drivers/display/fb/lv_linux_fbdev.h"
#endif
#endif

#ifndef APP_FRAMEBUFFER_DEVICE
#define APP_FRAMEBUFFER_DEVICE "/dev/fb0"
#endif

#ifndef APP_DRM_DEVICE
#define APP_DRM_DEVICE "/dev/dri/card0"
#endif

#ifndef APP_DRM_CONNECTOR_ID
#define APP_DRM_CONNECTOR_ID -1
#endif

#ifndef APP_CONFIG_FILE
#define APP_CONFIG_FILE "aurora.conf"
#endif

namespace app {
namespace {

void quit_requested_observer(lv_observer_t* observer, lv_subject_t* subject) {
    auto* running = static_cast<bool*>(lv_observer_get_user_data(observer));
    if (running && lv_subject_get_int(subject)) {
        *running = false;
    }
}

struct DarkModePersistence {
    std::string config_path;
    bool last_dark_mode;
};

std::string writable_config_path() {
#if USE_DESKTOP
    return APP_CONFIG_FILE;
#else
    if (const char* xdg_config_home = std::getenv("XDG_CONFIG_HOME")) {
        const std::filesystem::path root(xdg_config_home);
        if (!root.empty() && root.is_absolute()) {
            return (root / "aurora" / "aurora.conf").string();
        }
    }
    if (const char* home = std::getenv("HOME")) {
        const std::filesystem::path root(home);
        if (!root.empty() && root.is_absolute()) {
            return (root / ".config" / "aurora" / "aurora.conf").string();
        }
    }
    return APP_CONFIG_FILE;
#endif
}

void persist_dark_mode_observer(lv_observer_t* observer, lv_subject_t* subject) {
    auto* persistence = static_cast<DarkModePersistence*>(lv_observer_get_user_data(observer));
    if (!persistence) {
        return;
    }
    const bool dark_mode = lv_subject_get_int(subject) != 0;
    if (dark_mode == persistence->last_dark_mode) {
        return;
    }
    persistence->last_dark_mode = dark_mode;

    ApplicationConfig config;
    config.dark_mode = dark_mode;
    std::string error;
    if (save_application_config(persistence->config_path, config, error)) {
        LOG_INFO("saved config: {} (dark_mode={})",
                 persistence->config_path,
                 config.dark_mode ? "yes" : "no");
    } else {
        LOG_WARN("failed to save config {}: {}", persistence->config_path, error);
    }
}

// ── 曲库扫描：~/Music 递归 + 兜底 ~/Downloads ──
std::vector<std::string> scan_library() {
    namespace fs = std::filesystem;
    std::vector<std::string> result;

    const char* home = std::getenv("HOME");
    std::vector<fs::path> roots;
    if (home) {
        roots.emplace_back(fs::path(home) / "Music");
        roots.emplace_back(fs::path(home) / "Downloads");
    }

    auto is_audio = [](const fs::path& p) {
        std::string ext = p.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        return ext == ".flac" || ext == ".mp3" || ext == ".wav" || ext == ".ogg" ||
               ext == ".oga" || ext == ".aif" || ext == ".aiff";
    };

    std::error_code ec;
    for (const auto& root : roots) {
        if (!fs::is_directory(root, ec)) {
            continue;
        }
        for (fs::recursive_directory_iterator it(root, ec), end;
             it != end && !ec;
             it.increment(ec)) {
            if (it->is_regular_file(ec) && is_audio(it->path())) {
                result.push_back(it->path().string());
            }
        }
    }
    std::sort(result.begin(), result.end());
    LOG_INFO("library scanned: {} tracks", result.size());
    return result;
}

class ShortcutController {
public:
    ShortcutController(viewmodel::BaseViewModel& view_model, AssetManager& assets)
        : view_model_(view_model),
          toast_(std::make_unique<view::widgets::Toast>(lv_layer_top(), view_model, assets)),
          help_popup_(std::make_unique<view::widgets::HelpPopup>(lv_layer_top(), view_model, assets)) {
        toast_->build();
        help_popup_->build();
        platform::set_global_key_listener(global_key_listener, this);
        platform::set_key_release_listener(key_release_listener, this);
    }

    ~ShortcutController() {
        platform::clear_key_release_listener(key_release_listener, this);
        platform::clear_global_key_listener(global_key_listener, this);
    }

private:
    bool handle_key(uint32_t key, bool long_pressed) {
        if (key == platform::kKeyPrintScreen) {
            if (!long_pressed) {
                const auto result = platform::screenshot::capture_active_screen();
                if (result.success) {
                    LOG_INFO("screenshot saved: {}", result.path);
                    toast_->show_highlighted("Saved to ",
                                             result.path,
                                             "",
                                             view::widgets::ToastTone::Success,
                                             5200,
                                             true);
                } else {
                    LOG_WARN("screenshot failed: {}", result.error);
                    toast_->show("Screenshot failed: " + result.error,
                                 view::widgets::ToastTone::Error,
                                 3200);
                }
            }
            return true;
        }

        if (key == platform::kKeyHelp) {
            if (!long_pressed) {
                toast_->hide();
                if (help_popup_->visible()) {
                    help_popup_->hide();
                } else {
                    help_popup_->show(view_model_.current_page());
                }
            }
            return true;
        }

        if (help_popup_->visible()) {
            if (key == LV_KEY_ESC && !long_pressed) {
                help_popup_->hide();
            }
            return true;
        }

        // 长按 ESC = 退出（官方语义，任何页面通用）
        if (key == LV_KEY_ESC && long_pressed) {
            toast_->hide();
            view_model_.request_quit();
            return true;
        }

        // 4-8 交给底部导航键分发，不在此消费
        if (key >= '4' && key <= '8') {
            return false;
        }

        if (long_pressed) {
            return false;
        }

        const bool on_player = view_model_.current_page() == model::AppPage::Player;

        switch (key) {
            case LV_KEY_ESC:
                if (on_player) {
                    view_model_.show_library_page();
                } else {
                    // 曲库页短按 ESC：提示长按退出
                    toast_->show_persistent_highlighted("长按 ",
                                                        "ESC",
                                                        " 退出",
                                                        view::widgets::ToastTone::Warning);
                    exit_armed_ = true;
                }
                return true;

            case LV_KEY_ENTER:
                if (on_player) {
                    view_model_.toggle_play();
                } else {
                    view_model_.play_selected();
                }
                return true;

            case LV_KEY_UP:
                if (on_player) {
                    view_model_.volume_up();
                } else {
                    view_model_.select_prev();
                }
                return true;

            case LV_KEY_DOWN:
                if (on_player) {
                    view_model_.volume_down();
                } else {
                    view_model_.select_next();
                }
                return true;

            case LV_KEY_LEFT:
                if (on_player) {
                    view_model_.seek_relative(-10);
                    return true;
                }
                return false;

            case LV_KEY_RIGHT:
                if (on_player) {
                    view_model_.seek_relative(10);
                    return true;
                }
                return false;

            default:
                return false;
        }
    }

    void handle_release(uint32_t key) {
        if (key == LV_KEY_ESC && exit_armed_) {
            toast_->hide();
            exit_armed_ = false;
        }
    }

    static bool global_key_listener(uint32_t key,
                                    const char*,
                                    bool long_pressed,
                                    void* user_data) {
        auto* controller = static_cast<ShortcutController*>(user_data);
        return controller && controller->handle_key(key, long_pressed);
    }

    static void key_release_listener(uint32_t key, const char*, void* user_data) {
        auto* controller = static_cast<ShortcutController*>(user_data);
        if (controller) {
            controller->handle_release(key);
        }
    }

    viewmodel::BaseViewModel& view_model_;
    std::unique_ptr<view::widgets::Toast> toast_;
    std::unique_ptr<view::widgets::HelpPopup> help_popup_;
    bool exit_armed_{false};
};

#if !USE_DESKTOP
lv_display_t* init_device_display() {
#if APP_USE_DRM
    auto* display = lv_linux_drm_create();
    if (!display) {
        return nullptr;
    }
    if (lv_linux_drm_set_file(display, APP_DRM_DEVICE, APP_DRM_CONNECTOR_ID) != LV_RESULT_OK) {
        lv_display_delete(display);
        return nullptr;
    }
    platform::init_key_input(display);
    return display;
#else
    // 官方硬规则：fb 设备严禁硬编码，按启动器注入的环境变量回退
    // APPLAUNCH_LINUX_FBDEV_DEVICE → LV_LINUX_FBDEV_DEVICE → 编译默认(/dev/fb0)
    const char* fb_device = std::getenv("APPLAUNCH_LINUX_FBDEV_DEVICE");
    if (!fb_device || fb_device[0] == '\0') {
        fb_device = std::getenv("LV_LINUX_FBDEV_DEVICE");
    }
    if (!fb_device || fb_device[0] == '\0') {
        fb_device = APP_FRAMEBUFFER_DEVICE;
    }

    auto* display = lv_linux_fbdev_create();
    if (!display) {
        return nullptr;
    }
    if (lv_linux_fbdev_set_file(display, fb_device) != LV_RESULT_OK) {
        lv_display_delete(display);
        return nullptr;
    }
    platform::init_key_input(display);
    return display;
#endif
}
#endif

} // namespace

int Application::run() {
    logger::Logger::init();
    logger::Logger::set_tag("aurora");

    lv_init();

    AssetManager assets;
    for (const auto& root : assets.roots()) {
        LOG_INFO("asset root: {}", root.string());
    }

    viewmodel::BaseViewModel view_model;
    const std::string user_config_path = writable_config_path();
    std::string loaded_config_path = APP_CONFIG_FILE;
    if (user_config_path != APP_CONFIG_FILE) {
        std::error_code filesystem_error;
        if (std::filesystem::is_regular_file(user_config_path, filesystem_error)) {
            loaded_config_path = user_config_path;
        }
    }

    ApplicationConfig config;
    std::string config_error;
    if (load_application_config(loaded_config_path, config, config_error)) {
        view_model.set_dark_mode(config.dark_mode);
        LOG_INFO("loaded config: {} (dark_mode={})",
                 loaded_config_path,
                 config.dark_mode ? "yes" : "no");
    } else {
        LOG_WARN("failed to load config {}: {}; using defaults", loaded_config_path, config_error);
    }

#if USE_DESKTOP
    DesktopSimulatorFrame simulator_frame(assets);
    auto* display = simulator_frame.display();
#else
    auto* display = init_device_display();
#endif
    if (!display) {
        LOG_ERROR("failed to initialize display");
        return 1;
    }

    auto* standard_font = assets.load_standard_font(14);
    view::apply_lvgl_theme(display,
                           view_model.is_dark_mode(),
                           standard_font ? standard_font : LV_FONT_DEFAULT);

#if USE_DESKTOP
    simulator_frame.bind_dark_mode(view_model.dark_mode_subject());
#endif

    // 音频引擎 + 曲库扫描
    std::string audio_error;
    if (!service::AudioEngine::instance().init(&audio_error)) {
        LOG_WARN("audio engine init failed: {}", audio_error);
    }
    view_model.set_library(scan_library());

    ScreenManager screen_manager(view_model, assets);
    screen_manager.start();
    ShortcutController shortcuts(view_model, assets);

    DarkModePersistence dark_mode_persistence{user_config_path, view_model.is_dark_mode()};
    auto* dark_mode_observer = lv_subject_add_observer(view_model.dark_mode_subject(),
                                                       persist_dark_mode_observer,
                                                       &dark_mode_persistence);

    bool running = true;
    auto* quit_observer = lv_subject_add_observer(view_model.quit_requested_subject(),
                                                  quit_requested_observer,
                                                  &running);

    // 播放状态轮询：同步进度/曲终自动下一首
    auto* poll_timer = lv_timer_create(
        [](lv_timer_t* timer) {
            auto* vm = static_cast<viewmodel::BaseViewModel*>(lv_timer_get_user_data(timer));
            if (!vm) {
                return;
            }
            auto& engine = service::AudioEngine::instance();
            if (engine.consume_ended()) {
                vm->next_track();
            }
            if (engine.has_track()) {
                vm->refresh_player_ui();
            }
        },
        250,
        &view_model);

    LOG_INFO("LVGL app started at {}x{}",
             lv_display_get_horizontal_resolution(display),
             lv_display_get_vertical_resolution(display));

    // 自测键注入器：AURORA_TEST_KEYS="down,enter"（逗号分词，longesc=长按ESC）
    struct TestKeyDriver {
        std::vector<std::string> tokens;
        std::size_t index = 0;
        int tick_in_token = 0;
        lv_timer_t* timer = nullptr;

        static uint32_t token_key(const std::string& t) {
            if (t == "enter") return LV_KEY_ENTER;
            if (t == "up") return LV_KEY_UP;
            if (t == "down") return LV_KEY_DOWN;
            if (t == "left") return LV_KEY_LEFT;
            if (t == "right") return LV_KEY_RIGHT;
            if (t == "tab") return '\t';
            if (t == "backspace") return LV_KEY_BACKSPACE;
            if (t == "esc" || t == "longesc") return LV_KEY_ESC;
            return t.empty() ? 0 : static_cast<uint32_t>(t[0]);
        }

        static void tick(lv_timer_t* timer) {
            auto* self = static_cast<TestKeyDriver*>(lv_timer_get_user_data(timer));
            if (!self) return;
            if (self->index >= self->tokens.size()) {
                lv_timer_pause(timer);
                return;
            }
            const std::string& tok = self->tokens[self->index];
            const uint32_t key = token_key(tok);
            const int hold_ticks = (tok == "longesc") ? 4 : 1;
            if (self->tick_in_token == 0) {
                platform::inject_key(key, true);
            }
            self->tick_in_token++;
            if (self->tick_in_token > hold_ticks) {
                platform::inject_key(key, false);
                self->tick_in_token = 0;
                self->index++;
            }
        }
    };
    TestKeyDriver test_keys;
    if (const char* env_keys = std::getenv("AURORA_TEST_KEYS")) {
        std::string list(env_keys);
        std::size_t pos = 0;
        while (pos <= list.size()) {
            const auto comma = list.find(',', pos);
            const auto tok = list.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
            if (!tok.empty()) test_keys.tokens.push_back(tok);
            if (comma == std::string::npos) break;
            pos = comma + 1;
        }
        test_keys.timer = lv_timer_create(TestKeyDriver::tick, 200, &test_keys);
        LOG_INFO("test key driver armed: {} tokens", test_keys.tokens.size());
    }

    while (running
#if USE_DESKTOP
           && simulator_frame.process_events()
#endif
    ) {
        lv_timer_handler();
        lv_delay_ms(5);
    }

    if (poll_timer) {
        lv_timer_delete(poll_timer);
    }
    if (quit_observer) {
        lv_observer_remove(quit_observer);
    }
    if (dark_mode_observer) {
        lv_observer_remove(dark_mode_observer);
    }
    service::AudioEngine::instance().shutdown();

    return 0;
}

} // namespace app
