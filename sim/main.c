/**
 * @file main.c
 * @brief OVS PC 模拟器入口：SDL2 窗口宿主 LVGL UI
 *
 * 与 ESP 端共享 navigator/pages/bridge 与 lv_conf.h；
 * 显示/输入由 LVGL SDL 后端提供（鼠标=触摸、关窗即退出）。
 */

#include <SDL2/SDL.h>

#include "lvgl.h"
#include "logger.h"
#include "nav.h"

#define SIM_HOR_RES  320
#define SIM_VER_RES  240
#define SIM_ZOOM     2.0f   /* 2 倍窗口，方便观看调整 */

int main(void) {
    logger_set_level(LOG_LEVEL_INFO);
    LOGI("[SIM]", "OVS simulator starting (LVGL v%d.%d.%d, SDL2, %dx%d zoom %.0fx)",
         LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH,
         SIM_HOR_RES, SIM_VER_RES, SIM_ZOOM);

    lv_init();

    lv_display_t* disp = lv_sdl_window_create(SIM_HOR_RES, SIM_VER_RES);
    if (!disp) {
        LOGE("[SIM]", "SDL window create failed");
        return 1;
    }
    lv_sdl_window_set_zoom(disp, SIM_ZOOM);
    lv_sdl_mouse_create();   /* 鼠标作为触摸输入，可滑动 tileview */

    nav_init(lv_screen_active());

    LOGI("[SIM]", "running... close window to exit");
    while (1) {
        uint32_t next = lv_timer_handler();
        if (next > 10) {
            next = 10;
        }
        SDL_Delay(next ? next : 5);
    }
    return 0;
}
