idf_component_register(SRCS 
	"main.c"
	"../modules/mongoose/mongoose.c"
    INCLUDE_DIRS 
	"."
	"../modules/mongoose"
	REQUIRES wifi web led_ctrl beep_ctrl sr04_ctrl task_manager
	app_update
	esp_timer
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
