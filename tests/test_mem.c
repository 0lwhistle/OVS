/**
 * @file test_mem.c
 * @brief mem_pool PC 宿主测试（设计文档 §6 用例清单）
 *
 * 覆盖: libc 语义对表 / 记账核对 / 防护（野指针/重复释放/strict abort）/
 *       块池（耗尽/LIFO/越界/重复归还）/ 双线程压测账目归零。
 */

#include "mem.h"
#include "mem_pool.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/wait.h>

static int s_pass = 0;
static int s_fail = 0;

#define CHECK(cond) do { \
    if (cond) { s_pass++; } \
    else { s_fail++; printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

static uint32_t cur_of(mem_module_t mod) { return mem_stat_get(mod)->cur; }
static uint32_t frees_of(mem_module_t mod) { return mem_stat_get(mod)->frees; }

/* ---------- 1. libc 语义对表 ---------- */
static void test_semantics(void) {
    printf("[1] malloc/calloc/realloc/free semantics\n");

    uint8_t* p = (uint8_t*)mem_malloc(100);
    CHECK(p != NULL);
    memset(p, 0xAB, 100);                       /* 可写 100 字节 */
    mem_free(p);

    mem_free(NULL);                                 /* free(NULL) 不崩 */
    CHECK(1);

    uint32_t* z = (uint32_t*)mem_calloc(25, sizeof(uint32_t));
    CHECK(z != NULL);
    int zeroed = 1;
    for (int i = 0; i < 25; i++) if (z[i] != 0) zeroed = 0;
    CHECK(zeroed);
    mem_free(z);

    CHECK(mem_calloc((size_t)-1, 4) == NULL);   /* n*size 溢出 → NULL */

    /* realloc 全语义 */
    uint8_t* r = (uint8_t*)mem_realloc(NULL, 32);       /* NULL→malloc */
    CHECK(r != NULL);
    memset(r, 0x5A, 32);
    r = (uint8_t*)mem_realloc(r, 128);                  /* 扩容保内容 */
    CHECK(r != NULL);
    int kept = 1;
    for (int i = 0; i < 32; i++) if (r[i] != 0x5A) kept = 0;
    CHECK(kept);
    r = (uint8_t*)mem_realloc(r, 8);                    /* 缩容保前缀 */
    CHECK(r != NULL);
    CHECK(r[0] == 0x5A && r[7] == 0x5A);
    CHECK(mem_realloc(r, 0) == NULL);                   /* size 0 → free+NULL */
    r = NULL;

    /* malloc(0) 返回可 free 指针或 NULL 均合法 */
    void* z0 = mem_malloc(0);
    CHECK(z0 == NULL || z0 != NULL);                    /* 只要不崩 */
    mem_free(z0);

    /* 坏指针 realloc 被拒（指针指向"伪负载"，其头 = 我们清零的前 8B，无越界读） */
    void* raw = malloc(64);
    memset(raw, 0, 16);                                 /* 魔数必不为 MEM_MAGIC */
    void* saved = raw;
    CHECK(mem_realloc((uint8_t*)raw + 8, 32) == NULL);  /* 拒绝，不崩 */
    free(saved);                                        /* 回收 libc 块 */
}

/* ---------- 2. 记账核对 ---------- */
static void test_accounting(void) {
    printf("[2] accounting (header overhead, per-module buckets, peak)\n");
    uint32_t base_sys = cur_of(MEM_MOD_SYS);
    uint32_t base_vfs = cur_of(MEM_MOD_VFS);
    uint32_t base_frees = frees_of(MEM_MOD_SYS);

    void* p = mem_malloc(100);                          /* SYS 桶: +100+8 */
    CHECK(cur_of(MEM_MOD_SYS) == base_sys + 108);
    CHECK(frees_of(MEM_MOD_SYS) == base_frees);

    void* v = mem_alloc_(MEM_MOD_VFS, 50);              /* 指定桶: VFS +50+8 */
    CHECK(cur_of(MEM_MOD_VFS) == base_vfs + 58);
    CHECK(cur_of(MEM_MOD_SYS) == base_sys + 108);       /* 不串账 */

    mem_free(v);
    CHECK(cur_of(MEM_MOD_VFS) == base_vfs);

    /* peak */
    void* a = mem_malloc(200);
    uint32_t peak_before = mem_stat_get(MEM_MOD_SYS)->peak;
    void* b = mem_malloc(300);
    CHECK(mem_stat_get(MEM_MOD_SYS)->peak >= peak_before + 308);
    mem_free(a);
    mem_free(b);
    mem_free(p);
    CHECK(cur_of(MEM_MOD_SYS) == base_sys);
    CHECK(frees_of(MEM_MOD_SYS) == base_frees + 3);     /* p a b（v 记 VFS 桶） */

    CHECK(mem_stat_get(MEM_MOD_COUNT) == NULL);         /* 越界查询防护 */
}

/* ---------- 3. 防护（非 strict，LOGE 拒绝不崩） ---------- */
static void test_guards(void) {
    printf("[3] guards (wild free / double free, non-strict)\n");
    uint32_t base_sys = cur_of(MEM_MOD_SYS);
    uint32_t base_frees = frees_of(MEM_MOD_SYS);

    void* p = mem_malloc(32);
    mem_free(p);
    mem_free(p);                                        /* double free → 拒绝 */
    CHECK(cur_of(MEM_MOD_SYS) == base_sys);
    CHECK(frees_of(MEM_MOD_SYS) == base_frees + 1);     /* 第二次未销账 */

    void* raw = malloc(64);
    memset(raw, 0, 16);                                 /* 保证魔数不命中（头=raw 前 8B） */
    mem_free((uint8_t*)raw + 8);                        /* 野指针 → 拒绝 */
    CHECK(cur_of(MEM_MOD_SYS) == base_sys);
    free(raw);
}

/* ---------- 4. strict 模式 abort（fork 子进程验证 SIGABRT） ---------- */
static void test_strict_abort(void) {
    printf("[4] strict mode abort (fork)\n");
    pid_t pid = fork();
    if (pid == 0) {
        mem_strict_set(true);
        void* p = mem_malloc(16);
        mem_free(p);
        mem_free(p);                                    /* strict: 应 abort */
        _exit(0);                                       /* 不应到达 */
    }
    int status = 0;
    waitpid(pid, &status, 0);
    CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
    mem_strict_set(false);
}

/* ---------- 5. 块池 ---------- */
static void test_pool(void) {
    printf("[5] block pool (exhaust / LIFO / foreign / double-free)\n");
    mem_pool_t* pool = mem_pool_create("tst", 16, 4, MEM_MOD_VFS);
    CHECK(pool != NULL);

    void* b[4];
    for (int i = 0; i < 4; i++) b[i] = mem_pool_alloc(pool);
    for (int i = 0; i < 4; i++) CHECK(b[i] != NULL);
    CHECK(mem_pool_alloc(pool) == NULL);                /* 耗尽 */
    CHECK(mem_pool_in_use(pool) == 4 && mem_pool_peak_use(pool) == 4);

    mem_pool_free(pool, b[2]);
    CHECK(mem_pool_in_use(pool) == 3 && mem_pool_peak_use(pool) == 4);

    /* 越界/外培养指针拒绝 */
    int local;
    mem_pool_free(pool, &local);
    mem_pool_free(pool, (uint8_t*)b[0] + 1);            /* 范围内但不对齐 */
    CHECK(mem_pool_in_use(pool) == 3);

    /* LIFO */
    mem_pool_free(pool, b[3]);                          /* →2 */
    mem_pool_free(pool, b[1]);                          /* →1 */
    CHECK(mem_pool_alloc(pool) == b[1]);                /* →2 */
    CHECK(mem_pool_alloc(pool) == b[3]);                /* →3 */

    /* 重复归还拒绝 */
    mem_pool_free(pool, b[0]);                          /* →2 */
    mem_pool_free(pool, b[0]);                          /* 拒绝，仍 2 */
    CHECK(mem_pool_in_use(pool) == 2);

    /* 清场: x=b[0] y=b[2]，连同 b[1] b[3] 全部归还 */
    void* x = mem_pool_alloc(pool);
    void* y = mem_pool_alloc(pool);
    CHECK(x == b[0] && y == b[2]);
    mem_pool_free(pool, x);
    mem_pool_free(pool, y);
    mem_pool_free(pool, b[1]);
    mem_pool_free(pool, b[3]);
    CHECK(mem_pool_in_use(pool) == 0);
}

/* ---------- 6. 双线程压测 ---------- */
#define STRESS_N 50000
static void* stress_thread(void* arg) {
    (void)arg;
    unsigned seed = (unsigned)(uintptr_t)&seed;
    for (int i = 0; i < STRESS_N; i++) {
        size_t sz = (size_t)(rand_r(&seed) % 256) + 1;
        void* p = mem_malloc(sz);
        if (p) {
            if (i % 7 == 0) {
                p = mem_realloc(p, sz + 16);
            }
            mem_free(p);
        }
    }
    return NULL;
}

static void test_threads(void) {
    printf("[6] threaded stress (2 x %d alloc/free)\n", STRESS_N);
    uint32_t base = cur_of(MEM_MOD_SYS);
    pthread_t t1, t2;
    pthread_create(&t1, NULL, stress_thread, NULL);
    pthread_create(&t2, NULL, stress_thread, NULL);
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    CHECK(cur_of(MEM_MOD_SYS) == base);                 /* 账目精确归零 */
}

int main(void) {
    mem_strict_set(false);
    test_semantics();
    test_accounting();
    test_guards();
    test_strict_abort();
    test_pool();
    test_threads();
    mem_stat_print();

    printf("\n==== ovs_tests: %d passed, %d failed ====\n", s_pass, s_fail);
    return s_fail ? 1 : 0;
}
