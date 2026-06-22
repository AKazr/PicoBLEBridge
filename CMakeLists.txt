# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 15_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico_w CACHE STRING "Board type")
set(PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR})

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(BLEBridge C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

set(FATFS_SDK_SOURCE_DIR ${PICO_SDK_PATH}/lib/tinyusb/lib/fatfs/source)
set(FATFS_BUILD_DIR ${CMAKE_CURRENT_BINARY_DIR}/fatfs)
set(APP_BUILD_INFO_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
set(APP_BUILD_INFO_HEADER ${APP_BUILD_INFO_DIR}/app_build_info.h)

file(MAKE_DIRECTORY ${FATFS_BUILD_DIR})
file(MAKE_DIRECTORY ${APP_BUILD_INFO_DIR})
configure_file(${FATFS_SDK_SOURCE_DIR}/ff.c ${FATFS_BUILD_DIR}/ff.c COPYONLY)
configure_file(${FATFS_SDK_SOURCE_DIR}/ff.h ${FATFS_BUILD_DIR}/ff.h COPYONLY)
configure_file(${FATFS_SDK_SOURCE_DIR}/diskio.h ${FATFS_BUILD_DIR}/diskio.h COPYONLY)
configure_file(${CMAKE_CURRENT_LIST_DIR}/ffconf.h ${FATFS_BUILD_DIR}/ffconf.h COPYONLY)

if (NOT EXISTS ${APP_BUILD_INFO_HEADER})
    file(WRITE ${APP_BUILD_INFO_HEADER}
            "#ifndef APP_BUILD_INFO_H\n"
            "#define APP_BUILD_INFO_H\n\n"
            "#define APP_BUILD_NUMBER 0u\n"
            "#define APP_BUILD_DATETIME \"not built\"\n\n"
            "#endif\n"
    )
endif()

# Add executable. Default name is the project name, version 0.1

add_executable(BLEBridgeBootloader
        bootloader_stub.c
        )

pico_set_program_name(BLEBridgeBootloader "BLEBridgeBootloader")
pico_set_program_version(BLEBridgeBootloader "0.1")
pico_enable_stdio_uart(BLEBridgeBootloader 0)
pico_enable_stdio_usb(BLEBridgeBootloader 0)
pico_set_linker_script(BLEBridgeBootloader ${CMAKE_CURRENT_LIST_DIR}/bootloader_memmap.ld)
target_compile_definitions(BLEBridgeBootloader PRIVATE
        APP_IMAGE_BASE=0x10020000u
)
target_include_directories(BLEBridgeBootloader PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
)
target_link_libraries(BLEBridgeBootloader
        pico_stdlib
        hardware_flash
        hardware_watchdog
        )
pico_add_extra_outputs(BLEBridgeBootloader)

add_executable(BLEBridge 
        dhcpserver/dhcpserver.c
        dnsserver/dnsserver.c
        BLEBridge.c
        app_config.c
        app_firmware_update.c
        app_log.c
        app_narodmon.c
        app_runtime_config.c
        app_storage.c
        app_text.c
        app_web.c
        ble_scanner.c
        measurement_store.c
        minIni.c
        onewire_bus.c
        onewire_source.c
        onewire_worker.c
        fatfs_diskio.c
        status_led.c
        storage_backend.c
        usb_descriptors.c
        usb_msc_storage.c
        ${FATFS_BUILD_DIR}/ff.c
        )

pico_set_linker_script(BLEBridge ${CMAKE_CURRENT_LIST_DIR}/memmap_storage.ld)

add_custom_target(generate_app_build_info
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_LIST_DIR}/scripts/generate_build_info.py
                --build-number-file ${CMAKE_CURRENT_LIST_DIR}/build_number.txt
                --header ${APP_BUILD_INFO_HEADER}
        BYPRODUCTS ${APP_BUILD_INFO_HEADER}
        DEPENDS ${CMAKE_CURRENT_LIST_DIR}/scripts/generate_build_info.py
        COMMENT "Generating BLEBridge build information"
        VERBATIM
)
add_dependencies(BLEBridge generate_app_build_info)

pico_add_library(pico_httpd_content NOFLAG)
pico_set_lwip_httpd_content(pico_httpd_content INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/html/index.html
        ${CMAKE_CURRENT_LIST_DIR}/html/ble.html
        ${CMAKE_CURRENT_LIST_DIR}/html/onewire.html
        ${CMAKE_CURRENT_LIST_DIR}/html/settings.html
        ${CMAKE_CURRENT_LIST_DIR}/html/narodmon.html
        ${CMAKE_CURRENT_LIST_DIR}/html/narodmon.ssi
        ${CMAKE_CURRENT_LIST_DIR}/html/style.css
        ${CMAKE_CURRENT_LIST_DIR}/html/app.js
        ${CMAKE_CURRENT_LIST_DIR}/html/status.json
        ${CMAKE_CURRENT_LIST_DIR}/html/config.json
        ${CMAKE_CURRENT_LIST_DIR}/html/devices.json
        ${CMAKE_CURRENT_LIST_DIR}/html/onewire.json
        ${CMAKE_CURRENT_LIST_DIR}/html/update.txt
)

pico_set_program_name(BLEBridge "BLEBridge")
pico_set_program_version(BLEBridge "0.1")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(BLEBridge 0)
pico_enable_stdio_usb(BLEBridge 0)

# Add the standard library to the build
target_link_libraries(BLEBridge
        pico_stdlib)

# Add the standard include files to the build
target_include_directories(BLEBridge PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        ${FATFS_BUILD_DIR}
        ${APP_BUILD_INFO_DIR}
)

# Add any user requested libraries
target_link_libraries(BLEBridge 
        hardware_flash
        hardware_watchdog
        hardware_clocks
        pico_flash
        pico_multicore
        pico_cyw43_arch_lwip_poll
        pico_lwip_http
        pico_httpd_content
        pico_btstack_ble
        pico_btstack_cyw43
        tinyusb_device
        )

pico_add_extra_outputs(BLEBridge)

add_custom_command(TARGET BLEBridge POST_BUILD
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_LIST_DIR}/scripts/make_ota_package.py
                $<TARGET_FILE_DIR:BLEBridge>/BLEBridge.bin
                $<TARGET_FILE_DIR:BLEBridge>/BLEBridge.ota
        DEPENDS ${CMAKE_CURRENT_LIST_DIR}/scripts/make_ota_package.py
        COMMENT "Generating BLEBridge OTA package"
        VERBATIM
)
