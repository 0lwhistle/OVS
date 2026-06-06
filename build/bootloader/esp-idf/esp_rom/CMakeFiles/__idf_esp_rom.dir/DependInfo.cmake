
# Consider dependencies only in project.
set(CMAKE_DEPENDS_IN_PROJECT_ONLY OFF)

# The set of languages for which implicit dependencies are needed:
set(CMAKE_DEPENDS_LANGUAGES
  "ASM"
  )
# The set of files for implicit dependencies of each language:
set(CMAKE_DEPENDS_CHECK_ASM
  "/root/esp-idf/esp-idf-v6.0.1/components/esp_rom/patches/esp_rom_cache_writeback_esp32s3.S" "/root/progarms/web/build/bootloader/esp-idf/esp_rom/CMakeFiles/__idf_esp_rom.dir/patches/esp_rom_cache_writeback_esp32s3.S.obj"
  "/root/esp-idf/esp-idf-v6.0.1/components/esp_rom/patches/esp_rom_longjmp.S" "/root/progarms/web/build/bootloader/esp-idf/esp_rom/CMakeFiles/__idf_esp_rom.dir/patches/esp_rom_longjmp.S.obj"
  )
set(CMAKE_ASM_COMPILER_ID "GNU")

# Preprocessor definitions for this target.
set(CMAKE_TARGET_DEFINITIONS_ASM
  "BOOTLOADER_BUILD=1"
  "ESP_PLATFORM"
  "IDF_VER=\"v6.0.1\""
  "NON_OS_BUILD=1"
  "SOC_MMU_PAGE_SIZE=CONFIG_MMU_PAGE_SIZE"
  "SOC_XTAL_FREQ_MHZ=CONFIG_XTAL_FREQ"
  "_GLIBCXX_HAVE_POSIX_SEMAPHORE"
  "_GLIBCXX_USE_POSIX_SEMAPHORE"
  "_GNU_SOURCE"
  )

# The include file search paths:
set(CMAKE_ASM_TARGET_INCLUDE_PATH
  "config"
  "/root/esp-idf/esp-idf-v6.0.1/components/esp_rom/include"
  "/root/esp-idf/esp-idf-v6.0.1/components/esp_rom/esp32s3/include"
  "/root/esp-idf/esp-idf-v6.0.1/components/esp_rom/esp32s3/include/esp32s3"
  "/root/esp-idf/esp-idf-v6.0.1/components/esp_rom/esp32s3"
  "/root/esp-idf/esp-idf-v6.0.1/components/log/include"
  "/root/esp-idf/esp-idf-v6.0.1/components/esp_common/include"
  "/root/esp-idf/esp-idf-v6.0.1/components/esp_hw_support/include"
  "/root/esp-idf/esp-idf-v6.0.1/components/esp_hw_support/include/soc"
  "/root/esp-idf/esp-idf-v6.0.1/components/esp_hw_support/ldo/include"
  "/root/esp-idf/esp-idf-v6.0.1/components/esp_hw_support/debug_probe/include"
  "/root/esp-idf/esp-idf-v6.0.1/components/esp_hw_support/etm/include"
  "/root/esp-idf/esp-idf-v6.0.1/components/esp_hw_support/mspi_timing_tuning/include"
  "/root/esp-idf/esp-idf-v6.0.1/components/esp_hw_support/mspi_timing_tuning/tuning_scheme_impl/include"
  "/root/esp-idf/esp-idf-v6.0.1/components/esp_hw_support/power_supply/include"
  "/root/esp-idf/esp-idf-v6.0.1/components/esp_hw_support/modem/include"
  "/root/esp-idf/esp-idf-v6.0.1/components/esp_hw_support/include/soc/esp32s3"
  "/root/esp-idf/esp-idf-v6.0.1/components/esp_hw_support/port/esp32s3/."
  "/root/esp-idf/esp-idf-v6.0.1/components/esp_hw_support/port/esp32s3/include"
  "/root/esp-idf/esp-idf-v6.0.1/components/esp_hw_support/mspi_timing_tuning/port/esp32s3/."
  "/root/esp-idf/esp-idf-v6.0.1/components/esp_hw_support/mspi_timing_tuning/port/esp32s3/include"
  "/root/esp-idf/esp-idf-v6.0.1/components/esp_libc/platform_include"
  "/root/esp-idf/esp-idf-v6.0.1/components/xtensa/esp32s3/include"
  "/root/esp-idf/esp-idf-v6.0.1/components/xtensa/include"
  "/root/esp-idf/esp-idf-v6.0.1/components/xtensa/deprecated_include"
  "/root/esp-idf/esp-idf-v6.0.1/components/esp_hal_gpio/include"
  "/root/esp-idf/esp-idf-v6.0.1/components/esp_hal_gpio/esp32s3/include"
  "/root/esp-idf/esp-idf-v6.0.1/components/soc/include"
  "/root/esp-idf/esp-idf-v6.0.1/components/soc/esp32s3"
  "/root/esp-idf/esp-idf-v6.0.1/components/soc/esp32s3/include"
  "/root/esp-idf/esp-idf-v6.0.1/components/soc/esp32s3/register"
  "/root/esp-idf/esp-idf-v6.0.1/components/hal/platform_port/include"
  "/root/esp-idf/esp-idf-v6.0.1/components/hal/esp32s3/include"
  "/root/esp-idf/esp-idf-v6.0.1/components/hal/include"
  "/root/esp-idf/esp-idf-v6.0.1/components/esp_hal_usb/include"
  "/root/esp-idf/esp-idf-v6.0.1/components/esp_hal_usb/esp32s3/include"
  "/root/esp-idf/esp-idf-v6.0.1/components/esp_hal_pmu/include"
  "/root/esp-idf/esp-idf-v6.0.1/components/esp_hal_pmu/esp32s3/include"
  "/root/esp-idf/esp-idf-v6.0.1/components/esp_hal_ana_conv/include"
  "/root/esp-idf/esp-idf-v6.0.1/components/esp_hal_ana_conv/esp32s3/include"
  "/root/esp-idf/esp-idf-v6.0.1/components/esp_hal_dma/include"
  "/root/esp-idf/esp-idf-v6.0.1/components/esp_hal_dma/esp32s3/include"
  "/root/esp-idf/esp-idf-v6.0.1/components/esp_hal_uart/include"
  "/root/esp-idf/esp-idf-v6.0.1/components/esp_hal_uart/esp32s3/include"
  )

# The set of dependency files which are needed:
set(CMAKE_DEPENDS_DEPENDENCY_FILES
  "/root/esp-idf/esp-idf-v6.0.1/components/esp_rom/patches/esp_rom_cache_esp32s2_esp32s3.c" "esp-idf/esp_rom/CMakeFiles/__idf_esp_rom.dir/patches/esp_rom_cache_esp32s2_esp32s3.c.obj" "gcc" "esp-idf/esp_rom/CMakeFiles/__idf_esp_rom.dir/patches/esp_rom_cache_esp32s2_esp32s3.c.obj.d"
  "/root/esp-idf/esp-idf-v6.0.1/components/esp_rom/patches/esp_rom_crc.c" "esp-idf/esp_rom/CMakeFiles/__idf_esp_rom.dir/patches/esp_rom_crc.c.obj" "gcc" "esp-idf/esp_rom/CMakeFiles/__idf_esp_rom.dir/patches/esp_rom_crc.c.obj.d"
  "/root/esp-idf/esp-idf-v6.0.1/components/esp_rom/patches/esp_rom_efuse.c" "esp-idf/esp_rom/CMakeFiles/__idf_esp_rom.dir/patches/esp_rom_efuse.c.obj" "gcc" "esp-idf/esp_rom/CMakeFiles/__idf_esp_rom.dir/patches/esp_rom_efuse.c.obj.d"
  "/root/esp-idf/esp-idf-v6.0.1/components/esp_rom/patches/esp_rom_gpio.c" "esp-idf/esp_rom/CMakeFiles/__idf_esp_rom.dir/patches/esp_rom_gpio.c.obj" "gcc" "esp-idf/esp_rom/CMakeFiles/__idf_esp_rom.dir/patches/esp_rom_gpio.c.obj.d"
  "/root/esp-idf/esp-idf-v6.0.1/components/esp_rom/patches/esp_rom_print.c" "esp-idf/esp_rom/CMakeFiles/__idf_esp_rom.dir/patches/esp_rom_print.c.obj" "gcc" "esp-idf/esp_rom/CMakeFiles/__idf_esp_rom.dir/patches/esp_rom_print.c.obj.d"
  "/root/esp-idf/esp-idf-v6.0.1/components/esp_rom/patches/esp_rom_serial_output.c" "esp-idf/esp_rom/CMakeFiles/__idf_esp_rom.dir/patches/esp_rom_serial_output.c.obj" "gcc" "esp-idf/esp_rom/CMakeFiles/__idf_esp_rom.dir/patches/esp_rom_serial_output.c.obj.d"
  "/root/esp-idf/esp-idf-v6.0.1/components/esp_rom/patches/esp_rom_spiflash.c" "esp-idf/esp_rom/CMakeFiles/__idf_esp_rom.dir/patches/esp_rom_spiflash.c.obj" "gcc" "esp-idf/esp_rom/CMakeFiles/__idf_esp_rom.dir/patches/esp_rom_spiflash.c.obj.d"
  "/root/esp-idf/esp-idf-v6.0.1/components/esp_rom/patches/esp_rom_sys.c" "esp-idf/esp_rom/CMakeFiles/__idf_esp_rom.dir/patches/esp_rom_sys.c.obj" "gcc" "esp-idf/esp_rom/CMakeFiles/__idf_esp_rom.dir/patches/esp_rom_sys.c.obj.d"
  "/root/esp-idf/esp-idf-v6.0.1/components/esp_rom/patches/esp_rom_systimer.c" "esp-idf/esp_rom/CMakeFiles/__idf_esp_rom.dir/patches/esp_rom_systimer.c.obj" "gcc" "esp-idf/esp_rom/CMakeFiles/__idf_esp_rom.dir/patches/esp_rom_systimer.c.obj.d"
  )

# Targets to which this target links.
set(CMAKE_TARGET_LINKED_INFO_FILES
  "/root/progarms/web/build/bootloader/esp-idf/log/CMakeFiles/__idf_log.dir/DependInfo.cmake"
  "/root/progarms/web/build/bootloader/esp-idf/esp_common/CMakeFiles/__idf_esp_common.dir/DependInfo.cmake"
  "/root/progarms/web/build/bootloader/esp-idf/esp_hw_support/CMakeFiles/__idf_esp_hw_support.dir/DependInfo.cmake"
  "/root/progarms/web/build/bootloader/esp-idf/xtensa/CMakeFiles/__idf_xtensa.dir/DependInfo.cmake"
  "/root/progarms/web/build/bootloader/esp-idf/soc/CMakeFiles/__idf_soc.dir/DependInfo.cmake"
  "/root/progarms/web/build/bootloader/esp-idf/hal/CMakeFiles/__idf_hal.dir/DependInfo.cmake"
  "/root/progarms/web/build/bootloader/esp-idf/esp_hal_uart/CMakeFiles/__idf_esp_hal_uart.dir/DependInfo.cmake"
  "/root/progarms/web/build/bootloader/esp-idf/efuse/CMakeFiles/__idf_efuse.dir/DependInfo.cmake"
  "/root/progarms/web/build/bootloader/esp-idf/bootloader_support/CMakeFiles/__idf_bootloader_support.dir/DependInfo.cmake"
  "/root/progarms/web/build/bootloader/esp-idf/spi_flash/CMakeFiles/__idf_spi_flash.dir/DependInfo.cmake"
  "/root/progarms/web/build/bootloader/esp-idf/esp_hal_gpio/CMakeFiles/__idf_esp_hal_gpio.dir/DependInfo.cmake"
  "/root/progarms/web/build/bootloader/esp-idf/esp_hal_usb/CMakeFiles/__idf_esp_hal_usb.dir/DependInfo.cmake"
  "/root/progarms/web/build/bootloader/esp-idf/esp_hal_pmu/CMakeFiles/__idf_esp_hal_pmu.dir/DependInfo.cmake"
  "/root/progarms/web/build/bootloader/esp-idf/esp_hal_wdt/CMakeFiles/__idf_esp_hal_wdt.dir/DependInfo.cmake"
  "/root/progarms/web/build/bootloader/esp-idf/esp_hal_security/CMakeFiles/__idf_esp_hal_security.dir/DependInfo.cmake"
  "/root/progarms/web/build/bootloader/esp-idf/esp_hal_clock/CMakeFiles/__idf_esp_hal_clock.dir/DependInfo.cmake"
  "/root/progarms/web/build/bootloader/esp-idf/esp_system/CMakeFiles/__idf_esp_system.dir/DependInfo.cmake"
  "/root/progarms/web/build/bootloader/esp-idf/esp_hal_ana_conv/CMakeFiles/__idf_esp_hal_ana_conv.dir/DependInfo.cmake"
  "/root/progarms/web/build/bootloader/esp-idf/esp_bootloader_format/CMakeFiles/__idf_esp_bootloader_format.dir/DependInfo.cmake"
  "/root/progarms/web/build/bootloader/esp-idf/micro-ecc/CMakeFiles/__idf_micro-ecc.dir/DependInfo.cmake"
  "/root/progarms/web/build/bootloader/esp-idf/esp_security/CMakeFiles/__idf_esp_security.dir/DependInfo.cmake"
  "/root/progarms/web/build/bootloader/esp-idf/esp_hal_gpspi/CMakeFiles/__idf_esp_hal_gpspi.dir/DependInfo.cmake"
  "/root/progarms/web/build/bootloader/esp-idf/esp_hal_timg/CMakeFiles/__idf_esp_hal_timg.dir/DependInfo.cmake"
  "/root/progarms/web/build/bootloader/esp-idf/esp_hal_dma/CMakeFiles/__idf_esp_hal_dma.dir/DependInfo.cmake"
  )

# Fortran module output directory.
set(CMAKE_Fortran_TARGET_MODULE_DIR "")
