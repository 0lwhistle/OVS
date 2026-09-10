/**
 * @file audio_port.h
 * @brief 音频模块平台移植层（内部头，仅 audio_module 系源文件使用）
 *
 * ESP:   FreeRTOS 互斥信号量 / xTask / vTaskDelay / esp_timer / gpio_ctrl
 * PC:    pthread 互斥量 / pthread 线程 / nanosleep / clock_gettime（ovs_tests 门禁用）
 *
 * 可移植性原则（SKILL.md §7）：平台相关代码集中在移植层，业务 .c 不出现
 * 条件编译；SD 静音脚经 gpio 驱动控制，PC 侧为空操作。
 */

#ifndef AUDIO_PORT_H
#define AUDIO_PORT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- 互斥锁 ---------- */
typedef struct audio_lock audio_lock_t;

audio_lock_t* audio_lock_create(void);
bool          audio_lock_take(audio_lock_t* lock, uint32_t timeout_ms);
void          audio_lock_give(audio_lock_t* lock);
void          audio_lock_destroy(audio_lock_t* lock);

/* ---------- 线程（仅录音任务等长任务；周期短活走 tasker） ---------- */
bool audio_task_create(const char* name, int stack_size, int prio,
                       void (*fn)(void*), void* arg, void** handle_out);
void audio_task_delete(void* handle);   /* handle=NULL 表示删除自身 */

/* ---------- 时基与延时 ---------- */
void     audio_sleep_ms(uint32_t ms);
uint32_t audio_now_ms(void);

/* ---------- SD 静音脚（经 gpio 驱动；PC 空操作） ---------- */
/* active_high=true：写 1 为输出使能、写 0 为关断（MAX98357A SD 脚语义） */
bool audio_port_sd_init(int32_t pin, bool active_high);
bool audio_port_sd_write(int32_t pin, bool level);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_PORT_H */
