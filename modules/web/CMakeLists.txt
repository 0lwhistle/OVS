idf_component_register(
    SRCS "web.c" "mongoose/mongoose.c" "web_data/web_data.c"
    INCLUDE_DIRS "." "web_data" "mongoose"
    REQUIRES 
	wifi 
	spiffs 
	app_update 
	esp_timer
	freertos
	ota

)
