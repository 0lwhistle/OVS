/**
 * @file main.c
 * @brief W25Q128 + Holder 综合测试程序
 */

#include "tasker.h"
#include "event_bus.h"
#include "logger.h"
#include "holder.h"
#include "w25q128.h"
#include "dtree.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "nvs_flash.h"

static const char* TAG = "[MAIN_TEST]";

/* ============================================================ */
/*                    Holder 测试模块                             */
/* ============================================================ */
static int test_module_alpha_init(void) {
    LOGI(TAG, "[Holder] 初始化模块 alpha");
    return 0;  // 成功
}

static int test_module_beta_init(void) {
    LOGI(TAG, "[Holder] 初始化模块 beta");
    return 0;  // 成功
}

static int test_module_gamma_fail_init(void) {
    LOGI(TAG, "[Holder] 初始化模块 gamma (模拟失败)");
    return -1;  // 失败
}

static void test_holder(void) {
    LOGI(TAG, "=== Holder 测试开始 ===");
    
    // 1. 初始化 holder
    holder_err_t ret = holder_init();
    if (ret != HOLDER_OK) {
        LOGE(TAG, "holder_init 失败: %d", ret);
        return;
    }
    LOGI(TAG, "holder_init 成功");
    
    // 2. 注册模块
    holder_register_module("alpha", test_module_alpha_init, true, NULL);
    holder_register_module("beta", test_module_beta_init, false, NULL);
    holder_register_module("gamma", test_module_gamma_fail_init, false, NULL);  // 会失败但非必需
    
    LOGI(TAG, "已注册 3 个模块");
    
    // 3. 初始化所有模块 (遇到非必需模块错误不停止)
    ret = holder_init_all(false);
    if (ret != HOLDER_OK) {
        LOGE(TAG, "holder_init_all 返回: %d (预期部分失败)", ret);
    }
    
    // 4. 检查模块状态
    LOGI(TAG, "alpha ready: %s", holder_is_module_ready("alpha") ? "YES" : "NO");
    LOGI(TAG, "beta  ready: %s", holder_is_module_ready("beta") ? "YES" : "NO");
    LOGI(TAG, "gamma ready: %s", holder_is_module_ready("gamma") ? "YES" : "NO");
    
    // 5. 打印状态报告
    holder_print_status();
    
    // 6. 清理
    holder_destroy();
    LOGI(TAG, "=== Holder 测试结束 ===");
}

/* ============================================================ */
/*                    W25Q128 测试                               */
/* ============================================================ */
static void test_w25q128(void) {
    LOGI(TAG, "=== W25Q128 测试开始 ===");
    
    // 1. 初始化
    w25q128_err_t ret = w25q128_init();
    if (ret != W25Q128_OK) {
        LOGE(TAG, "W25Q128 初始化失败: %d", ret);
        return;
    }
    
    // 2. 获取信息
    w25q128_info_t info;
    if (w25q128_get_info(&info) == W25Q128_OK) {
        LOGI(TAG, "Flash: %luMB, %lu sectors", 
             info.total_size / (1024*1024), info.sector_count);
    }
    
    // 3. 擦除扇区0
    LOGI(TAG, "擦除扇区 0...");
    ret = w25q128_erase_sector(0);
    if (ret != W25Q128_OK) {
        LOGE(TAG, "擦除失败: %d", ret);
        goto deinit;
    }
    LOGI(TAG, "擦除成功");
    
    // 4. 写入数据
    const char* test_str = "Hello W25Q128! Data verified.";
    LOGI(TAG, "写入: \"%s\"", test_str);
    ret = w25q128_write(0, test_str, strlen(test_str) + 1);
    if (ret != W25Q128_OK) {
        LOGE(TAG, "写入失败: %d", ret);
        goto deinit;
    }
    LOGI(TAG, "写入成功");
    
    // 5. 读取并校验
    char buf[64] = {0};
    ret = w25q128_read(0, buf, strlen(test_str) + 1);
    if (ret != W25Q128_OK) {
        LOGE(TAG, "读取失败: %d", ret);
        goto deinit;
    }
    LOGI(TAG, "读取: \"%s\"", buf);
    if (strcmp(test_str, buf) == 0) { LOGI(TAG, "✅ 校验成功!"); } else { LOGE(TAG, "❌ 校验失败!"); }

deinit:
    w25q128_deinit();
    LOGI(TAG, "=== W25Q128 测试结束 ===");
}

/* ============================================================ */
/*                     主函数                                    */
/* ============================================================ */
void app_main(void) {
    LOGI(TAG, "========================================");
    LOGI(TAG, "  W25Q128 + Holder 综合测试");
    LOGI(TAG, "========================================");

    esp_err_t nvs_ret = nvs_flash_init();
    if (nvs_ret == ESP_ERR_NVS_NO_FREE_PAGES || nvs_ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_ret);

    event_bus_init();
    tasker_init();
    dtree_init();

    test_holder();
    test_w25q128();
    
    LOGI(TAG, "========================================");
    LOGI(TAG, "  所有测试完成!");
    LOGI(TAG, "========================================");
}
