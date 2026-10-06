set(loader "${PROJECT_SOURCE_DIR}/src/platform/tc002/loader")
# Fixture paths must fit the emulated vendor property value.
string(MD5 harness_key "${CMAKE_CURRENT_BINARY_DIR}")
set(harness "/tmp/awtrix-loader-${harness_key}")
set(fast_usb AWTRIX_LOADER_USB_CHECK_MS=100 AWTRIX_LOADER_USB_RETRY_TICKS=4
  AWTRIX_LOADER_DEPLOY_CHECK_MS=25 AWTRIX_LOADER_DEPLOY_STALE_SECONDS=0.3 AWTRIX_LOADER_SETTLE_MS=25)

add_awtrix_test(NAME tc002-loader-policy SOURCES test_loader_policy.c "${loader}/loader_policy.c"
  "${loader}/loader_state.c" INCLUDES "${PROJECT_SOURCE_DIR}/src" DEFINITIONS _GNU_SOURCE SANITIZE TIMEOUT 20)

add_library(tc002-loader-under-test MODULE "${loader}/loader.c" "${loader}/loader_policy.c"
  "${loader}/loader_state.c" "${loader}/rescue_panel.c" ${TC002_FLASH_C_SOURCES})
set_target_properties(tc002-loader-under-test PROPERTIES C_VISIBILITY_PRESET hidden)
target_compile_definitions(tc002-loader-under-test PRIVATE _GNU_SOURCE ${fast_usb}
  AWTRIX_LOADER_DATA_DIR="${harness}/data/awtrix-ng"
  AWTRIX_LOADER_RUNTIME_DIR="${harness}/tmp"
  AWTRIX_LOADER_TEST_IMAGE_DIR="${harness}/image"
  AWTRIX_LOADER_PROC_MTD="${harness}/proc-mtd"
  AWTRIX_LOADER_MTD_DIR="${harness}/mtd"
  AWTRIX_LOADER_MTD_BLOCK_DIR="${harness}/block"
  AWTRIX_LOADER_LOOP_MODULE="${harness}/loop.ko"
  AWTRIX_LOADER_PANEL_SPI="${harness}/spidev"
  AWTRIX_LOADER_GPIO_DIR="${harness}/gpio"
  AWTRIX_LOADER_VENDOR_LIBRARY="${harness}/res/lib/libzkgui.so"
  AWTRIX_LOADER_INPUT_DIR="${harness}/input"
  AWTRIX_LOADER_SETPROP="${harness}/bin/setprop"
  AWTRIX_LOADER_UPGRADE_DIR="${harness}/storage"
  AWTRIX_LOADER_USB_ROLE="${harness}/sys/bus/platform/devices/soc:usbotg/otg_role"
  AWTRIX_LOADER_IDLE_SHELL="${harness}/bin/sh")
target_link_libraries(tc002-loader-under-test PRIVATE ${CMAKE_DL_LIBS} awtrix_warnings)

# The loader as the clock gets it, with its real mount path, which the loader under test replaces:
# compiled so the host build catches what only the ARM loader build would.
add_library(tc002-loader-production OBJECT "${loader}/loader.c")
set_target_properties(tc002-loader-production PROPERTIES C_VISIBILITY_PRESET hidden)
target_compile_definitions(tc002-loader-production PRIVATE _GNU_SOURCE)
target_link_libraries(tc002-loader-production PRIVATE awtrix_warnings)

add_library(tc002-loader-fake-vendor MODULE test_fake_vendor.c)
add_executable(tc002-loader-fake-daemon test_fake_daemon.c)
add_awtrix_test(NAME tc002-loader-host SOURCES test_loader_host.c ${TC002_SLOT_C_SOURCES} LIBRARIES ${CMAKE_DL_LIBS}
  INCLUDES "${PROJECT_SOURCE_DIR}/src"
  COMMAND "$<TARGET_FILE:tc002-loader-under-test>" "$<TARGET_FILE:tc002-loader-fake-vendor>"
          "$<TARGET_FILE:tc002-loader-fake-daemon>"
  TIMEOUT 120)
set_target_properties(tc002-loader-host-test PROPERTIES ENABLE_EXPORTS ON)
foreach(target tc002-loader-fake-vendor tc002-loader-fake-daemon tc002-loader-host-test)
  target_link_libraries(${target} PRIVATE awtrix_warnings)
  target_compile_definitions(${target} PRIVATE _GNU_SOURCE AWTRIX_LOADER_TEST_ROOT="${harness}" ${fast_usb})
endforeach()
