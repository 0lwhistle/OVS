/**
 * @file mem_pool.h
 * @brief mem_pool —— 定长块池（热路径固定尺寸对象，零元数据开销）
 *
 * 实现: 侵入式单链空闲栈（LIFO），空闲块自身存放 next 指针。
 * 铁律: 块指针必须用 mem_pool_free 释放（不可传给 mem_free，会被 magic 检查拦截）。
 */

#ifndef MEM_POOL_H
#define MEM_POOL_H

#include <stddef.h>
#include "mem.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mem_pool mem_pool_t;

/**
 * @brief 创建块池（存储与元数据均计入 owner 模块账目）
 * @param name 池名（统计打印用，复制内部，最长 15 字符）
 * @param block_size 块大小（自动向上取整到指针对齐）
 * @param blocks 块数量
 * @return 池句柄；失败返回 NULL
 */
mem_pool_t* mem_pool_create(const char* name, size_t block_size, size_t blocks,
                            mem_module_t owner);

/**
 * @brief 取一块（O(1)）；池空返回 NULL（不阻塞）
 */
void* mem_pool_alloc(mem_pool_t* pool);

/**
 * @brief 归还一块；范围/对齐/重复归还校验，违规 LOGE 拒绝（strict 时 abort）
 */
void mem_pool_free(mem_pool_t* pool, void* blk);

/* 统计（in_use/peak 为块数） */
uint32_t mem_pool_in_use(mem_pool_t* pool);
uint32_t mem_pool_peak_use(mem_pool_t* pool);

#ifdef __cplusplus
}
#endif

#endif /* MEM_POOL_H */
