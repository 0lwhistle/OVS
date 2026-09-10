/**
 * @file audio_player_port_esp.c
 * @brief 音量持久化 ESP 实现（NVS，命名空间 "audio_player"，键 "volume"）
 */

#include "audio_player_port.h"
#include "logger.h"

#include "nvs_flash.h"
#include "nvs.h"

#include <inttypes.h>

static const char* TAG = "[AUDIO_PLAYER]";
#define AP_NVS_NS     "audio_player"
#define AP_NVS_KEY    "volume"

int apl_port_volume_load(uint8_t* out_volume) {
    if (!out_volume) {
        return -1;
    }
    nvs_handle_t h;
    if (nvs_open(AP_NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        return -1;   /* 首次启动无记录属正常 */
    }
    uint8_t vol = 0;
    esp_err_t err = nvs_get_u8(h, AP_NVS_KEY, &vol);
    nvs_close(h);
    if (err != ESP_OK) {
        return -1;
    }
    *out_volume = vol;
    LOGI(TAG, "volume loaded from nvs: %u", vol);
    return 0;
}

int apl_port_volume_save(uint8_t volume) {
    nvs_handle_t h;
    if (nvs_open(AP_NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        return -1;
    }
    esp_err_t err = nvs_set_u8(h, AP_NVS_KEY, volume);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    if (err != ESP_OK) {
        LOGW(TAG, "volume save failed: %s", esp_err_to_name(err));
        return -1;
    }
    LOGI(TAG, "volume saved to nvs: %u", volume);
    return 0;
}
