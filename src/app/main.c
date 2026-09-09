/**
 * @file main.c
 * @brief OVS 应用入口：系统启动编排
 *
 * 启动顺序（依赖关系决定，勿随意调换）：
 *   NVS → event_bus/tasker → SPIFFS → 设备树(dtree_init，可选 A/B 槽)
 *   → 网络/OTA/Web(保护区) → W25Q128 → VFS 挂载(设备树 vfs 节点)
 *
 * VFS 功能测试/压力测试收在 OVS_RUN_APP_TESTS 编译开关内（默认关），
 * 需要时置 1 烧录运行；测试代码主体在 vfs_stress 模块。
 */

#include "tasker.h"
#include "event_bus.h"
#include "logger.h"
#include "dtree.h"
#include "esp_spiffs.h"
#include "w25q128.h"
#include "w25q128_vfs.h"
#include "internal_flash_vfs.h"
#include "ovs_vfs.h"
#include "lvgl_app.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "nvs_flash.h"

static const char* TAG = "[MAIN]";

/* 应用级测试开关：1 = 启动时跑 VFS 功能测试 + 压力测试（约 13 分钟） */
#define OVS_RUN_APP_TESTS  0

/* 网络模式热切换自测：1 = STA 稳定后自动 STA→AP→STA 一个来回（免重启验证） */
#define OVS_NET_HOTSWAP_TEST  0

/* 分区重排过渡开关：W25Q128 分区布局变更后置 1 烧录一次（挂载失败自动格式化），
 * 完成过渡后必须置回 0，避免日后每次启动静默清空已存媒体文件 */
#define OVS_MEDIA_FORMAT_ON_FIRST_BOOT  0

/* ============================================================================
 * ▶▶▶ OTA / 网络 保护区 —— 本块由 OTA 线维护，其他开发线请勿删改 ◀◀◀
 * ============================================================================
 * 作用：提供开发期固件 OTA 迭代通道（build 后 scripts/ota_push.sh 免串口更新）。
 *
 * 其他线协作规则（务必遵守，违反 = 设备失去 OTA 能力，只能串口救援）：
 *   1. OVS_ENABLE_NET 必须保持 1。推送到设备的固件若为 0，
 *      Web/OTA 服务不会启动，之后只能串口烧录恢复；
 *   2. app_main 中的 net_stack_init() 调用勿移除（见下方标记）；
 *   3. 本块的头文件包含、函数实现请勿改动；
 *      调整网络行为请改 net_mgr/web/ota 各自模块，不要在 main.c 里写逻辑；
 *   4. 改动本块前先与 OTA 线确认（docs/development_log_net_ota.md）。
 * ========================================================================== */
#define OVS_ENABLE_NET  1

#if OVS_ENABLE_NET
#include "net_mgr.h"
#include "web.h"
#include "ota.h"

/**
 * @brief 网络 + OTA + Web 初始化（开发期固件 OTA 迭代通道）
 *
 * 启动流程：STA 连接（凭据存 NVS，出厂默认在 wifi.h）→ Web 服务。
 * OTA 固件上传端点: POST /api/ota/firmware（流式，支持 app+设备树容器）；
 * 状态查询: GET /api/ota/status；上传脚本: scripts/ota_push.sh。
 * 升级后 15s 内未确认有效则 bootloader 自动回退旧固件（回滚保护）。
 */
static void net_stack_init(void) {
    ota_init();                       /* 含回滚确认定时器 */

    net_mgr_init(NULL);               /* NULL = NVS/出厂默认配置 */
    net_mgr_start(NET_MODE_STA);      /* 开发期默认 STA；AP 用 NET_MODE_AP */

    web_spiffs_init();
    web_server_start();               /* 内部自建任务 */
}
#endif
/* ======================== OTA/网络 保护区结束 ============================ */

#if OVS_ENABLE_NET && OVS_NET_HOTSWAP_TEST
/* ============================================================ */
/*     网络模式热切换自测（OVS_NET_HOTSWAP_TEST=1 时编译）        */
/*  流程：STA 稳定 → 切 AP(20s) → 切回 STA → 打印 PASS/FAIL      */
/*  关键日志只看 [TEST] 行；全程免重启，期间 Web 一直在跑。        */
/* ============================================================ */
static void net_hotswap_log_status(const char *step) {
    net_status_t st = {0};
    net_mgr_get_status(&st);
    LOGI("[TEST]", "%s: mode=%s state=%s ssid=%s ip=%s",
         step, net_mode_to_str(st.mode), net_state_to_str(st.state),
         st.ssid, st.ip);
}

static void net_hotswap_test_task(void *arg) {
    (void)arg;
    net_mode_t prev = NET_MODE_OFF;

    /* 等 STA 连接稳定 */
    for (int i = 0; i < 30; i++) {
        net_status_t st = {0};
        net_mgr_get_status(&st);
        if (st.state == NET_STATE_CONNECTED) break;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    vTaskDelay(pdMS_TO_TICKS(5000));
    net_hotswap_log_status("baseline(sta)");

    /* 1. STA -> AP */
    net_err_t e1 = net_mgr_switch_mode(NET_MODE_AP, &prev);
    LOGI("[TEST]", "step1 sta->ap rc=%d prev=%s", e1, net_mode_to_str(prev));
    vTaskDelay(pdMS_TO_TICKS(5000));
    net_hotswap_log_status("in-ap");
    vTaskDelay(pdMS_TO_TICKS(15000));   /* AP 保持 20s */

    /* 2. AP -> STA */
    net_err_t e2 = net_mgr_switch_mode(NET_MODE_STA, &prev);
    LOGI("[TEST]", "step2 ap->sta rc=%d prev=%s", e2, net_mode_to_str(prev));

    /* 等重连（最多 25s） */
    bool reconnected = false;
    for (int i = 0; i < 25; i++) {
        net_status_t st = {0};
        net_mgr_get_status(&st);
        if (st.state == NET_STATE_CONNECTED) {
            reconnected = true;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    net_hotswap_log_status("back-to-sta");

    LOGI("[TEST]", "RESULT: %s (rc1=%d rc2=%d)",
         (e1 == NET_OK && e2 == NET_OK && reconnected) ? "PASS" : "FAIL",
         e1, e2);
    LOGI("[TEST]", "hot-switch test DONE");
    vTaskDelete(NULL);
}
#endif

/* ============================================================ */
/*                    VFS 栈与挂载                                */
/* ============================================================ */

/** 注册块设备并按设备树 vfs 节点挂载（无 vfs 节点时用默认布局） */
static void vfs_stack_start(void) {
    vfs_err_t ret = vfs_init();
    if (ret != VFS_OK) {
        LOGE(TAG, "VFS init failed: %d", ret);
        return;
    }

    w25q128_register_vfs();
    internal_flash_register_vfs("littlefs");

    if (!dtree_has_node("vfs")) {
        LOGW(TAG, "Device tree 'vfs' node not found, using default layout");
        vfs_mount_littlefs("/audio", "w25q128", 0, 8 * 1024 * 1024, true);
        vfs_mount_littlefs("/font", "w25q128", 8 * 1024 * 1024, 8 * 1024 * 1024, true);
        vfs_mount_littlefs("/config", "internal", 0, 0, true);
        return;
    }

    int array_size = dtree_array_size("vfs.mounts");
    if (array_size <= 0) {
        LOGW(TAG, "No mounts configured in device tree");
        return;
    }

    int mount_count = 0;
    for (int i = 0; i < array_size; i++) {
        dtree_node_t* item = dtree_array_item("vfs.mounts", i);
        if (!item) {
            LOGW(TAG, "Failed to get mount config at index %d", i);
            continue;
        }

        const char* mount_path = NULL;
        const char* device = NULL;
        int32_t offset = 0;
        int32_t size = 0;
        int32_t format_if_fail = 1;

        dtree_get_string(item, "path", &mount_path);
        dtree_get_string(item, "device", &device);
        dtree_get_int(item, "offset", &offset);
        dtree_get_int(item, "size", &size);
        dtree_get_int(item, "format_if_fail", &format_if_fail);

#if OVS_MEDIA_FORMAT_ON_FIRST_BOOT
        /* 过渡期：外部 Flash 分区重排后首次烧录时格式化重建 */
        if (device && strcmp(device, "w25q128") == 0) {
            format_if_fail = 1;
        }
#endif

        if (!mount_path || !device) {
            LOGW(TAG, "Invalid mount config at index %d", i);
            continue;
        }

        ret = vfs_mount_littlefs(mount_path, device, offset, size, format_if_fail);
        if (ret == VFS_OK) {
            mount_count++;
            LOGI(TAG, "VFS mounted: %s (%s @+%" PRId32 ", %" PRId32 "KB)",
                 mount_path, device, offset, size / 1024);
        } else {
            LOGE(TAG, "VFS mount failed: %s (err=%d)", mount_path, ret);
            LOGW(TAG, "提示：若刚变更过分区布局，置 OVS_MEDIA_FORMAT_ON_FIRST_BOOT=1 烧录一次");
        }
    }
    LOGI(TAG, "VFS mounts ready: %d/%d", mount_count, array_size);
}

#if OVS_RUN_APP_TESTS
/* ============================================================ */
/*           应用级测试（OVS_RUN_APP_TESTS=1 时编译）             */
/* ============================================================ */
#include "vfs_stress.h"
#include "esp_timer.h"

static void test_performance(const char* path) {
    LOGI(TAG, "--- 性能测试: %s ---", path);

    size_t test_size = 4096;
    uint8_t* buf = malloc(test_size);
    if (!buf) {
        LOGE(TAG, "Failed to allocate test buffer");
        return;
    }
    for (int i = 0; i < (int)test_size; i++) {
        buf[i] = i & 0xFF;
    }

    char filepath[64];
    snprintf(filepath, sizeof(filepath), "%s/perf_test.bin", path);

    int64_t start = esp_timer_get_time();
    FILE* f = fopen(filepath, "wb");
    bool write_ok = false;
    if (f) {
        size_t written = fwrite(buf, 1, test_size, f);
        write_ok = (written == test_size) && (fclose(f) == 0);
    }
    int64_t write_ms = esp_timer_get_time() - start;

    start = esp_timer_get_time();
    f = fopen(filepath, "rb");
    bool read_ok = false;
    if (f) {
        size_t rd = fread(buf, 1, test_size, f);
        read_ok = (rd == test_size) && (fclose(f) == 0);
    }
    int64_t read_ms = esp_timer_get_time() - start;

    if (write_ok) {
        LOGI(TAG, "写入 %uKB: %.2f ms (%.2f KB/s)", test_size / 1024,
             write_ms / 1000.0f, (test_size / 1024.0f) / (write_ms / 1000000.0f));
    } else {
        LOGW(TAG, "写入 %uKB 失败", test_size / 1024);
    }
    if (read_ok) {
        LOGI(TAG, "读取 %uKB: %.2f ms (%.2f KB/s)", test_size / 1024,
             read_ms / 1000.0f, (test_size / 1024.0f) / (read_ms / 1000000.0f));
    } else {
        LOGW(TAG, "读取 %uKB 失败", test_size / 1024);
    }

    unlink(filepath);
    free(buf);
}

static void test_vfs(void) {
    LOGI(TAG, "=== VFS 功能测试 ===");
    vfs_print_status();

    LOGI(TAG, "--- 文件读写 ---");
    FILE* f = fopen("/audio/test.txt", "w");
    if (f) {
        bool ok = (fprintf(f, "Hello VFS! This is a test file on W25Q128.") > 0);
        ok = (fclose(f) == 0) && ok;
        LOGI(TAG, "%s 写入 /audio/test.txt", ok ? "✅" : "❌");
    }
    f = fopen("/audio/test.txt", "r");
    if (f) {
        char buf[64] = {0};
        size_t rd = fread(buf, 1, sizeof(buf) - 1, f);
        bool ok = (rd > 0 && !ferror(f)) && (fclose(f) == 0);
        LOGI(TAG, "%s 读取 /audio/test.txt: \"%s\"", ok ? "✅" : "❌", buf);
    }

    LOGI(TAG, "--- 路径匹配 ---");
    LOGI(TAG, "/audio/test.bin  -> %s", vfs_is_mounted("/audio/test.bin") ? "已挂载" : "未挂载");
    LOGI(TAG, "/font/hzk16.bin  -> %s", vfs_is_mounted("/font/hzk16.bin") ? "已挂载" : "未挂载");
    LOGI(TAG, "/config/settings.json -> %s", vfs_is_mounted("/config/settings.json") ? "已挂载" : "未挂载");
    LOGI(TAG, "/other/file.txt  -> %s", vfs_is_mounted("/other/file.txt") ? "已挂载" : "未挂载");

    test_performance("/audio");

    vfs_mount_info_t info;
    if (vfs_get_mount_info_by_path("/audio/test.bin", &info) == VFS_OK) {
        LOGI(TAG, "✅ mount_info_by_path: path=%s, device=%s, size=%zu",
             info.virtual_path, info.device_name, info.size);
    } else {
        LOGE(TAG, "❌ mount_info_by_path 失败");
    }

    LOGI(TAG, "=== VFS 功能测试完成 ===");
}
#endif /* OVS_RUN_APP_TESTS */

/* ============================================================ */
/*                     主函数                                    */
/* ============================================================ */
void app_main(void) {
    LOGI(TAG, "========================================");
    LOGI(TAG, "  OVS boot (Open Voice Assistant)");
    LOGI(TAG, "========================================");

    /* NVS 初始化 */
    esp_err_t nvs_ret = nvs_flash_init();
    if (nvs_ret == ESP_ERR_NVS_NO_FREE_PAGES || nvs_ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_ret);

    /* 核心服务 */
    event_bus_init();
    tasker_init();

    /* SPIFFS（出厂设备树回退源 + 运行时存储） */
    esp_vfs_spiffs_conf_t spiffs_conf = {
        .base_path = "/spiffs",
        .partition_label = "spiffs",
        .max_files = 8,
    };
    esp_err_t spiffs_ret = esp_vfs_spiffs_register(&spiffs_conf);
    if (spiffs_ret != ESP_OK) {
        LOGW(TAG, "SPIFFS mount failed: %s", esp_err_to_name(spiffs_ret));
    }

    /* 设备树（A/B 槽 → SPIFFS 回退） */
    dtree_err_t dtree_ret = dtree_init();
    if (dtree_ret != DTREE_OK) {
        LOGE(TAG, "Device tree init failed: %d", dtree_ret);
    }

#if OVS_ENABLE_NET
    /* ▶ OTA 线保护区：net_stack_init 调用点，勿移除（说明见文件头部保护区注释）
     * 依赖顺序：必须在 dtree_init() 之后（net_mgr 从设备树读 WiFi 配置），
     * 依赖 NVS/event_bus/tasker/SPIFFS 已就绪 */
    net_stack_init();
#endif

#if OVS_ENABLE_NET && OVS_NET_HOTSWAP_TEST
    xTaskCreate(net_hotswap_test_task, "net_hs_test", 4096, NULL,
                tskIDLE_PRIORITY + 1, NULL);
#endif

    /* W25Q128 外部 Flash + VFS 挂载 */
    w25q128_err_t w25_ret = w25q128_init();
    if (w25_ret != W25Q128_OK) {
        LOGE(TAG, "W25Q128 init failed: %d", w25_ret);
    }
    vfs_stack_start();

    /* LVGL UI（双平台骨架：显示为 null 输出冒烟，st7789 对接见 REFACTORING_PLAN 6.2；
     * 自建 lvgl 任务，与 tasker 长任务纪律一致） */
    lvgl_app_err_t lvgl_ret = lvgl_app_init();
    if (lvgl_ret != LVGL_APP_OK) {
        LOGE(TAG, "LVGL app init failed: %d", lvgl_ret);
    }

#if OVS_RUN_APP_TESTS
    LOGI(TAG, "App tests enabled (OVS_RUN_APP_TESTS=1)");
    test_vfs();
    int stress_failed = vfs_stress_run();
    if (stress_failed == 0) {
        LOGI(TAG, "✅ VFS 压力测试全部通过");
    } else if (stress_failed > 0) {
        LOGE(TAG, "❌ VFS 压力测试未通过: %d 项失败", stress_failed);
    }
#endif

    LOGI(TAG, "Boot complete");
}
