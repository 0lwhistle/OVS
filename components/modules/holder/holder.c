/**
 * @file holder.c
 * @brief 全局硬件模块注册表管理器实现
 * 
 * @version 1.0
 * @date 2026-09-07
 */

#include "holder.h"
#include "logger.h"
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_timer.h"

static const char* TAG = "[HOLDER]";

/**
 * @brief 模块节点结构（链表节点）
 */
typedef struct holder_node {
    holder_module_info_t info;
    struct holder_node* next;
} holder_node_t;

/**
 * @brief holder上下文结构
 */
typedef struct {
    holder_node_t* head;                /**< 链表头 */
    uint32_t count;                     /**< 模块数量 */
    SemaphoreHandle_t mutex;            /**< 互斥锁 */
    bool initialized;                   /**< 是否已初始化 */
} holder_context_t;

static holder_context_t s_holder_ctx = {0};

/**
 * @brief 查找模块节点（需要持有锁）
 */
static holder_node_t* find_module_node(const char* name) {
    if (!name || !s_holder_ctx.head) {
        return NULL;
    }
    
    holder_node_t* current = s_holder_ctx.head;
    while (current) {
        if (strcmp(current->info.name, name) == 0) {
            return current;
        }
        current = current->next;
    }
    return NULL;
}

/**
 * @brief 初始化holder模块
 */
holder_err_t holder_init(void) {
    if (s_holder_ctx.initialized) {
        LOGW(TAG, "Holder already initialized");
        return HOLDER_OK;
    }
    
    s_holder_ctx.mutex = xSemaphoreCreateMutex();
    if (!s_holder_ctx.mutex) {
        LOGE(TAG, "Failed to create mutex");
        return HOLDER_ERR_MUTEX;
    }
    
    s_holder_ctx.head = NULL;
    s_holder_ctx.count = 0;
    s_holder_ctx.initialized = true;
    
    LOGI(TAG, "Holder initialized");
    return HOLDER_OK;
}

/**
 * @brief 注册模块到holder
 */
holder_err_t holder_register_module(const char* name, 
                                   holder_module_init_fn init_fn,
                                   bool required,
                                   void* user_data) {
    if (!s_holder_ctx.initialized) {
        LOGE(TAG, "Holder not initialized");
        return HOLDER_ERR_NOT_INITIALIZED;
    }
    
    if (!name || !init_fn) {
        LOGE(TAG, "Invalid parameters");
        return HOLDER_ERR_INVALID_PARAM;
    }
    
    if (xSemaphoreTake(s_holder_ctx.mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        LOGE(TAG, "Failed to take mutex");
        return HOLDER_ERR_MUTEX;
    }
    
    // 检查是否已注册
    if (find_module_node(name)) {
        xSemaphoreGive(s_holder_ctx.mutex);
        LOGW(TAG, "Module %s already registered", name);
        return HOLDER_ERR_ALREADY_REGISTERED;
    }
    
    // 分配新节点
    holder_node_t* new_node = (holder_node_t*)malloc(sizeof(holder_node_t));
    if (!new_node) {
        xSemaphoreGive(s_holder_ctx.mutex);
        LOGE(TAG, "Failed to allocate memory for module %s", name);
        return HOLDER_ERR_NO_MEMORY;
    }
    
    // 初始化节点信息
    new_node->info.name = name;
    new_node->info.init_fn = init_fn;
    new_node->info.state = HOLDER_MODULE_STATE_REGISTERED;
    new_node->info.error_count = 0;
    new_node->info.init_time_ms = 0;
    new_node->info.last_error = NULL;
    new_node->info.required = required;
    new_node->info.user_data = user_data;
    new_node->next = NULL;
    
    // 添加到链表尾部
    if (!s_holder_ctx.head) {
        s_holder_ctx.head = new_node;
    } else {
        holder_node_t* tail = s_holder_ctx.head;
        while (tail->next) {
            tail = tail->next;
        }
        tail->next = new_node;
    }
    
    s_holder_ctx.count++;
    xSemaphoreGive(s_holder_ctx.mutex);
    
    LOGI(TAG, "Module %s registered (required: %s)", name, required ? "yes" : "no");
    return HOLDER_OK;
}

/**
 * @brief 注销模块
 */
holder_err_t holder_unregister_module(const char* name) {
    if (!s_holder_ctx.initialized) {
        return HOLDER_ERR_NOT_INITIALIZED;
    }
    
    if (!name) {
        return HOLDER_ERR_INVALID_PARAM;
    }
    
    if (xSemaphoreTake(s_holder_ctx.mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return HOLDER_ERR_MUTEX;
    }
    
    holder_node_t* prev = NULL;
    holder_node_t* current = s_holder_ctx.head;
    
    while (current) {
        if (strcmp(current->info.name, name) == 0) {
            // 找到节点，从链表中移除
            if (prev) {
                prev->next = current->next;
            } else {
                s_holder_ctx.head = current->next;
            }
            
            free(current);
            s_holder_ctx.count--;
            xSemaphoreGive(s_holder_ctx.mutex);
            
            LOGI(TAG, "Module %s unregistered", name);
            return HOLDER_OK;
        }
        prev = current;
        current = current->next;
    }
    
    xSemaphoreGive(s_holder_ctx.mutex);
    LOGW(TAG, "Module %s not found for unregister", name);
    return HOLDER_ERR_NOT_FOUND;
}

/**
 * @brief 初始化指定模块
 */
holder_err_t holder_init_module(const char* name) {
    if (!s_holder_ctx.initialized) {
        return HOLDER_ERR_NOT_INITIALIZED;
    }
    
    if (!name) {
        return HOLDER_ERR_INVALID_PARAM;
    }
    
    if (xSemaphoreTake(s_holder_ctx.mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return HOLDER_ERR_MUTEX;
    }
    
    holder_node_t* node = find_module_node(name);
    if (!node) {
        xSemaphoreGive(s_holder_ctx.mutex);
        return HOLDER_ERR_NOT_FOUND;
    }
    
    // 检查状态
    if (node->info.state == HOLDER_MODULE_STATE_READY) {
        xSemaphoreGive(s_holder_ctx.mutex);
        LOGI(TAG, "Module %s already initialized", name);
        return HOLDER_OK;
    }
    
    // 设置初始化中状态
    node->info.state = HOLDER_MODULE_STATE_INITIALIZING;
    xSemaphoreGive(s_holder_ctx.mutex);
    
    // 记录开始时间
    int64_t start_time = esp_timer_get_time();
    
    // 调用初始化函数
    int ret = node->info.init_fn();
    
    // 计算初始化耗时
    int64_t end_time = esp_timer_get_time();
    uint32_t init_time_ms = (uint32_t)((end_time - start_time) / 1000);
    
    if (xSemaphoreTake(s_holder_ctx.mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return HOLDER_ERR_MUTEX;
    }
    
    node->info.init_time_ms = init_time_ms;
    
    if (ret == 0) {
        node->info.state = HOLDER_MODULE_STATE_READY;
        node->info.last_error = NULL;
        LOGI(TAG, "Module %s initialized successfully (%lu ms)", name, init_time_ms);
    } else {
        node->info.state = HOLDER_MODULE_STATE_ERROR;
        node->info.error_count++;
        node->info.last_error = "Initialization failed";
        LOGE(TAG, "Module %s initialization failed (error: %d, time: %lu ms)", 
             name, ret, init_time_ms);
    }
    
    xSemaphoreGive(s_holder_ctx.mutex);
    return HOLDER_OK;
}

/**
 * @brief 初始化所有已注册模块
 */
holder_err_t holder_init_all(bool stop_on_required_error) {
    if (!s_holder_ctx.initialized) {
        return HOLDER_ERR_NOT_INITIALIZED;
    }
    
    LOGI(TAG, "Initializing all registered modules...");
    
    if (xSemaphoreTake(s_holder_ctx.mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return HOLDER_ERR_MUTEX;
    }
    
    holder_node_t* current = s_holder_ctx.head;
    holder_err_t result = HOLDER_OK;
    
    while (current) {
        holder_node_t* next = current->next;
        const char* name = current->info.name;
        bool required = current->info.required;
        
        xSemaphoreGive(s_holder_ctx.mutex);
        
        // 初始化模块
        holder_err_t ret = holder_init_module(name);
        
        if (ret != HOLDER_OK) {
            LOGE(TAG, "Failed to initialize module %s", name);
            if (required && stop_on_required_error) {
                result = HOLDER_ERR_INIT_FAILED;
                break;
            }
        } else {
            // 检查初始化是否成功
            holder_module_state_t state = holder_get_module_state(name);
            if (state == HOLDER_MODULE_STATE_ERROR) {
                if (required && stop_on_required_error) {
                    result = HOLDER_ERR_INIT_FAILED;
                    break;
                }
            }
        }
        
        if (xSemaphoreTake(s_holder_ctx.mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
            return HOLDER_ERR_MUTEX;
        }
        
        current = next;
    }
    
    xSemaphoreGive(s_holder_ctx.mutex);
    
    // 打印状态
    holder_print_status();
    
    if (result == HOLDER_OK) {
        LOGI(TAG, "All modules initialized successfully");
    } else {
        LOGE(TAG, "Some required modules failed to initialize");
    }
    
    return result;
}

/**
 * @brief 获取模块信息
 */
holder_err_t holder_get_module_info(const char* name, holder_module_info_t* info) {
    if (!s_holder_ctx.initialized) {
        return HOLDER_ERR_NOT_INITIALIZED;
    }
    
    if (!name || !info) {
        return HOLDER_ERR_INVALID_PARAM;
    }
    
    if (xSemaphoreTake(s_holder_ctx.mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return HOLDER_ERR_MUTEX;
    }
    
    holder_node_t* node = find_module_node(name);
    if (!node) {
        xSemaphoreGive(s_holder_ctx.mutex);
        return HOLDER_ERR_NOT_FOUND;
    }
    
    *info = node->info;
    xSemaphoreGive(s_holder_ctx.mutex);
    
    return HOLDER_OK;
}

/**
 * @brief 检查模块是否就绪
 */
bool holder_is_module_ready(const char* name) {
    return holder_get_module_state(name) == HOLDER_MODULE_STATE_READY;
}

/**
 * @brief 获取模块状态
 */
holder_module_state_t holder_get_module_state(const char* name) {
    if (!s_holder_ctx.initialized || !name) {
        return HOLDER_MODULE_STATE_UNKNOWN;
    }
    
    if (xSemaphoreTake(s_holder_ctx.mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return HOLDER_MODULE_STATE_UNKNOWN;
    }
    
    holder_node_t* node = find_module_node(name);
    if (!node) {
        xSemaphoreGive(s_holder_ctx.mutex);
        return HOLDER_MODULE_STATE_UNKNOWN;
    }
    
    holder_module_state_t state = node->info.state;
    xSemaphoreGive(s_holder_ctx.mutex);
    
    return state;
}

/**
 * @brief 设置模块状态
 */
holder_err_t holder_set_module_state(const char* name, holder_module_state_t state) {
    if (!s_holder_ctx.initialized) {
        return HOLDER_ERR_NOT_INITIALIZED;
    }
    
    if (!name || state >= HOLDER_MODULE_STATE_MAX) {
        return HOLDER_ERR_INVALID_PARAM;
    }
    
    if (xSemaphoreTake(s_holder_ctx.mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return HOLDER_ERR_MUTEX;
    }
    
    holder_node_t* node = find_module_node(name);
    if (!node) {
        xSemaphoreGive(s_holder_ctx.mutex);
        return HOLDER_ERR_NOT_FOUND;
    }
    
    node->info.state = state;
    xSemaphoreGive(s_holder_ctx.mutex);
    
    LOGI(TAG, "Module %s state changed to %d", name, state);
    return HOLDER_OK;
}

/**
 * @brief 打印所有模块状态
 */
void holder_print_status(void) {
    if (!s_holder_ctx.initialized) {
        LOGE(TAG, "Holder not initialized");
        return;
    }
    
    if (xSemaphoreTake(s_holder_ctx.mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        LOGE(TAG, "Failed to take mutex for printing");
        return;
    }
    
    LOGI(TAG, "=== Module Status Report ===");
    LOGI(TAG, "Total modules: %lu", s_holder_ctx.count);
    
    holder_node_t* current = s_holder_ctx.head;
    while (current) {
        const char* state_str;
        switch (current->info.state) {
            case HOLDER_MODULE_STATE_UNKNOWN: state_str = "UNKNOWN"; break;
            case HOLDER_MODULE_STATE_REGISTERED: state_str = "REGISTERED"; break;
            case HOLDER_MODULE_STATE_INITIALIZING: state_str = "INITIALIZING"; break;
            case HOLDER_MODULE_STATE_READY: state_str = "READY"; break;
            case HOLDER_MODULE_STATE_ERROR: state_str = "ERROR"; break;
            case HOLDER_MODULE_STATE_DISABLED: state_str = "DISABLED"; break;
            default: state_str = "INVALID"; break;
        }
        
        LOGI(TAG, "Module: %-15s | State: %-12s | Required: %-3s | Errors: %lu | Init time: %lu ms",
             current->info.name,
             state_str,
             current->info.required ? "Yes" : "No",
             current->info.error_count,
             current->info.init_time_ms);
        
        if (current->info.last_error) {
            LOGW(TAG, "  Last error: %s", current->info.last_error);
        }
        
        current = current->next;
    }
    
    LOGI(TAG, "============================");
    xSemaphoreGive(s_holder_ctx.mutex);
}

/**
 * @brief 获取已注册模块数量
 */
uint32_t holder_get_module_count(void) {
    if (!s_holder_ctx.initialized) {
        return 0;
    }
    
    if (xSemaphoreTake(s_holder_ctx.mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return 0;
    }
    
    uint32_t count = s_holder_ctx.count;
    xSemaphoreGive(s_holder_ctx.mutex);
    
    return count;
}

/**
 * @brief 检查holder是否已初始化
 */
bool holder_is_initialized(void) {
    return s_holder_ctx.initialized;
}

/**
 * @brief 销毁holder模块
 */
void holder_destroy(void) {
    if (!s_holder_ctx.initialized) {
        return;
    }
    
    if (xSemaphoreTake(s_holder_ctx.mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return;
    }
    
    // 释放所有节点
    holder_node_t* current = s_holder_ctx.head;
    while (current) {
        holder_node_t* next = current->next;
        free(current);
        current = next;
    }
    
    s_holder_ctx.head = NULL;
    s_holder_ctx.count = 0;
    xSemaphoreGive(s_holder_ctx.mutex);
    
    // 删除互斥锁
    if (s_holder_ctx.mutex) {
        vSemaphoreDelete(s_holder_ctx.mutex);
        s_holder_ctx.mutex = NULL;
    }
    
    s_holder_ctx.initialized = false;
    LOGI(TAG, "Holder destroyed");
}
