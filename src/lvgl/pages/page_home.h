/**
 * @file page_home.h
 * @brief 页面层：主页（时钟/温湿度卡/网络卡/未读角标）
 *
 * 零业务头文件：本页只 include lvgl.h、层内头与 bridge.h（层内桥接接口，
 * 仅用 _() 宏取文案）。数据一律由 presenter 经 setter 注入，页面不自取。
 */

#ifndef PAGE_HOME_H
#define PAGE_HOME_H

#include "lvgl.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** screen_create_cb 兼容的页面构建函数 */
lv_obj_t* page_home_create(lv_obj_t* parent);

/* ---- presenter 注入接口（全部 LVGL 线程内调用） ---- */

/** 时间（synced=false 照常显示并灰显） */
void page_home_set_time(uint8_t hour, uint8_t min, bool synced);

/** 温湿度（milli 单位；valid=false 显示"暂无数据"） */
void page_home_set_sensor(bool valid, int32_t temp_m_c, int32_t humi_m_p);

/** 网络（mode: 0=OFF 1=STA 2=AP；字段与 WEB /api/net/info 一致） */
void page_home_set_net(int mode, const char* ssid, const char* ip,
                       int8_t rssi, bool switching);

/** 未读角标（当前占位 0） */
void page_home_set_unread(int count);

/** presenter 注入"设置页"跳转回调（每次 on_enter 重注入；NULL 关闭） */
void page_home_set_nav_cb(void (*on_settings)(lv_event_t* e));

#ifdef __cplusplus
}
#endif

#endif /* PAGE_HOME_H */
