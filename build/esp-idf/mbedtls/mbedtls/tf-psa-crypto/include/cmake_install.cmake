# Install script for directory: /root/esp-idf/esp-idf-v6.0.1/components/mbedtls/mbedtls/tf-psa-crypto/include

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "/usr/local")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "TRUE")
endif()

# Set default install directory permissions.
if(NOT DEFINED CMAKE_OBJDUMP)
  set(CMAKE_OBJDUMP "/root/.espressif/tools/xtensa-esp-elf/esp-15.2.0_20251204/xtensa-esp-elf/bin/xtensa-esp32s3-elf-objdump")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/psa" TYPE FILE PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ FILES
    "/root/esp-idf/esp-idf-v6.0.1/components/mbedtls/mbedtls/tf-psa-crypto/include/psa/crypto.h"
    "/root/esp-idf/esp-idf-v6.0.1/components/mbedtls/mbedtls/tf-psa-crypto/include/psa/crypto_adjust_auto_enabled.h"
    "/root/esp-idf/esp-idf-v6.0.1/components/mbedtls/mbedtls/tf-psa-crypto/include/psa/crypto_adjust_config_dependencies.h"
    "/root/esp-idf/esp-idf-v6.0.1/components/mbedtls/mbedtls/tf-psa-crypto/include/psa/crypto_adjust_config_derived.h"
    "/root/esp-idf/esp-idf-v6.0.1/components/mbedtls/mbedtls/tf-psa-crypto/include/psa/crypto_adjust_config_key_pair_types.h"
    "/root/esp-idf/esp-idf-v6.0.1/components/mbedtls/mbedtls/tf-psa-crypto/include/psa/crypto_adjust_config_synonyms.h"
    "/root/esp-idf/esp-idf-v6.0.1/components/mbedtls/mbedtls/tf-psa-crypto/include/psa/crypto_builtin_composites.h"
    "/root/esp-idf/esp-idf-v6.0.1/components/mbedtls/mbedtls/tf-psa-crypto/include/psa/crypto_builtin_key_derivation.h"
    "/root/esp-idf/esp-idf-v6.0.1/components/mbedtls/mbedtls/tf-psa-crypto/include/psa/crypto_builtin_primitives.h"
    "/root/esp-idf/esp-idf-v6.0.1/components/mbedtls/mbedtls/tf-psa-crypto/include/psa/crypto_compat.h"
    "/root/esp-idf/esp-idf-v6.0.1/components/mbedtls/mbedtls/tf-psa-crypto/include/psa/crypto_config.h"
    "/root/esp-idf/esp-idf-v6.0.1/components/mbedtls/mbedtls/tf-psa-crypto/include/psa/crypto_driver_common.h"
    "/root/esp-idf/esp-idf-v6.0.1/components/mbedtls/mbedtls/tf-psa-crypto/include/psa/crypto_driver_contexts_composites.h"
    "/root/esp-idf/esp-idf-v6.0.1/components/mbedtls/mbedtls/tf-psa-crypto/include/psa/crypto_driver_contexts_key_derivation.h"
    "/root/esp-idf/esp-idf-v6.0.1/components/mbedtls/mbedtls/tf-psa-crypto/include/psa/crypto_driver_contexts_primitives.h"
    "/root/esp-idf/esp-idf-v6.0.1/components/mbedtls/mbedtls/tf-psa-crypto/include/psa/crypto_driver_random.h"
    "/root/esp-idf/esp-idf-v6.0.1/components/mbedtls/mbedtls/tf-psa-crypto/include/psa/crypto_extra.h"
    "/root/esp-idf/esp-idf-v6.0.1/components/mbedtls/mbedtls/tf-psa-crypto/include/psa/crypto_platform.h"
    "/root/esp-idf/esp-idf-v6.0.1/components/mbedtls/mbedtls/tf-psa-crypto/include/psa/crypto_sizes.h"
    "/root/esp-idf/esp-idf-v6.0.1/components/mbedtls/mbedtls/tf-psa-crypto/include/psa/crypto_struct.h"
    "/root/esp-idf/esp-idf-v6.0.1/components/mbedtls/mbedtls/tf-psa-crypto/include/psa/crypto_types.h"
    "/root/esp-idf/esp-idf-v6.0.1/components/mbedtls/mbedtls/tf-psa-crypto/include/psa/crypto_values.h"
    )
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tf-psa-crypto" TYPE FILE PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ FILES
    "/root/esp-idf/esp-idf-v6.0.1/components/mbedtls/mbedtls/tf-psa-crypto/include/tf-psa-crypto/build_info.h"
    "/root/esp-idf/esp-idf-v6.0.1/components/mbedtls/mbedtls/tf-psa-crypto/include/tf-psa-crypto/version.h"
    )
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/mbedtls" TYPE FILE PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ FILES
    "/root/esp-idf/esp-idf-v6.0.1/components/mbedtls/mbedtls/tf-psa-crypto/include/../drivers/builtin/include/mbedtls/config_adjust_legacy_crypto.h"
    "/root/esp-idf/esp-idf-v6.0.1/components/mbedtls/mbedtls/tf-psa-crypto/include/../drivers/builtin/include/mbedtls/private_access.h"
    "/root/esp-idf/esp-idf-v6.0.1/components/mbedtls/mbedtls/tf-psa-crypto/include/mbedtls/asn1.h"
    "/root/esp-idf/esp-idf-v6.0.1/components/mbedtls/mbedtls/tf-psa-crypto/include/mbedtls/asn1write.h"
    "/root/esp-idf/esp-idf-v6.0.1/components/mbedtls/mbedtls/tf-psa-crypto/include/mbedtls/base64.h"
    "/root/esp-idf/esp-idf-v6.0.1/components/mbedtls/mbedtls/tf-psa-crypto/include/mbedtls/compat-3-crypto.h"
    "/root/esp-idf/esp-idf-v6.0.1/components/mbedtls/mbedtls/tf-psa-crypto/include/mbedtls/constant_time.h"
    "/root/esp-idf/esp-idf-v6.0.1/components/mbedtls/mbedtls/tf-psa-crypto/include/mbedtls/lms.h"
    "/root/esp-idf/esp-idf-v6.0.1/components/mbedtls/mbedtls/tf-psa-crypto/include/mbedtls/md.h"
    "/root/esp-idf/esp-idf-v6.0.1/components/mbedtls/mbedtls/tf-psa-crypto/include/mbedtls/memory_buffer_alloc.h"
    "/root/esp-idf/esp-idf-v6.0.1/components/mbedtls/mbedtls/tf-psa-crypto/include/mbedtls/nist_kw.h"
    "/root/esp-idf/esp-idf-v6.0.1/components/mbedtls/mbedtls/tf-psa-crypto/include/mbedtls/pem.h"
    "/root/esp-idf/esp-idf-v6.0.1/components/mbedtls/mbedtls/tf-psa-crypto/include/mbedtls/pk.h"
    "/root/esp-idf/esp-idf-v6.0.1/components/mbedtls/mbedtls/tf-psa-crypto/include/mbedtls/platform.h"
    "/root/esp-idf/esp-idf-v6.0.1/components/mbedtls/mbedtls/tf-psa-crypto/include/mbedtls/platform_time.h"
    "/root/esp-idf/esp-idf-v6.0.1/components/mbedtls/mbedtls/tf-psa-crypto/include/mbedtls/platform_util.h"
    "/root/esp-idf/esp-idf-v6.0.1/components/mbedtls/mbedtls/tf-psa-crypto/include/mbedtls/psa_util.h"
    "/root/esp-idf/esp-idf-v6.0.1/components/mbedtls/mbedtls/tf-psa-crypto/include/mbedtls/threading.h"
    )
endif()

