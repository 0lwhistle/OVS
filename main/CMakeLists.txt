idf_component_register(SRCS 
	"main.c"
	"../modules/web/mongoose/mongoose.c"
	"../modules/web/web_data/web_data.c"
    INCLUDE_DIRS 
	"../modules/logger"
	"."
	"../modules/web/mongoose"
	"../modules/web/web_data"
	"../modules/devices_ctrl/wifi"
	"../API/tasker"
	"../modules/tasker/task_manager"
	"../modules/tasker/task_worker"
	REQUIRES 
	wifi web 
	led_ctrl 
	beep_ctrl 
	sr04_ctrl 
	task_manager
	task_worker
	tasker
	app_update
	spiffs
	esp_timer
	heartbeat
	)


# 关键：直接在 CMake 层面定义 Mongoose 需要的宏
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    MG_ARCH=MG_ARCH_ESP32
    MG_ENABLE_TCPIP=0
    MG_ENABLE_SOCKET=1
    MG_ENABLE_POLL=1
    MG_ENABLE_LOG=1
    MG_IO_SIZE=512
    MG_ENABLE_MD5=1
)

# ========== 预编译头文件（加速编译） ==========
# 把几乎每个 .c 文件都会 include 的稳定头文件预编译
# 这样每个 .c 编译时就不用反复解析这些头文件了
target_precompile_headers(${COMPONENT_LIB} PRIVATE
    <stdint.h>
    <stdbool.h>
    <string.h>
    <stdio.h>
    <stdlib.h>
    <esp_log.h>
    <esp_err.h>
    <freertos/FreeRTOS.h>
    <freertos/task.h>
)
