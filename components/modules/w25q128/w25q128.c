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
#include "dtree.h"
#include "freertos/task.h"

#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

static const char* TAG = "[W25Q128]";

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

/** Flash信息 */
static w25q128_info_t s_info = {0};

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
static w25q128_err_t w25q128_wait_busy(void) {
    uint8_t status;
    uint32_t timeout = 0;
    
    do {
        w25q128_err_t err = w25q128_read_status(&status);
        if (err != W25Q128_OK) {
            return err;
        }
        
        if (!(status & W25Q128_STATUS_BUSY)) {
            return W25Q128_OK;
        }
        
        vTaskDelay(pdMS_TO_TICKS(1));
        timeout++;
    } while (timeout < 5000);
    
    LOGE(TAG, "Timeout waiting for Flash");
    return W25Q128_ERR_TIMEOUT;
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
    uint32_t timeout = 0;
    w25q128_err_t err;
    do {
        err = w25q128_read_status(&status);
        if (err != W25Q128_OK) {
            return err;
        }
        
        if (status & W25Q128_STATUS_WEL) {
            return W25Q128_OK;
        }
        
        vTaskDelay(pdMS_TO_TICKS(1));
        timeout++;
    } while (timeout < 1000);
    
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
    
    /* 从设备树读取SPI总线配置 */
    spi_drv_config_t spi_config;
    spi_drv_err_t spi_err = spi_drv_load_config(&spi_config);
    if (spi_err != SPI_DRV_OK) {
        LOGE(TAG, "Failed to load SPI config from device tree: %d", spi_err);
        return W25Q128_ERR_SPI;
    }
    
    /* 从设备树读取Flash特定配置 */
    int32_t cs_pin = 13;      /* 默认值 */
    int32_t flash_freq = 20;  /* 默认值 MHz */
    
    dtree_err_t dt_err;
    dt_err = DTREE_INT("spi.flash", "cs_pin", &cs_pin);
    if (dt_err != DTREE_OK) {
        LOGW(TAG, "Failed to read cs_pin from dtree, using default: %d", cs_pin);
    }
    
    dt_err = DTREE_INT("spi.flash", "spi_freq_mhz", &flash_freq);
    if (dt_err != DTREE_OK) {
        LOGW(TAG, "Failed to read spi_freq_mhz from dtree, using default: %d", flash_freq);
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
    spi_err = spi_drv_add_device(s_spi_handle, 
                                 (int)cs_pin, 
                                 (int)(flash_freq * 1000000), 
                                 spi_config.mode, 
                                 &s_spi_dev);
    if (spi_err != SPI_DRV_OK) {
        LOGE(TAG, "Failed to add SPI device: %d", spi_err);
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
        return W25Q128_ERR_SPI;
    }
    
    /* 解析JEDEC ID (rx_data[1]是制造商ID，rx_data[2]是存储类型，rx_data[3]是容量) */
    uint8_t id[3] = {rx_data[1], rx_data[2], rx_data[3]};
    
    LOGI(TAG, "JEDEC ID: 0x%02X 0x%02X 0x%02X", id[0], id[1], id[2]);
    
    /* 验证ID (Winbond W25Q128: EF 40 18) */
    if (id[0] == 0xFF && id[1] == 0xFF && id[2] == 0xFF) {
        LOGE(TAG, "JEDEC ID is 0xFF 0xFF 0xFF, SPI communication failed!");
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
        spi_drv_deinit(s_spi_handle);
        s_spi_handle = NULL;
        s_spi_dev = NULL;
    }
    
    s_initialized = false;
    
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
    
    *info = s_info;
    
    return W25Q128_OK;
}

w25q128_err_t w25q128_read(uint32_t addr, void* buffer, size_t size) {
    if (!buffer) {
        return W25Q128_ERR_PARAM;
    }
    
    if (!s_initialized) {
        return W25Q128_ERR_NOT_INIT;
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
        return W25Q128_ERR_SPI;
    }
    
    /* 构造发送缓冲区 */
    tx_buf[0] = W25Q128_CMD_READ;
    tx_buf[1] = (addr >> 16) & 0xFF;
    tx_buf[2] = (addr >> 8) & 0xFF;
    tx_buf[3] = addr & 0xFF;
    memset(tx_buf + 4, 0xFF, size);  /* 发送0xFF作为dummy bytes */
    
    /* 全双工传输 */
    spi_drv_err_t err = spi_drv_transfer(s_spi_handle, s_spi_dev, tx_buf, rx_buf, total);
    
    if (err != SPI_DRV_OK) {
        free(tx_buf);
        free(rx_buf);
        return W25Q128_ERR_SPI;
    }
    
    /* 复制接收到的数据（跳过前4个字节的命令阶段） */
    memcpy(buffer, rx_buf + 4, size);
    
    free(tx_buf);
    free(rx_buf);
    
    return W25Q128_OK;
}

w25q128_err_t w25q128_write(uint32_t addr, const void* data, size_t size) {
    if (!data) {
        return W25Q128_ERR_PARAM;
    }
    
    if (!s_initialized) {
        return W25Q128_ERR_NOT_INIT;
    }
    
    if (addr + size > s_info.total_size) {
        LOGE(TAG, "Write out of range: addr=0x%06lX, size=%zu", addr, size);
        return W25Q128_ERR_PARAM;
    }
    
    /* 分页写入 */
    size_t remaining = size;
    const uint8_t* src = (const uint8_t*)data;
    uint32_t current_addr = addr;
    
    while (remaining > 0) {
        /* 计算当前页剩余空间 */
        size_t page_remaining = W25Q128_PAGE_SIZE - (current_addr % W25Q128_PAGE_SIZE);
        size_t write_size = (remaining < page_remaining) ? remaining : page_remaining;
        
        /* 写入一页 */
        w25q128_err_t err = w25q128_write_page(current_addr, src, write_size);
        if (err != W25Q128_OK) {
            LOGE(TAG, "Write failed at 0x%06lX", current_addr);
            return err;
        }
        
        remaining -= write_size;
        src += write_size;
        current_addr += write_size;
    }
    
    return W25Q128_OK;
}

w25q128_err_t w25q128_erase_sector(uint32_t sector_index) {
    if (!s_initialized) {
        return W25Q128_ERR_NOT_INIT;
    }
    
    if (sector_index >= s_info.sector_count) {
        LOGE(TAG, "Sector index out of range: %lu", sector_index);
        return W25Q128_ERR_PARAM;
    }
    
    uint32_t addr = sector_index * W25Q128_SECTOR_SIZE;
    
    LOGD(TAG, "Erasing sector %lu (addr=0x%06lX)", sector_index, addr);
    
    /* 使能写操作 */
    w25q128_err_t err = w25q128_write_enable();
    if (err != W25Q128_OK) {
        return err;
    }
    
    /* 发送擦除命令 */
    uint8_t cmd[4];
    cmd[0] = W25Q128_CMD_SECTOR_ERASE;
    cmd[1] = (addr >> 16) & 0xFF;
    cmd[2] = (addr >> 8) & 0xFF;
    cmd[3] = addr & 0xFF;
    
    spi_drv_err_t spi_err = spi_drv_write(s_spi_handle, s_spi_dev, cmd, 4);
    if (spi_err != SPI_DRV_OK) {
        return W25Q128_ERR_SPI;
    }
    
    /* 等待擦除完成 */
    return w25q128_wait_busy();
}

w25q128_err_t w25q128_erase_chip(void) {
    if (!s_initialized) {
        return W25Q128_ERR_NOT_INIT;
    }
    
    LOGI(TAG, "Erasing entire chip...");
    
    /* 使能写操作 */
    w25q128_err_t err = w25q128_write_enable();
    if (err != W25Q128_OK) {
        return err;
    }
    
    /* 发送整片擦除命令 */
    uint8_t cmd = W25Q128_CMD_CHIP_ERASE;
    spi_drv_err_t spi_err = spi_drv_write(s_spi_handle, s_spi_dev, &cmd, 1);
    if (spi_err != SPI_DRV_OK) {
        return W25Q128_ERR_SPI;
    }
    
    /* 等待擦除完成（可能需要较长时间） */
    LOGI(TAG, "Waiting for chip erase to complete...");
    return w25q128_wait_busy();
}

bool w25q128_is_initialized(void) {
    return s_initialized;
}
