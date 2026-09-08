/**
 * @file ota.c
 * @brief OTA 固件升级模块实现（esp_ota_ops 隔离层）
 */

#include "ota.h"

#include <string.h>
#include <stdio.h>

#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_app_desc.h"
#include "esp_timer.h"
#include "ota_sha256.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "logger.h"

static const char *TAG = "[OTA]";

/* 启动稳定判定窗口：此后仍未确认则下次重启 bootloader 回退旧槽 */
#define OTA_CONFIRM_DELAY_US  (15ULL * 1000ULL * 1000ULL)

// ---------- 模块状态 ----------
static bool s_initialized = false;
static volatile ota_state_t s_state = OTA_STATE_IDLE;

static esp_ota_handle_t s_handle = 0;
static const esp_partition_t *s_target_partition = NULL;
static ota_sha256_ctx_t s_sha_ctx;
static bool s_sha_started = false;

static size_t s_received = 0;
static size_t s_expected = 0;
static uint8_t s_sha_digest[32];      /* 最近的完整摘要（DONE 态有效） */
static bool s_digest_valid = false;

static char s_version[OTA_VERSION_STR_MAX] = {0};
static char s_project[OTA_PROJECT_STR_MAX] = {0};

static esp_timer_handle_t s_confirm_timer = NULL;

// ---------- 槽位辅助 ----------
static int partition_to_slot(const esp_partition_t *p) {
    if (!p) return -1;
    if (p->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0) return 0;
    if (p->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_1) return 1;
    return -1;
}

// ---------- 回滚确认 ----------
void ota_confirm_running(void) {
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t st;
    if (!running) return;

    if (esp_ota_get_state_partition(running, &st) == ESP_OK &&
        st == ESP_OTA_IMG_PENDING_VERIFY) {
        esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
        if (err == ESP_OK) {
            LOGI(TAG, "Running firmware confirmed valid (slot %d)",
                 partition_to_slot(running));
        } else {
            LOGE(TAG, "mark_app_valid failed: %s", esp_err_to_name(err));
        }
    }
}

static void confirm_timer_cb(void *arg) {
    (void)arg;
    ota_confirm_running();
}

const char *ota_err_to_str(ota_err_t err) {
    switch (err) {
        case OTA_OK:                return "ok";
        case OTA_ERR_NOT_INIT:      return "not initialized";
        case OTA_ERR_INVALID_PARAM: return "invalid param";
        case OTA_ERR_INVALID_STATE: return "invalid state";
        case OTA_ERR_NO_MEMORY:     return "no memory";
        case OTA_ERR_FLASH:         return "flash error";
        case OTA_ERR_IMAGE_INVALID: return "image invalid";
        case OTA_ERR_NOT_FOUND:     return "ota partition not found";
        default:                    return "unknown";
    }
}

const char *ota_state_to_str(ota_state_t state) {
    switch (state) {
        case OTA_STATE_RECEIVING: return "receiving";
        case OTA_STATE_VERIFYING: return "verifying";
        case OTA_STATE_DONE:      return "done";
        case OTA_STATE_FAILED:    return "failed";
        default:                  return "idle";
    }
}

// ---------- 初始化 ----------
ota_err_t ota_init(void) {
    if (s_initialized) return OTA_OK;

    const esp_app_desc_t *app = esp_app_get_description();
    if (app) {
        snprintf(s_version, sizeof(s_version), "%s", app->version);
        snprintf(s_project, sizeof(s_project), "%s", app->project_name);
    }

    // 回滚保护确认定时器：启动 15s 后认为固件稳定
    esp_timer_create_args_t args = {
        .callback = confirm_timer_cb,
        .name = "ota_confirm",
    };
    if (esp_timer_create(&args, &s_confirm_timer) == ESP_OK) {
        esp_timer_start_once(s_confirm_timer, OTA_CONFIRM_DELAY_US);
    } else {
        LOGW(TAG, "confirm timer create failed (rollback confirm manual only)");
    }

    s_initialized = true;
    LOGI(TAG, "OTA initialized (firmware v%s, slot %d, rollback-confirm 15s)",
         s_version,
         partition_to_slot(esp_ota_get_running_partition()));
    return OTA_OK;
}

// ---------- 开始 ----------
ota_err_t ota_begin(size_t expected_size) {
    if (!s_initialized) return OTA_ERR_NOT_INIT;
    if (s_state == OTA_STATE_RECEIVING || s_state == OTA_STATE_VERIFYING) {
        return OTA_ERR_INVALID_STATE;
    }

    s_target_partition = esp_ota_get_next_update_partition(NULL);
    if (!s_target_partition) {
        LOGE(TAG, "No OTA update partition");
        return OTA_ERR_NOT_FOUND;
    }

    esp_err_t err = esp_ota_begin(s_target_partition,
                                  expected_size > 0 ? expected_size
                                                    : OTA_SIZE_UNKNOWN,
                                  &s_handle);
    if (err != ESP_OK) {
        LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        s_target_partition = NULL;
        s_state = OTA_STATE_FAILED;
        return OTA_ERR_FLASH;
    }

    ota_sha256_init(&s_sha_ctx);
    s_sha_started = true;

    s_received = 0;
    s_expected = expected_size;
    s_digest_valid = false;
    s_state = OTA_STATE_RECEIVING;

    LOGI(TAG, "OTA begin -> slot %d (expected %u bytes)",
         partition_to_slot(s_target_partition), (unsigned)expected_size);
    return OTA_OK;
}

// ---------- 写入 ----------
ota_err_t ota_write(const void *data, size_t len) {
    if (!s_initialized) return OTA_ERR_NOT_INIT;
    if (!data || len == 0) return OTA_ERR_INVALID_PARAM;
    if (s_state != OTA_STATE_RECEIVING) return OTA_ERR_INVALID_STATE;

    esp_err_t err = esp_ota_write(s_handle, data, len);
    if (err != ESP_OK) {
        LOGE(TAG, "esp_ota_write failed at %u: %s",
             (unsigned)s_received, esp_err_to_name(err));
        s_state = OTA_STATE_FAILED;
        return OTA_ERR_FLASH;
    }

    ota_sha256_update(&s_sha_ctx, data, len);

    s_received += len;
    return OTA_OK;
}

// ---------- 结束校验 ----------
ota_err_t ota_end(void) {
    if (!s_initialized) return OTA_ERR_NOT_INIT;
    if (s_state != OTA_STATE_RECEIVING) return OTA_ERR_INVALID_STATE;

    s_state = OTA_STATE_VERIFYING;

    uint8_t digest[32];
    if (!s_sha_started) {
        s_state = OTA_STATE_FAILED;
        return OTA_ERR_IMAGE_INVALID;
    }
    ota_sha256_final(&s_sha_ctx, digest);

    esp_err_t err = esp_ota_end(s_handle);
    if (err != ESP_OK) {
        LOGE(TAG, "esp_ota_end failed: %s (%u bytes received)",
             esp_err_to_name(err), (unsigned)s_received);
        s_state = OTA_STATE_FAILED;
        return (err == ESP_ERR_OTA_VALIDATE_FAILED) ? OTA_ERR_IMAGE_INVALID
                                                    : OTA_ERR_FLASH;
    }

    err = esp_ota_set_boot_partition(s_target_partition);
    if (err != ESP_OK) {
        LOGE(TAG, "set_boot_partition failed: %s", esp_err_to_name(err));
        s_state = OTA_STATE_FAILED;
        return OTA_ERR_FLASH;
    }

    // 校验通过才暴露最终 SHA256
    memcpy(s_sha_digest, digest, 32);
    s_digest_valid = true;
    s_state = OTA_STATE_DONE;
    LOGI(TAG, "OTA done: slot %d, %u bytes verified (rollback pending until confirm)",
         partition_to_slot(s_target_partition), (unsigned)s_received);
    return OTA_OK;
}

// ---------- 中止 ----------
ota_err_t ota_abort(void) {
    if (!s_initialized) return OTA_ERR_NOT_INIT;
    if (s_state != OTA_STATE_RECEIVING && s_state != OTA_STATE_FAILED) {
        // DONE 之后不允许 abort（重启已在路上）
        if (s_state != OTA_STATE_DONE) return OTA_ERR_INVALID_STATE;
        return OTA_OK;
    }

    esp_ota_abort(s_handle);
    s_target_partition = NULL;
    s_received = 0;
    s_expected = 0;
    s_sha_started = false;
    s_digest_valid = false;
    s_state = OTA_STATE_IDLE;
    LOGW(TAG, "OTA aborted");
    return OTA_OK;
}

// ---------- 状态 ----------
ota_err_t ota_get_status(ota_status_t *out) {
    if (!out) return OTA_ERR_INVALID_PARAM;
    if (!s_initialized) return OTA_ERR_NOT_INIT;

    memset(out, 0, sizeof(*out));
    out->state = s_state;
    out->received = s_received;
    out->expected = s_expected;
    out->current_slot = partition_to_slot(esp_ota_get_running_partition());
    out->target_slot = partition_to_slot(s_target_partition);

    // DONE 态给出最终摘要；RECEIVING 态调用方可自行按需查询中间值，
    // 这里置零避免误导
    if (s_digest_valid && s_state == OTA_STATE_DONE) {
        memcpy(out->sha256, s_sha_digest, 32);
    }

    esp_ota_img_states_t img_state;
    const esp_partition_t *running = esp_ota_get_running_partition();
    out->pending_verify =
        (running &&
         esp_ota_get_state_partition(running, &img_state) == ESP_OK &&
         img_state == ESP_OTA_IMG_PENDING_VERIFY);

    snprintf(out->version, sizeof(out->version), "%s", s_version);
    snprintf(out->project, sizeof(out->project), "%s", s_project);
    return OTA_OK;
}

// ---------- 重启 ----------
/* 延迟重启必须用 esp_timer（不能阻塞 web 事件循环，
 * 否则 HTTP 200 响应来不及 flush，推送端只见连接重置/挂起） */
static void ota_reboot_timer_cb(void *arg) {
    (void)arg;
    esp_restart();
}

void ota_reboot(void) {
    LOGW(TAG, "Rebooting into new firmware in 500ms...");
    esp_timer_handle_t t = NULL;
    esp_timer_create_args_t args = {
        .callback = ota_reboot_timer_cb,
        .name = "ota_reboot",
    };
    if (esp_timer_create(&args, &t) == ESP_OK) {
        esp_timer_start_once(t, 500 * 1000ULL);
    } else {
        esp_restart();
    }
}
