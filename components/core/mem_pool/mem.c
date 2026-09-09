/**
 * @file mem.c
 * @brief mem_pool 核心实现：分配头记账 + 防护 + 统计
 *
 * 布局: [mem_hdr_t 8B][负载]；free 按指针回退 8B 取头校验。
 * 锁策略: 统计/头字段进短临界区，实际 malloc/free 在锁外原样执行。
 * 平台: ESP=portMUX 自旋锁；其余(PC) =pthread 互斥量。
 */

#include "mem.h"
#include "logger.h"

#include <stdlib.h>
#include <string.h>

#if defined(ESP_PLATFORM)
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
#define MEM_LOCK()   portENTER_CRITICAL(&s_lock)
#define MEM_UNLOCK() portEXIT_CRITICAL(&s_lock)
#else
#include <pthread.h>
static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
#define MEM_LOCK()   pthread_mutex_lock(&s_lock)
#define MEM_UNLOCK() pthread_mutex_unlock(&s_lock)
#endif

static const char* TAG = "[MEM]";

/* ===== 分配头（8B，编译期锁定布局） ===== */
typedef struct {
    uint16_t magic;    /* MEM_MAGIC；free 前清零 → 立即可捕获 double-free */
    uint8_t  module;   /* mem_module_t */
    uint8_t  flags;    /* MEM_FLAG_* */
    uint32_t size;     /* 负载字节数 */
} mem_hdr_t;

#define MEM_MAGIC      0x4D45u
#define MEM_FLAG_HEAP  0x00u
#define MEM_FLAG_DMA   0x01u
#define MEM_FLAG_PSRAM 0x02u

_Static_assert(sizeof(mem_hdr_t) == 8, "mem_hdr_t must stay 8 bytes");
_Static_assert(_Alignof(mem_hdr_t) <= 8, "mem_hdr_t alignment must stay <= 8");

static mem_mod_stat_t s_stats[MEM_MOD_COUNT];
static bool s_strict = false;

static const char* module_name(mem_module_t mod) {
    static const char* names[MEM_MOD_COUNT] = {
        "SYS", "APP", "NET", "WEB", "OTA", "VFS", "DTREE", "LVGL",
        "AUDIO", "MEDIA", "LORA", "TIME", "POWER", "PROV",
    };
    return (mod < MEM_MOD_COUNT) ? names[mod] : "?";
}

void mem_strict_set(bool on) {
    s_strict = on;
}

bool mem_strict_active(void) {
    return s_strict;
}

void* mem_alloc_(mem_module_t mod, size_t size) {
    if (mod >= MEM_MOD_COUNT) {
        mod = MEM_MOD_SYS;
    }
    if (size > SIZE_MAX - sizeof(mem_hdr_t) - 16u) {
        MEM_LOCK();
        s_stats[mod].fails++;
        MEM_UNLOCK();
        return NULL;
    }

    uint8_t* raw = (uint8_t*)malloc(size + sizeof(mem_hdr_t));
    mem_hdr_t* hdr = (mem_hdr_t*)raw;
    if (raw) {
        hdr->magic = MEM_MAGIC;
        hdr->module = (uint8_t)mod;
        hdr->flags = MEM_FLAG_HEAP;
        hdr->size = (uint32_t)size;
    }

    MEM_LOCK();
    if (raw) {
        s_stats[mod].allocs++;
        s_stats[mod].cur += (uint32_t)size + sizeof(mem_hdr_t);
        if (s_stats[mod].cur > s_stats[mod].peak) {
            s_stats[mod].peak = s_stats[mod].cur;
        }
    } else {
        s_stats[mod].fails++;
    }
    MEM_UNLOCK();

    if (!raw) {
        LOGE(TAG, "alloc %u bytes for %s failed", (unsigned)size, module_name(mod));
        return NULL;
    }
    return raw + sizeof(mem_hdr_t);
}

void* mem_calloc_(mem_module_t mod, size_t n, size_t size) {
    if (n != 0 && size > SIZE_MAX / n) {
        MEM_LOCK();
        s_stats[mod < MEM_MOD_COUNT ? mod : MEM_MOD_SYS].fails++;
        MEM_UNLOCK();
        return NULL;
    }
    size_t total = n * size;
    void* p = mem_alloc_(mod, total);
    if (p) {
        memset(p, 0, total);
    }
    return p;
}

void* mem_realloc_(mem_module_t mod, void* ptr, size_t size) {
    if (ptr == NULL) {
        return mem_alloc_(mod, size);
    }
    if (size == 0) {
        mem_free_(ptr);
        return NULL;
    }

    mem_hdr_t* hdr = (mem_hdr_t*)((uint8_t*)ptr - sizeof(mem_hdr_t));
    if (hdr->magic != MEM_MAGIC) {
        if (s_strict) {
            LOGE(TAG, "realloc: bad magic ptr=%p -- STRICT abort", ptr);
            abort();
        }
        LOGE(TAG, "realloc: bad magic ptr=%p -- reject", ptr);
        return NULL;
    }

    size_t old_size = hdr->size;
    void* newp = mem_alloc_(mod, size);
    if (!newp) {
        return NULL;    /* 原块保持有效，与 libc 一致 */
    }
    memcpy(newp, ptr, old_size < size ? old_size : size);
    mem_free_(ptr);
    return newp;
}

void mem_free_(void* ptr) {
    if (ptr == NULL) {
        return;
    }
    mem_hdr_t* hdr = (mem_hdr_t*)((uint8_t*)ptr - sizeof(mem_hdr_t));
    if (hdr->magic != MEM_MAGIC) {
        if (s_strict) {
            LOGE(TAG, "free: bad magic ptr=%p -- STRICT abort", ptr);
            abort();
        }
        LOGE(TAG, "free: bad magic ptr=%p (wild/double free?) -- reject", ptr);
        return;
    }

    MEM_LOCK();
    s_stats[hdr->module < MEM_MOD_COUNT ? hdr->module : MEM_MOD_SYS].frees++;
    s_stats[hdr->module < MEM_MOD_COUNT ? hdr->module : MEM_MOD_SYS].cur -=
        hdr->size + sizeof(mem_hdr_t);
    MEM_UNLOCK();

    hdr->magic = 0;     /* 捕获后续 double-free */
    free(hdr);
}

void* mem_heap_alloc_(mem_module_t mod, size_t size, uint32_t caps) {
    if (mod >= MEM_MOD_COUNT) {
        mod = MEM_MOD_SYS;
    }
    if (size > SIZE_MAX - sizeof(mem_hdr_t) - 16u) {
        MEM_LOCK();
        s_stats[mod].fails++;
        MEM_UNLOCK();
        return NULL;
    }

    uint8_t* raw;
#if defined(ESP_PLATFORM)
    raw = (uint8_t*)heap_caps_malloc(size + sizeof(mem_hdr_t), caps);
#else
    (void)caps;
    raw = (uint8_t*)malloc(size + sizeof(mem_hdr_t));
#endif
    if (raw) {
        mem_hdr_t* hdr = (mem_hdr_t*)raw;
        hdr->magic = MEM_MAGIC;
        hdr->module = (uint8_t)mod;
        hdr->flags = (caps & 0x02) ? MEM_FLAG_PSRAM : MEM_FLAG_DMA;
        hdr->size = (uint32_t)size;
    }

    MEM_LOCK();
    if (raw) {
        s_stats[mod].allocs++;
        s_stats[mod].cur += (uint32_t)size + sizeof(mem_hdr_t);
        if (s_stats[mod].cur > s_stats[mod].peak) {
            s_stats[mod].peak = s_stats[mod].cur;
        }
    } else {
        s_stats[mod].fails++;
    }
    MEM_UNLOCK();

    if (!raw) {
        LOGE(TAG, "heap alloc %u bytes (caps=0x%x) for %s failed",
             (unsigned)size, (unsigned)caps, module_name(mod));
        return NULL;
    }
    return raw + sizeof(mem_hdr_t);
}

void mem_heap_free_(void* ptr) {
    /* 同一套头校验/记账，释放路径与 mem_free_ 完全一致 */
    mem_free_(ptr);
}

void mem_stat_print(void) {
    uint32_t total_cur = 0, total_peak = 0, total_allocs = 0, total_frees = 0, total_fails = 0;
    LOGI(TAG, "module       current    peak   allocs    frees    fails");
    for (int i = 0; i < MEM_MOD_COUNT; i++) {
        const mem_mod_stat_t* s = &s_stats[i];
        if (s->allocs == 0 && s->frees == 0 && s->fails == 0) {
            continue;
        }
        LOGI(TAG, "%-9s %9u %7u %8u %8u %8u",
             module_name((mem_module_t)i), (unsigned)s->cur, (unsigned)s->peak,
             (unsigned)s->allocs, (unsigned)s->frees, (unsigned)s->fails);
        total_cur += s->cur; total_peak += s->peak;
        total_allocs += s->allocs; total_frees += s->frees; total_fails += s->fails;
    }
    LOGI(TAG, "%-9s %9u %7u %8u %8u %8u",
         "total", (unsigned)total_cur, (unsigned)total_peak,
         (unsigned)total_allocs, (unsigned)total_frees, (unsigned)total_fails);
}

const mem_mod_stat_t* mem_stat_get(mem_module_t mod) {
    return (mod < MEM_MOD_COUNT) ? &s_stats[mod] : NULL;
}
