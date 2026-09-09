/**
 * @file app_init.c
 * @brief 应用初始化注册表实现（holder 依赖拓扑编排，REFACTORING_PLAN 4.4）
 *
 * 批次与依赖（required 仅核心+存储，其余 optional 失败降级）:
 *   [批0] event_bus → tasker → dtree                (required)
 *   [批1] w25q128(dep dtree) → ovs_vfs(dep 两者)     (required)
 *   [批2] net_stack(dep dtree, 由 main.c 注入)       (required)
 *   [批3] st7789 / cst816s / aht30 (dep dtree)       (optional——失败无屏/无传感器仍可 Web 管理)
 *   [批4] heartbeat (dep net_stack)                  (optional——C12 接线修复)
 *   [批5] lvgl_app (dep dtree；真实屏幕依赖待 6.2 后补 st7789+cst816s) (optional)
 *
 * 降级验收: 编译期 -DOVS_FORCE_FAIL_MODULE='"模块名"' 可令指定 optional
 * 模块 init 强制失败（验收"拔掉任一可选模块系统降级运行"）。
 */

#include "app_init.h"
#include "holder.h"
#include "event_bus.h"
#include "tasker.h"
#include "dtree.h"
#include "logger.h"
#include "mem.h"

#include "w25q128.h"
#include "w25q128_vfs.h"
#include "internal_flash_vfs.h"
#include "ovs_vfs.h"

#include "st7789.h"
#include "cst816s.h"
#include "aht30.h"
#include "heartbeat.h"
#include "lvgl_app.h"

#include <string.h>
#include <stdio.h>
#include <inttypes.h>

static const char* TAG = "[APP_INIT]";

/* ===== OTA 线保护区桥接（main.c 注入，本文件不 include 保护区内容） ===== */
static void (*s_net_stack_fn)(void) = NULL;

void app_init_set_net_stack(void (*fn)(void)) {
    s_net_stack_fn = fn;
}

/* ===== 降级验收钩子 ===== */
#ifndef OVS_FORCE_FAIL_MODULE
#define OVS_FORCE_FAIL_MODULE ""
#endif

static inline int force_fail_check(const char* name) {
    return (OVS_FORCE_FAIL_MODULE[0] != '\0' && strcmp(name, OVS_FORCE_FAIL_MODULE) == 0) ? -1 : 0;
}

/* ===== 模块初始化函数 ===== */

static int mod_event_bus(void) {
    if (force_fail_check("event_bus")) return -1;
    return (event_bus_init() == EVENT_BUS_OK) ? 0 : -1;
}

static int mod_tasker(void) {
    if (force_fail_check("tasker")) return -1;
    return (tasker_init() == TASK_OK) ? 0 : -1;
}

static int mod_dtree(void) {
    if (force_fail_check("dtree")) return -1;
    dtree_err_t ret = dtree_init();
    if (ret != DTREE_OK) {
        LOGE(TAG, "Device tree init failed: %d", ret);
        return -1;
    }
    return 0;
}

static int mod_w25q128(void) {
    if (force_fail_check("w25q128")) return -1;
    w25q128_err_t ret = w25q128_init();
    if (ret != W25Q128_OK) {
        LOGE(TAG, "W25Q128 init failed: %d", ret);
        return -1;
    }
    return 0;
}

/**
 * VFS 栈与挂载（自 main.c vfs_stack_start 迁入，逻辑不变）
 * 注册块设备并按设备树 vfs 节点挂载（无 vfs 节点时用默认布局）
 */
static int mod_vfs(void) {
    if (force_fail_check("ovs_vfs")) return -1;

    vfs_err_t ret = vfs_init();
    if (ret != VFS_OK) {
        LOGE(TAG, "VFS init failed: %d", ret);
        return -1;
    }

    w25q128_register_vfs();
    internal_flash_register_vfs("littlefs");

    if (!dtree_has_node("vfs")) {
        LOGW(TAG, "Device tree 'vfs' node not found, using default layout");
        vfs_mount_littlefs("/audio", "w25q128", 0, 8 * 1024 * 1024, true);
        vfs_mount_littlefs("/font", "w25q128", 8 * 1024 * 1024, 8 * 1024 * 1024, true);
        vfs_mount_littlefs("/config", "internal", 0, 0, true);
        return 0;
    }

    int array_size = dtree_array_size("vfs.mounts");
    if (array_size <= 0) {
        LOGW(TAG, "No mounts configured in device tree");
        return 0;
    }

    int mount_count = 0;
    for (int i = 0; i < array_size; i++) {
        dtree_node_t* item = dtree_array_item("vfs.mounts", i);
        if (!item) {
            LOGW(TAG, "Failed to get mount config at index %d", i);
            continue;
        }

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

        ret = vfs_mount_littlefs(mount_path, device, offset, size, format_if_fail);
        if (ret == VFS_OK) {
            mount_count++;
            LOGI(TAG, "VFS mounted: %s (%s @+%" PRId32 ", %" PRId32 "KB)",
                 mount_path, device, offset, size / 1024);
        } else {
            LOGE(TAG, "VFS mount failed: %s (err=%d)", mount_path, ret);
            LOGW(TAG, "提示：若刚变更过分区布局，置 OVS_MEDIA_FORMAT_ON_FIRST_BOOT=1 烧录一次");
        }
    }
    LOGI(TAG, "VFS mounts ready: %d/%d", mount_count, array_size);
    return 0;
}

static int mod_net_stack(void) {
    if (s_net_stack_fn == NULL) {
        return -1;
    }
    s_net_stack_fn();
    return 0;
}

static int mod_st7789(void) {
    if (force_fail_check("st7789")) return -1;
    return (st7789_init() == ST7789_OK) ? 0 : -1;
}

static int mod_cst816s(void) {
    if (force_fail_check("cst816s")) return -1;
    return (cst816s_init() == CST816S_OK) ? 0 : -1;
}

static int mod_aht30(void) {
    if (force_fail_check("aht30")) return -1;
    return (aht30_init() == AHT30_OK) ? 0 : -1;
}

static int mod_heartbeat(void) {
    if (force_fail_check("heartbeat")) return -1;
    return (heartbeat_init() == 0) ? 0 : -1;
}

static int mod_lvgl_app(void) {
    if (force_fail_check("lvgl_app")) return -1;
    return (lvgl_app_init() == LVGL_APP_OK) ? 0 : -1;
}

/* ===== 依赖表（holder 保存指针，须静态存储期） ===== */
static const char* const DEPS_NONE[] = {NULL};
static const char* const DEPS_EVENT_BUS[] = {"event_bus", NULL};
static const char* const DEPS_TASKER[] = {"event_bus", "tasker", NULL};
static const char* const DEPS_DTREE[] = {"event_bus", "tasker", "dtree", NULL};
static const char* const DEPS_VFS[] = {"dtree", "w25q128", NULL};
static const char* const DEPS_NET[] = {"dtree", NULL};
static const char* const DEPS_HEARTBEAT[] = {"net_stack", NULL};

int app_init_setup(void) {
    if (holder_init() != HOLDER_OK) {
        LOGE(TAG, "holder init failed");
        return -1;
    }

    /* [批0] 核心 */
    holder_register_module_ex("event_bus", mod_event_bus, true, DEPS_NONE, 0, NULL);
    holder_register_module_ex("tasker", mod_tasker, true, DEPS_EVENT_BUS, 1, NULL);
    holder_register_module_ex("dtree", mod_dtree, true, DEPS_TASKER, 2, NULL);

    /* [批1] 存储 */
    holder_register_module_ex("w25q128", mod_w25q128, true, DEPS_DTREE, 1, NULL);
    holder_register_module_ex("ovs_vfs", mod_vfs, true, DEPS_VFS, 2, NULL);

    /* [批2] 网络/OTA/Web（OTA 线保护区：net_stack_init 内部顺序不变，
     *  函数指针由 main.c 注入；OVS_ENABLE_NET=0 时本模块不注册） */
    if (s_net_stack_fn != NULL) {
        holder_register_module_ex("net_stack", mod_net_stack, true, DEPS_NET, 1, NULL);
    }

    /* [批3] 人机外设（optional：失败降级为无屏/无触摸/无传感器，Web 仍可用） */
    holder_register_module_ex("st7789", mod_st7789, false, DEPS_DTREE, 1, NULL);
    holder_register_module_ex("cst816s", mod_cst816s, false, DEPS_DTREE, 1, NULL);
    holder_register_module_ex("aht30", mod_aht30, false, DEPS_DTREE, 1, NULL);

    /* [批4] 服务 */
    if (s_net_stack_fn != NULL) {
        holder_register_module_ex("heartbeat", mod_heartbeat, false, DEPS_HEARTBEAT, 1, NULL);
    }

    /* [批5] 应用 */
    holder_register_module_ex("lvgl_app", mod_lvgl_app, false, DEPS_DTREE, 1, NULL);

    return 0;
}

int app_init_run(void) {
    int rc = holder_init_all(true);   /* required 失败即停（R9: OTA 15s 回滚兜底） */
    holder_print_status();            /* 验收: 各模块 init 耗时/状态一览 */
    event_bus_print_subscribers();    /* 验收: 订阅者清单（≥6） */
    return rc;
}
