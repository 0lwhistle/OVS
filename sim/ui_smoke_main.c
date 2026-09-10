/**
 * @file ui_smoke_main.c
 * @brief UI headless 冒烟测试（无渲染，验证六层装配/导航/语言热切换/mock 数据）
 *
 * 用法: ./ovs_ui_smoke，退出码 0=通过。
 * 只操作 LVGL 对象树（不调 lv_timer_handler，不建显示设备），
 * 可在任何无显示环境（CI 容器）运行。
 */

#include "lvgl.h"
#include "logger.h"
#include "theme_base.h"
#include "bridge.h"
#include "navigator.h"
#include "presenter_home.h"
#include "presenter_settings.h"
#include "presenter_standby.h"
#include "page_standby.h"

#include <stdio.h>
#include <string.h>

static int s_pass = 0, s_fail = 0;
#define CHECK(cond) do { \
    if (cond) { s_pass++; } \
    else { s_fail++; printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

/* 虚拟显示 flush 桩（本测试永不触发渲染） */
static void smoke_flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    (void)disp;
    (void)area;
    (void)px_map;
    lv_display_flush_ready(disp);
}

int main(void) {
    logger_set_level(LOG_LEVEL_INFO);

    lv_init();

    /* 无头环境：创建虚拟显示设备（320x240，不设置 flush，永不渲染），
     * 使 lv_scr_act() 可用，对象树照常构建 */
    lv_display_t* disp = lv_display_create(320, 240);
    lv_display_set_flush_cb(disp, smoke_flush_cb);

    /* 装配（无显示设备；不渲染只建对象树） */
    theme_base_init();
    ui_bridge_init();
    navigator_init(lv_scr_act());
    presenter_home_init();
    presenter_settings_init();
    presenter_standby_init();
    navigator_push("home");

    /* [1] 导航：home → settings（switch 平级替换） */
    CHECK(strcmp(navigator_current(), "home") == 0);
    CHECK(navigator_switch("settings") == NAV_OK);
    CHECK(strcmp(navigator_current(), "settings") == 0);

    /* [2] 语言热切换：zh-CN → en-US → 重建当前页 */
    CHECK(strcmp(ui_bridge_lang_get(), "zh-CN") == 0);
    CHECK(ui_bridge_lang_set("en-US"));
    CHECK(navigator_reload() == NAV_OK);
    CHECK(strcmp(navigator_current(), "settings") == 0);
    CHECK(strcmp(ui_bridge_tr("SET_VOLUME"), "Volume") == 0);
    CHECK(strcmp(ui_bridge_tr("SET_VOLUME_ZH_CHECK"), "SET_VOLUME_ZH_CHECK") == 0);  /* 未命中回退原文 */

    /* [3] mock 数据（I2/I3 契约字段） */
    ui_bridge_sensor_t sensor;
    CHECK(ui_bridge_sensor_get(&sensor));
    CHECK(sensor.valid && sensor.temp_m_c == 26500 && sensor.humi_m_p == 48200);
    ui_bridge_net_t net;
    CHECK(ui_bridge_net_get(&net));
    CHECK(net.mode == UI_NET_MODE_STA && strcmp(net.ip, "192.168.2.111") == 0 &&
          net.rssi == -52);

    /* [4] mock 音量 */
    ui_bridge_volume_set(66);
    CHECK(ui_bridge_volume_get() == 66);

    /* [5] 待机画面创建函数（I6 cb）可直接构建/销毁 */
    lv_obj_t* standby = page_standby_screen_create(lv_layer_top());
    CHECK(standby != NULL);
    lv_obj_delete(standby);

    /* [6] 回到 home（switch 语义，栈不加深） */
    CHECK(navigator_switch("home") == NAV_OK);
    CHECK(strcmp(navigator_current(), "home") == 0);

    printf("\n==== ui_smoke: %d passed, %d failed ====\n", s_pass, s_fail);
    return s_fail ? 1 : 0;
}
