set(COMPONENT_NAME lora)
# lora_tp（LoRa 可靠传输层）并入本组件：链路驱动（lora.c）+ 传输服务
# （lora_tp.c）同组件分层，对外仍只暴露 lora_tp.h（契约 I7）
idf_component_register(
    SRCS "lora.c" "lora_tp.c"
    INCLUDE_DIRS "."
    REQUIRES uart_drv event_bus tasker_api logger dtbs esp_driver_gpio
)
