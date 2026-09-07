/**
 * @file dtree.h
 * @brief 设备树解析器公共 API
 * 
 * 模仿 Linux 设备树模型，使用 JSON 描述硬件配置。
 * 为各驱动模块提供统一的硬件配置读取接口。
 * 
 * 设计原则：
 * 1. 硬件参数完全由设备树描述，程序中不硬编码默认值
 * 2. 属性读取失败时返回错误码，调用者必须处理错误
 * 3. 提供完整的错误处理机制
 */

#ifndef DTREE_H
#define DTREE_H

#include <stdint.h>
#include <stdbool.h>
#include "logger.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========== 错误码 ========== */
typedef enum {
    DTREE_OK = 0,               /**< 成功 */
    DTREE_ERR_NOT_INIT = -1,    /**< 未初始化 */
    DTREE_ERR_NOT_FOUND = -2,   /**< 节点或属性未找到 */
    DTREE_ERR_TYPE = -3,        /**< 类型错误 */
    DTREE_ERR_PARAM = -4,       /**< 参数错误 */
    DTREE_ERR_PARSE = -5,       /**< 解析错误 */
    DTREE_ERR_IO = -6,          /**< IO错误 */
} dtree_err_t;

/* ========== 节点句柄 ========== */
typedef struct dtree_node dtree_node_t;

/* ========== 公共 API ========== */

/**
 * @brief 初始化设备树
 * 
 * 加载所有 JSON 配置文件到内存。
 * 必须在使用其他 dtree API 之前调用。
 * 
 * @return DTREE_OK 成功，其他值失败
 */
dtree_err_t dtree_init(void);

/**
 * @brief 检查设备树是否已初始化
 * 
 * @return true 已初始化，false 未初始化
 */
bool dtree_is_initialized(void);

/**
 * @brief 获取根节点
 * 
 * @return 根节点句柄，失败返回 NULL
 */
dtree_node_t* dtree_get_root(void);

/**
 * @brief 获取子节点
 * 
 * @param parent 父节点
 * @param name   子节点名称
 * @return 子节点句柄，未找到返回 NULL
 */
dtree_node_t* dtree_get_child(dtree_node_t* parent, const char* name);

/**
 * @brief 通过路径获取节点
 * 
 * @param path 节点路径，如 "i2s.microphone" 或 "spi.lcd_display"
 * @return 节点句柄，未找到返回 NULL
 */
dtree_node_t* dtree_get_node(const char* path);

/**
 * @brief 检查节点是否存在
 * 
 * @param path 节点路径
 * @return true 存在，false 不存在
 */
bool dtree_has_node(const char* path);

/**
 * @brief 检查属性是否存在
 * 
 * @param node     节点句柄
 * @param property 属性名称
 * @return true 存在，false 不存在
 */
bool dtree_has_property(dtree_node_t* node, const char* property);

/**
 * @brief 获取节点的 compatible 字符串
 * 
 * @param node 节点句柄
 * @return compatible 字符串，未找到返回 NULL
 */
const char* dtree_get_compatible(dtree_node_t* node);

/**
 * @brief 获取整数属性值
 * 
 * @param node     节点句柄
 * @param property 属性名称
 * @param value    输出参数，存储读取到的值
 * @return DTREE_OK 成功，其他值失败
 */
dtree_err_t dtree_get_int(dtree_node_t* node, const char* property, int32_t* value);

/**
 * @brief 获取无符号整数属性值
 * 
 * @param node     节点句柄
 * @param property 属性名称
 * @param value    输出参数，存储读取到的值
 * @return DTREE_OK 成功，其他值失败
 */
dtree_err_t dtree_get_uint(dtree_node_t* node, const char* property, uint32_t* value);

/**
 * @brief 获取字符串属性值
 * 
 * @param node     节点句柄
 * @param property 属性名称
 * @param value    输出参数，存储字符串指针（不要修改或释放）
 * @return DTREE_OK 成功，其他值失败
 */
dtree_err_t dtree_get_string(dtree_node_t* node, const char* property, const char** value);

/**
 * @brief 获取布尔属性值
 * 
 * @param node     节点句柄
 * @param property 属性名称
 * @param value    输出参数，存储布尔值
 * @return DTREE_OK 成功，其他值失败
 */
dtree_err_t dtree_get_bool(dtree_node_t* node, const char* property, bool* value);

/**
 * @brief 获取浮点数属性值
 * 
 * @param node     节点句柄
 * @param property 属性名称
 * @param value    输出参数，存储浮点值
 * @return DTREE_OK 成功，其他值失败
 */
dtree_err_t dtree_get_float(dtree_node_t* node, const char* property, float* value);

/* ========== 便捷宏：通过路径直接获取属性 ========== */

/**
 * @brief 通过完整路径获取整数
 * @param path  节点路径
 * @param prop  属性名
 * @param value 指向 int32_t 变量的指针
 * @return dtree_err_t 错误码
 */
#define DTREE_INT(path, prop, value) \
    dtree_get_int(dtree_get_node(path), prop, value)

/**
 * @brief 通过完整路径获取无符号整数
 */
#define DTREE_UINT(path, prop, value) \
    dtree_get_uint(dtree_get_node(path), prop, value)

/**
 * @brief 通过完整路径获取字符串
 */
#define DTREE_STR(path, prop, value) \
    dtree_get_string(dtree_get_node(path), prop, value)

/**
 * @brief 通过完整路径获取布尔值
 */
#define DTREE_BOOL(path, prop, value) \
    dtree_get_bool(dtree_get_node(path), prop, value)

/**
 * @brief 通过完整路径获取浮点数
 */
#define DTREE_FLOAT(path, prop, value) \
    dtree_get_float(dtree_get_node(path), prop, value)

/* ========== 设备树文件路径常量 ========== */
#define DTREE_CONFIG_DIR    "/spiffs"  /**< JSON 配置目录 */


/**
 * @brief 检查设备树操作结果并打印错误
 * @param op   操作描述
 * @param err  错误码
 * 
 * 用法：
 *   DTREE_CHECK_ERROR("Read bclk_pin", err);
 *   if (err != DTREE_OK) { return ERROR; }
 */
#define DTREE_CHECK_ERROR(op, err) \
    do { if ((err) != DTREE_OK) LOGE("[DTREE]", "%s failed: %d", (op), (err)); } while(0)

#ifdef __cplusplus
}
#endif


#endif /* DTREE_H */

/* ========== 数组 API ========== */

/**
 * @brief 获取数组大小
 * @param node 父节点
 * @param property 数组属性名
 * @return 数组大小，-1 表示错误
 */
int dtree_get_array_size(dtree_node_t* node, const char* property);

/**
 * @brief 获取数组元素（返回子节点）
 * @param node 父节点
 * @param property 数组属性名
 * @param index 数组索引
 * @return 子节点指针，NULL 表示不存在
 */
dtree_node_t* dtree_get_array_item(dtree_node_t* node, const char* property, int index);

/**
 * @brief 通过路径获取数组大小
 * @param path 路径（如 "vfs.mounts"）
 * @return 数组大小，-1 表示错误
 */
int dtree_array_size(const char* path);

/**
 * @brief 通过路径获取数组元素
 * @param path 路径（如 "vfs.mounts"）
 * @param index 数组索引
 * @return 子节点指针，NULL 表示不存在
 */
dtree_node_t* dtree_array_item(const char* path, int index);
