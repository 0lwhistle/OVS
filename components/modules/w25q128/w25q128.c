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
 * @brief 等待Flash空闲
 */
static w25q128_err_t w25q128_wait_busy(void) {
    uint8_t status;
    uint32_t timeout = 0;
    
    do {
        /* 发送读状态命令 */
        uint8_t cmd = W25Q128_CMD_READ_STATUS1;
        /* CS pin managed by SPI driver */
        spi_drv_write(s_spi_handle, s_spi_dev, &cmd, 1);
        spi_drv_read(s_spi_handle, s_spi_dev, &status, 1);
        /* CS pin managed by SPI driver */
        
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
    spi_drv_write(s_spi_handle, s_spi_dev, &cmd, 1);
    
    /* 等待WEL位置位 */
    uint8_t status;
    uint32_t timeout = 0;
    do {
        cmd = W25Q128_CMD_READ_STATUS1;
        /* CS pin managed by SPI driver */
        spi_drv_write(s_spi_handle, s_spi_dev, &cmd, 1);
        spi_drv_read(s_spi_handle, s_spi_dev, &status, 1);
        /* CS pin managed by SPI driver */
        
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
    spi_drv_write(s_spi_handle, s_spi_dev, buf, 4 + size);
    
    free(buf);
    
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
    
    /* 加载SPI配置 */
    spi_drv_config_t spi_config;
    spi_drv_err_t spi_err = spi_drv_load_config(&spi_config);
    if (spi_err != SPI_DRV_OK) {
        LOGE(TAG, "Failed to load SPI config: %d", spi_err);
        return W25Q128_ERR_SPI;
    }
    
    /* 初始化SPI驱动（如果尚未初始化） */
    if (!s_spi_handle) {
        spi_err = spi_drv_init(&spi_config, &s_spi_handle);
        if (spi_err != SPI_DRV_OK) {
            LOGE(TAG, "Failed to init SPI: %d", spi_err);
            return W25Q128_ERR_SPI;
        }
    }
    
    /* 添加Flash设备到SPI总线 */
    int32_t cs_pin, spi_freq;
    DTREE_INT("spi.flash", "cs_pin", &cs_pin);
    DTREE_INT("spi.flash", "spi_freq_mhz", &spi_freq);
    if (spi_freq == 0) spi_freq = 20;
    
    spi_err = spi_drv_add_device(s_spi_handle, (int)cs_pin, 
                                  (int)(spi_freq * 1000000), 0, &s_spi_dev);
    if (spi_err != SPI_DRV_OK) {
        LOGE(TAG, "Failed to add SPI device: %d", spi_err);
        return W25Q128_ERR_SPI;
    }
    
    /* 读取JEDEC ID */
    uint8_t cmd = W25Q128_CMD_JEDEC_ID;
    uint8_t id[3];
    
    spi_drv_write(s_spi_handle, s_spi_dev, &cmd, 1);
    spi_drv_read(s_spi_handle, s_spi_dev, id, 3);
    
    LOGI(TAG, "JEDEC ID: 0x%02X 0x%02X 0x%02X", id[0], id[1], id[2]);
    
    /* 验证ID (Winbond W25Q128: EF 40 18) */
    if (id[0] != 0xEF || id[1] != 0x40 || id[2] != 0x18) {
        LOGW(TAG, "Unexpected JEDEC ID, expected Winbond W25Q128");
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
    
    /* 构造读命令 */
    uint8_t cmd[4];
    cmd[0] = W25Q128_CMD_READ;
    cmd[1] = (addr >> 16) & 0xFF;
    cmd[2] = (addr >> 8) & 0xFF;
    cmd[3] = addr & 0xFF;
    
    /* 发送命令和地址 */
    spi_drv_write(s_spi_handle, s_spi_dev, cmd, 4);
    
    /* 读取数据 */
    spi_drv_read(s_spi_handle, s_spi_dev, buffer, size);
    
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
    
    spi_drv_write(s_spi_handle, s_spi_dev, cmd, 4);
    
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
    spi_drv_write(s_spi_handle, s_spi_dev, &cmd, 1);
    
    /* 等待擦除完成（可能需要较长时间） */
    LOGI(TAG, "Waiting for chip erase to complete...");
    return w25q128_wait_busy();
}

bool w25q128_is_initialized(void) {
    return s_initialized;
}
