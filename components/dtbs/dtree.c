/**
 * @file dtree.c
 * @brief 设备树解析器实现
 * 
 * 使用 cJSON 解析 JSON 配置文件，为各驱动模块提供硬件配置读取。
 * 设计原则：硬件参数完全由设备树描述，程序中不硬编码默认值。
 */

#include "dtree.h"
#include "dtb_ab.h"
#include "logger.h"
#include "mem.h"
#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char* TAG = "[DTREE]";

/* ========== 内部数据结构 ========== */

struct dtree_node {
    cJSON* json;        /**< cJSON 节点指针 */
    const char* name;   /**< 节点名称 */
};

/* ========== 全局变量 ========== */
static cJSON* s_root = NULL;            /**< 根 JSON 对象 */
static bool s_initialized = false;      /**< 初始化标志 */
static dtree_node_t s_root_node;        /**< 根节点句柄 */

/* ========== 内部函数 ========== */

/**
 * @brief 从文件加载 JSON
 */
static cJSON* load_json_file(const char* filepath) {
    FILE* fp = fopen(filepath, "r");
    if (!fp) {
        LOGW(TAG, "Cannot open file: %s", filepath);
        return NULL;
    }
    
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    char* buffer = (char*)mem_malloc(size + 1);
    if (!buffer) {
        fclose(fp);
        LOGE(TAG, "Memory alloc failed for: %s", filepath);
        return NULL;
    }
    
    fread(buffer, 1, size, fp);
    buffer[size] = '\0';
    fclose(fp);
    
    cJSON* json = cJSON_Parse(buffer);
    mem_free(buffer);
    
    if (!json) {
        LOGE(TAG, "JSON parse error in: %s", filepath);
        LOGE(TAG, "Error: %s", cJSON_GetErrorPtr());
    }
    
    return json;
}

/**
 * @brief 加载设备树 JSON 文件（单棵树）
 */
static cJSON* load_device_tree(const char* dirpath, const char* filename) {
    char filepath[512];
    snprintf(filepath, sizeof(filepath), "%s/%s", dirpath, filename);

    cJSON* root = load_json_file(filepath);
    if (!root) {
        LOGE(TAG, "Device tree file not found or invalid: %s", filepath);
        return NULL;
    }

    if (!cJSON_IsObject(root)) {
        LOGE(TAG, "Device tree root is not a JSON object: %s", filepath);
        cJSON_Delete(root);
        return NULL;
    }

    LOGI(TAG, "Loaded device tree: %s", filepath);
    return root;
}

/* ========== 公共 API 实现 ========== */

dtree_err_t dtree_init(void) {
    if (s_initialized && s_root) {
        LOGI(TAG, "Already initialized");
        return DTREE_OK;
    }

    LOGI(TAG, "Initializing device tree...");

    /* 首选 A/B 槽（OTA 更新的设备树），失败回退固件内置 SPIFFS 文件 */
    uint8_t *json_text = NULL;
    size_t json_len = 0;
    int slot = -1;
    if (dtb_ab_load(&json_text, &json_len, &slot) == 0) {
        s_root = cJSON_Parse((char *)json_text);
        mem_free(json_text);
        if (!s_root) {
            LOGE(TAG, "slot %d JSON parse error: %s", slot,
                 cJSON_GetErrorPtr() ? cJSON_GetErrorPtr() : "?");
        } else {
            LOGI(TAG, "Loaded device tree from A/B slot %d", slot);
        }
    }

    if (!s_root) {
        LOGI(TAG, "Falling back to built-in %s/%s",
             DTREE_CONFIG_DIR, DTREE_CONFIG_FILE);
        s_root = load_device_tree(DTREE_CONFIG_DIR, DTREE_CONFIG_FILE);
    }
    if (!s_root) {
        LOGE(TAG, "Failed to load device tree file");
        return DTREE_ERR_IO;
    }
    
    // 初始化根节点
    s_root_node.json = s_root;
    s_root_node.name = "root";
    
    s_initialized = true;
    LOGI(TAG, "Device tree initialized successfully");
    
    // 打印加载的模块
    cJSON* item = NULL;
    LOGI(TAG, "Available modules:");
    cJSON_ArrayForEach(item, s_root) {
        const char* compat = NULL;
        cJSON* compat_node = cJSON_GetObjectItem(item, "compatible");
        if (compat_node && cJSON_IsString(compat_node)) {
            compat = compat_node->valuestring;
        }
        LOGI(TAG, "  - %s (%s)", item->string, compat ? compat : "unknown");
    }
    
    return DTREE_OK;
}

bool dtree_is_initialized(void) {
    return s_initialized && s_root != NULL;
}

dtree_node_t* dtree_get_root(void) {
    if (!s_initialized) {
        LOGW(TAG, "Not initialized");
        return NULL;
    }
    return &s_root_node;
}

/* 节点池 - 避免静态变量被覆盖 */
#define DTREE_NODE_POOL_SIZE 16
static dtree_node_t s_node_pool[DTREE_NODE_POOL_SIZE];
static int s_pool_index = 0;

/**
 * @brief 从节点池分配一个包装 cJSON 的节点
 *
 * name 取自 cJSON 的键（指向树内字符串，生命周期与设备树相同），
 * 避免引用调用方栈上的临时路径片段。
 */
static dtree_node_t* node_pool_alloc(cJSON* json, const char* fallback_name) {
    dtree_node_t* node = &s_node_pool[s_pool_index % DTREE_NODE_POOL_SIZE];
    s_pool_index++;
    node->json = json;
    node->name = (json && json->string) ? json->string : fallback_name;
    return node;
}

dtree_node_t* dtree_get_child(dtree_node_t* parent, const char* name) {
    if (!parent || !parent->json || !name) {
        return NULL;
    }

    cJSON* child = cJSON_GetObjectItem(parent->json, name);
    if (!child) {
        return NULL;
    }

    return node_pool_alloc(child, name);
}

dtree_node_t* dtree_get_node(const char* path) {
    if (!s_initialized || !path) {
        return NULL;
    }
    
    // 复制路径字符串（因为需要修改）
    char path_copy[256];
    strncpy(path_copy, path, sizeof(path_copy) - 1);
    path_copy[sizeof(path_copy) - 1] = '\0';
    
    // 从根节点开始遍历
    dtree_node_t* current = &s_root_node;
    char* token = strtok(path_copy, ".");
    
    while (token && current) {
        current = dtree_get_child(current, token);
        token = strtok(NULL, ".");
    }
    
    return current;
}

bool dtree_has_node(const char* path) {
    return dtree_get_node(path) != NULL;
}

bool dtree_has_property(dtree_node_t* node, const char* property) {
    if (!node || !node->json || !property) {
        return false;
    }
    
    cJSON* item = cJSON_GetObjectItem(node->json, property);
    return item != NULL;
}

const char* dtree_get_compatible(dtree_node_t* node) {
    const char* value = NULL;
    dtree_err_t err = dtree_get_string(node, "compatible", &value);
    if (err != DTREE_OK) {
        return NULL;
    }
    return value;
}

dtree_err_t dtree_get_int(dtree_node_t* node, const char* property, int32_t* value) {
    if (!node || !node->json || !property || !value) {
        return DTREE_ERR_PARAM;
    }
    
    cJSON* item = cJSON_GetObjectItem(node->json, property);
    if (!item) {
        LOGW(TAG, "Property '%s' not found", property);
        return DTREE_ERR_NOT_FOUND;
    }
    
    if (cJSON_IsNumber(item)) {
        *value = (int32_t)item->valueint;
        return DTREE_OK;
    }
    
    /* boolean: true→1, false→0 */
    if (cJSON_IsBool(item)) {
        *value = cJSON_IsTrue(item) ? 1 : 0;
        return DTREE_OK;
    }
    
    // 尝试解析十六进制字符串
    if (cJSON_IsString(item)) {
        const char* str = item->valuestring;
        if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
            *value = (int32_t)strtol(str, NULL, 16);
            return DTREE_OK;
        }
        *value = (int32_t)atoi(str);
        return DTREE_OK;
    }
    
    LOGW(TAG, "Property '%s' type mismatch, expected number", property);
    return DTREE_ERR_TYPE;
}

dtree_err_t dtree_get_uint(dtree_node_t* node, const char* property, uint32_t* value) {
    if (!node || !node->json || !property || !value) {
        return DTREE_ERR_PARAM;
    }
    
    cJSON* item = cJSON_GetObjectItem(node->json, property);
    if (!item) {
        LOGW(TAG, "Property '%s' not found", property);
        return DTREE_ERR_NOT_FOUND;
    }
    
    if (cJSON_IsNumber(item)) {
        *value = (uint32_t)item->valuedouble;
        return DTREE_OK;
    }
    
    if (cJSON_IsString(item)) {
        const char* str = item->valuestring;
        if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
            *value = (uint32_t)strtoul(str, NULL, 16);
            return DTREE_OK;
        }
        *value = (uint32_t)strtoul(str, NULL, 10);
        return DTREE_OK;
    }
    
    LOGW(TAG, "Property '%s' type mismatch, expected number", property);
    return DTREE_ERR_TYPE;
}

dtree_err_t dtree_get_string(dtree_node_t* node, const char* property, const char** value) {
    if (!node || !node->json || !property || !value) {
        return DTREE_ERR_PARAM;
    }
    
    cJSON* item = cJSON_GetObjectItem(node->json, property);
    if (!item) {
        LOGW(TAG, "Property '%s' not found", property);
        return DTREE_ERR_NOT_FOUND;
    }
    
    if (cJSON_IsString(item)) {
        *value = item->valuestring;
        return DTREE_OK;
    }
    
    LOGW(TAG, "Property '%s' type mismatch, expected string", property);
    return DTREE_ERR_TYPE;
}

dtree_err_t dtree_get_bool(dtree_node_t* node, const char* property, bool* value) {
    if (!node || !node->json || !property || !value) {
        return DTREE_ERR_PARAM;
    }
    
    cJSON* item = cJSON_GetObjectItem(node->json, property);
    if (!item) {
        LOGW(TAG, "Property '%s' not found", property);
        return DTREE_ERR_NOT_FOUND;
    }
    
    if (cJSON_IsBool(item)) {
        *value = cJSON_IsTrue(item);
        return DTREE_OK;
    }
    
    if (cJSON_IsString(item)) {
        const char* str = item->valuestring;
        *value = (strcmp(str, "true") == 0 || strcmp(str, "yes") == 0 || strcmp(str, "1") == 0);
        return DTREE_OK;
    }
    
    if (cJSON_IsNumber(item)) {
        *value = item->valueint != 0;
        return DTREE_OK;
    }
    
    LOGW(TAG, "Property '%s' type mismatch, expected boolean", property);
    return DTREE_ERR_TYPE;
}

dtree_err_t dtree_get_float(dtree_node_t* node, const char* property, float* value) {
    if (!node || !node->json || !property || !value) {
        return DTREE_ERR_PARAM;
    }
    
    cJSON* item = cJSON_GetObjectItem(node->json, property);
    if (!item) {
        LOGW(TAG, "Property '%s' not found", property);
        return DTREE_ERR_NOT_FOUND;
    }
    
    if (cJSON_IsNumber(item)) {
        *value = (float)item->valuedouble;
        return DTREE_OK;
    }
    
    if (cJSON_IsString(item)) {
        *value = (float)atof(item->valuestring);
        return DTREE_OK;
    }
    
    LOGW(TAG, "Property '%s' type mismatch, expected float", property);
    return DTREE_ERR_TYPE;
}

/* ========== 节点遍历（compatible / 父节点）实现 ========== */

/**
 * @brief 在对象子树中深度优先查找带指定 compatible 的节点
 */
static cJSON* find_compatible_json(cJSON* root, const char* compatible) {
    if (!cJSON_IsObject(root)) {
        return NULL;
    }

    cJSON* compat = cJSON_GetObjectItem(root, "compatible");
    if (compat && cJSON_IsString(compat) &&
        strcmp(compat->valuestring, compatible) == 0) {
        return root;
    }

    cJSON* item = NULL;
    cJSON_ArrayForEach(item, root) {
        if (cJSON_IsObject(item)) {
            cJSON* hit = find_compatible_json(item, compatible);
            if (hit) {
                return hit;
            }
        }
    }

    return NULL;
}

/**
 * @brief 查找包含 target 作为直接成员的最近对象祖先
 */
static cJSON* find_parent_json(cJSON* root, const cJSON* target) {
    if (!cJSON_IsObject(root)) {
        return NULL;
    }

    cJSON* item = NULL;
    cJSON_ArrayForEach(item, root) {
        if (item == target) {
            return root;
        }
        if (cJSON_IsObject(item)) {
            cJSON* hit = find_parent_json(item, target);
            if (hit) {
                return hit;
            }
        }
    }

    return NULL;
}

dtree_node_t* dtree_find_by_compatible(const char* compatible) {
    if (!s_initialized || !compatible) {
        return NULL;
    }

    cJSON* hit = find_compatible_json(s_root, compatible);
    if (!hit) {
        LOGW(TAG, "No node with compatible '%s'", compatible);
        return NULL;
    }

    return node_pool_alloc(hit, compatible);
}

dtree_node_t* dtree_get_parent(dtree_node_t* node) {
    if (!s_initialized || !node || !node->json) {
        return NULL;
    }

    if (node->json == s_root) {
        return NULL;    /* 根节点没有父节点 */
    }

    cJSON* parent = find_parent_json(s_root, node->json);
    if (!parent) {
        return NULL;
    }

    return node_pool_alloc(parent, "root");
}

const char* dtree_get_node_name(dtree_node_t* node) {
    return (node && node->name) ? node->name : NULL;
}

dtree_err_t dtree_get_host_id(dtree_node_t* node, const char* prefix, int32_t* id) {
    if (!node || !node->name || !prefix || !id) {
        return DTREE_ERR_PARAM;
    }

    const char* value = node->name;
    size_t prefix_len = strlen(prefix);
    if (strncmp(value, prefix, prefix_len) != 0) {
        LOGE(TAG, "Node name '%s' does not match prefix '%s'", value, prefix);
        return DTREE_ERR_TYPE;
    }

    const char* num = value + prefix_len;
    if (*num == '\0') {
        LOGE(TAG, "Node name '%s' has no numeric id", value);
        return DTREE_ERR_TYPE;
    }

    for (const char* p = num; *p != '\0'; p++) {
        if (*p < '0' || *p > '9') {
            LOGE(TAG, "Node name '%s' is not '<prefix><number>'", value);
            return DTREE_ERR_TYPE;
        }
    }

    *id = (int32_t)atoi(num);
    return DTREE_OK;
}

/* ========== 数组 API 实现 ========== */

int dtree_get_array_size(dtree_node_t* node, const char* property) {
    if (!node || !node->json || !property) {
        return -1;
    }
    
    cJSON* array = cJSON_GetObjectItem(node->json, property);
    if (!array || !cJSON_IsArray(array)) {
        return -1;
    }
    
    return cJSON_GetArraySize(array);
}

dtree_node_t* dtree_get_array_item(dtree_node_t* node, const char* property, int index) {
    if (!node || !node->json || !property) {
        return NULL;
    }
    
    cJSON* array = cJSON_GetObjectItem(node->json, property);
    if (!array || !cJSON_IsArray(array)) {
        return NULL;
    }
    
    cJSON* item = cJSON_GetArrayItem(array, index);
    if (!item) {
        return NULL;
    }
    
    static dtree_node_t child_node;
    child_node.json = item;
    child_node.name = property;
    
    return &child_node;
}

int dtree_array_size(const char* path) {
    if (!s_initialized || !path) {
        return -1;
    }
    
    dtree_node_t* node = dtree_get_node(path);
    if (!node || !node->json) {
        LOGW(TAG, "dtree_array_size: node not found for path '%s'", path);
        return -1;
    }
    
    /* 路径本身就是数组 */
    if (cJSON_IsArray(node->json)) {
        int size = cJSON_GetArraySize(node->json);
        LOGI(TAG, "dtree_array_size: '%s' is array, size=%d", path, size);
        return size;
    }
    
    LOGW(TAG, "dtree_array_size: '%s' is NOT array (type=%d)", path, node->json->type);
    return -1;
}

dtree_node_t* dtree_array_item(const char* path, int index) {
    if (!s_initialized || !path) {
        return NULL;
    }
    
    dtree_node_t* node = dtree_get_node(path);
    if (!node || !node->json) {
        return NULL;
    }
    
    /* 路径本身就是数组 */
    if (cJSON_IsArray(node->json)) {
        cJSON* item = cJSON_GetArrayItem(node->json, index);
        if (!item) {
            return NULL;
        }
        
        static dtree_node_t child_node;
        child_node.json = item;
        child_node.name = path;
        
        return &child_node;
    }
    
    return NULL;
}
