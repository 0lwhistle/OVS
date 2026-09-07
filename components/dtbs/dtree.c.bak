/**
 * @file dtree.c
 * @brief 设备树解析器实现
 * 
 * 使用 cJSON 解析 JSON 配置文件，为各驱动模块提供硬件配置读取。
 */

#include "dtree.h"
#include "logger.h"
#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

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
    
    char* buffer = (char*)malloc(size + 1);
    if (!buffer) {
        fclose(fp);
        LOGE(TAG, "Memory alloc failed for: %s", filepath);
        return NULL;
    }
    
    fread(buffer, 1, size, fp);
    buffer[size] = '\0';
    fclose(fp);
    
    cJSON* json = cJSON_Parse(buffer);
    free(buffer);
    
    if (!json) {
        LOGE(TAG, "JSON parse error in: %s", filepath);
        LOGE(TAG, "Error: %s", cJSON_GetErrorPtr());
    }
    
    return json;
}

/**
 * @brief 合并 JSON 对象
 */
static void merge_json_objects(cJSON* dest, cJSON* src) {
    if (!dest || !src || !cJSON_IsObject(dest) || !cJSON_IsObject(src)) {
        return;
    }
    
    cJSON* item = NULL;
    cJSON_ArrayForEach(item, src) {
        cJSON* existing = cJSON_GetObjectItem(dest, item->string);
        if (existing) {
            // 如果两边都是对象，递归合并
            if (cJSON_IsObject(existing) && cJSON_IsObject(item)) {
                merge_json_objects(existing, item);
            }
        } else {
            // 复制并添加到目标
            cJSON* copy = cJSON_Duplicate(item, 1);
            if (copy) {
                cJSON_AddItemToObject(dest, item->string, copy);
            }
        }
    }
}

/**
 * @brief 加载目录下所有 JSON 文件
 */
static cJSON* load_all_json_files(const char* dirpath) {
    cJSON* root = cJSON_CreateObject();
    if (!root) {
        LOGE(TAG, "Failed to create root JSON object");
        return NULL;
    }
    
    DIR* dir = opendir(dirpath);
    if (!dir) {
        LOGW(TAG, "Config directory not found: %s", dirpath);
        LOGI(TAG, "Using built-in default configs");
        return root;
    }
    
    struct dirent* entry;
    int file_count = 0;
    
    while ((entry = readdir(dir)) != NULL) {
        // 只处理 .json 文件
        char* ext = strrchr(entry->d_name, '.');
        if (!ext || strcmp(ext, ".json") != 0) {
            continue;
        }
        
        // 构造完整路径
        char filepath[512];
        snprintf(filepath, sizeof(filepath), "%s/%s", dirpath, entry->d_name);
        
        // 加载 JSON
        cJSON* file_json = load_json_file(filepath);
        if (file_json) {
            // 使用文件名（不含扩展名）作为键
            char key[64];
            strncpy(key, entry->d_name, sizeof(key) - 1);
            ext = strrchr(key, '.');
            if (ext) *ext = '\0';
            
            cJSON_AddItemToObject(root, key, file_json);
            file_count++;
            LOGI(TAG, "Loaded: %s", entry->d_name);
        }
    }
    
    closedir(dir);
    LOGI(TAG, "Loaded %d config files from %s", file_count, dirpath);
    
    return root;
}

/* ========== 公共 API 实现 ========== */

dtree_err_t dtree_init(void) {
    if (s_initialized && s_root) {
        LOGI(TAG, "Already initialized");
        return DTREE_OK;
    }
    
    LOGI(TAG, "Initializing device tree...");
    
    // 加载所有 JSON 配置文件
    s_root = load_all_json_files(DTREE_CONFIG_DIR);
    if (!s_root) {
        LOGE(TAG, "Failed to load config files");
        return DTREE_ERR_NOT_INIT;
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

dtree_node_t* dtree_get_root(void) {
    if (!s_initialized) {
        LOGW(TAG, "Not initialized");
        return NULL;
    }
    return &s_root_node;
}

dtree_node_t* dtree_get_child(dtree_node_t* parent, const char* name) {
    if (!parent || !parent->json || !name) {
        return NULL;
    }
    
    cJSON* child = cJSON_GetObjectItem(parent->json, name);
    if (!child) {
        return NULL;
    }
    
    // 使用静态变量返回（单线程环境足够）
    static dtree_node_t child_node;
    child_node.json = child;
    child_node.name = name;
    
    return &child_node;
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

const char* dtree_get_compatible(dtree_node_t* node) {
    return dtree_get_string(node, "compatible");
}

int32_t dtree_get_int(dtree_node_t* node, const char* property, int32_t default_val) {
    if (!node || !node->json || !property) {
        return default_val;
    }
    
    cJSON* item = cJSON_GetObjectItem(node->json, property);
    if (!item) {
        return default_val;
    }
    
    if (cJSON_IsNumber(item)) {
        return (int32_t)item->valueint;
    }
    
    // 尝试解析十六进制字符串
    if (cJSON_IsString(item)) {
        const char* str = item->valuestring;
        if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
            return (int32_t)strtol(str, NULL, 16);
        }
        return (int32_t)atoi(str);
    }
    
    return default_val;
}

uint32_t dtree_get_uint(dtree_node_t* node, const char* property, uint32_t default_val) {
    if (!node || !node->json || !property) {
        return default_val;
    }
    
    cJSON* item = cJSON_GetObjectItem(node->json, property);
    if (!item) {
        return default_val;
    }
    
    if (cJSON_IsNumber(item)) {
        return (uint32_t)item->valuedouble;
    }
    
    if (cJSON_IsString(item)) {
        const char* str = item->valuestring;
        if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
            return (uint32_t)strtoul(str, NULL, 16);
        }
        return (uint32_t)strtoul(str, NULL, 10);
    }
    
    return default_val;
}

const char* dtree_get_string(dtree_node_t* node, const char* property) {
    if (!node || !node->json || !property) {
        return NULL;
    }
    
    cJSON* item = cJSON_GetObjectItem(node->json, property);
    if (item && cJSON_IsString(item)) {
        return item->valuestring;
    }
    
    return NULL;
}

bool dtree_get_bool(dtree_node_t* node, const char* property, bool default_val) {
    if (!node || !node->json || !property) {
        return default_val;
    }
    
    cJSON* item = cJSON_GetObjectItem(node->json, property);
    if (!item) {
        return default_val;
    }
    
    if (cJSON_IsBool(item)) {
        return cJSON_IsTrue(item);
    }
    
    if (cJSON_IsString(item)) {
        const char* str = item->valuestring;
        return (strcmp(str, "true") == 0 || strcmp(str, "yes") == 0 || strcmp(str, "1") == 0);
    }
    
    if (cJSON_IsNumber(item)) {
        return item->valueint != 0;
    }
    
    return default_val;
}

float dtree_get_float(dtree_node_t* node, const char* property, float default_val) {
    if (!node || !node->json || !property) {
        return default_val;
    }
    
    cJSON* item = cJSON_GetObjectItem(node->json, property);
    if (item && cJSON_IsNumber(item)) {
        return (float)item->valuedouble;
    }
    
    if (item && cJSON_IsString(item)) {
        return (float)atof(item->valuestring);
    }
    
    return default_val;
}
