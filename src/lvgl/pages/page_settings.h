/**
 * @file page_settings.h
 * @brief 页面层：设置页（音量/语言/设备名占位/待机占位）
 *
 * 零业务头文件：文案经 bridge.h 的 _() 宏；音量/语言操作由 presenter
 * 经回调注入执行。
 */

#ifndef PAGE_SETTINGS_H
#define PAGE_SETTINGS_H

#include "lvgl.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** screen_create_cb 兼容的页面构建函数 */
lv_obj_t* page_settings_create(lv_obj_t* parent);

/** presenter 注入操作回调（每次 on_enter 重注入） */
typedef struct {
    void (*on_back)(void* user_data);                     /* 返回键 */
    void (*on_volume)(uint8_t volume, void* user_data);   /* slider 松手/变更 */
    void (*on_language)(const char* lang, void* user_data);/* 语言切换 */
    void* user_data;
} page_settings_ops_t;

void page_settings_set_ops(const page_settings_ops_t* ops);

/** presenter 回填：当前音量（0~100，slider 定位） */
void page_settings_set_volume(uint8_t volume);

/** presenter 回填：当前设备名（占位） */
void page_settings_set_device_name(const char* name);

#ifdef __cplusplus
}
#endif

#endif /* PAGE_SETTINGS_H */
