/**
 * @file w25q128.c
 * @brief W25Q128 NOR Flash存储模块实现
 * 
 * 实现W25Q128 Flash的初始化、读写、擦除等功能。
 * 使用SPI总线通信，通过event_bus发布存储状态事件。
 * 
 * @author OVS Team
 * @date 2026-09-07
 */

#include "w25q128.h"
#include "spi_drv.h"
#include "event_bus.h"
#include "tasker.h"
#include "logger.h"

#include "freertos/FreeRTOS.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "dtree.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <inttypes.h>

static const char* TAG = "[W25Q128]";

/* 本模块服务的设备（设备树 compatible），初始化时按它查找自己的节点 */
#define W25Q128_DT_COMPAT   "w25q128-flash"

/** 设备级互斥锁：串行化多任务对同一 SPI 设备的并发访问
 * （ESP-IDF SPI 驱动不允许两个任务对同一设备句柄并发做事务） */
static SemaphoreHandle_t s_dev_mutex = NULL;

/* ========================================================================== */
/*                              常量定义                                       */
/* ========================================================================== */

/** W25Q128 SPI命令 */
#define W25Q128_CMD_READ            0x03
#define W25Q128_CMD_WRITE_ENABLE    0x06
#define W25Q128_CMD_WRITE_DISABLE   0x04
#define W25Q128_CMD_READ_STATUS1    0x05
#define W25Q128_CMD_PAGE_PROGRAM    0x02
#define W25Q128_CMD_SECTOR_ERASE    0x20
#define W25Q128_CMD_BLOCK_ERASE_32K 0x52
#define W25Q128_CMD_BLOCK_ERASE_64K 0xD8
#define W25Q128_CMD_CHIP_ERASE      0xC7
#define W25Q128_CMD_JEDEC_ID        0x9F
#define W25Q128_CMD_POWER_DOWN      0xB9
#define W25Q128_CMD_RELEASE_PD      0xAB

/** W25Q128 状态寄存器位 */
#define W25Q128_STATUS_BUSY         0x01
#define W25Q128_STATUS_WEL          0x02

/** W25Q128 参数 */
#define W25Q128_PAGE_SIZE           256
#define W25Q128_SECTOR_SIZE         4096
#define W25Q128_BLOCK_SIZE_32K      32768
#define W25Q128_BLOCK_SIZE_64K      65536
#define W25Q128_TOTAL_SIZE          (16 * 1024 * 1024)  /* 16MB */

/**
 * 单次/扇区操作忙等上限。
 * 健康芯片通常几十~几百 ms 内完成，但供电/批次差异可能更长；
 * 每次操作前已有 JEDEC 探测，芯片不在线时不会进入该等待，
 * 因此这里保留宽裕上限避免误报超时。
 */
#define W25Q128_BUSY_TIMEOUT_MS     5000

/** 整片擦除忙等上限（W25Q128 整片擦除耗时较长） */
#define W25Q128_CHIP_ERASE_TIMEOUT_MS 60000

/** 连续失败多少次后进入 FAULT */
#define W25Q128_FAULT_THRESHOLD     3

/** FAULT 后多久允许尝试一次恢复探测 */
#define W25Q128_RECOVERY_INTERVAL_MS 1000

/** busy 轮询间隔：ESP-IDF FreeRTOS 默认 100Hz，1ms 会取整为 0 tick */
#define W25Q128_POLL_DELAY_MS       10

/* ========================================================================== */

/* ========================================================================== */
/*                              内部变量                                       */
/* ========================================================================== */

/** SPI驱动句柄 */
static spi_drv_handle_t s_spi_handle = NULL;

/** SPI设备句柄 */
static spi_device_handle_t s_spi_dev = NULL;

/** 初始化标志 */
static bool s_initialized = false;

/** 设备状态 */
static w25q128_state_t s_state = W25Q128_STATE_READY;

/** 连续错误计数 */
static uint32_t s_error_count = 0;

/** 进入 FAULT 的时间（us） */
static uint64_t s_fault_time_us = 0;

/** Flash信息 */
static w25q128_info_t s_info = {0};

/* ========================================================================== */
/*                    健康状态机（READY / FAULT）                              */
/* ========================================================================== */

/**
 * @brief JEDEC ID 探测：芯片在线且类型正确才返回 true
 */
static bool w25q128_probe_device(void) {
    if (!s_spi_handle || !s_spi_dev) {
        return false;
    }

    uint8_t tx_data[4] = {W25Q128_CMD_JEDEC_ID, 0x00, 0x00, 0x00};
    uint8_t rx_data[4] = {0};

    if (spi_drv_transfer(s_spi_handle, s_spi_dev, tx_data, rx_data, 4) != SPI_DRV_OK) {
        return false;
    }

    return rx_data[1] == 0xEF && rx_data[2] == 0x40 && rx_data[3] == 0x18;
}

/**
 * @brief 进入 FAULT 状态（只发布一次事件）
 */
static void w25q128_enter_fault(void) {
    if (s_state == W25Q128_STATE_FAULT) {
        return;
    }

    s_state = W25Q128_STATE_FAULT;
    s_fault_time_us = esp_timer_get_time();
    LOGE(TAG, "Storage FAULT: %lu consecutive errors", (unsigned long)s_error_count);
    EVENT_BUS_PUBLISH_EMPTY(EVENT_STORAGE_ERROR);
}

/**
 * @brief 从 FAULT 恢复到 READY
 */
static void w25q128_recover(void) {
    if (s_state != W25Q128_STATE_FAULT) {
        return;
    }

    s_state = W25Q128_STATE_READY;
    s_error_count = 0;
    LOGI(TAG, "Storage recovered, W25Q128 back to READY");
    EVENT_BUS_PUBLISH_EMPTY(EVENT_STORAGE_READY);
}

static w25q128_err_t w25q128_on_operation_result(w25q128_err_t err);

/**
 * @brief 每次公开操作前的守卫
 *
 * READY：先探测一次，不在线立即计入错误并可能进入 FAULT；
 * FAULT：至少等待 RECOVERY_INTERVAL_MS，才允许再探测一次；
 * 探测成功则恢复 READY 并继续本次操作。
 */
static w25q128_err_t w25q128_begin_operation(void) {
    if (!s_initialized) {
        return W25Q128_ERR_NOT_INIT;
    }

    if (s_state == W25Q128_STATE_FAULT) {
        uint64_t now = esp_timer_get_time();
        if (now - s_fault_time_us < (uint64_t)W25Q128_RECOVERY_INTERVAL_MS * 1000ULL) {
            return W25Q128_ERR_OFFLINE;
        }

        if (!w25q128_probe_device()) {
            /* 仍然不在线：刷新探测时间，避免被高频重试持续打扰 */
            s_fault_time_us = now;
            return W25Q128_ERR_OFFLINE;
        }

        w25q128_recover();
        return W25Q128_OK;
    }

    if (!w25q128_probe_device()) {
        return w25q128_on_operation_result(W25Q128_ERR_OFFLINE);
    }

    return W25Q128_OK;
}

/**
 * @brief 操作结束：成功清零计数；失败累计并可能进入 FAULT
 */
static w25q128_err_t w25q128_on_operation_result(w25q128_err_t err) {
    if (err == W25Q128_OK) {
        s_error_count = 0;
        return W25Q128_OK;
    }

    if (s_state != W25Q128_STATE_FAULT) {
        s_error_count++;
        LOGW(TAG, "Operation failed (%d), error_count=%lu",
             err, (unsigned long)s_error_count);
        if (s_error_count >= W25Q128_FAULT_THRESHOLD) {
            w25q128_enter_fault();
        }
    }
    return err;
}

/* ========================================================================== */
/*                              内部函数                                       */
/* ========================================================================== */

/**
 * @brief 读取状态寄存器1
 */
static w25q128_err_t w25q128_read_status(uint8_t* status) {
    uint8_t tx_data[2] = {W25Q128_CMD_READ_STATUS1, 0x00};
    uint8_t rx_data[2] = {0};
    
    spi_drv_err_t err = spi_drv_transfer(s_spi_handle, s_spi_dev, tx_data, rx_data, 2);
    if (err != SPI_DRV_OK) {
        return W25Q128_ERR_SPI;
    }
    
    *status = rx_data[1];
    return W25Q128_OK;
}

/**
 * @brief 等待Flash空闲
 */
static w25q128_err_t w25q128_wait_busy_ms(uint32_t timeout_ms) {
    uint8_t status;
    int64_t start_us = esp_timer_get_time();

    /* 阶段1：立即查询——典型页编程 tPP<1ms，多数情况一次即完成，
     * 消除固定 10ms 轮询粒度造成的写入吞吐上限 */
    w25q128_err_t err = w25q128_read_status(&status);
    if (err != W25Q128_OK) {
        return err;
    }
    if (!(status & W25Q128_STATUS_BUSY)) {
        return W25Q128_OK;
    }

    /* 阶段2：前 2ms 短自旋细粒度轮询（覆盖页编程窗口，不切换任务） */
    while ((esp_timer_get_time() - start_us) < 2000) {
        esp_rom_delay_us(100);
        err = w25q128_read_status(&status);
        if (err != W25Q128_OK) {
            return err;
        }
        if (!(status & W25Q128_STATUS_BUSY)) {
            return W25Q128_OK;
        }
    }

    /* 阶段3：长等待（扇区/整片擦除），按 tick 让出 CPU 轮询 */
    while (true) {
        vTaskDelay(1);
        err = w25q128_read_status(&status);
        if (err != W25Q128_OK) {
            return err;
        }
        if (!(status & W25Q128_STATUS_BUSY)) {
            return W25Q128_OK;
        }
        if ((esp_timer_get_time() - start_us) >= (int64_t)timeout_ms * 1000) {
            break;
        }
    }

    LOGE(TAG, "Timeout waiting for Flash");
    return W25Q128_ERR_TIMEOUT;
}

static w25q128_err_t w25q128_wait_busy(void) {
    return w25q128_wait_busy_ms(W25Q128_BUSY_TIMEOUT_MS);
}

/**
 * @brief 使能写操作
 */
static w25q128_err_t w25q128_write_enable(void) {
    uint8_t cmd = W25Q128_CMD_WRITE_ENABLE;
    spi_drv_err_t spi_err = spi_drv_write(s_spi_handle, s_spi_dev, &cmd, 1);
    if (spi_err != SPI_DRV_OK) {
        return W25Q128_ERR_SPI;
    }
    
    /* 等待WEL位置位 */
    uint8_t status;
    uint32_t elapsed_ms = 0;
    TickType_t poll_ticks = pdMS_TO_TICKS(W25Q128_POLL_DELAY_MS);
    if (poll_ticks == 0) {
        poll_ticks = 1;
    }
    w25q128_err_t err;
    do {
        err = w25q128_read_status(&status);
        if (err != W25Q128_OK) {
            return err;
        }
        
        if (status & W25Q128_STATUS_WEL) {
            return W25Q128_OK;
        }
        
        vTaskDelay(poll_ticks);
        elapsed_ms += W25Q128_POLL_DELAY_MS;
    } while (elapsed_ms < 1000);
    
    return W25Q128_ERR_TIMEOUT;
}

/**
 * @brief 写入一页数据
 */
static w25q128_err_t w25q128_write_page(uint32_t addr, const void* data, size_t size) {
    if (size > W25Q128_PAGE_SIZE) {
        return W25Q128_ERR_PARAM;
    }
    
    /* 使能写操作 */
    w25q128_err_t err = w25q128_write_enable();
    if (err != W25Q128_OK) {
        return err;
    }
    
    /* 构造命令缓冲区 */
    uint8_t* buf = (uint8_t*)malloc(4 + size);
    if (!buf) {
        return W25Q128_ERR_SPI;
    }
    
    buf[0] = W25Q128_CMD_PAGE_PROGRAM;
    buf[1] = (addr >> 16) & 0xFF;
    buf[2] = (addr >> 8) & 0xFF;
    buf[3] = addr & 0xFF;
    memcpy(buf + 4, data, size);
    
    /* 发送数据 */
    spi_drv_err_t spi_err = spi_drv_write(s_spi_handle, s_spi_dev, buf, 4 + size);
    free(buf);
    
    if (spi_err != SPI_DRV_OK) {
        return W25Q128_ERR_SPI;
    }
    
    /* 等待写入完成 */
    return w25q128_wait_busy();
}

/* ========================================================================== */
/*                              公共API实现                                    */
/* ========================================================================== */

w25q128_err_t w25q128_init(void) {
    if (s_initialized) {
        LOGW(TAG, "Already initialized");
        return W25Q128_OK;
    }
    
    LOGI(TAG, "Initializing W25Q128 Flash...");

    if (!s_dev_mutex) {
        s_dev_mutex = xSemaphoreCreateMutex();
        if (!s_dev_mutex) {
            LOGE(TAG, "Create device mutex failed");
            return W25Q128_ERR_SPI;
        }
    }

    /* 按 compatible 定位自己的设备节点，父节点即所属 SPI 总线 */
    dtree_node_t* dev_node = dtree_find_by_compatible(W25Q128_DT_COMPAT);
    if (!dev_node) {
        LOGE(TAG, "Device node '%s' not found in device tree", W25Q128_DT_COMPAT);
        return W25Q128_ERR_SPI;
    }
    dtree_node_t* bus_node = dtree_get_parent(dev_node);
    if (!bus_node) {
        LOGE(TAG, "Device node '%s' has no parent bus node", W25Q128_DT_COMPAT);
        return W25Q128_ERR_SPI;
    }

    /* 从父总线节点读取SPI总线配置 */
    spi_drv_config_t spi_config;
    spi_drv_err_t spi_err = spi_drv_load_config(bus_node, &spi_config);
    if (spi_err != SPI_DRV_OK) {
        LOGE(TAG, "Failed to load SPI config from device tree: %d", spi_err);
        return W25Q128_ERR_SPI;
    }

    /* 从设备节点读取Flash特定配置 */
    int32_t cs_pin = 13;      /* 默认值 */
    int32_t flash_freq = 20;  /* 默认值 MHz */

    dtree_err_t dt_err;
    dt_err = dtree_get_int(dev_node, "cs_pin", &cs_pin);
    if (dt_err != DTREE_OK) {
        LOGW(TAG, "Failed to read cs_pin from dtree, using default: %" PRId32, cs_pin);
    }

    dt_err = dtree_get_int(dev_node, "spi_freq_mhz", &flash_freq);
    if (dt_err != DTREE_OK) {
        LOGW(TAG, "Failed to read spi_freq_mhz from dtree, using default: %" PRId32, flash_freq);
    }
    
    LOGI(TAG, "Device tree config: CS=GPIO%d, Freq=%d MHz", (int)cs_pin, (int)flash_freq);
    
    /* 初始化SPI驱动（如果尚未初始化） */
    if (!s_spi_handle) {
        spi_err = spi_drv_init(&spi_config, &s_spi_handle);
        if (spi_err != SPI_DRV_OK) {
            LOGE(TAG, "Failed to init SPI: %d", spi_err);
            return W25Q128_ERR_SPI;
        }
    }
    
    /* 添加Flash设备到SPI总线 */
    spi_dev_config_t dev_config = {
        .cs_pin = (int)cs_pin,
        .clock_speed_hz = (int)(flash_freq * 1000000),
        .mode = spi_config.mode,
        .xfer_mode = SPI_XFER_MODE_POLLING,  // Flash使用轮询，小数据量
        .max_transfer_sz = 4096,
    };
    spi_err = spi_drv_add_device(s_spi_handle, &dev_config, &s_spi_dev);
    if (spi_err != SPI_DRV_OK) {
        LOGE(TAG, "Failed to add SPI device: %d", spi_err);
        if (s_spi_handle) {
            spi_drv_deinit(s_spi_handle);
            s_spi_handle = NULL;
        }
        return W25Q128_ERR_SPI;
    }
    
    /* 发送释放掉电命令，确保W25Q128处于活动状态 */
    uint8_t cmd = W25Q128_CMD_RELEASE_PD;
    
    spi_drv_write(s_spi_handle, s_spi_dev, &cmd, 1);
    vTaskDelay(pdMS_TO_TICKS(50));  /* 等待芯片唤醒 */
    /* 使用全双工传输读取JEDEC ID */
    uint8_t tx_data[4] = {W25Q128_CMD_JEDEC_ID, 0x00, 0x00, 0x00};
    uint8_t rx_data[4] = {0};
    
    LOGI(TAG, "Reading JEDEC ID with full-duplex transfer...");
    spi_err = spi_drv_transfer(s_spi_handle, s_spi_dev, tx_data, rx_data, 4);
    if (spi_err != SPI_DRV_OK) {
        LOGE(TAG, "Failed to read JEDEC ID: %d", spi_err);
        spi_drv_remove_device(s_spi_handle, s_spi_dev);
        s_spi_dev = NULL;
        spi_drv_deinit(s_spi_handle);
        s_spi_handle = NULL;
        return W25Q128_ERR_SPI;
    }
    
    /* 解析JEDEC ID (rx_data[1]是制造商ID，rx_data[2]是存储类型，rx_data[3]是容量) */
    uint8_t id[3] = {rx_data[1], rx_data[2], rx_data[3]};
    
    LOGI(TAG, "JEDEC ID: 0x%02X 0x%02X 0x%02X", id[0], id[1], id[2]);
    
    /* 验证ID (Winbond W25Q128: EF 40 18) */
    if (id[0] == 0xFF && id[1] == 0xFF && id[2] == 0xFF) {
        LOGE(TAG, "JEDEC ID is 0xFF 0xFF 0xFF, SPI communication failed!");
        spi_drv_remove_device(s_spi_handle, s_spi_dev);
        s_spi_dev = NULL;
        spi_drv_deinit(s_spi_handle);
        s_spi_handle = NULL;
        return W25Q128_ERR_SPI;
    }
    
    if (id[0] != 0xEF || id[1] != 0x40 || id[2] != 0x18) {
        LOGW(TAG, "Unexpected JEDEC ID: 0x%02X 0x%02X 0x%02X", id[0], id[1], id[2]);
        LOGW(TAG, "Expected: 0xEF 0x40 0x18 (Winbond W25Q128)");
    }
    
    /* 保存信息 */
    s_info.manufacturer_id = id[0];
    s_info.memory_type = id[1];
    s_info.capacity = id[2];
    s_info.total_size = W25Q128_TOTAL_SIZE;
    s_info.sector_size = W25Q128_SECTOR_SIZE;
    s_info.page_size = W25Q128_PAGE_SIZE;
    s_info.sector_count = W25Q128_TOTAL_SIZE / W25Q128_SECTOR_SIZE;
    
    s_initialized = true;
    s_state = W25Q128_STATE_READY;
    s_error_count = 0;
    s_fault_time_us = 0;
    
    LOGI(TAG, "W25Q128 Flash initialized: %lu MB, %lu sectors", 
         s_info.total_size / (1024 * 1024), s_info.sector_count);
    
    /* 发布存储就绪事件 */
    EVENT_BUS_PUBLISH_EMPTY(EVENT_STORAGE_READY);
    
    return W25Q128_OK;
}

w25q128_err_t w25q128_deinit(void) {
    if (!s_initialized) {
        return W25Q128_OK;
    }
    
    LOGI(TAG, "Deinitializing W25Q128 Flash...");
    
    /* 发送掉电命令 */
    uint8_t cmd = W25Q128_CMD_POWER_DOWN;
    spi_drv_write(s_spi_handle, s_spi_dev, &cmd, 1);
    
    /* 反初始化SPI */
    if (s_spi_handle) {
        if (s_spi_dev) {
            spi_drv_remove_device(s_spi_handle, s_spi_dev);
            s_spi_dev = NULL;
        }
        spi_drv_deinit(s_spi_handle);
        s_spi_handle = NULL;
    }
    
    s_initialized = false;
    s_state = W25Q128_STATE_READY;
    s_error_count = 0;
    s_fault_time_us = 0;
    
    LOGI(TAG, "W25Q128 Flash deinitialized");
    
    return W25Q128_OK;
}

w25q128_err_t w25q128_get_info(w25q128_info_t* info) {
    if (!info) {
        return W25Q128_ERR_PARAM;
    }
    
    if (!s_initialized) {
        return W25Q128_ERR_NOT_INIT;
    }

    if (s_state == W25Q128_STATE_FAULT) {
        return W25Q128_ERR_OFFLINE;
    }
    
    *info = s_info;
    
    return W25Q128_OK;
}

static w25q128_err_t w25q128_read_locked(uint32_t addr, void* buffer, size_t size) {
    if (!buffer) {
        return W25Q128_ERR_PARAM;
    }

    w25q128_err_t err = w25q128_begin_operation();
    if (err != W25Q128_OK) {
        return err;
    }

    if (addr + size > s_info.total_size) {
        LOGE(TAG, "Read out of range: addr=0x%06lX, size=%zu", addr, size);
        return W25Q128_ERR_PARAM;
    }
    
    /* 使用全双工传输：发送命令+地址，同时接收数据
     * W25Q128 读取命令需要CS在整个序列中保持低电平：
     * 发送: cmd(1) + addr(3) + dummy(N)
     * 接收: ignore(4) + data(N)
     */
    size_t total = 4 + size;
    uint8_t* tx_buf = (uint8_t*)malloc(total);
    uint8_t* rx_buf = (uint8_t*)malloc(total);
    if (!tx_buf || !rx_buf) {
        free(tx_buf);
        free(rx_buf);
        return w25q128_on_operation_result(W25Q128_ERR_SPI);
    }
    
    /* 构造发送缓冲区 */
    tx_buf[0] = W25Q128_CMD_READ;
    tx_buf[1] = (addr >> 16) & 0xFF;
    tx_buf[2] = (addr >> 8) & 0xFF;
    tx_buf[3] = addr & 0xFF;
    memset(tx_buf + 4, 0xFF, size);  /* 发送0xFF作为dummy bytes */
    
    /* 全双工传输 */
    spi_drv_err_t spi_err = spi_drv_transfer(s_spi_handle, s_spi_dev, tx_buf, rx_buf, total);
    
    if (spi_err != SPI_DRV_OK) {
        free(tx_buf);
        free(rx_buf);
        return w25q128_on_operation_result(W25Q128_ERR_SPI);
    }
    
    /* 复制接收到的数据（跳过前4个字节的命令阶段） */
    memcpy(buffer, rx_buf + 4, size);
    
    free(tx_buf);
    free(rx_buf);
    
    return w25q128_on_operation_result(W25Q128_OK);
}

static w25q128_err_t w25q128_write_locked(uint32_t addr, const void* data, size_t size) {
    if (!data) {
        return W25Q128_ERR_PARAM;
    }

    w25q128_err_t err = w25q128_begin_operation();
    if (err != W25Q128_OK) {
        return err;
    }

    if (addr + size > s_info.total_size) {
        LOGE(TAG, "Write out of range: addr=0x%06lX, size=%zu", addr, size);
        return W25Q128_ERR_PARAM;
    }
    
    /* 分页写入 */
    size_t remaining = size;
    const uint8_t* src = (const uint8_t*)data;
    uint32_t current_addr = addr;
    uint32_t page_count = 0;
    
    while (remaining > 0) {
        /* 计算当前页剩余空间 */
        size_t page_remaining = W25Q128_PAGE_SIZE - (current_addr % W25Q128_PAGE_SIZE);
        size_t write_size = (remaining < page_remaining) ? remaining : page_remaining;
        
        /* 写入一页 */
        err = w25q128_write_page(current_addr, src, write_size);
        if (err != W25Q128_OK) {
            LOGE(TAG, "Write failed at 0x%06lX", current_addr);
            return w25q128_on_operation_result(err);
        }

        remaining -= write_size;
        src += write_size;
        current_addr += write_size;
        page_count++;
    }

    if (page_count > 0) {
        LOGD(TAG, "Write completed: addr=0x%06lX, pages=%lu",
             (unsigned long)addr, (unsigned long)page_count);
    }

    return w25q128_on_operation_result(W25Q128_OK);
}

static w25q128_err_t w25q128_erase_sector_locked(uint32_t sector_index) {
    w25q128_err_t err = w25q128_begin_operation();
    if (err != W25Q128_OK) {
        return err;
    }

    if (sector_index >= s_info.sector_count) {
        LOGE(TAG, "Sector index out of range: %lu", sector_index);
        return W25Q128_ERR_PARAM;
    }
    
    uint32_t addr = sector_index * W25Q128_SECTOR_SIZE;
    
    LOGD(TAG, "Erasing sector %lu (addr=0x%06lX)", sector_index, addr);
    
    /* 使能写操作 */
    err = w25q128_write_enable();
    if (err != W25Q128_OK) {
        return w25q128_on_operation_result(err);
    }
    
    /* 发送擦除命令 */
    uint8_t cmd[4];
    cmd[0] = W25Q128_CMD_SECTOR_ERASE;
    cmd[1] = (addr >> 16) & 0xFF;
    cmd[2] = (addr >> 8) & 0xFF;
    cmd[3] = addr & 0xFF;
    
    spi_drv_err_t spi_err = spi_drv_write(s_spi_handle, s_spi_dev, cmd, 4);
    if (spi_err != SPI_DRV_OK) {
        return w25q128_on_operation_result(W25Q128_ERR_SPI);
    }
    
    /* 等待擦除完成 */
    return w25q128_on_operation_result(w25q128_wait_busy());
}

static w25q128_err_t w25q128_erase_chip_locked(void) {
    w25q128_err_t err = w25q128_begin_operation();
    if (err != W25Q128_OK) {
        return err;
    }

    LOGI(TAG, "Erasing entire chip...");
    
    /* 使能写操作 */
    err = w25q128_write_enable();
    if (err != W25Q128_OK) {
        return w25q128_on_operation_result(err);
    }
    
    /* 发送整片擦除命令 */
    uint8_t cmd = W25Q128_CMD_CHIP_ERASE;
    spi_drv_err_t spi_err = spi_drv_write(s_spi_handle, s_spi_dev, &cmd, 1);
    if (spi_err != SPI_DRV_OK) {
        return w25q128_on_operation_result(W25Q128_ERR_SPI);
    }
    
    /* 等待擦除完成（可能需要较长时间） */
    LOGI(TAG, "Waiting for chip erase to complete...");
    return w25q128_on_operation_result(
        w25q128_wait_busy_ms(W25Q128_CHIP_ERASE_TIMEOUT_MS));
}

bool w25q128_is_initialized(void) {
    return s_initialized;
}

bool w25q128_is_ready(void) {
    return s_initialized && s_state == W25Q128_STATE_READY;
}

static w25q128_err_t w25q128_health_check_locked(void) {
    if (!s_initialized) {
        return W25Q128_ERR_NOT_INIT;
    }

    if (w25q128_probe_device()) {
        w25q128_recover();
        return W25Q128_OK;
    }

    return w25q128_on_operation_result(W25Q128_ERR_OFFLINE);
}

/* ========== 设备级串行化包装（多任务安全） ========== */
/* 同一 SPI 设备句柄不允许跨任务并发事务，VFS 块设备回调可能来自
 * 不同任务（不同挂载点的文件操作），在此统一串行化 */

#define W25Q128_DEV_LOCK() do { \
        if (!s_dev_mutex) { \
            return W25Q128_ERR_NOT_INIT; \
        } \
        xSemaphoreTake(s_dev_mutex, portMAX_DELAY); \
    } while (0)

#define W25Q128_DEV_UNLOCK() do { xSemaphoreGive(s_dev_mutex); } while (0)

w25q128_err_t w25q128_read(uint32_t addr, void* buffer, size_t size) {
    W25Q128_DEV_LOCK();
    w25q128_err_t err = w25q128_read_locked(addr, buffer, size);
    W25Q128_DEV_UNLOCK();
    return err;
}

w25q128_err_t w25q128_write(uint32_t addr, const void* data, size_t size) {
    W25Q128_DEV_LOCK();
    w25q128_err_t err = w25q128_write_locked(addr, data, size);
    W25Q128_DEV_UNLOCK();
    return err;
}

w25q128_err_t w25q128_erase_sector(uint32_t sector_index) {
    W25Q128_DEV_LOCK();
    w25q128_err_t err = w25q128_erase_sector_locked(sector_index);
    W25Q128_DEV_UNLOCK();
    return err;
}

w25q128_err_t w25q128_erase_chip(void) {
    W25Q128_DEV_LOCK();
    w25q128_err_t err = w25q128_erase_chip_locked();
    W25Q128_DEV_UNLOCK();
    return err;
}

w25q128_err_t w25q128_health_check(void) {
    W25Q128_DEV_LOCK();
    w25q128_err_t err = w25q128_health_check_locked();
    W25Q128_DEV_UNLOCK();
    return err;
}
