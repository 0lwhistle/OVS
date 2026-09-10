/**
 * @file presenter_settings.c
 * @brief 展示器层：设置页 presenter 实现
 *
 * 音量 slider → ui_bridge_volume_set（批次① audio_player_set_volume）；
 * 语言切换 → ui_bridge_lang_set（I1 i18n_set_language）→ 延迟重建当前页
 * （lv_async_call，避免在控件事件内销毁自身对象）。
 */

#include "presenter_settings.h"
#include "page_settings.h"
#include "bridge.h"
#include "navigator.h"
#include "logger.h"

static const char* TAG = "[PRE_SET]";

/* ==================== 页面操作回调（页面 → bridge） ==================== */

static void on_back(void* user_data) {
    (void)user_data;
    navigator_switch("home");
}

static void on_volume(uint8_t volume, void* user_data) {
    (void)user_data;
    ui_bridge_volume_set(volume);   /* mock：存变量 / 真身：audio_player_set_volume */
}

static void deferred_reload_cb(void* arg) {
    (void)arg;
    navigator_reload();   /* 语言热切换：重建当前页取新文案 */
}

static void on_language(const char* lang, void* user_data) {
    (void)user_data;
    if (ui_bridge_lang_set(lang)) {
        LOGI(TAG, "language switched -> %s, reload page", lang);
        lv_async_call(deferred_reload_cb, NULL);
    } else {
        LOGW(TAG, "unsupported language: %s", lang ? lang : "?");
    }
}

/* ==================== 生命周期 ==================== */

static void on_page_enter(void) {
    static const page_settings_ops_t ops = {
        .on_back = on_back,
        .on_volume = on_volume,
        .on_language = on_language,
        .user_data = NULL,
    };
    page_settings_set_ops(&ops);

    page_settings_set_volume(ui_bridge_volume_get());

    char name[32];
    ui_bridge_device_name_get(name, sizeof(name));
    page_settings_set_device_name(name);
}

/* ==================== 初始化 ==================== */

void presenter_settings_init(void) {
    static const navigator_page_t page = {
        .id = "settings",
        .create = page_settings_create,
        .on_enter = on_page_enter,
        .on_exit = NULL,
    };
    navigator_register(&page);
}
