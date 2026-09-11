/**
 * @file i18n.c
 * @brief 契约 I1 多语言服务实现（cJSON 解析 flat KV，未命中回退原文）
 */

#include "i18n.h"
#include "logger.h"
#include "mem.h"

#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char* TAG = "[I18N]";

#define I18N_PATH_MAX     160
#define I18N_LANG_MAX     12
#define I18N_MISS_MAX     32   /* 节流：最多记住 32 个已告警 miss label */
#define I18N_FILE_MAX     (128 * 1024)

/** 词条（深拷贝存储，cJSON 树用完即毁） */
typedef struct {
    char* label;
    char* text;
} i18n_entry_t;

static i18n_entry_t* s_entries = NULL;
static int s_entry_count = 0;
static char s_lang[I18N_LANG_MAX] = "";
static char s_base_path[96] = I18N_DEFAULT_BASE_PATH;
static bool s_loaded = false;

/* 节流 miss 告警：每种 label 只 LOGW 一次 */
static char s_miss_logged[I18N_MISS_MAX][32];
static int s_miss_count = 0;

static char* dup_str(const char* src) {
    size_t n = strlen(src) + 1;
    char* p = (char*)mem_malloc(n);
    if (p) {
        memcpy(p, src, n);
    }
    return p;
}

static void free_entries(void) {
    for (int i = 0; i < s_entry_count; i++) {
        mem_free(s_entries[i].label);
        mem_free(s_entries[i].text);
    }
    if (s_entries) {
        mem_free(s_entries);
        s_entries = NULL;
    }
    s_entry_count = 0;
}

/** bsearch 比较 */
static int entry_cmp(const void* a, const void* b) {
    return strcmp(((const i18n_entry_t*)a)->label, ((const i18n_entry_t*)b)->label);
}

static void throttle_miss_log(const char* label) {
    for (int i = 0; i < s_miss_count; i++) {
        if (strcmp(s_miss_logged[i], label) == 0) {
            return;
        }
    }
    if (s_miss_count < I18N_MISS_MAX) {
        strncpy(s_miss_logged[s_miss_count], label, sizeof(s_miss_logged[0]) - 1);
        s_miss_logged[s_miss_count][sizeof(s_miss_logged[0]) - 1] = '\0';
        s_miss_count++;
        LOGW(TAG, "label not found: %s (lang=%s)", label, s_lang);
    }
}

static i18n_err_t load_language(const char* lang) {
    char path[I18N_PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s.json", s_base_path, lang);

    FILE* fp = fopen(path, "rb");
    if (!fp) {
        LOGE(TAG, "language pack not found: %s", path);
        return I18N_ERR_IO;
    }

    i18n_err_t ret = I18N_OK;
    char* buf = NULL;
    cJSON* root = NULL;

    if (fseek(fp, 0, SEEK_END) != 0) { ret = I18N_ERR_IO; goto cleanup; }
    long size = ftell(fp);
    if (size <= 0 || size > I18N_FILE_MAX) {
        LOGE(TAG, "bad language pack size %ld: %s", size, path);
        ret = I18N_ERR_IO;
        goto cleanup;
    }
    rewind(fp);

    buf = (char*)mem_malloc((size_t)size + 1);
    if (!buf) {
        LOGE(TAG, "oom for language pack (%ld B)", size);
        ret = I18N_ERR_NOMEM;
        goto cleanup;
    }
    if (fread(buf, 1, (size_t)size, fp) != (size_t)size) {
        ret = I18N_ERR_IO;
        goto cleanup;
    }
    buf[size] = '\0';

    root = cJSON_Parse(buf);
    if (!root) {
        LOGE(TAG, "json parse failed: %s", path);
        ret = I18N_ERR_FORMAT;
        goto cleanup;
    }
    cJSON* strings = cJSON_GetObjectItemCaseSensitive(root, "strings");
    if (!cJSON_IsObject(strings)) {
        LOGE(TAG, "missing 'strings' object: %s", path);
        ret = I18N_ERR_FORMAT;
        goto cleanup;
    }

    {
        int n = cJSON_GetArraySize(strings);
        i18n_entry_t* arr = (i18n_entry_t*)mem_malloc(sizeof(i18n_entry_t) * (size_t)(n > 0 ? n : 1));
        if (!arr) {
            ret = I18N_ERR_NOMEM;
            goto cleanup;
        }
        int k = 0;
        cJSON* it = NULL;
        cJSON_ArrayForEach(it, strings) {
            if (!cJSON_IsString(it) || !it->string) {
                continue;   /* 非法值跳过，不让整包失效 */
            }
            arr[k].label = dup_str(it->string);
            arr[k].text = dup_str(cJSON_GetStringValue(it));
            if (!arr[k].label || !arr[k].text) {
                ret = I18N_ERR_NOMEM;
                mem_free(arr[k].label);
                mem_free(arr[k].text);
                break;
            }
            k++;
        }
        if (ret == I18N_OK) {
            free_entries();   /* 切换语言：换掉旧表 */
            s_entries = arr;
            s_entry_count = k;
            qsort(s_entries, (size_t)k, sizeof(i18n_entry_t), entry_cmp);
            snprintf(s_lang, sizeof(s_lang), "%s", lang);
            s_loaded = true;
            LOGI(TAG, "language pack loaded: %s (%d labels)", path, k);
        } else {
            for (int j = 0; j < k; j++) {
                mem_free(arr[j].label);
                mem_free(arr[j].text);
            }
            mem_free(arr);
        }
    }

cleanup:
    if (root) cJSON_Delete(root);
    if (buf) mem_free(buf);
    fclose(fp);
    return ret;
}

i18n_err_t i18n_init(const char* default_lang) {
    if (s_loaded) {
        free_entries();
        s_loaded = false;
        s_lang[0] = '\0';
        s_miss_count = 0;
    }
    if (!default_lang || default_lang[0] == '\0') {
        return I18N_OK;
    }
    return load_language(default_lang);
}

i18n_err_t i18n_set_language(const char* lang) {
    if (!lang || lang[0] == '\0' || strlen(lang) >= I18N_LANG_MAX) {
        return I18N_ERR_PARAM;
    }
    i18n_err_t ret = load_language(lang);
    if (ret != I18N_OK) {
        LOGW(TAG, "switch to %s failed (%d), keep %s", lang, ret, s_lang);
    }
    return ret;
}

const char* i18n_get(const char* label) {
    if (!label) {
        return "";
    }
    if (!s_loaded || s_entry_count == 0) {
        return label;
    }
    i18n_entry_t key;
    key.label = (char*)label;
    i18n_entry_t* hit = (i18n_entry_t*)bsearch(&key, s_entries, (size_t)s_entry_count,
                                               sizeof(i18n_entry_t), entry_cmp);
    if (!hit) {
        throttle_miss_log(label);
        return label;
    }
    return hit->text;
}

const char* i18n_current(void) {
    return s_lang;
}

bool i18n_is_loaded(void) {
    return s_loaded;
}

void i18n_set_base_path(const char* path) {
    if (!path || path[0] == '\0' || strlen(path) >= sizeof(s_base_path)) {
        return;
    }
    snprintf(s_base_path, sizeof(s_base_path), "%s", path);
}

void i18n_deinit(void) {
    free_entries();
    s_loaded = false;
    s_lang[0] = '\0';
    s_miss_count = 0;
}
