/**
 * @file dtree.h
 * @brief 设备树解析器公共 API
 * 
 * 模仿 Linux 设备树模型，使用 JSON 描述硬件配置。
 * 为各驱动模块提供统一的硬件配置读取接口。
 */

#ifndef DTREE_H
#define DTREE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========== 错误码 ========== */
typedef enum {
    DTREE_OK = 0,               /**< 成功 */
    DTREE_ERR_NOT_INIT = -1,    /**< 未初始化 */
    DTREE_ERR_NOT_FOUND = -2,   /**< 节点未找到 */
    DTREE_ERR_TYPE = -3,        /**< 类型错误 */
    DTREE_ERR_PARAM = -4,       /**< 参数错误 */
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
 * @brief 获取节点的 compatible 字符串
 * 
 * @param node 节点句柄
 * @return compatible 字符串，未找到返回 NULL
 */
const char* dtree_get_compatible(dtree_node_t* node);

/**
 * @brief 获取整数属性值
 * 
 * @param node       节点句柄
 * @param property   属性名称
 * @param default_val 默认值
 * @return 属性值，未找到返回默认值
 */
int32_t dtree_get_int(dtree_node_t* node, const char* property, int32_t default_val);

/**
 * @brief 获取无符号整数属性值
 * 
 * @param node       节点句柄
 * @param property   属性名称
 * @param default_val 默认值
 * @return 属性值，未找到返回默认值
 */
uint32_t dtree_get_uint(dtree_node_t* node, const char* property, uint32_t default_val);

/**
 * @brief 获取字符串属性值
 * 
 * @param node       节点句柄
 * @param property   属性名称
 * @return 字符串值，未找到返回 NULL
 */
const char* dtree_get_string(dtree_node_t* node, const char* property);

/**
 * @brief 获取布尔属性值
 * 
 * @param node       节点句柄
 * @param property   属性名称
 * @param default_val 默认值
 * @return 布尔值
 */
bool dtree_get_bool(dtree_node_t* node, const char* property, bool default_val);

/**
 * @brief 获取浮点数属性值
 * 
 * @param node       节点句柄
 * @param property   属性名称
 * @param default_val 默认值
 * @return 浮点值
 */
float dtree_get_float(dtree_node_t* node, const char* property, float default_val);

/* ========== 便捷宏：通过路径直接获取属性 ========== */

/**
 * @brief 通过完整路径获取整数
 * @param path 节点路径
 * @param prop 属性名
 * @param def  默认值
 */
#define DTREE_INT(path, prop, def) \
    dtree_get_int(dtree_get_node(path), prop, def)

/**
 * @brief 通过完整路径获取无符号整数
 */
#define DTREE_UINT(path, prop, def) \
    dtree_get_uint(dtree_get_node(path), prop, def)

/**
 * @brief 通过完整路径获取字符串
 */
#define DTREE_STR(path, prop) \
    dtree_get_string(dtree_get_node(path), prop)

/**
 * @brief 通过完整路径获取布尔值
 */
#define DTREE_BOOL(path, prop, def) \
    dtree_get_bool(dtree_get_node(path), prop, def)

/**
 * @brief 通过完整路径获取浮点数
 */
#define DTREE_FLOAT(path, prop, def) \
    dtree_get_float(dtree_get_node(path), prop, def)

/* ========== 设备树文件路径常量 ========== */
#define DTREE_CONFIG_DIR    "/spiffs/dtbs"  /**< JSON 配置目录 */

#ifdef __cplusplus
}
#endif

#endif /* DTREE_H */
