/**
 * @file mem_pool.c
 * @brief 定长块池实现
 *
 * 池元数据与块存储都经 mem_alloc_ 计入 owner 模块账目。
 * 归还校验: 地址范围 + 块对齐 + 空闲链扫描（防重复归还，池小开销可忽略）。
 * 锁: 与 mem.c 共用同一把锁的语义（各自独立锁即可，临界区极短）。
 */

#include "mem_pool.h"
#include "logger.h"

#include <stdlib.h>
#include <string.h>

#if defined(ESP_PLATFORM)
#include "freertos/FreeRTOS.h"
static portMUX_TYPE s_pool_lock = portMUX_INITIALIZER_UNLOCKED;
#define POOL_LOCK()   portENTER_CRITICAL(&s_pool_lock)
#define POOL_UNLOCK() portEXIT_CRITICAL(&s_pool_lock)
#else
#include <pthread.h>
static pthread_mutex_t s_pool_lock = PTHREAD_MUTEX_INITIALIZER;
#define POOL_LOCK()   pthread_mutex_lock(&s_pool_lock)
#define POOL_UNLOCK() pthread_mutex_unlock(&s_pool_lock)
#endif

static const char* TAG = "[MEM]";

struct mem_pool {
    char     name[16];
    size_t   block_size;    /* 已对齐（≥sizeof(void*)） */
    size_t   blocks;
    uint32_t in_use;
    uint32_t peak;
    mem_module_t owner;
    uint8_t* storage;       /* 块存储区起点 */
    uint8_t* storage_end;   /* 终点（开区间） */
    void*    free_head;     /* 空闲链栈顶 */
};

mem_pool_t* mem_pool_create(const char* name, size_t block_size, size_t blocks,
                            mem_module_t owner) {
    if (!name || block_size == 0 || blocks == 0) {
        LOGE(TAG, "pool_create: invalid args");
        return NULL;
    }
    if (owner >= MEM_MOD_COUNT) {
        owner = MEM_MOD_SYS;
    }

    mem_pool_t* pool = (mem_pool_t*)mem_alloc_(owner, sizeof(mem_pool_t));
    if (!pool) {
        return NULL;
    }
    if (block_size < sizeof(void*)) {
        block_size = sizeof(void*);
    }
    /* 块大小向上取整到 void* 对齐，保证空闲链指针与块内容对齐安全 */
    block_size = (block_size + sizeof(void*) - 1) & ~(sizeof(void*) - 1);

    pool->block_size = block_size;
    pool->blocks = blocks;
    pool->in_use = 0;
    pool->peak = 0;
    pool->owner = owner;
    strncpy(pool->name, name, sizeof(pool->name) - 1);
    pool->name[sizeof(pool->name) - 1] = '\0';

    pool->storage = (uint8_t*)mem_alloc_(owner, block_size * blocks);
    if (!pool->storage) {
        mem_free_(pool);
        return NULL;
    }
    pool->storage_end = pool->storage + block_size * blocks;
    pool->free_head = NULL;
    for (size_t i = blocks; i > 0; i--) {
        void* blk = pool->storage + (i - 1) * block_size;
        *(void**)blk = pool->free_head;
        pool->free_head = blk;
    }

    LOGI(TAG, "pool '%s' ready: %u x %uB (owner=%d)",
         pool->name, (unsigned)blocks, (unsigned)block_size, (int)owner);
    return pool;
}

void* mem_pool_alloc(mem_pool_t* pool) {
    if (!pool) {
        return NULL;
    }
    void* blk;
    POOL_LOCK();
    if (pool->free_head) {
        blk = pool->free_head;
        pool->free_head = *(void**)blk;
        pool->in_use++;
        if (pool->in_use > pool->peak) {
            pool->peak = pool->in_use;
        }
    } else {
        blk = NULL;
    }
    POOL_UNLOCK();
    return blk;
}

void mem_pool_free(mem_pool_t* pool, void* blk) {
    if (!pool || !blk) {
        return;
    }

    /* 范围 + 对齐校验 */
    uint8_t* p = (uint8_t*)blk;
    if (p < pool->storage || p >= pool->storage_end ||
        (size_t)(p - pool->storage) % pool->block_size != 0) {
        if (mem_strict_active()) {
            LOGE(TAG, "pool '%s': foreign ptr %p -- STRICT abort", pool->name, blk);
            abort();
        }
        LOGE(TAG, "pool '%s': foreign ptr %p -- reject", pool->name, blk);
        return;
    }

    POOL_LOCK();
    /* 空闲链扫描防重复归还 */
    for (void* it = pool->free_head; it; it = *(void**)it) {
        if (it == blk) {
            POOL_UNLOCK();
            if (mem_strict_active()) {
                LOGE(TAG, "pool '%s': double free %p -- STRICT abort", pool->name, blk);
                abort();
            }
            LOGE(TAG, "pool '%s': double free %p -- reject", pool->name, blk);
            return;
        }
    }
    *(void**)blk = pool->free_head;
    pool->free_head = blk;
    pool->in_use--;
    POOL_UNLOCK();
}

uint32_t mem_pool_in_use(mem_pool_t* pool) {
    return pool ? pool->in_use : 0;
}

uint32_t mem_pool_peak_use(mem_pool_t* pool) {
    return pool ? pool->peak : 0;
}
