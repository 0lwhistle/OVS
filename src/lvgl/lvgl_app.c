/**
 * @file lvgl_app.c
 * @brief LVGL 应用层入口实现（ESP 端）
 *
 * 骨架阶段（REFACTORING_PLAN Phase 3 前置）：
 *   lv_init → display_port（null 输出冒烟）→ navigator → 演示页 → lvgl_task。
 * 真实 st7789/cst816s 对接见 REFACTORING_PLAN 6.2/6.3。
 */

#include "lvgl_app.h"
#include "lvgl.h"
#include "logger.h"
#include "display_port.h"
#include "nav.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"

static const char* TAG = "[LVGL]";

static SemaphoreHandle_t s_mutex = NULL;
static uint8_t s_brightness = 200;

#define LVGL_TASK_STACK_SIZE  (16 * 1024)   /* REFACTORING_PLAN 双核任务表 */
#define LVGL_TASK_PRIORITY    4
#define LVGL_TASK_CORE        1
#define LVGL_TICK_MS          5

/* ========== 内部函数 ========== */

/* LVGL v9 时基回调：对接 esp_timer（PC 模拟器由 SDL 驱动自装，互不影响） */
static uint32_t lvgl_tick_ms_cb(void) {
    return (uint32_t)(esp_timer_get_time() / 1000LL);
}

/** 冒烟日志：每 5s 报告 flush 帧计数，OTA 后据此确认渲染管线存活 */
static void smoke_timer_cb(lv_timer_t* timer) {
    (void)timer;
    LOGI(TAG, "smoke: flush_count=%u", (unsigned)display_port_get_flush_count());
}

static void lvgl_task(void* arg) {
    (void)arg;
    while (1) {
        uint32_t delay_ms = LVGL_TICK_MS;
        if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            uint32_t next = lv_timer_handler();
            xSemaphoreGive(s_mutex);
            if (next > 33) {
                next = 33;
            }
            delay_ms = next ? next : LVGL_TICK_MS;
        }
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

/* ========== 公共 API ========== */

lvgl_app_err_t lvgl_app_init(void) {
    if (s_mutex) {
        LOGW(TAG, "Already initialized");
        return LVGL_APP_OK;
    }

    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        LOGE(TAG, "Failed to create mutex");
        return LVGL_APP_ERR_INIT;
    }

    lv_init();
    lv_tick_set_cb(lvgl_tick_ms_cb);

    lv_display_t* disp = display_port_init();
    if (!disp) {
        LOGE(TAG, "Display port init failed");
        return LVGL_APP_ERR_INIT;
    }

    nav_init(lv_screen_active());
    lv_timer_create(smoke_timer_cb, 5000, NULL);

    BaseType_t ret = xTaskCreatePinnedToCore(lvgl_task, "lvgl",
                                             LVGL_TASK_STACK_SIZE, NULL,
                                             LVGL_TASK_PRIORITY, NULL,
                                             LVGL_TASK_CORE);
    if (ret != pdPASS) {
        LOGE(TAG, "Failed to create lvgl task");
        return LVGL_APP_ERR_INIT;
    }

    LOGI(TAG, "LVGL runtime started (core%d, %dKB stack, prio%d)",
         LVGL_TASK_CORE, LVGL_TASK_STACK_SIZE / 1024, LVGL_TASK_PRIORITY);
    return LVGL_APP_OK;
}

void lvgl_app_lock(void) {
    if (s_mutex) {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
    }
}

void lvgl_app_unlock(void) {
    if (s_mutex) {
        xSemaphoreGive(s_mutex);
    }
}

void lvgl_app_set_brightness(uint8_t brightness) {
    /* TODO: power_srv 接入 LEDC 背光 PWM（REFACTORING_PLAN 12.3） */
    s_brightness = brightness;
}

uint8_t lvgl_app_get_brightness(void) {
    return s_brightness;
}
