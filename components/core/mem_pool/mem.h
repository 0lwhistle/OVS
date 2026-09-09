/**
 * @file mem.h
 * @brief mem_pool 核心内存管理 —— 堆替换层（drop-in）+ 显式能力层
 *
 * 设计文档: docs/superpowers/specs/2026-09-09-mem-pool-design.md
 *
 * 用法（迁移存量 malloc/free 代码）:
 *   1. .c 文本替换: malloc(→mem_malloc(  calloc(→mem_calloc(
 *                    realloc(→mem_realloc(  free(→mem_free(
 *   2. 组件 CMakeLists 加一行标签（可选，缺省落 SYS 桶）:
 *        target_compile_definitions(${COMPONENT_LIB} PRIVATE MEM_MODULE_TAG=MEM_MOD_NET)
 *
 * 铁律: ISR 上下文禁止调用本头文件任何接口。
 */

#ifndef MEM_H
#define MEM_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#if defined(ESP_PLATFORM)
#include "esp_heap_caps.h"      /* MALLOC_CAP_DMA / MALLOC_CAP_SPIRAM 真实值 */
#else
#ifndef MALLOC_CAP_DMA
#define MALLOC_CAP_DMA   0x08
#endif
#ifndef MALLOC_CAP_SPIRAM
#define MALLOC_CAP_SPIRAM 0x400
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ========== 模块归属枚举（只增不删） ========== */
typedef enum {
    MEM_MOD_SYS = 0,   /* 未标注/核心杂项 */
    MEM_MOD_APP,       /* main/app 编排层 */
    MEM_MOD_NET,       /* net_mgr + wifi 驱动 */
    MEM_MOD_WEB,       /* web + mongoose */
    MEM_MOD_OTA,
    MEM_MOD_VFS,       /* ovs_vfs + w25q128 + internal_flash */
    MEM_MOD_DTREE,
    MEM_MOD_LVGL,      /* 预留: LV_STDLIB_CUSTOM 收编后启用 */
    MEM_MOD_AUDIO,
    MEM_MOD_MEDIA,
    MEM_MOD_LORA,
    MEM_MOD_TIME,
    MEM_MOD_POWER,
    MEM_MOD_PROV,      /* ble_prov */
    MEM_MOD_CORE,      /* event_bus / tasker / holder 等核心服务 */
    MEM_MOD_DRV,       /* 总线/外设驱动层 */
    MEM_MOD_COUNT
} mem_module_t;

/* ========== 单模块统计 ========== */
typedef struct {
    uint32_t cur;      /* 当前占用字节（含 8B 头开销） */
    uint32_t peak;     /* 峰值字节 */
    uint32_t allocs;   /* 累计分配笔数 */
    uint32_t frees;    /* 累计释放笔数 */
    uint32_t fails;    /* 累计分配失败笔数 */
} mem_mod_stat_t;

/* ========== 核心实现（mem.c，一般勿直接调用；测试/特殊归属时可用） ========== */
void* mem_alloc_  (mem_module_t mod, size_t size);
void* mem_calloc_ (mem_module_t mod, size_t n, size_t size);
void* mem_realloc_(mem_module_t mod, void* ptr, size_t size);
void  mem_free_   (void* ptr);      /* 归属从分配头读取，不需要标签 */
void* mem_heap_alloc_(mem_module_t mod, size_t size, uint32_t caps); /* PC 忽略 caps */
void  mem_heap_free_ (void* ptr);   /* 与 mem_free_ 等价（同一套头校验） */

/* ========== drop-in 层（签名与 libc 一致，文本替换目标） ========== */
#ifndef MEM_MODULE_TAG
#define MEM_MODULE_TAG MEM_MOD_SYS
#endif

static inline void* mem_malloc(size_t size) {
    return mem_alloc_((mem_module_t)MEM_MODULE_TAG, size);
}
static inline void* mem_calloc(size_t n, size_t size) {
    return mem_calloc_((mem_module_t)MEM_MODULE_TAG, n, size);
}
static inline void* mem_realloc(void* ptr, size_t size) {
    return mem_realloc_((mem_module_t)MEM_MODULE_TAG, ptr, size);
}
static inline void mem_free(void* ptr) {
    mem_free_(ptr);
}

/* ========== 显式能力层（DMA/PSRAM，调用点少，手动迁移） ========== */
static inline void* mem_heap_alloc(size_t size, uint32_t caps) {
    return mem_heap_alloc_((mem_module_t)MEM_MODULE_TAG, size, caps);
}
static inline void* mem_dma_alloc(size_t size) {
    return mem_heap_alloc_((mem_module_t)MEM_MODULE_TAG, size, MALLOC_CAP_DMA);
}
static inline void* mem_psram_alloc(size_t size) {
    return mem_heap_alloc_((mem_module_t)MEM_MODULE_TAG, size, MALLOC_CAP_SPIRAM);
}

/* ========== 统计与诊断 ========== */
void mem_stat_print(void);
const mem_mod_stat_t* mem_stat_get(mem_module_t mod);
void mem_strict_set(bool on);       /* true: 头校验失败直接 abort（测试/抓虫用），默认 false */
bool mem_strict_active(void);       /* 查询当前 strict 状态 */

#ifdef __cplusplus
}
#endif

#endif /* MEM_H */
