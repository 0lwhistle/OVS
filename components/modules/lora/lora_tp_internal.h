/**
 * @file lora_tp_internal.h
 * @brief lora_tp 内部实例 API（私有头，仅 tests/ 回环测试 include）
 *
 * 公共 API 是单例（静态默认实例）；PC 回环测试需要两个对等实例
 * （A/B 互发），故内部按实例上下文拆分。公共接口保持 design §2 冻结
 * 集不变——本头的函数不属于公共契约。
 */

#ifndef LORA_TP_INTERNAL_H
#define LORA_TP_INTERNAL_H

#include "lora_tp.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 配置（设备树 lora_tp 节点读取，全部有缺省兜底） */
typedef struct {
    uint16_t local_addr;            /* 本机地址，缺省 0x0001 */
    uint8_t  tx_queue_depth;        /* 缺省 4 */
    uint8_t  window;                /* 滑窗 W，缺省 4 */
    uint8_t  max_retries;           /* 整窗重试次数，缺省 3 */
    uint32_t msg_max_bytes;         /* 缺省 65536 */
    uint32_t rx_inline_max_bytes;   /* 缺省 2048 */
    uint32_t ack_timeout_ms[3];     /* NORMAL/FAST/SLOW（手册核实前占位） */
    uint32_t tx_gap_ms[3];          /* NORMAL/FAST/SLOW（手册核实前占位） */
} lora_tp_cfg_t;

/** 缺省配置（design §5 数值，占位标注见 lora_tp.c） */
void lora_tp_cfg_default(lora_tp_cfg_t* cfg);

/** 实例上下文（不透明；调用方分配，lora_tp_inst_init 初始化） */
typedef struct lora_tp_ctx lora_tp_ctx_t;

/** 实例上下文字节数（调用方据此分配内存） */
size_t lora_tp_inst_ctx_size(void);

lora_tp_ctx_t* lora_tp_inst_ctx(void* mem, size_t mem_size);

lora_tp_err_t lora_tp_inst_init(lora_tp_ctx_t* inst, const lora_tp_deps_t* deps,
                                const lora_tp_cfg_t* cfg);
lora_tp_err_t lora_tp_inst_deinit(lora_tp_ctx_t* inst);
void lora_tp_inst_tick(lora_tp_ctx_t* inst);

lora_tp_err_t lora_tp_inst_send(lora_tp_ctx_t* inst, const lora_tp_des_t* des,
                                const lora_tp_source_t* src, uint32_t total,
                                lora_tp_token_t* token);
lora_tp_err_t lora_tp_inst_cancel(lora_tp_ctx_t* inst, lora_tp_token_t token);
lora_tp_err_t lora_tp_inst_query(lora_tp_ctx_t* inst, lora_tp_token_t token,
                                 lora_tp_state_t* st, uint32_t* permille);
lora_tp_err_t lora_tp_inst_register_rx(lora_tp_ctx_t* inst,
                                       lora_tp_rx_cb_t cb, void* user);
lora_tp_err_t lora_tp_inst_set_rx_sink(lora_tp_ctx_t* inst,
                                       const lora_tp_sink_t* sink);
lora_tp_err_t lora_tp_inst_get_stats(lora_tp_ctx_t* inst, lora_tp_stats_t* s);

/** 测试注入：直接喂一帧（mock 驱动回环走这里，绕过 drv->register_rx） */
void lora_tp_inst_on_frame(lora_tp_ctx_t* inst, const uint8_t* data, size_t len,
                           int8_t rssi);

#ifdef __cplusplus
}
#endif

#endif /* LORA_TP_INTERNAL_H */
