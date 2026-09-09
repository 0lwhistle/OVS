/**
 * @file vfs_stress.c
 * @brief VFS 文件系统压力测试实现
 *
 * 测试分区：/media(8MB) /audio(6MB) /font(2MB)，测试文件放在各分区
 * /stress 目录下，结束后尽力清理。失败项立即打印并在最终总结中重复列出。
 */

#include "vfs_stress.h"
#include "mem.h"
#include "logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_system.h"

static const char* TAG = "[VFS_STRESS]";

static int64_t now_us(void) {
    return esp_timer_get_time();
}

/* ========== 结果统计 ========== */
#define MAX_FAILS 32
static int s_total;
static int s_failed;
static int s_fail_count;
static char s_fail_msgs[MAX_FAILS][176];

/* ========== 板块统计与性能记录（供最终总结输出） ========== */
typedef struct {
    const char* name;
    int checks;
    int fails;
    int64_t ms;
} sec_stat_t;
static sec_stat_t s_secs[8];
static int s_sec_count;
static char s_perf_lines[12][96];
static int s_perf_count;
static bool s_mount_media, s_mount_audio, s_mount_font;

static void sec_record(const char* name, int checks0, int fails0, int64_t t0) {
    int checks = s_total - checks0;
    int fails = s_failed - fails0;
    int64_t ms = (now_us() - t0) / 1000;
    if (s_sec_count < 8) {
        s_secs[s_sec_count].name = name;
        s_secs[s_sec_count].checks = checks;
        s_secs[s_sec_count].fails = fails;
        s_secs[s_sec_count].ms = ms;
        s_sec_count++;
    }
    LOGI(TAG, "%s 完成: 检查 %d, 失败 %d, 耗时 %lld ms",
         name, checks, fails, (long long)ms);
}

static void perf_log(const char* fmt, ...) {
    char line[96];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    if (s_perf_count < 12) {
        snprintf(s_perf_lines[s_perf_count], sizeof(s_perf_lines[0]), "%s", line);
        s_perf_count++;
    }
    LOGI(TAG, "  %s", line);
}

/**
 * @brief 记录一条检查结果。失败时立即打印并存入总结列表。
 */
static bool st_check(bool cond, const char* name, const char* detail_fmt, ...) {
    s_total++;
    if (cond) {
        return true;
    }
    s_failed++;

    char detail[128] = "";
    if (detail_fmt && detail_fmt[0]) {
        va_list ap;
        va_start(ap, detail_fmt);
        vsnprintf(detail, sizeof(detail), detail_fmt, ap);
        va_end(ap);
    }

    LOGE(TAG, "FAIL [%s] %s", name, detail);

    if (s_fail_count < MAX_FAILS) {
        snprintf(s_fail_msgs[s_fail_count], sizeof(s_fail_msgs[0]),
                 "[%s] %s", name, detail);
        s_fail_count++;
    }
    return false;
}

/* ========== 工具 ========== */

#define BUF_SIZE (64 * 1024)
static uint8_t* s_buf1;
static uint8_t* s_buf2;

/* 确定性伪随机（可复现） */
static uint32_t s_lcg;
static void seed_set(uint32_t seed) { s_lcg = seed ? seed : 1; }
static uint32_t next_u32(void) {
    s_lcg = s_lcg * 1664525u + 1013904223u;
    return s_lcg;
}

/* 用 LCG 序列填充缓冲区（state 由调用方持有，保证写入/校验序列一致） */
static void fill_lcg(uint8_t* buf, size_t len, uint32_t* state) {
    for (size_t i = 0; i < len; i++) {
        *state = *state * 1664525u + 1013904223u;
        buf[i] = (uint8_t)(*state >> 16);
    }
}

static void fill_pattern(uint8_t* buf, size_t len, uint8_t pat) {
    memset(buf, pat, len);
}

static bool verify_pattern(const uint8_t* buf, size_t len, uint8_t pat) {
    for (size_t i = 0; i < len; i++) {
        if (buf[i] != pat) {
            return false;
        }
    }
    return true;
}

/* 递归删除目录树 */
static void rmtree(const char* dir) {
    DIR* d = opendir(dir);
    if (!d) return;
    struct dirent* e;
    char path[300];
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        struct stat st;
        snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
            rmtree(path);
        } else {
            remove(path);
        }
    }
    closedir(d);
    rmdir(dir);
}

/* ========== A. 数据完整性（/media） ========== */

static void stress_data_integrity(void) {
    LOGI(TAG, "--- A. 数据完整性 ---");
    int64_t t0 = now_us();
    char path[256];
    int section_fail0 = s_failed;
    int section_checks0 = s_total;

    /* A1. 固定模式 × 多种尺寸 写读校验 */
    static const uint8_t patterns[] = {0x00, 0xFF, 0xAA, 0x55, 0xC3};
    static const size_t sizes[] = {1, 17, 512, 4096, 32768};
    for (size_t pi = 0; pi < sizeof(patterns); pi++) {
        for (size_t si = 0; si < sizeof(sizes) / sizeof(sizes[0]); si++) {
            size_t len = sizes[si];
            snprintf(path, sizeof(path), "/media/stress/pat_%02x_%u.bin",
                     patterns[pi], (unsigned)len);
            fill_pattern(s_buf1, len, patterns[pi]);

            FILE* fp = fopen(path, "wb");
            bool ok = (fp != NULL);
            size_t w = ok ? fwrite(s_buf1, 1, len, fp) : 0;
            ok = ok && (w == len) && (fclose(fp) == 0);

            fp = ok ? fopen(path, "rb") : NULL;
            size_t r = ok ? fread(s_buf2, 1, len, fp) : 0;
            ok = ok && (r == len) && verify_pattern(s_buf2, len, patterns[pi]);
            ok = ok && (fclose(fp) == 0);

            char detail[96];
            snprintf(detail, sizeof(detail), "len=%u pat=0x%02x w=%u r=%u",
                     (unsigned)len, patterns[pi], (unsigned)w, (unsigned)r);
            st_check(ok, "A1 模式写读校验", detail);
            remove(path);
        }
        LOGI(TAG, "  A1 进度: 位型 0x%02X 完成 (%u/%u)",
             patterns[pi], (unsigned)(pi + 1), (unsigned)(sizeof(patterns) / sizeof(patterns[0])));
    }

    /* A2. 边界尺寸：4KB-1 与 64KB，随机数据写读比对 */
    static const size_t edge_sizes[] = {4095, 64 * 1024};
    for (size_t i = 0; i < sizeof(edge_sizes) / sizeof(edge_sizes[0]); i++) {
        size_t len = edge_sizes[i];
        snprintf(path, sizeof(path), "/media/stress/edge_%u.bin", (unsigned)len);
        seed_set(0xE0 + i);
        fill_lcg(s_buf1, len, &s_lcg);

        FILE* fp = fopen(path, "wb");
        bool ok = (fp != NULL);
        size_t w = ok ? fwrite(s_buf1, 1, len, fp) : 0;
        ok = ok && (w == len) && (fclose(fp) == 0);

        fp = ok ? fopen(path, "rb") : NULL;
        size_t r = ok ? fread(s_buf2, 1, len, fp) : 0;
        ok = ok && (r == len) && (fclose(fp) == 0);

        /* 重放随机序列到 s_buf1（覆盖已写副本），与读回的 s_buf2 比对 */
        seed_set(0xE0 + i);
        fill_lcg(s_buf1, len, &s_lcg);
        ok = ok && (memcmp(s_buf1, s_buf2, len) == 0);

        char detail[80];
        snprintf(detail, sizeof(detail), "len=%u w=%u r=%u",
                 (unsigned)len, (unsigned)w, (unsigned)r);
        st_check(ok, "A2 边界尺寸写读", detail);
        remove(path);
    }

    /* A3. 随机位置覆盖写后全文件校验 */
    {
        size_t len = 64 * 1024;
        snprintf(path, sizeof(path), "/media/stress/rand_ovr.bin");
        seed_set(77);
        fill_lcg(s_buf1, len, &s_lcg);

        FILE* fp = fopen(path, "wb");
        bool ok = (fp != NULL) && (fwrite(s_buf1, 1, len, fp) == len) && (fclose(fp) == 0);

        /* 50 次随机位置 256B 覆盖，同步更新内存模型 */
        seed_set(1234);
        for (int i = 0; ok && i < 50; i++) {
            if (i % 10 == 0) {
                LOGI(TAG, "  A3 进度: %d/50 次随机覆盖, 已耗时 %lld s",
                     i, (long long)((now_us() - t0) / 1000000));
            }
            long off = (long)(next_u32() % (len - 256));
            fill_pattern(s_buf1 + off, 256, (uint8_t)(i + 1));
            fp = fopen(path, "r+b");
            ok = (fp != NULL);
            if (ok) {
                fseek(fp, off, SEEK_SET);
                ok = (fwrite(s_buf1 + off, 1, 256, fp) == 256) && (fclose(fp) == 0);
            }
        }

        /* 全文件读回，与内存模型比对 */
        fp = ok ? fopen(path, "rb") : NULL;
        ok = ok && (fp != NULL);
        if (fp) {
            for (size_t off = 0; ok && off < len; off += BUF_SIZE) {
                size_t chunk = (len - off > BUF_SIZE) ? BUF_SIZE : (len - off);
                size_t r = fread(s_buf2, 1, chunk, fp);
                ok = (r == chunk) && (memcmp(s_buf1 + off, s_buf2, chunk) == 0);
            }
            fclose(fp);
        }
        st_check(ok, "A3 随机覆盖写校验", "64KB 文件 50 次随机覆盖");
        remove(path);
    }

    /* A4. 追加写 100 次（周期性重开文件） */
    {
        snprintf(path, sizeof(path), "/media/stress/append.dat");
        seed_set(555);
        remove(path);
        FILE* fp = fopen(path, "ab");
        bool ok = (fp != NULL);
        for (int i = 0; ok && i < 100; i++) {
            fill_lcg(s_buf1, 128, &s_lcg);
            ok = (fwrite(s_buf1, 1, 128, fp) == 128);
            if (ok && i % 10 == 9) {
                fclose(fp);
                fp = fopen(path, "ab");
                ok = (fp != NULL);
            }
        }
        if (fp) fclose(fp);

        struct stat st = {0};
        ok = ok && (stat(path, &st) == 0) && (st.st_size == 100 * 128);

        /* 读回：与重放序列比对 */
        fp = ok ? fopen(path, "rb") : NULL;
        seed_set(555);
        ok = ok && (fp != NULL);
        if (fp) {
            for (int i = 0; ok && i < 100; i++) {
                fill_lcg(s_buf2, 128, &s_lcg);
                size_t r = fread(s_buf1, 1, 128, fp);
                ok = (r == 128) && (memcmp(s_buf1, s_buf2, 128) == 0);
            }
            fclose(fp);
        }
        st_check(ok, "A4 追加写校验", "100 次 x 128B");
        remove(path);
    }

    rmtree("/media/stress");
    sec_record("A 数据完整性", section_checks0, section_fail0, t0);
}

/* ========== B. 目录与文件管理（/media） ========== */

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
static void stress_dir_ops(void) {
    LOGI(TAG, "--- B. 目录与文件管理 ---");
    int64_t t0 = now_us();
    char path[512];
    char path2[512];
    int section_fail0 = s_failed;
    int section_checks0 = s_total;

    /* B1. 深层嵌套目录（A 板块清理会移除 /media/stress，先重建父目录） */
    {
        mkdir("/media/stress", 0755);
        snprintf(path, sizeof(path), "/media/stress");
        bool ok = true;
        for (int i = 0; i < 8 && ok; i++) {
            snprintf(path2, sizeof(path2), "%s/d%d", path, i);
            ok = (mkdir(path2, 0755) == 0);
            snprintf(path, sizeof(path), "%s", path2);
        }
        snprintf(path2, sizeof(path2), "%s/deep.txt", path);
        FILE* fp = fopen(path2, "w");
        ok = ok && (fp != NULL);
        if (fp) { fputs("deep", fp); fclose(fp); }

        fp = fopen(path2, "r");
        char buf[16] = {0};
        ok = ok && (fp != NULL);
        if (fp) {
            size_t r = fread(buf, 1, 15, fp);
            ok = (r == 4) && (fclose(fp) == 0);
        }
        ok = ok && (strcmp(buf, "deep") == 0);
        st_check(ok, "B1 深层嵌套目录", "8 级目录 + 文件读写 (路径=%s)", path2);
        rmtree("/media/stress/d0");
    }

    /* B2. 大量小文件：创建/遍历/删除 */
    {
        mkdir("/media/stress", 0755);
        int n = 150;
        bool ok = true;
        for (int i = 0; i < n && ok; i++) {
            snprintf(path, sizeof(path), "/media/stress/many_%03d.dat", i);
            FILE* fp = fopen(path, "w");
            ok = (fp != NULL);
            if (ok) {
                fprintf(fp, "file-%d", i);
                ok = (fclose(fp) == 0);
            }
            if (i % 10 == 9) {
                vTaskDelay(1);  /* 小文件元数据操作密集，防止 idle 饿死 */
            }
            if (i % 50 == 49) {
                LOGI(TAG, "  B2 进度: %d/%d 个小文件, 已耗时 %lld s",
                     i + 1, n, (long long)((now_us() - t0) / 1000000));
            }
        }
        st_check(ok, "B2 小文件创建", "%d 个", n);

        DIR* d = opendir("/media/stress");
        int count = 0;
        if (d) {
            struct dirent* e;
            while ((e = readdir(d)) != NULL) {
                if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) count++;
            }
            closedir(d);
        }
        st_check(count == n, "B2 小文件遍历计数", "期望 %d 实际 %d", n, count);

        ok = true;
        for (int i = 0; i < n && ok; i++) {
            snprintf(path, sizeof(path), "/media/stress/many_%03d.dat", i);
            ok = (remove(path) == 0);
            if (i % 10 == 9) {
                vTaskDelay(1);
            }
        }
        st_check(ok, "B2 小文件删除", "%d 个", n);

        d = opendir("/media/stress");
        count = 0;
        if (d) {
            struct dirent* e;
            while ((e = readdir(d)) != NULL) {
                if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) count++;
            }
            closedir(d);
        }
        st_check(count == 0, "B2 删除后目录为空", "剩余 %d", count);
    }

    /* B3. 重命名与覆盖语义 */
    {
        mkdir("/media/stress", 0755);
        snprintf(path, sizeof(path), "/media/stress/ren_a.txt");
        snprintf(path2, sizeof(path2), "/media/stress/ren_b.txt");
        remove(path); remove(path2);

        FILE* fp = fopen(path, "w");
        bool ok = (fp != NULL);
        if (fp) { fputs("AAA", fp); fclose(fp); }

        ok = ok && (rename(path, path2) == 0);
        fp = fopen(path, "r");
        st_check(ok && (fp == NULL), "B3 重命名后旧名消失", "");
        if (fp) fclose(fp);

        fp = fopen(path2, "w");
        ok = ok && (fp != NULL);
        if (fp) { fputs("B", fp); fclose(fp); }

        struct stat st = {0};
        ok = ok && (stat(path2, &st) == 0) && (st.st_size == 1);
        st_check(ok, "B3 覆盖写截断语义", "size=%ld", (long)st.st_size);
        remove(path2);
    }

    /* B4. 错误处理：非法操作必须优雅失败 */
    {
        FILE* fp = fopen("/media/stress/no_such_file.txt", "r");
        bool ok = (fp == NULL);
        st_check(ok, "B4 读不存在文件失败", "");
        if (fp) fclose(fp);

        fp = fopen("/media/stress/missing_dir/x.txt", "w");
        ok = (fp == NULL);
        st_check(ok, "B4 写入不存在目录失败", "");
        if (fp) fclose(fp);

        ok = (remove("/media/stress/definitely_missing.bin") != 0);
        st_check(ok, "B4 删除不存在文件失败", "");

        ok = (mkdir("/media/stress", 0755) != 0);
        st_check(ok, "B4 重复创建目录失败", "");

        char longname[320];
        memset(longname, 'a', sizeof(longname) - 1);
        longname[sizeof(longname) - 1] = '\0';
        snprintf(path, sizeof(path), "/media/stress/%s", longname);
        fp = fopen(path, "w");
        ok = (fp == NULL);
        st_check(ok, "B4 超长文件名被拒绝", "");
        if (fp) fclose(fp);
    }

    rmtree("/media/stress");
    sec_record("B 目录与文件管理", section_checks0, section_fail0, t0);
}
#pragma GCC diagnostic pop

/* ========== C. 容量与碎片（/font 2MB） ========== */

static void stress_capacity(void) {
    LOGI(TAG, "--- C. 容量与碎片（/font 2MB） ---");
    int64_t t0 = now_us();
    char path[256];
    int section_fail0 = s_failed;
    int section_checks0 = s_total;
    mkdir("/font/stress", 0755);

    /* C1. 打满到 ENOSPC，删除后验证空间回收 */
    {
        int idx = 0;
        bool enospc = false;
        size_t total = 0;
        while (idx < 200) {
            snprintf(path, sizeof(path), "/font/stress/fill_%03d.bin", idx);
            FILE* fp = fopen(path, "wb");
            if (!fp) { enospc = true; break; }
            size_t w = fwrite(s_buf1, 1, 16 * 1024, fp);
            fclose(fp);
            if (w != 16 * 1024) {
                remove(path);
                enospc = true;
                break;
            }
            total += 16 * 1024;
            idx++;
            if (idx % 8 == 0) {
                vTaskDelay(1);  /* 显式让步，杜绝空闲任务饿死触发看门狗 */
                LOGI(TAG, "  C1 打满进度: %u KB / %d 文件, 已耗时 %lld s",
                     (unsigned)(total / 1024), idx,
                     (long long)((now_us() - t0) / 1000000));
            }
        }
        st_check(enospc, "C1 容量打满触发ENOSPC", "写入 %u KB / %d 个文件",
                 (unsigned)(total / 1024), idx);
        st_check(total >= 1536 * 1024, "C1 可用容量合理", "实际 %u KB（期望 1.5M~2M）",
                 (unsigned)(total / 1024));

        for (int i = 0; i <= idx; i++) {
            snprintf(path, sizeof(path), "/font/stress/fill_%03d.bin", i);
            remove(path);
        }

        /* 回收验证：再写 512KB 并读回 */
        bool ok = false;
        size_t w = 0, r = 0;
        FILE* fp = fopen("/font/stress/after_free.bin", "wb");
        ok = (fp != NULL);
        if (ok) {
            fill_pattern(s_buf1, BUF_SIZE, 0x5A);
            for (int i = 0; i < 8; i++) {
                w = fwrite(s_buf1, 1, BUF_SIZE, fp);
                if (w != BUF_SIZE) { ok = false; break; }
            }
            fclose(fp);
        }
        fp = ok ? fopen("/font/stress/after_free.bin", "rb") : NULL;
        if (fp) {
            for (int i = 0; i < 8; i++) {
                r = fread(s_buf2, 1, BUF_SIZE, fp);
                if (r != BUF_SIZE || !verify_pattern(s_buf2, BUF_SIZE, 0x5A)) {
                    ok = false;
                    break;
                }
            }
            fclose(fp);
        }
        st_check(ok, "C1 删除后空间回收可复用", "w=%u r=%u", (unsigned)w, (unsigned)r);
        remove("/font/stress/after_free.bin");
    }

    /* C2. 碎片化：20 文件交错增长 → 删除奇数 → 500KB 大文件写入碎片空间 */
    {
        int n = 20;
        size_t per = 32 * 1024;
        bool ok = true;

        FILE* fps[20];
        char paths[20][64];
        for (int i = 0; i < n; i++) {
            snprintf(paths[i], sizeof(paths[i]), "/font/stress/frag_%02d.bin", i);
            fps[i] = fopen(paths[i], "wb");
            ok = ok && (fps[i] != NULL);
        }
        fill_pattern(s_buf1, 4096, 0x00);
        int round = 0;
        for (size_t off = 0; ok && off < per; off += 4096) {
            s_buf1[0] = (uint8_t)off;
            for (int i = 0; i < n; i++) {
                if (fps[i] && fwrite(s_buf1, 1, 4096, fps[i]) != 4096) { ok = false; }
            }
            round++;
            if (round % 4 == 0) {
                vTaskDelay(1);
                LOGI(TAG, "  C2 交错写入进度: %d/%u KB (第 %d 轮), 已耗时 %lld s",
                     (unsigned)(off + 4096) / 1024, (unsigned)(per / 1024), round,
                     (long long)((now_us() - t0) / 1000000));
            }
        }
        for (int i = 0; i < n; i++) {
            if (fps[i]) fclose(fps[i]);
        }
        st_check(ok, "C2 交错写入20文件", "各 32KB");

        for (int i = 1; i < n; i += 2) {
            remove(paths[i]);
        }

        snprintf(path, sizeof(path), "/font/stress/big_after_frag.bin");
        FILE* fp = fopen(path, "wb");
        ok = (fp != NULL);
        size_t big_written = 0;
        if (ok) {
            seed_set(99);
            for (size_t off = 0; off < 500 * 1024; off += BUF_SIZE) {
                fill_lcg(s_buf1, BUF_SIZE, &s_lcg);
                size_t w = fwrite(s_buf1, 1, BUF_SIZE, fp);
                if (w != BUF_SIZE) { ok = false; break; }
                big_written += w;
                if (big_written % (128 * 1024) == 0) {
                    vTaskDelay(1);
                    LOGI(TAG, "  C2 大文件进度: %u/5120 KB, 已耗时 %lld s",
                         (unsigned)(big_written / 1024),
                         (long long)((now_us() - t0) / 1000000));
                }
            }
            fclose(fp);
        }
        st_check(ok, "C2 碎片化后大文件写入", "已写 %u/5120 KB",
                 (unsigned)(big_written / 1024));

        fp = ok ? fopen(path, "rb") : NULL;
        ok = ok && (fp != NULL);
        seed_set(99);
        if (fp) {
            for (size_t off = 0; ok && off < 500 * 1024; off += BUF_SIZE) {
                size_t chunk = (500 * 1024 - off > BUF_SIZE) ? BUF_SIZE : (500 * 1024 - off);
                fill_lcg(s_buf2, chunk, &s_lcg);
                size_t r = fread(s_buf1, 1, chunk, fp);
                ok = (r == chunk) && (memcmp(s_buf1, s_buf2, chunk) == 0);
            }
            fclose(fp);
        }
        st_check(ok, "C2 碎片化大文件读回校验", "");
        remove(path);
        for (int i = 0; i < n; i += 2) {
            remove(paths[i]);
        }
    }

    rmtree("/font/stress");
    sec_record("C 容量与碎片（/font 2MB）", section_checks0, section_fail0, t0);
}

/* ========== D. 多任务并发（/audio ×2 + /media ×1） ========== */

typedef struct {
    const char* path;
    size_t bytes;
    uint32_t seed;
    volatile bool ok;
} wrk_t;

static SemaphoreHandle_t s_worker_sem;
static SemaphoreHandle_t s_task_done;

static void stress_worker(void* arg) {
    wrk_t* w = (wrk_t*)arg;
    w->ok = false;

    uint8_t* buf = mem_malloc(4096);
    uint8_t* expect = mem_malloc(4096);

    if (buf && expect) {
        FILE* fp = fopen(w->path, "wb");
        if (fp) {
            uint32_t state = w->seed;
            bool ok = true;
            for (size_t off = 0; ok && off < w->bytes; off += 4096) {
                size_t chunk = (w->bytes - off > 4096) ? 4096 : (w->bytes - off);
                fill_lcg(buf, chunk, &state);
                ok = (fwrite(buf, 1, chunk, fp) == chunk);
                if ((off / 4096) % 8 == 7) {
                    vTaskDelay(1);  /* 长写循环周期性让步 */
                }
            }
            fclose(fp);

            fp = ok ? fopen(w->path, "rb") : NULL;
            ok = ok && (fp != NULL);
            state = w->seed;
            if (fp) {
                for (size_t off = 0; ok && off < w->bytes; off += 4096) {
                    size_t chunk = (w->bytes - off > 4096) ? 4096 : (w->bytes - off);
                    fill_lcg(expect, chunk, &state);
                    size_t r = fread(buf, 1, chunk, fp);
                    ok = (r == chunk) && (memcmp(buf, expect, chunk) == 0);
                }
                fclose(fp);
            }
            w->ok = ok;
        }
    }

    mem_free(buf);
    mem_free(expect);
    xSemaphoreGive(s_worker_sem);
    vTaskDelete(NULL);
}

static void stress_concurrent(void) {
    LOGI(TAG, "--- D. 多任务并发 ---");
    int64_t t0 = now_us();
    int section_fail0 = s_failed;
    int section_checks0 = s_total;

    s_worker_sem = xSemaphoreCreateCounting(3, 0);

    static wrk_t workers[3];
    workers[0] = (wrk_t){"/audio/stress_con_a.bin", 256 * 1024, 0xA1, false};
    workers[1] = (wrk_t){"/audio/stress_con_b.bin", 256 * 1024, 0xB2, false};
    workers[2] = (wrk_t){"/media/stress_con_c.bin", 256 * 1024, 0xC3, false};

    for (int i = 0; i < 3; i++) {
        if (xTaskCreate(stress_worker, "vfs_wrk", 6144, &workers[i], 5, NULL) != pdPASS) {
            xSemaphoreGive(s_worker_sem);
            LOGE(TAG, "worker %d create failed", i);
        }
    }

    int done = 0;
    bool all_done = true;
    int64_t t_wait = now_us();
    while (done < 3) {
        if (xSemaphoreTake(s_worker_sem, pdMS_TO_TICKS(5000)) == pdTRUE) {
            done++;
            continue;
        }
        int64_t elapsed_s = (now_us() - t_wait) / 1000000;
        if (elapsed_s >= 120) {
            all_done = false;
            LOGE(TAG, "  D 等待超时: %d/3 完成", done);
            break;
        }
        LOGI(TAG, "  D 进度: %d/3 个 worker 完成, 已等待 %lld s", done, (long long)elapsed_s);
    }
    st_check(all_done, "D 并发任务全部完成", "120s 超时");
    st_check(workers[0].ok, "D 并发写读校验 /audio#1", "256KB");
    st_check(workers[1].ok, "D 并发写读校验 /audio#2", "256KB");
    st_check(workers[2].ok, "D 并发写读校验 /media  ", "256KB（与 /audio 并行）");

    remove("/audio/stress_con_a.bin");
    remove("/audio/stress_con_b.bin");
    remove("/media/stress_con_c.bin");
    vSemaphoreDelete(s_worker_sem);
    s_worker_sem = NULL;

    sec_record("D 多任务并发", section_checks0, section_fail0, t0);
}

/* ========== E. 性能统计（只报告，不计成败） ========== */

static void stress_performance(void) {
    LOGI(TAG, "--- E. 性能统计 ---");
    int64_t t0 = now_us();
    char path[64];
    snprintf(path, sizeof(path), "/media/stress_perf.bin");

    static const size_t blocks[] = {1024, 4096, 32768, 64 * 1024};
    const size_t volume = 256 * 1024;

    for (size_t bi = 0; bi < sizeof(blocks) / sizeof(blocks[0]); bi++) {
        size_t blk = blocks[bi];
        FILE* fp = fopen(path, "wb");
        if (!fp) { LOGE(TAG, "perf: open failed"); continue; }

        int64_t t0 = now_us();
        for (size_t off = 0; off < volume; off += blk) {
            if (fwrite(s_buf1, 1, blk, fp) != blk) break;
        }
        fclose(fp);
        double w_ms = (now_us() - t0) / 1000.0;

        fp = fopen(path, "rb");
        if (!fp) continue;
        t0 = now_us();
        size_t total_r = 0;
        for (size_t off = 0; off < volume; off += blk) {
            total_r += fread(s_buf2, 1, blk, fp);
        }
        fclose(fp);
        double r_ms = (now_us() - t0) / 1000.0;

        perf_log("块 %5u B: 写 %6.1f KB/s | 读 %6.1f KB/s",
                 (unsigned)blk,
                 volume / 1024.0 / (w_ms / 1000.0),
                 total_r / 1024.0 / (r_ms / 1000.0));
    }

    /* 随机 4KB 读延迟 */
    {
        FILE* fp = fopen(path, "rb");
        if (fp) {
            seed_set(4242);
            const int n = 50;
            int64_t lat_sum = 0;
            for (int i = 0; i < n; i++) {
                long off = (long)(next_u32() % (256 * 1024 - 4096));
                fseek(fp, off, SEEK_SET);
                int64_t t1 = now_us();
                fread(s_buf2, 1, 4096, fp);
                lat_sum += now_us() - t1;
            }
            fclose(fp);
            perf_log("随机 4KB 读: 平均 %lld us", (long long)(lat_sum / n));
        }
    }

    /* 小文件创建速率 */
    {
        mkdir("/media/stress", 0755);
        int64_t t1 = now_us();
        for (int i = 0; i < 50; i++) {
            snprintf(path, sizeof(path), "/media/stress/sf_%02d", i);
            FILE* fp = fopen(path, "w");
            if (fp) { fputs("x", fp); fclose(fp); }
            if (i % 10 == 9) {
                vTaskDelay(1);
            }
        }
        double ms = (now_us() - t1) / 1000.0;
        perf_log("小文件创建: 50 个耗时 %.0f ms（%.1f 个/秒）",
                 ms, 50.0 / (ms / 1000.0));
        for (int i = 0; i < 50; i++) {
            snprintf(path, sizeof(path), "/media/stress/sf_%02d", i);
            remove(path);
        }
        rmtree("/media/stress");
    }

    remove("/media/stress_perf.bin");
    sec_record("E 性能统计", s_total, s_failed, t0);
}

/* ========== 总控 ========== */

static void vfs_stress_task(void* arg) {
    (void)arg;
    int64_t start = now_us();

    LOGI(TAG, "========================================");
    LOGI(TAG, "  VFS 文件系统压力测试开始");
    LOGI(TAG, "========================================");

    /* 预检：三个分区是否挂载可写 */
    bool media_ok = true, audio_ok = true, font_ok = true;
    FILE* fp = fopen("/media/.probe", "w");
    if (fp) { fputs("1", fp); fclose(fp); remove("/media/.probe"); } else media_ok = false;
    fp = fopen("/audio/.probe", "w");
    if (fp) { fputs("1", fp); fclose(fp); remove("/audio/.probe"); } else audio_ok = false;
    fp = fopen("/font/.probe", "w");
    if (fp) { fputs("1", fp); fclose(fp); remove("/font/.probe"); } else font_ok = false;

    s_mount_media = media_ok;
    s_mount_audio = audio_ok;
    s_mount_font = font_ok;
    st_check(media_ok, "P0 /media 已挂载可写", "");
    st_check(audio_ok, "P0 /audio 已挂载可写", "");
    st_check(font_ok, "P0 /font 已挂载可写", "");

    if (media_ok) stress_data_integrity();
    if (media_ok) stress_dir_ops();
    if (font_ok) stress_capacity();
    if (media_ok && audio_ok) stress_concurrent();
    if (media_ok) stress_performance();

    double total_s = (now_us() - start) / 1000000.0;

    /* ========== 最终总结（整块可复制反馈） ========== */
    LOGI(TAG, "======== VFS 压力测试总结（复制以下整块） ========");
    LOGI(TAG, "[环境] 总耗时: %.1f s | 空闲堆: %u KB (最低 %u KB)",
         total_s,
         (unsigned)(esp_get_free_heap_size() / 1024),
         (unsigned)(esp_get_minimum_free_heap_size() / 1024));
    LOGI(TAG, "[分区] /media:%s /audio:%s /font:%s",
         s_mount_media ? "OK" : "未挂载",
         s_mount_audio ? "OK" : "未挂载",
         s_mount_font ? "OK" : "未挂载");
    LOGI(TAG, "[总计] 检查项: %d | 通过: %d | 失败: %d",
         s_total, s_total - s_failed, s_failed);
    for (int i = 0; i < s_sec_count; i++) {
        LOGI(TAG, "[板块] %s: 检查 %d, 失败 %d, 耗时 %lld ms",
             s_secs[i].name, s_secs[i].checks, s_secs[i].fails,
             (long long)s_secs[i].ms);
    }
    if (s_perf_count > 0) {
        for (int i = 0; i < s_perf_count; i++) {
            LOGI(TAG, "[性能] %s", s_perf_lines[i]);
        }
    }
    if (s_fail_count > 0) {
        for (int i = 0; i < s_fail_count; i++) {
            LOGE(TAG, "[失败%d] %s", i + 1, s_fail_msgs[i]);
        }
        LOGE(TAG, "[结论] 未通过: %d 项失败", s_failed);
    } else {
        LOGI(TAG, "[结论] 全部通过");
    }
    LOGI(TAG, "======== VFS 压力测试总结结束 ========");
    if (s_fail_count > 0) {
        LOGE(TAG, ">>> 结果: 未通过 (%d 项失败) <<<", s_failed);
    } else {
        LOGI(TAG, ">>> 结果: 全部通过 <<<");
    }

    mem_free(s_buf1);
    mem_free(s_buf2);
    s_buf1 = s_buf2 = NULL;
    xSemaphoreGive(s_task_done);
    vTaskDelete(NULL);
}

int vfs_stress_run(void) {
    if (s_buf1) {
        LOGW(TAG, "Stress test already running");
        return -1;
    }

    s_total = 0;
    s_failed = 0;
    s_fail_count = 0;
    memset(s_fail_msgs, 0, sizeof(s_fail_msgs));

    s_buf1 = mem_malloc(BUF_SIZE);
    s_buf2 = mem_malloc(BUF_SIZE);
    if (!s_buf1 || !s_buf2) {
        LOGE(TAG, "Alloc %u KB buffers failed", (unsigned)(BUF_SIZE / 1024));
        mem_free(s_buf1);
        mem_free(s_buf2);
        s_buf1 = s_buf2 = NULL;
        return -1;
    }

    s_task_done = xSemaphoreCreateBinary();
    if (!s_task_done) {
        mem_free(s_buf1);
        mem_free(s_buf2);
        s_buf1 = s_buf2 = NULL;
        return -1;
    }

    if (xTaskCreate(vfs_stress_task, "vfs_stress", 16384, NULL, 5, NULL) != pdPASS) {
        LOGE(TAG, "Create stress task failed");
        vSemaphoreDelete(s_task_done);
        mem_free(s_buf1);
        mem_free(s_buf2);
        s_buf1 = s_buf2 = NULL;
        return -1;
    }

    /* 阻塞等待测试任务完成 */
    xSemaphoreTake(s_task_done, portMAX_DELAY);
    vSemaphoreDelete(s_task_done);
    s_task_done = NULL;

    return s_failed;
}
