/**
 * @file dtree_mock.c
 * @brief 设备树 mock 实现（ovs_tests 音频门禁用）
 *
 * 只实现音频路径用到的 3 个 API（find_by_compatible/get_parent/get_int），
 * 节点以 compatible 字符串标识；测试用 dtree_mock_set_int 预置键值，
 * 未预置的键返回 DTREE_ERR_NOT_FOUND（驱动走缺省兜底路径）。
 */

#include "dtree.h"

#include <string.h>

#define DTREE_MOCK_MAX_ENTRIES 16

struct dtree_node {
    const char* compat;
};

static struct dtree_node s_nodes[] = {
    { "i2s-microphone" },
    { "i2s-amplifier" },
    { "ovs-audio-policy" },
};
#define DTREE_MOCK_NODE_COUNT (sizeof(s_nodes) / sizeof(s_nodes[0]))

typedef struct {
    const struct dtree_node* node;
    const char* prop;
    int32_t value;
} dtree_mock_entry_t;

static dtree_mock_entry_t s_entries[DTREE_MOCK_MAX_ENTRIES];
static int s_entry_count = 0;

void dtree_mock_reset(void) {
    s_entry_count = 0;
}

void dtree_mock_set_int(const char* compat, const char* prop, int32_t value) {
    if (s_entry_count >= DTREE_MOCK_MAX_ENTRIES) {
        return;
    }
    s_entries[s_entry_count].node = (const struct dtree_node*)compat;
    s_entries[s_entry_count].prop = prop;
    s_entries[s_entry_count].value = value;
    s_entry_count++;
}

dtree_node_t* dtree_find_by_compatible(const char* compatible) {
    if (!compatible) {
        return NULL;
    }
    for (unsigned i = 0; i < DTREE_MOCK_NODE_COUNT; i++) {
        if (strcmp(s_nodes[i].compat, compatible) == 0) {
            return &s_nodes[i];
        }
    }
    return NULL;
}

dtree_node_t* dtree_get_parent(dtree_node_t* node) {
    return node;   /* mock 单层即可满足 audio_module 的 bus 定位 */
}

dtree_err_t dtree_get_int(dtree_node_t* node, const char* property, int32_t* value) {
    if (!node || !property || !value) {
        return DTREE_ERR_PARAM;
    }
    for (int i = 0; i < s_entry_count; i++) {
        if (s_entries[i].node == node && strcmp(s_entries[i].prop, property) == 0) {
            *value = s_entries[i].value;
            return DTREE_OK;
        }
    }
    return DTREE_ERR_NOT_FOUND;
}
