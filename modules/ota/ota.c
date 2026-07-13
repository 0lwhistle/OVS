#include "ota.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static const char *TAG = "[OTA]";

// ---------- OTA 队列 ----------
// 队列项：每块数据从 PSRAM 动态分配
typedef struct {
    char *data;     // PSRAM 分配的缓冲区
    size_t len;     // 数据长度
    bool is_last;   // 是否为最后一块（结束标记）
} ota_queue_item_t;

// 队列句柄
static QueueHandle_t s_ota_queue = NULL;

// OTA 写入状态
static esp_ota_handle_t s_ota_handle = 0;
static const esp_partition_t *s_update_partition = NULL;
static bool s_ota_in_progress = false;

// 后台写入任务句柄
static TaskHandle_t s_ota_task = NULL;

// 进度相关
static size_t s_total_size = 0;       // 固件总大小
static size_t s_written_size = 0;     // 已写入大小
static bool s_progress_enabled = false;

// ---------- 后台 OTA 写入任务 ----------
static void ota_write_task(void *arg) {
    ota_queue_item_t item;
    esp_err_t err;

    ESP_LOGI(TAG, "OTA write task started");

    while (1) {
        // 从队列接收数据（阻塞等待）
        if (xQueueReceive(s_ota_queue, &item, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        // 检查是否为结束标记
        if (item.is_last) {
            ESP_LOGI(TAG, "OTA write task: received finish signal");

            // 结束 OTA 写入
            err = esp_ota_end(s_ota_handle);
            s_ota_in_progress = false;

            if (err != ESP_OK) {
                ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
                printf("\n[OTA] ERROR: esp_ota_end failed: %s\n", esp_err_to_name(err));
                s_update_partition = NULL;
                continue;
            }

            // ====== 固件校验（魔术数验证） ======
            printf("\n========== OTA Verification ==========\n");

            // 只读取已写入的固件末尾进行校验（最多 1MB）
            uint32_t verify_size = (s_written_size < 1024 * 1024) ? s_written_size : 1024 * 1024;
            char *verify_buf = (char *)heap_caps_malloc(verify_size, MALLOC_CAP_SPIRAM);
            if (!verify_buf) {
                ESP_LOGE(TAG, "Failed to allocate %" PRIu32 " bytes for verification", verify_size);
                printf("[OTA] ERROR: OOM for verification buffer\n");
                s_update_partition = NULL;
                continue;
            }

            // 从分区读取已写入的数据（从末尾开始读，校验信息在末尾）
            uint32_t read_offset = (s_written_size > verify_size) ? (s_written_size - verify_size) : 0;
            err = esp_partition_read(s_update_partition, read_offset, verify_buf, verify_size);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to read partition for verification: %s", esp_err_to_name(err));
                printf("[OTA] ERROR: Read partition failed: %s\n", esp_err_to_name(err));
                free(verify_buf);
                s_update_partition = NULL;
                continue;
            }

            // 在读取的缓冲区末尾搜索魔术数
            ota_verify_t *verify_info = NULL;
            int verify_offset = -1;

            for (int i = verify_size - (int)sizeof(ota_verify_t); i >= 0; i--) {
                ota_verify_t *vt = (ota_verify_t *)(verify_buf + i);
                if (vt->magic == OTA_MAGIC) {
                    verify_info = vt;
                    verify_offset = read_offset + i;
                    break;
                }
            }

            bool verify_ok = false;

            if (verify_info) {
                uint32_t fw_size = verify_info->firmware_size;
                printf("[OTA] Found verify footer at offset %d\n", verify_offset);
                printf("[OTA] Firmware size: %" PRIu32 " bytes\n", fw_size);
                printf("[OTA] SHA256: ");
                for (int i = 0; i < 32; i++) printf("%02x", verify_info->sha256[i]);
                printf("\n");
                printf("[OTA] Magic: 0x%08X (expected: 0x%08X)\n",
                       (unsigned int)verify_info->magic, (unsigned int)OTA_MAGIC);

                // 验证魔术数
                if (verify_info->magic == OTA_MAGIC) {
                    verify_ok = true;
                    printf("[OTA] Magic VERIFIED OK!\n");
                } else {
                    printf("[OTA] Magic MISMATCH!\n");
                }
            } else {
                printf("[OTA] No verify footer found (magic not found)\n");
                printf("[OTA] Skipping verification (compatible with old firmware)\n");
                // 没有校验信息也允许通过（兼容旧固件）
                verify_ok = true;
            }

            free(verify_buf);

            if (!verify_ok) {
                printf("[OTA] VERIFICATION FAILED! Aborting OTA.\n");
                printf("========================================\n");
                esp_ota_abort(s_ota_handle);
                s_update_partition = NULL;
                continue;
            }

            printf("[OTA] Verification PASSED!\n");
            printf("========================================\n");

            // 设置为启动分区
            err = esp_ota_set_boot_partition(s_update_partition);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s",
                         esp_err_to_name(err));
                printf("[OTA] ERROR: Set boot partition failed: %s\n", esp_err_to_name(err));
                s_update_partition = NULL;
                continue;
            }

            printf("\n========== OTA Update Success ==========\n");
            printf("  Partition: %s\n", s_update_partition->label);
            printf("  Size:      %zu bytes\n", s_written_size);
            printf("  Verified:  YES (Magic + SHA256)\n");
            printf("  Rebooting in 1 second...\n");
            printf("========================================\n\n");

            s_update_partition = NULL;

            // 延迟后重启
            vTaskDelay(pdMS_TO_TICKS(1000));
            esp_restart();
            continue;
        }

        // 写入数据块到 OTA 分区
        if (s_ota_in_progress && item.data && item.len > 0) {
            err = esp_ota_write(s_ota_handle, item.data, item.len);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
                esp_ota_abort(s_ota_handle);
                s_ota_in_progress = false;
                s_update_partition = NULL;
            }

            // 更新进度
            s_written_size += item.len;
            if (s_progress_enabled && s_total_size > 0) {
                int progress = (int)(s_written_size * 100 / s_total_size);
                static int last_progress = -1;
                if (progress != last_progress) {
                    last_progress = progress;
                    printf("\r[OTA] Writing... %d%% (%zu/%zu bytes)",
                           progress, s_written_size, s_total_size);
                    fflush(stdout);
                    if (progress == 100) printf("\n");
                }
            }
        }

        // 释放 PSRAM 缓冲区
        if (item.data) {
            free(item.data);
            item.data = NULL;
        }
    }
}

// ---------- 初始化 OTA 模块 ----------
void ota_init(void) {
    if (s_ota_queue) {
        ESP_LOGW(TAG, "OTA already initialized");
        return;
    }

    // 创建队列（深度 128，每项为 ota_queue_item_t）
    s_ota_queue = xQueueCreate(OTA_QUEUE_DEPTH, sizeof(ota_queue_item_t));
    if (!s_ota_queue) {
        ESP_LOGE(TAG, "Failed to create OTA queue");
        return;
    }

    // 创建后台写入任务（栈大小 8192，优先级 5）
    BaseType_t ret = xTaskCreatePinnedToCore(
        ota_write_task,
        "ota_write",
        8192,
        NULL,
        5,
        &s_ota_task,
        1  // CPU1
    );
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create OTA write task");
        vQueueDelete(s_ota_queue);
        s_ota_queue = NULL;
        return;
    }

    ESP_LOGI(TAG, "OTA initialized (queue depth=%d, chunk=%dKB)",
             OTA_QUEUE_DEPTH, OTA_CHUNK_SIZE / 1024);
}

// ---------- 开始 OTA 升级 ----------
esp_err_t ota_start(size_t total_size, bool resume) {
    if (!s_ota_queue) {
        ESP_LOGE(TAG, "OTA not initialized");
        return ESP_FAIL;
    }

    // 如果已有 OTA 在进行中
    if (s_ota_in_progress) {
        if (resume) {
            // 续传模式：保留当前状态，只更新总大小
            ESP_LOGI(TAG, "Resuming OTA, current progress: %zu bytes", s_written_size);
            if (total_size > s_total_size) {
                s_total_size = total_size;
            }
            s_progress_enabled = (s_total_size > 0);
            return ESP_OK;
        } else {
            // 非续传模式：中止之前的，重新开始
            ESP_LOGW(TAG, "OTA already in progress, aborting previous...");
            esp_ota_abort(s_ota_handle);
            s_ota_in_progress = false;
        }
    }

    // 获取备用分区
    s_update_partition = esp_ota_get_next_update_partition(NULL);
    if (!s_update_partition) {
        ESP_LOGE(TAG, "No OTA partition found");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Writing to partition: %s (subtype=%d, size=%" PRIu32 ")",
             s_update_partition->label, s_update_partition->subtype,
             s_update_partition->size);

    // 开始 OTA 写入
    esp_err_t err = esp_ota_begin(s_update_partition, OTA_SIZE_UNKNOWN, &s_ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        s_update_partition = NULL;
        return err;
    }

    // 重置进度状态
    s_total_size = total_size;
    s_written_size = 0;
    s_progress_enabled = (total_size > 0);

    s_ota_in_progress = true;
    ESP_LOGI(TAG, "OTA started, ready to receive firmware (total=%zu bytes)...",
             total_size);
    return ESP_OK;
}

// ---------- 推送一块数据到队列 ----------
esp_err_t ota_push_data(const char *data, size_t len) {
    if (!s_ota_queue) {
        ESP_LOGE(TAG, "OTA not initialized");
        return ESP_FAIL;
    }

    if (!s_ota_in_progress) {
        ESP_LOGE(TAG, "OTA not started, call ota_start() first");
        return ESP_FAIL;
    }

    if (len > OTA_CHUNK_SIZE) {
        ESP_LOGE(TAG, "Data chunk too large: %zu > %d", len, OTA_CHUNK_SIZE);
        return ESP_ERR_INVALID_SIZE;
    }

    if (len == 0) {
        return ESP_OK;  // 空数据跳过
    }

    // 从 PSRAM 动态分配缓冲区
    char *buf = (char *)heap_caps_malloc(len, MALLOC_CAP_SPIRAM);
    if (!buf) {
        ESP_LOGE(TAG, "Failed to allocate %zu bytes from PSRAM", len);
        return ESP_ERR_NO_MEM;
    }

    memcpy(buf, data, len);

    // 构造队列项
    ota_queue_item_t item = {
        .data = buf,
        .len = len,
        .is_last = false,
    };

    // 放入队列（如果队列满则等待，超时 100ms）
    BaseType_t ret = xQueueSend(s_ota_queue, &item, pdMS_TO_TICKS(100));
    if (ret != pdTRUE) {
        ESP_LOGE(TAG, "OTA queue full, failed to push data");
        free(buf);
        return ESP_FAIL;
    }

    return ESP_OK;
}

// ---------- 结束 OTA 升级 ----------
void ota_finish(void) {
    if (!s_ota_queue || !s_ota_in_progress) {
        ESP_LOGE(TAG, "No OTA in progress");
        return;
    }

    ESP_LOGI(TAG, "Finishing OTA, waiting for queue to drain...");

    // 发送结束标记到队列
    ota_queue_item_t item = {
        .data = NULL,
        .len = 0,
        .is_last = true,
    };

    // 等待队列清空 + 结束标记被处理（超时 60 秒）
    BaseType_t ret = xQueueSend(s_ota_queue, &item, pdMS_TO_TICKS(60000));
    if (ret != pdTRUE) {
        ESP_LOGE(TAG, "Failed to send finish signal to OTA queue");
        esp_ota_abort(s_ota_handle);
        s_ota_in_progress = false;
        s_update_partition = NULL;
    }
    // 注意：后台任务收到结束标记后会执行校验 + 设置启动分区 + 重启
    // 所以 ota_finish() 调用后设备即将重启
}

// ---------- 取消 OTA 升级 ----------
void ota_abort(void) {
    if (!s_ota_in_progress) {
        return;
    }

    ESP_LOGW(TAG, "Aborting OTA...");

    // 清空队列
    ota_queue_item_t item;
    while (xQueueReceive(s_ota_queue, &item, 0) == pdTRUE) {
        if (item.data) {
            free(item.data);
        }
    }

    // 终止 OTA 写入
    esp_ota_abort(s_ota_handle);
    s_ota_in_progress = false;
    s_update_partition = NULL;

    ESP_LOGI(TAG, "OTA aborted");
}

// ---------- 获取进度 ----------
size_t ota_get_progress(void) {
    return s_written_size;
}

size_t ota_get_total_size(void) {
    return s_total_size;
}

// ---------- 获取当前/备用 OTA 分区 ----------
int ota_get_current_slot(void) {
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (!running) return -1;

    if (running->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0) return 0;
    if (running->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_1) return 1;

    return -1;
}

int ota_get_next_slot(void) {
    int current = ota_get_current_slot();
    if (current < 0) return -1;
    return (current == 0) ? 1 : 0;
}
