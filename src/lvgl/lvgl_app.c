/**
 * @file lvgl_app.c
 * @brief LVGL 应用层入口实现
 */

#include "lvgl_app.h"
#include "lvgl.h"
#include "logger.h"
#include "dtree.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char* TAG = "[LVGL_APP]";

static bool s_initialized = false;
static SemaphoreHandle_t s_lvgl_mutex = NULL;
static uint8_t s_brightness = 128;

/* ========== 外部函数声明 ========== */
/* navigator */
extern void nav_init(lv_obj_t* screen);

/* pages - 由 navigator 管理 */
extern void page_home_register(void);

/* ========== 公共 API ========== */

lvgl_app_err_t lvgl_app_init(void) {
    if (s_initialized) {
        LOGW(TAG, "Already initialized");
        return LVGL_APP_OK;
    }
    
    LOGI(TAG, "Initializing LVGL application...");
    
    /* 创建互斥锁 */
    s_lvgl_mutex = xSemaphoreCreateMutex();
    if (!s_lvgl_mutex) {
        LOGE(TAG, "Failed to create mutex");
        return LVGL_APP_ERR_INIT;
    }
    
    /* 初始化 LVGL 库 */
    lv_init();
    LOGI(TAG, "LVGL library initialized");
    
    /* TODO: 初始化显示驱动 (从设备树读取引脚) */
    /* TODO: 初始化输入设备 (触摸) */
    
    /* 初始化导航器 */
    nav_init(lv_scr_act());
    
    /* 注册页面 */
    page_home_register();
    /* TODO: 注册其他页面 */
    
    s_initialized = true;
    LOGI(TAG, "LVGL application initialized");
    
    return LVGL_APP_OK;
}

void lvgl_app_handler(uint32_t timeout_ms) {
    if (!s_initialized) return;
    
    lvgl_app_lock();
    lv_timer_handler();
    lvgl_app_unlock();
    
    vTaskDelay(pdMS_TO_TICKS(timeout_ms));
}

void lvgl_app_set_brightness(uint8_t brightness) {
    s_brightness = brightness;
    /* TODO: 设置背光 PWM */
}

uint8_t lvgl_app_get_brightness(void) {
    return s_brightness;
}

void lvgl_app_lock(void) {
    if (s_lvgl_mutex) {
        xSemaphoreTake(s_lvgl_mutex, portMAX_DELAY);
    }
}

void lvgl_app_unlock(void) {
    if (s_lvgl_mutex) {
        xSemaphoreGive(s_lvgl_mutex);
    }
}
