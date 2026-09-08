/**
 * @file main.c
 * @brief VFS 虚拟文件系统测试
 */

#include "tasker.h"
#include "event_bus.h"
#include "logger.h"
#include "dtree.h"
#include "esp_spiffs.h"
#include "esp_timer.h"
#include "w25q128.h"
#include "w25q128_vfs.h"
#include "internal_flash_vfs.h"
#include "ovs_vfs.h"
#include "vfs_stress.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include <inttypes.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "nvs_flash.h"

static const char* TAG = "[MAIN]";

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
 * OTA 固件上传端点: POST /api/ota/firmware（流式）；
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

/* 分区重排过渡开关：W25Q128 分区布局变更后置 1 烧录一次（挂载失败自动格式化），
 * 完成过渡后必须置回 0，避免日后每次启动静默清空已存媒体文件 */
#define OVS_MEDIA_FORMAT_ON_FIRST_BOOT  0

/* ============================================================ */
/*                    设备树配置挂载                              */
/* ============================================================ */
static void vfs_auto_mount_from_dtree(void) {
    LOGI(TAG, "--- 从设备树自动挂载 VFS ---");
    
    /* 检查 vfs 节点是否存在 */
    if (!dtree_has_node("vfs")) {
        LOGW(TAG, "Device tree 'vfs' node not found, using default config");
        
        /* 使用默认配置 */
        vfs_mount_littlefs("/audio", "w25q128", 0, 8 * 1024 * 1024, true);
        vfs_mount_littlefs("/font", "w25q128", 8 * 1024 * 1024, 8 * 1024 * 1024, true);
        vfs_mount_littlefs("/config", "internal", 0, 0, true);
        return;
    }
    
    /* 获取 mounts 数组大小 */
    int array_size = dtree_array_size("vfs.mounts");
    if (array_size <= 0) {
        LOGW(TAG, "No mounts configured in device tree");
        return;
    }
    
    LOGI(TAG, "Found %d mount configs", array_size);
    
    /* 遍历挂载点 */
    int mount_count = 0;
    
    for (int i = 0; i < array_size; i++) {
        /* 获取数组元素 */
        dtree_node_t* item = dtree_array_item("vfs.mounts", i);
        if (!item) {
            LOGW(TAG, "Failed to get mount config at index %d", i);
            continue;
        }
        
        /* 读取配置 */
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

        LOGI(TAG, "Mount config: path=%s, device=%s, offset=%" PRId32 ", size=%" PRId32,
             mount_path, device, offset, size);

        /* 挂载 */
        vfs_err_t ret = vfs_mount_littlefs(mount_path, device, offset, size, format_if_fail);
        if (ret == VFS_OK) {
            mount_count++;
            LOGI(TAG, "✅ 挂载成功: %s", mount_path);
        } else {
            LOGE(TAG, "❌ 挂载失败: %s (err=%d)", mount_path, ret);
            LOGW(TAG, "提示：若刚变更过分区布局，置 OVS_MEDIA_FORMAT_ON_FIRST_BOOT=1 烧录一次");
        }
    }
    
    LOGI(TAG, "自动挂载完成: %d 个挂载点", mount_count);
}

/* ============================================================ */
/*                    性能测试                                    */
/* ============================================================ */
static void test_performance(const char* path) {
    LOGI(TAG, "");
    LOGI(TAG, "--- 性能测试: %s ---", path);
    
    /* 写入性能测试 */
    size_t test_size = 4096;  // 4KB
    uint8_t* buf = malloc(test_size);
    if (!buf) {
        LOGE(TAG, "Failed to allocate test buffer");
        return;
    }
    
    /* 填充测试数据 */
    for (int i = 0; i < test_size; i++) {
        buf[i] = i & 0xFF;
    }
    
    /* 写入测试 */
    char filepath[64];
    snprintf(filepath, sizeof(filepath), "%s/perf_test.bin", path);
    
    int64_t start = esp_timer_get_time();
    FILE* f = fopen(filepath, "wb");
    bool write_ok = false;
    if (f) {
        size_t written = fwrite(buf, 1, test_size, f);
        bool close_ok = (fclose(f) == 0);
        write_ok = (written == test_size) && close_ok;
        if (!write_ok) {
            LOGE(TAG, "性能写入失败: written=%u, close_ok=%d",
                 (unsigned)written, close_ok ? 1 : 0);
        }
    }
    int64_t end = esp_timer_get_time();
    
    float write_ms = (end - start) / 1000.0f;
    if (write_ok) {
        LOGI(TAG, "写入 %uKB: %.2f ms (%.2f KB/s)",
             test_size/1024, write_ms, (test_size/1024.0f) / (write_ms/1000.0f));
    } else {
        LOGW(TAG, "写入 %uKB 失败, 耗时 %.2f ms", test_size/1024, write_ms);
    }
    
    /* 读取测试 */
    start = esp_timer_get_time();
    f = fopen(filepath, "rb");
    bool read_ok = false;
    if (f) {
        size_t rd = fread(buf, 1, test_size, f);
        bool close_ok = (fclose(f) == 0);
        read_ok = (rd == test_size) && close_ok;
        if (!read_ok) {
            LOGE(TAG, "性能读取失败: read=%u, close_ok=%d",
                 (unsigned)rd, close_ok ? 1 : 0);
        }
    }
    end = esp_timer_get_time();
    
    float read_ms = (end - start) / 1000.0f;
    if (read_ok) {
        LOGI(TAG, "读取 %uKB: %.2f ms (%.2f KB/s)",
             test_size/1024, read_ms, (test_size/1024.0f) / (read_ms/1000.0f));
    } else {
        LOGW(TAG, "读取 %uKB 失败, 耗时 %.2f ms", test_size/1024, read_ms);
    }
    
    /* 清理 */
    unlink(filepath);
    free(buf);
    
    LOGI(TAG, "性能测试完成");
}

/* ============================================================ */
/*                    VFS 测试                                    */
/* ============================================================ */
static void test_vfs(void) {
    LOGI(TAG, "=== VFS 虚拟文件系统测试 ===");
    
    /* 1. 初始化 VFS */
    vfs_err_t ret = vfs_init();
    if (ret != VFS_OK) {
        LOGE(TAG, "VFS init failed: %d", ret);
        return;
    }
    LOGI(TAG, "✅ VFS 初始化成功");
    
    /* 2. 注册块设备 */
    w25q128_register_vfs();
    LOGI(TAG, "✅ W25Q128 注册到 VFS");
    
    internal_flash_register_vfs("littlefs");
    LOGI(TAG, "✅ 内部 Flash 注册到 VFS");
    
    /* 3. 打印块设备状态 */
    vfs_block_dev_print_all();
    
    /* 4. 从设备树自动挂载 */
    vfs_auto_mount_from_dtree();
    
    /* 5. 打印 VFS 状态 */
    vfs_print_status();
    
    /* 6. 测试文件操作 */
    LOGI(TAG, "");
    LOGI(TAG, "--- 文件操作测试 ---");
    
    /* 写入测试 */
    FILE* f = fopen("/audio/test.txt", "w");
    if (f) {
        bool ok = (fprintf(f, "Hello VFS! This is a test file on W25Q128.") > 0);
        ok = (fclose(f) == 0) && ok;
        if (ok) {
            LOGI(TAG, "✅ 写入文件成功: /audio/test.txt");
        } else {
            LOGE(TAG, "❌ 写入文件失败（磁盘错误）: /audio/test.txt");
        }
    } else {
        LOGE(TAG, "❌ 写入文件失败");
    }
    
    /* 读取测试 */
    f = fopen("/audio/test.txt", "r");
    if (f) {
        char buf[64] = {0};
        size_t rd = fread(buf, 1, sizeof(buf) - 1, f);
        bool ok = (rd > 0 && !ferror(f));
        ok = (fclose(f) == 0) && ok;
        if (ok) {
            LOGI(TAG, "✅ 读取文件成功: \"%s\"", buf);
        } else {
            LOGE(TAG, "❌ 读取文件失败（磁盘错误）");
        }
    } else {
        LOGE(TAG, "❌ 读取文件失败");
    }

    /* /media 媒体分区写入读取测试 */
    f = fopen("/media/test.txt", "w");
    if (f) {
        bool ok = (fprintf(f, "Hello MEDIA!") > 0);
        ok = (fclose(f) == 0) && ok;
        LOGI(TAG, "%s 写入 /media/test.txt", ok ? "✅" : "❌");
    }
    f = fopen("/media/test.txt", "r");
    if (f) {
        char buf[32] = {0};
        size_t rd = fread(buf, 1, sizeof(buf) - 1, f);
        bool ok = (rd > 0 && !ferror(f));
        ok = (fclose(f) == 0) && ok;
        LOGI(TAG, "%s 读取 /media/test.txt: \"%s\"", ok ? "✅" : "❌", buf);
    } else {
        LOGE(TAG, "❌ 打开 /media/test.txt 失败");
    }
    
    /* 7. 测试路径匹配 */
    LOGI(TAG, "");
    LOGI(TAG, "--- 路径匹配测试 ---");
    LOGI(TAG, "/audio/test.bin -> %s", vfs_is_mounted("/audio/test.bin") ? "已挂载" : "未挂载");
    LOGI(TAG, "/font/hzk16.bin -> %s", vfs_is_mounted("/font/hzk16.bin") ? "已挂载" : "未挂载");
    LOGI(TAG, "/config/settings.json -> %s", vfs_is_mounted("/config/settings.json") ? "已挂载" : "未挂载");
    LOGI(TAG, "/media/pic001.jpg -> %s", vfs_is_mounted("/media/pic001.jpg") ? "已挂载" : "未挂载");
    LOGI(TAG, "/other/file.txt -> %s", vfs_is_mounted("/other/file.txt") ? "已挂载" : "未挂载");
    
    /* 8. 性能测试 */
    test_performance("/audio");
    /* 9. 测试便捷 API */
    LOGI(TAG, "");
    LOGI(TAG, "--- 便捷 API 测试 ---");
    
    /* 测试 vfs_get_mount_info_by_path */
    vfs_mount_info_t info;
    ret = vfs_get_mount_info_by_path("/audio/test.bin", &info);
    if (ret == VFS_OK) {
        LOGI(TAG, "✅ vfs_get_mount_info_by_path 成功: path=%s, device=%s, size=%zu", 
             info.virtual_path, info.device_name, info.size);
    } else {
        LOGE(TAG, "❌ vfs_get_mount_info_by_path 失败: %d", ret);
    }
    
    /* 测试 vfs_unmount_all */
    LOGI(TAG, "测试 vfs_unmount_all...");
    ret = vfs_unmount_all();
    if (ret == VFS_OK) {
        LOGI(TAG, "✅ vfs_unmount_all 成功");
    } else {
        LOGE(TAG, "❌ vfs_unmount_all 失败: %d", ret);
    }
    
    /* 重新挂载以继续测试 */
    vfs_auto_mount_from_dtree();
    
    /* 最终状态 */
    LOGI(TAG, "");
    vfs_print_status();
    
    LOGI(TAG, "=== VFS 测试完成 ===");
}

/* ============================================================ */
/*                     主函数                                    */
/* ============================================================ */
void app_main(void) {
    LOGI(TAG, "========================================");
    LOGI(TAG, "  VFS 虚拟文件系统测试");
    LOGI(TAG, "========================================");

    /* NVS 初始化 */
    esp_err_t nvs_ret = nvs_flash_init();
    if (nvs_ret == ESP_ERR_NVS_NO_FREE_PAGES || nvs_ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_ret);

    /* 核心服务初始化 */
    event_bus_init();
    tasker_init();

    /* SPIFFS 初始化（设备树配置） */
    esp_vfs_spiffs_conf_t spiffs_conf = {
        .base_path = "/spiffs",
        .partition_label = "spiffs",
        .max_files = 8,
    };
    esp_err_t spiffs_ret = esp_vfs_spiffs_register(&spiffs_conf);
    if (spiffs_ret != ESP_OK) {
        LOGW(TAG, "SPIFFS mount failed: %s", esp_err_to_name(spiffs_ret));
    } else {
        LOGI(TAG, "SPIFFS mounted");
    }

    /* 设备树初始化 */
    LOGI(TAG, "Initializing device tree...");
    dtree_err_t dtree_ret = dtree_init();
    if (dtree_ret != DTREE_OK) {
        LOGE(TAG, "Device tree init failed: %d", dtree_ret);
    } else {
        LOGI(TAG, "Device tree initialized");
        /* 检查 vfs 节点是否存在 */
        LOGI(TAG, "Checking 'vfs' node: %s", dtree_has_node("vfs") ? "EXISTS" : "NOT FOUND");
    }

#if OVS_ENABLE_NET
    /* ▶ OTA 线保护区：net_stack_init 调用点，勿移除（说明见文件头部保护区注释）
     * 依赖顺序：必须在 dtree_init() 之后（net_mgr 从设备树读 WiFi 配置），
     * 依赖 NVS/event_bus/tasker/SPIFFS 已就绪 */
    net_stack_init();
#endif
    
    /* W25Q128 初始化 */
    w25q128_err_t w25_ret = w25q128_init();
    if (w25_ret != W25Q128_OK) {
        LOGE(TAG, "W25Q128 init failed: %d", w25_ret);
    } else {
        LOGI(TAG, "W25Q128 initialized");
    }
    
    /* VFS 测试 */
    test_vfs();

    /* VFS 压力测试（失败项在末尾总结中列出） */
    int stress_failed = vfs_stress_run();
    if (stress_failed == 0) {
        LOGI(TAG, "✅ VFS 压力测试全部通过");
    } else if (stress_failed > 0) {
        LOGE(TAG, "❌ VFS 压力测试未通过: %d 项失败（见上方总结）", stress_failed);
    } /* <0: 测试未能启动，日志中已有原因 */
    
    LOGI(TAG, "========================================");
    LOGI(TAG, "  测试完成!");
    LOGI(TAG, "========================================");
}
