/**
 * @file event_bus_types.h
 * @brief 事件总线类型定义
 * 
 * 定义事件类型枚举和事件数据结构。
 * 事件类型按模块分组，高16位为模块ID，低16位为事件ID。
 * 
 * @author OVS Team
 * @date 2026-09-07
 */

#ifndef EVENT_BUS_TYPES_H
#define EVENT_BUS_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/*                              常量定义                                       */
/* ========================================================================== */

/** 事件总线版本 */
#define EVENT_BUS_VERSION_MAJOR     1
#define EVENT_BUS_VERSION_MINOR     0
#define EVENT_BUS_VERSION_PATCH     0

/** 事件队列大小 */
#define EVENT_BUS_QUEUE_SIZE        32

/** 最大订阅者数量 */
#define EVENT_BUS_MAX_SUBSCRIBERS   64

/** 最大事件数据大小 (字节) */
#define EVENT_BUS_MAX_EVENT_SIZE    256

/** 事件处理任务栈大小 */
#define EVENT_BUS_TASK_STACK_SIZE   4096

/** 事件处理任务优先级 (FreeRTOS) */
#define EVENT_BUS_TASK_PRIORITY     5

/** 事件处理任务轮询间隔 (ms) */
#define EVENT_BUS_POLL_INTERVAL_MS  10

/** 无效的订阅ID */
#define EVENT_BUS_INVALID_SUB_ID    0

/* ========================================================================== */
/*                              模块ID定义                                     */
/* ========================================================================== */

/**
 * @brief 模块ID定义
 * 
 * 每个模块有唯一的ID，用于事件类型的高位标识
 */
#define MODULE_ID_SYSTEM        0x0001
#define MODULE_ID_WIFI          0x0002
#define MODULE_ID_SENSOR        0x0003
#define MODULE_ID_TOUCH         0x0004
#define MODULE_ID_LORA          0x0005
#define MODULE_ID_UI            0x0006
#define MODULE_ID_AUDIO         0x0007
#define MODULE_ID_STORAGE       0x0008
#define MODULE_ID_DISPLAY       0x0009
#define MODULE_ID_WEB           0x000A

/* ========================================================================== */
/*                              事件类型枚举                                   */
/* ========================================================================== */

/**
 * @brief 事件类型枚举
 * 
 * 事件类型格式: (MODULE_ID << 16) | EVENT_ID
 * 
 * 使用示例:
 * @code
 *   event_bus_publish(EVENT_WIFI_CONNECTED, &data, sizeof(data));
 * @endcode
 */
typedef enum {
    /* ====================================================================== */
    /* 系统事件 (MODULE_ID_SYSTEM = 0x0001)                                    */
    /* ====================================================================== */
    
    /** 系统启动完成 */
    EVENT_SYSTEM_STARTUP            = (MODULE_ID_SYSTEM << 16) | 0x0001,
    
    /** 系统即将关闭 */
    EVENT_SYSTEM_SHUTDOWN           = (MODULE_ID_SYSTEM << 16) | 0x0002,
    
    /** 系统错误 */
    EVENT_SYSTEM_ERROR              = (MODULE_ID_SYSTEM << 16) | 0x0003,
    
    /** 系统重启请求 */
    EVENT_SYSTEM_REBOOT             = (MODULE_ID_SYSTEM << 16) | 0x0004,
    
    /** 看门狗喂狗 */
    EVENT_SYSTEM_WATCHDOG_FEED      = (MODULE_ID_SYSTEM << 16) | 0x0005,
    
    /* ====================================================================== */
    /* WiFi事件 (MODULE_ID_WIFI = 0x0002)                                     */
    /* ====================================================================== */
    
    /** WiFi已连接到AP */
    EVENT_WIFI_CONNECTED            = (MODULE_ID_WIFI << 16) | 0x0001,
    
    /** WiFi已断开连接 */
    EVENT_WIFI_DISCONNECTED         = (MODULE_ID_WIFI << 16) | 0x0002,
    
    /** WiFi扫描完成 */
    EVENT_WIFI_SCAN_DONE            = (MODULE_ID_WIFI << 16) | 0x0003,
    
    /** WiFi开始切换AP */
    EVENT_WIFI_SWITCH_START         = (MODULE_ID_WIFI << 16) | 0x0004,
    
    /** WiFi切换AP完成 */
    EVENT_WIFI_SWITCH_DONE          = (MODULE_ID_WIFI << 16) | 0x0005,
    
    /** WiFi切换AP失败 */
    EVENT_WIFI_SWITCH_FAILED        = (MODULE_ID_WIFI << 16) | 0x0006,
    
    /** WiFi获取IP地址 */
    EVENT_WIFI_GOT_IP               = (MODULE_ID_WIFI << 16) | 0x0007,
    
    /** SoftAP已启动 */
    EVENT_WIFI_AP_STARTED           = (MODULE_ID_WIFI << 16) | 0x0008,
    
    /** SoftAP已停止 */
    EVENT_WIFI_AP_STOPPED           = (MODULE_ID_WIFI << 16) | 0x0009,

    /** 网络模式切换完成（STA/AP/OFF 互斥切换，net_mgr_switch_mode 触发） */
    EVENT_WIFI_MODE_CHANGED         = (MODULE_ID_WIFI << 16) | 0x000A,
    
    /* ====================================================================== */
    /* 传感器事件 (MODULE_ID_SENSOR = 0x0003)                                  */
    /* ====================================================================== */
    
    /** 温湿度数据更新 */
    EVENT_SENSOR_TEMP_HUMIDITY      = (MODULE_ID_SENSOR << 16) | 0x0001,
    
    /** 传感器读取错误 */
    EVENT_SENSOR_ERROR              = (MODULE_ID_SENSOR << 16) | 0x0002,
    
    /* ====================================================================== */
    /* 触控事件 (MODULE_ID_TOUCH = 0x0004)                                     */
    /* ====================================================================== */
    
    /** 触摸按下 */
    EVENT_TOUCH_PRESS               = (MODULE_ID_TOUCH << 16) | 0x0001,
    
    /** 触摸释放 */
    EVENT_TOUCH_RELEASE             = (MODULE_ID_TOUCH << 16) | 0x0002,
    
    /** 触摸滑动 */
    EVENT_TOUCH_SWIPE               = (MODULE_ID_TOUCH << 16) | 0x0003,
    
    /** 触摸长按 */
    EVENT_TOUCH_LONG_PRESS          = (MODULE_ID_TOUCH << 16) | 0x0004,
    
    
    /* ====================================================================== */
    /* Web事件 (MODULE_ID_WEB = 0x000A)                                       */
    /* ====================================================================== */
    
    /** WebSocket客户端连接 */
    /* ====================================================================== */
    /* LoRa事件 (MODULE_ID_LORA = 0x0005)                                     */
    /* ====================================================================== */

    /** LoRa数据接收事件 */
    EVENT_LORA_DATA_RECEIVED        = (MODULE_ID_LORA << 16) | 0x0001,

    /** LoRa发送完成事件 */
    EVENT_LORA_SEND_COMPLETE        = (MODULE_ID_LORA << 16) | 0x0002,

    /** LoRa发送失败事件 */
    EVENT_LORA_SEND_FAILED          = (MODULE_ID_LORA << 16) | 0x0003,

    /** LoRa模块就绪事件 */
    EVENT_LORA_READY                = (MODULE_ID_LORA << 16) | 0x0004,

    /* ====================================================================== */
    /* UI事件 (MODULE_ID_UI = 0x0006)                                         */
    /* ====================================================================== */

    /** 页面切换事件 */
    EVENT_UI_PAGE_CHANGE            = (MODULE_ID_UI << 16) | 0x0001,

    /** 动画完成事件 */
    EVENT_UI_ANIMATION_DONE         = (MODULE_ID_UI << 16) | 0x0002,

    /** UI刷新请求事件 */
    EVENT_UI_REFRESH_REQUEST        = (MODULE_ID_UI << 16) | 0x0003,

    /* ====================================================================== */
    /* 音频事件 (MODULE_ID_AUDIO = 0x0007)                                    */
    /* ====================================================================== */

    /** 开始播放事件 */
    EVENT_AUDIO_PLAY_START          = (MODULE_ID_AUDIO << 16) | 0x0001,

    /** 播放完成事件 */
    EVENT_AUDIO_PLAY_DONE           = (MODULE_ID_AUDIO << 16) | 0x0002,

    /** 开始录音事件 */
    EVENT_AUDIO_RECORD_START        = (MODULE_ID_AUDIO << 16) | 0x0003,

    /** 录音完成事件 */
    EVENT_AUDIO_RECORD_DONE         = (MODULE_ID_AUDIO << 16) | 0x0004,

    /** 音频数据事件 */
    EVENT_AUDIO_DATA                = (MODULE_ID_AUDIO << 16) | 0x0005,

    /** 音频模块就绪事件 */
    EVENT_AUDIO_READY               = (MODULE_ID_AUDIO << 16) | 0x0006,

    /* ====================================================================== */
    /* 存储事件 (MODULE_ID_STORAGE = 0x0008)                                  */
    /* ====================================================================== */

    /** 存储操作完成事件 */
    EVENT_STORAGE_OPERATION_DONE    = (MODULE_ID_STORAGE << 16) | 0x0001,

    /** 存储错误事件 */
    EVENT_STORAGE_ERROR             = (MODULE_ID_STORAGE << 16) | 0x0002,

    /** 存储模块就绪事件 */
    EVENT_STORAGE_READY             = (MODULE_ID_STORAGE << 16) | 0x0003,

    /* ====================================================================== */
    /* 显示事件 (MODULE_ID_DISPLAY = 0x0009)                                  */
    /* ====================================================================== */

    /** 显示刷新完成事件 */
    EVENT_DISPLAY_REFRESH_DONE      = (MODULE_ID_DISPLAY << 16) | 0x0001,

    /** 背光状态变更事件 */
    EVENT_DISPLAY_BACKLIGHT_CHANGE  = (MODULE_ID_DISPLAY << 16) | 0x0002,

    /** 显示模块就绪事件 */
    EVENT_DISPLAY_READY             = (MODULE_ID_DISPLAY << 16) | 0x0003,
    EVENT_WEB_WS_CLIENT_CONNECTED   = (MODULE_ID_WEB << 16) | 0x0001,
    
    /** WebSocket客户端断开 */
    EVENT_WEB_WS_CLIENT_DISCONNECTED= (MODULE_ID_WEB << 16) | 0x0002,
    
    /** Web请求处理完成 */
    EVENT_WEB_REQUEST_DONE          = (MODULE_ID_WEB << 16) | 0x0003,
    
    /* 边界值，用于类型检查 */
    EVENT_TYPE_MAX                  = 0x7FFFFFFF
} event_type_t;

/* ========================================================================== */
/*                              触控手势枚举                                   */
/* ========================================================================== */

/**
 * @brief 触控手势类型
 */
typedef enum {
    TOUCH_GESTURE_NONE          = 0,    /**< 无手势 */
    TOUCH_GESTURE_PRESS         = 1,    /**< 按下 */
    TOUCH_GESTURE_RELEASE       = 2,    /**< 释放 */
    TOUCH_GESTURE_SWIPE_LEFT    = 3,    /**< 向左滑动 */
    TOUCH_GESTURE_SWIPE_RIGHT   = 4,    /**< 向右滑动 */
    TOUCH_GESTURE_SWIPE_UP      = 5,    /**< 向上滑动 */
    TOUCH_GESTURE_SWIPE_DOWN    = 6,    /**< 向下滑动 */
    TOUCH_GESTURE_LONG_PRESS    = 7,    /**< 长按 */
} touch_gesture_t;

/* ========================================================================== */
/*                              事件数据结构                                   */
/* ========================================================================== */

/**
 * @brief WiFi连接事件数据
 */
typedef struct {
    char ssid[33];              /**< AP名称 */
    int rssi;                   /**< 信号强度 (dBm) */
    int authmode;               /**< 加密方式 */
    uint32_t ip_addr;           /**< IP地址 (网络字节序) */
} event_wifi_connected_t;

/**
 * @brief WiFi断开事件数据
 */
typedef struct {
    int reason;                 /**< 断开原因 (ESP-IDF wifi_err_reason_t) */
} event_wifi_disconnected_t;

/**
 * @brief WiFi扫描完成事件数据
 */
typedef struct {
    int ap_count;               /**< 扫描到的AP数量 */
} event_wifi_scan_done_t;

/**
 * @brief WiFi切换事件数据
 */
typedef struct {
    char ssid[33];              /**< 目标AP名称 */
    bool success;               /**< 切换是否成功 */
    int elapsed_ms;             /**< 切换耗时 (ms) */
} event_wifi_switch_t;

/**
 * @brief 网络模式切换事件数据（值取 net_mode_t，避免反向依赖 net_mgr）
 */
typedef struct {
    uint8_t old_mode;           /**< 切换前模式 (net_mode_t) */
    uint8_t new_mode;           /**< 切换后模式 (net_mode_t) */
} event_wifi_mode_changed_t;

/**
 * @brief 温湿度传感器事件数据
 */
typedef struct {
    float temperature;          /**< 温度 (°C) */
    float humidity;             /**< 湿度 (%RH) */
    uint32_t timestamp;         /**< 采集时间戳 (ms) */
} event_sensor_temp_humidity_t;

/**
 * @brief 触控事件数据
 */
typedef struct {
    int x;                      /**< X坐标 */
    int y;                      /**< Y坐标 */
    touch_gesture_t gesture;    /**< 手势类型 */
} event_touch_t;

/**
 * @brief LoRa数据接收事件数据
 */
typedef struct {
    uint8_t src_addr;           /**< 源地址 */
    uint8_t data[240];          /**< 数据内容 */
    uint16_t data_len;          /**< 数据长度 */
    int8_t rssi;                /**< 接收信号强度 */
} event_lora_data_received_t;

/**
 * @brief LoRa发送完成事件数据
 */
typedef struct {
    bool success;               /**< 发送是否成功 */
    uint32_t timestamp;         /**< 发送时间戳 */
} event_lora_send_done_t;

/**
 * @brief 页面切换事件数据
 */
typedef struct {
    int from_page;              /**< 源页面ID */
    int to_page;                /**< 目标页面ID */
} event_ui_page_change_t;

/**
 * @brief 系统错误事件数据
 */
typedef struct {
    int error_code;             /**< 错误代码 */
    char module[16];            /**< 出错模块名 */
    char message[64];           /**< 错误描述 */
} event_system_error_t;

/* ========================================================================== */
/*                              事件头结构                                     */
/* ========================================================================== */

/**
 * @brief 事件头结构
 * 
 * 每个事件都包含此头部信息
 */
typedef struct {
    event_type_t type;          /**< 事件类型 */
    uint32_t timestamp;         /**< 事件产生时间戳 (ms) */
    uint16_t data_len;          /**< 事件数据长度 (不包括此头) */
    uint16_t reserved;          /**< 保留字段，对齐 */
} event_header_t;

/**
 * @brief 完整事件结构
 * 
 * 包含事件头和可变长度的数据
 * 实际使用时，data 数组的大小由 data_len 决定
 */
typedef struct {
    event_header_t header;      /**< 事件头 */
    uint8_t data[];             /**< 柔性数组，存放事件数据 */
} event_t;

/* ========================================================================== */
/*                              错误码定义                                     */
/* ========================================================================== */

/**
 * @brief 事件总线错误码
 */
typedef enum {
    EVENT_BUS_OK                =  0,   /**< 成功 */
    EVENT_BUS_ERR_INVALID_PARAM = -1,   /**< 无效参数 */
    EVENT_BUS_ERR_NOT_INIT      = -2,   /**< 未初始化 */
    EVENT_BUS_ERR_ALREADY_INIT  = -3,   /**< 已初始化 */
    EVENT_BUS_ERR_QUEUE_FULL    = -4,   /**< 队列已满 */
    EVENT_BUS_ERR_NO_MEMORY     = -5,   /**< 内存不足 */
    EVENT_BUS_ERR_NOT_FOUND     = -6,   /**< 订阅者未找到 */
    EVENT_BUS_ERR_MAX_SUBSCRIBERS = -7, /**< 订阅者数量已达上限 */
    EVENT_BUS_ERR_TIMEOUT       = -8,   /**< 操作超时 */
    EVENT_BUS_ERR_INTERNAL      = -9,   /**< 内部错误 */
} event_bus_err_t;

/* ========================================================================== */
/*                              类型安全宏                                     */
/* ========================================================================== */

/**
 * @brief 发布事件的类型安全宏
 * 
 * 自动计算数据长度，确保类型安全
 * 
 * @param type 事件类型 (event_type_t)
 * @param data_ptr 数据指针 (指向具体事件数据结构的指针)
 * 
 * @code
 *   event_sensor_temp_humidity_t temp_data = {
 *       .temperature = 25.5f,
 *       .humidity = 60.0f,
 *       .timestamp = get_timestamp()
 *   };
 *   EVENT_BUS_PUBLISH(EVENT_SENSOR_TEMP_HUMIDITY, &temp_data);
 * @endcode
 */
#define EVENT_BUS_PUBLISH(type, data_ptr) \
    event_bus_publish(type, data_ptr, sizeof(*(data_ptr)))

/**
 * @brief 发布无数据事件的便捷宏
 * 
 * @param type 事件类型 (event_type_t)
 * 
 * @code
 *   EVENT_BUS_PUBLISH_EMPTY(EVENT_SYSTEM_STARTUP);
 * @endcode
 */
#define EVENT_BUS_PUBLISH_EMPTY(type) \
    event_bus_publish(type, NULL, 0)

#ifdef __cplusplus
}
#endif

/* ========================================================================== */
/*                              补充事件数据结构                               */
/* ========================================================================== */

/**
 * @brief LoRa数据事件数据
 */
typedef struct {
    const uint8_t* data;        /**< 数据指针 */
    size_t length;              /**< 数据长度 */
} event_lora_data_t;

/**
 * @brief 音频数据事件数据
 */
typedef struct {
    const uint8_t* data;        /**< 数据指针 */
    size_t length;              /**< 数据长度 */
    uint32_t sample_rate;       /**< 采样率 */
    uint8_t bits_per_sample;    /**< 位宽 */
} event_audio_data_t;

/**
 * @brief 存储操作事件数据
 */
typedef struct {
    bool success;               /**< 操作是否成功 */
    uint32_t address;           /**< 操作地址 */
    size_t size;                /**< 数据大小 */
} event_storage_op_t;

/**
 * @brief 显示事件数据
 */
typedef struct {
    uint16_t x;                 /**< X坐标 */
    uint16_t y;                 /**< Y坐标 */
    uint16_t width;             /**< 宽度 */
    uint16_t height;            /**< 高度 */
} event_display_region_t;


#endif /* EVENT_BUS_TYPES_H */
