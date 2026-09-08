/**
 * @file dtb_ab.c
 * @brief 设备树 A/B 裸分区管理实现（设计见 dtb_ab.h）
 */

#include "dtb_ab.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "esp_partition.h"
#include "esp_ota_ops.h"
#include "nvs_flash.h"
#include "sha256.h"
#include "logger.h"

static const char *TAG = "[DTB_AB]";

/* 槽内格式常量（与 scripts/pack_dtb.py 一致） */
#define DTB_MAGIC       0x49425444U      /* "DTBI" 小端 */
#define DTB_FMT_VERSION 1
#define DTB_HDR_SIZE    44

typedef struct {
    uint32_t magic;
    uint8_t  version;
    uint8_t  rsv[3];
    uint32_t json_len;
    uint8_t  sha[32];
} dtb_hdr_t;

/* NVS 存储 */
#define DTB_NVS_NS      "dtb"
#define DTB_NVS_ACTIVE  "active"
#define DTB_NVS_TRIAL   "trial"
#define DTB_NO_SLOT     0xFF

static const char *s_slot_labels[2] = { "dtb_0", "dtb_1" };

// ---------- NVS 辅助 ----------
static void nvs_get_u8_default(nvs_handle_t h, const char *key,
                               uint8_t def, uint8_t *out) {
    *out = def;
    if (nvs_get_u8(h, key, out) != ESP_OK) {
        *out = def;
    }
}

static int nvs_rw_u8(const char *key, uint8_t def, uint8_t *out,
                     bool write) {
    nvs_handle_t h;
    if (nvs_open(DTB_NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        LOGE(TAG, "nvs_open failed");
        return -1;
    }
    int rc = 0;
    if (write) {
        if (nvs_set_u8(h, key, *out) != ESP_OK || nvs_commit(h) != ESP_OK) {
            LOGE(TAG, "nvs write %s failed", key);
            rc = -1;
        }
    } else {
        nvs_get_u8_default(h, key, def, out);
    }
    nvs_close(h);
    return rc;
}

// ---------- 槽辅助 ----------
static const esp_partition_t *slot_partition(int slot) {
    if (slot < 0 || slot > 1) return NULL;
    return esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                    (esp_partition_subtype_t)0xA0,
                                    s_slot_labels[slot]);
}

/** 解析头部并做基本边界检查 */
static int parse_hdr(const uint8_t *bin, size_t len, size_t *json_len) {
    if (len < DTB_HDR_SIZE) return -1;
    dtb_hdr_t h;
    memcpy(&h, bin, sizeof(h));
    if (h.magic != DTB_MAGIC || h.version != DTB_FMT_VERSION) return -1;
    if (h.json_len == 0 || h.json_len > DTB_AB_MAX_JSON) return -1;
    if (DTB_HDR_SIZE + h.json_len > len) return -1;
    *json_len = h.json_len;
    return 0;
}

/** 读槽并整体校验；成功返回 malloc 的 JSON 文本（'\0' 结尾，调用方 free） */
static uint8_t *slot_read_json(int slot, size_t *out_len) {
    const esp_partition_t *p = slot_partition(slot);
    if (!p) {
        LOGW(TAG, "partition %s not found", s_slot_labels[slot]);
        return NULL;
    }

    dtb_hdr_t h;
    if (esp_partition_read(p, 0, &h, sizeof(h)) != ESP_OK) return NULL;
    if (h.magic != DTB_MAGIC || h.version != DTB_FMT_VERSION) {
        LOGW(TAG, "slot %d: no valid image", slot);
        return NULL;
    }
    if (h.json_len == 0 || h.json_len > DTB_AB_MAX_JSON ||
        DTB_HDR_SIZE + h.json_len > p->size) {
        LOGW(TAG, "slot %d: bad json_len %u", slot, (unsigned)h.json_len);
        return NULL;
    }

    uint8_t *json = malloc(h.json_len + 1);
    if (!json) {
        LOGE(TAG, "oom for %u bytes", (unsigned)h.json_len);
        return NULL;
    }
    if (esp_partition_read(p, DTB_HDR_SIZE, json, h.json_len) != ESP_OK) {
        free(json);
        return NULL;
    }
    json[h.json_len] = '\0';

    /* SHA 只回答"内容是否与写入时一致"（位翻转/半截数据），不做语义校验 */
    sha256_ctx_t ctx;
    uint8_t digest[32];
    sha256_init(&ctx);
    sha256_update(&ctx, json, h.json_len);
    sha256_final(&ctx, digest);
    if (memcmp(digest, h.sha, 32) != 0) {
        LOGW(TAG, "slot %d: sha mismatch", slot);
        free(json);
        return NULL;
    }

    *out_len = h.json_len;
    return json;
}

/** 运行中 app 是否处于待确认窗口 */
static bool running_pending_verify(void) {
    const esp_partition_t *run = esp_ota_get_running_partition();
    esp_ota_img_states_t st;
    return run &&
           esp_ota_get_state_partition(run, &st) == ESP_OK &&
           st == ESP_OTA_IMG_PENDING_VERIFY;
}

// ---------- 公共 API ----------
int dtb_ab_load(uint8_t **json, size_t *len, int *used_slot) {
    if (!json || !len || !used_slot) return -1;
    *json = NULL;
    *used_slot = -1;

    uint8_t active = 0, trial = DTB_NO_SLOT;
    nvs_rw_u8(DTB_NVS_ACTIVE, 0, &active, false);
    nvs_rw_u8(DTB_NVS_TRIAL, DTB_NO_SLOT, &trial, false);
    if (active > 1) active = 0;

    bool pending = running_pending_verify();
    if (!pending && trial != DTB_NO_SLOT) {
        /* 非试运行启动却残留 trial（如回滚后的启动）→ 清理 */
        uint8_t ff = DTB_NO_SLOT;
        nvs_rw_u8(DTB_NVS_TRIAL, DTB_NO_SLOT, &ff, true);
    }

    /* 待确认窗口：新 app 配新树；否则用 active */
    int order[2];
    if (pending && trial <= 1) {
        order[0] = trial;  order[1] = (trial == 0) ? 1 : 0;
        LOGI(TAG, "app pending-verify: trying trial slot %d first", trial);
    } else {
        order[0] = active; order[1] = (active == 0) ? 1 : 0;
    }

    for (int i = 0; i < 2; i++) {
        size_t n = 0;
        uint8_t *j = slot_read_json(order[i], &n);
        if (j) {
            *json = j;
            *len = n;
            *used_slot = order[i];
            LOGI(TAG, "device tree from slot %d (%u bytes)%s",
                 order[i], (unsigned)n, i == 1 ? " [fallback]" : "");
            return 0;
        }
    }
    LOGW(TAG, "no valid dtb slot (active=%u trial=%u pending=%d)",
         active, trial, pending);
    return -1;
}

int dtb_ab_write_inactive(const uint8_t *dtb_bin, size_t len,
                          int *written_slot) {
    if (!dtb_bin || !written_slot || len <= DTB_HDR_SIZE) return -1;
    *written_slot = -1;

    size_t json_len = 0;
    if (parse_hdr(dtb_bin, len, &json_len) != 0) {
        LOGE(TAG, "invalid dtb container (len=%u)", (unsigned)len);
        return -1;
    }

    uint8_t active = 0;
    nvs_rw_u8(DTB_NVS_ACTIVE, 0, &active, false);
    if (active > 1) active = 0;
    int target = (active == 0) ? 1 : 0;

    const esp_partition_t *p = slot_partition(target);
    if (!p) {
        LOGE(TAG, "partition %s not found", s_slot_labels[target]);
        return -1;
    }

    /* 擦除整个槽（64KB，一次到位）再整段写入 */
    if (esp_partition_erase_range(p, 0, p->size) != ESP_OK) {
        LOGE(TAG, "erase %s failed", s_slot_labels[target]);
        return -1;
    }
    if (esp_partition_write(p, 0, dtb_bin, len) != ESP_OK) {
        LOGE(TAG, "write %s failed", s_slot_labels[target]);
        return -1;
    }

    /* 读回校验：JSON 部分分块读出做 SHA 比对头部记录 */
    uint8_t digest[32];
    sha256_ctx_t ctx;
    sha256_init(&ctx);
    const size_t chunk = 1024;
    uint8_t buf[1024];
    size_t off = DTB_HDR_SIZE, remain = json_len;
    while (remain > 0) {
        size_t n = remain < chunk ? remain : chunk;
        if (esp_partition_read(p, off, buf, n) != ESP_OK) {
            LOGE(TAG, "readback failed");
            return -1;
        }
        sha256_update(&ctx, buf, n);
        off += n;
        remain -= n;
    }
    sha256_final(&ctx, digest);
    if (memcmp(digest, dtb_bin + 12, 32) != 0) {
        LOGE(TAG, "readback sha mismatch");
        return -1;
    }

    LOGI(TAG, "dtb written to slot %d (%u bytes json), pending commit",
         target, (unsigned)json_len);
    *written_slot = target;
    return 0;
}

int dtb_ab_commit_trial(int slot) {
    if (slot < 0 || slot > 1) return -1;
    uint8_t s = (uint8_t)slot;
    int rc = nvs_rw_u8(DTB_NVS_TRIAL, DTB_NO_SLOT, &s, true);
    if (rc == 0) {
        LOGI(TAG, "trial slot %d registered (active untouched)", slot);
    }
    return rc;
}

void dtb_ab_confirm_trial(void) {
    uint8_t trial = DTB_NO_SLOT;
    nvs_rw_u8(DTB_NVS_TRIAL, DTB_NO_SLOT, &trial, false);
    if (trial > 1) return;

    uint8_t s = trial;
    if (nvs_rw_u8(DTB_NVS_ACTIVE, 0, &s, true) == 0) {
        uint8_t ff = DTB_NO_SLOT;
        nvs_rw_u8(DTB_NVS_TRIAL, DTB_NO_SLOT, &ff, true);
        LOGI(TAG, "dtb confirmed: active -> slot %d", trial);
    }
}

int dtb_ab_set_active(int slot) {
    if (slot < 0 || slot > 1) return -1;
    uint8_t s = (uint8_t)slot;
    int rc = nvs_rw_u8(DTB_NVS_ACTIVE, 0, &s, true);
    if (rc == 0) {
        uint8_t ff = DTB_NO_SLOT;
        nvs_rw_u8(DTB_NVS_TRIAL, DTB_NO_SLOT, &ff, true);
        LOGI(TAG, "dtb active -> slot %d (takes effect after reboot)", slot);
    }
    return rc;
}

int dtb_ab_active_slot(void) {
    uint8_t active = 0;
    nvs_rw_u8(DTB_NVS_ACTIVE, 0, &active, false);
    return (active <= 1) ? active : 0;
}
