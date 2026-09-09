/**
 * @file dtb_ab.h
 * @brief 设备树 A/B 裸分区管理（事务性指针切换 + 试运行配对）
 *
 * 两个裸分区槽（dtb_0/dtb_1，子类型 0xA0）存放 pack_dtb.py 生成的容器：
 *   [ "DTBI"(4) | ver(1) | rsv(3) | json_len(4LE) | sha256(json)(32) | json ]
 *
 * OTA 只写非活动槽；全部校验通过后经 NVS 指针提交：
 *   - 随 app OTA：写 slot 后置 dtb_trial=slot（不动 active），新 app 15s
 *     待确认窗口内用 trial 槽（app+树配对试运行），ota_confirm_running()
 *     确认后 active=trial；app 崩溃回滚则 active 未动，天然回退旧树。
 *   - 独立更新通道：写 slot 校验通过后直接 set_active 翻转。
 * 网络中断/断电 → 指针未动 → 启动照常使用旧树。
 *
 * 槽格式与 scripts/pack_dtb.py 保持一致，改动需两端同步。
 */

#ifndef DTB_AB_H
#define DTB_AB_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 槽内 JSON 长度上限（槽 64KB 减去头部与余量） */
#define DTB_AB_MAX_JSON  (32 * 1024)

/**
 * @brief 启动选树：按 app 待确认状态选取槽并读出 JSON 文本
 *
 * 选取顺序：app PENDING_VERIFY → dtb_trial 槽；否则 dtb_active 槽；
 * 选中槽校验失败自动尝试另一槽。app 不在待确认窗口时顺带清掉残留 trial。
 *
 * @param out 成功时填充；json 为调用方 free 的缓冲（已补 '\0'）
 * @return 0 成功；-1 两槽均不可用
 */
int dtb_ab_load(uint8_t **json, size_t *len, int *used_slot);

/**
 * @brief 校验容器并写入非活动槽（擦除→写入→读回校验 SHA）
 * @param dtb_bin pack_dtb.py 生成的完整容器
 * @param written_slot 输出实际写入的槽号
 * @return 0 成功；-1 容器非法/写失败/校验不符
 */
int dtb_ab_write_inactive(const uint8_t *dtb_bin, size_t len, int *written_slot);

/** OTA 提交：配合 app 待确认窗口，仅登记 trial，不动 active */
int dtb_ab_commit_trial(int slot);

/** app 15s 确认有效时调用：active=trial 并清除 trial */
void dtb_ab_confirm_trial(void);

/** 独立更新通道：直接翻转 active（下次启动生效） */
int dtb_ab_set_active(int slot);

/** 当前活动槽号（调试/状态查询） */
int dtb_ab_active_slot(void);

#ifdef __cplusplus
}
#endif

#endif /* DTB_AB_H */
