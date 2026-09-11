/**
 * @file i18n.h
 * @brief 契约 I1 多语言服务（[HUB] 数据中枢）
 *
 * 翻译源：SPIFFS /i18n/<lang>.json（schema {"lang","ver","strings"}，
 * 与 Web 上位机 GET /i18n/<lang>.json 同一份文件，单一事实来源；
 * 种子见 assets/i18n/，键清单见 three_tasks_plan.md 附录 A）。
 *
 * 线程契约：非线程安全——i18n_init/i18n_set_language/i18n_get 约定在
 * UI 任务上下文调用（板端唯一消费者是 LVGL bridge；Web 直接读文件，
 * 不经本组件）。
 *
 * 可移植性：公共头无平台依赖；文件访问走 stdio（ovs_vfs 挂载），
 * JSON 解析用 thirdparty/cJSON，内存走 mem_pool。
 */

#ifndef I18N_H
#define I18N_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 错误码（负值=错误） */
typedef enum {
    I18N_OK          = 0,
    I18N_ERR_PARAM   = -1,   /* 参数非法 */
    I18N_ERR_IO      = -2,   /* 语言包文件读取失败 */
    I18N_ERR_NOMEM   = -3,   /* 内存不足 */
    I18N_ERR_FORMAT  = -4,   /* JSON 结构不符合 schema */
    I18N_ERR_NO_LANG = -5,   /* 未初始化（当前无语言包） */
} i18n_err_t;

/** 缺省语言包基础路径（ovs_vfs SPIFFS 挂载点 + /i18n） */
#define I18N_DEFAULT_BASE_PATH "/spiffs/i18n"

/**
 * @brief 初始化并载入缺省语言包
 *
 * 失败时组件保持"未载入"状态（i18n_get 回退 label 原文），
 * 不阻塞启动（optional 模块降级语义）。
 *
 * @param default_lang 语言码，如 "zh-CN"；NULL 时只置路径不载入
 */
i18n_err_t i18n_init(const char* default_lang);

/** 热切换语言（如 "zh-CN"）；成功后 UI 层应重建页面刷新文案 */
i18n_err_t i18n_set_language(const char* lang);

/** 翻译：未命中返回 label 原文（每种 label 首次 miss 节流 LOGW 一条） */
const char* i18n_get(const char* label);

/** 当前语言码（未初始化返回 ""） */
const char* i18n_current(void);

/** 是否已有语言包成功载入 */
bool i18n_is_loaded(void);

/* ---- 附加 API（PC 测试/特殊挂载点用，超出 §3 契约的 additive 部分） ---- */

/** 设置语言包基础目录（须在 i18n_init 前调用；缺省 I18N_DEFAULT_BASE_PATH） */
void i18n_set_base_path(const char* path);

/** 释放当前语言包（测试用；之后可重新 init） */
void i18n_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* I18N_H */
