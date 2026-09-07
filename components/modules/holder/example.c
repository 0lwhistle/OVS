/**
 * @file example.c
 * @brief holder模块使用示例
 * 
 * 展示如何使用holder管理硬件模块注册和初始化。
 */

#include "holder.h"
#include "logger.h"
#include <stdio.h>

static const char* TAG = "[EXAMPLE]";

// 示例模块初始化函数
static int example_module_a_init(void) {
    LOGI(TAG, "Initializing module A");
    // 模拟初始化成功
    return 0;
}

static int example_module_b_init(void) {
    LOGI(TAG, "Initializing module B");
    // 模拟初始化失败
    return -1;
}

static int example_module_c_init(void) {
    LOGI(TAG, "Initializing module C");
    // 模拟初始化成功
    return 0;
}

void holder_example(void) {
    LOGI(TAG, "=== Holder Example ===");
    
    // 1. 初始化holder
    holder_err_t ret = holder_init();
    if (ret != HOLDER_OK) {
        LOGE(TAG, "Failed to initialize holder: %d", ret);
        return;
    }
    
    // 2. 注册模块
    holder_register_module("module_a", example_module_a_init, true, NULL);
    holder_register_module("module_b", example_module_b_init, false, NULL);  // 非必需模块
    holder_register_module("module_c", example_module_c_init, true, NULL);
    
    // 3. 初始化所有模块
    ret = holder_init_all(true);  // 遇到必需模块错误时停止
    if (ret != HOLDER_OK) {
        LOGE(TAG, "Some required modules failed to initialize");
    }
    
    // 4. 检查模块状态
    LOGI(TAG, "Module A ready: %s", holder_is_module_ready("module_a") ? "yes" : "no");
    LOGI(TAG, "Module B ready: %s", holder_is_module_ready("module_b") ? "yes" : "no");
    LOGI(TAG, "Module C ready: %s", holder_is_module_ready("module_c") ? "yes" : "no");
    
    // 5. 打印状态报告
    holder_print_status();
    
    // 6. 清理
    holder_destroy();
}
