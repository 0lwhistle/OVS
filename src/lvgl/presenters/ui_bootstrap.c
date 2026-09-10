/**
 * @file ui_bootstrap.c
 * @brief UI 引导实现
 *
 * 装配顺序：主题 → 桥（audio_player 等）→ 事件总线（sim 环境兜底初始化）
 * → 导航器 → presenters（注册页面）→ 待机（I6）→ 压栈首页。
 */

#include "ui_bootstrap.h"
#include "theme_base.h"
#include "bridge.h"
#include "navigator.h"
#include "presenter_home.h"
#include "presenter_settings.h"
#include "presenter_standby.h"
#include "event_bus.h"
#include "logger.h"

static const char* TAG = "[UI_BOOT]";

void ui_bootstrap_run(lv_obj_t* root) {
    static bool s_done = false;
    if (s_done) {
        LOGW(TAG, "Already bootstrapped");
        return;
    }

    theme_base_init();
    ui_bridge_init();

    /* ESP 由 app_init 编排已初始化；PC 模拟器在此兜底（重复 init 安全） */
    event_bus_err_t bus_err = event_bus_init();
    if (bus_err != EVENT_BUS_OK && bus_err != EVENT_BUS_ERR_ALREADY_INIT) {
        LOGW(TAG, "event_bus init failed: %d (UI 事件刷新降级)", bus_err);
    }

    navigator_init(root);
    presenter_home_init();
    presenter_settings_init();
    presenter_standby_init();

    navigator_push("home");
    s_done = true;
    LOGI(TAG, "UI bootstrapped (page=%s)", navigator_current() ? navigator_current() : "?");
}
