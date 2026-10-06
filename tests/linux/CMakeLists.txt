set(linux "${PROJECT_SOURCE_DIR}/src/platform/linux")

add_awtrix_test(NAME posix-files SOURCES test_posix_files.cpp LIBRARIES awtrix_posix SANITIZE)
target_link_options(posix-files-test PRIVATE -Wl,--wrap=fsync)

add_awtrix_test(NAME linux-login SOURCES test_login.cpp "${linux}/LinuxLanLogin.cpp"
  LIBRARIES awtrix_host SANITIZE)

add_awtrix_test(NAME linux-device-facts SOURCES test_linux_device_facts.cpp "${linux}/LinuxDeviceFacts.cpp"
  LIBRARIES awtrix_host)
target_link_options(linux-device-facts-test PRIVATE -Wl,--wrap=getifaddrs -Wl,--wrap=freeifaddrs)
add_awtrix_test(NAME linux-performance-unit SOURCES test_performance.cpp "${linux}/LinuxPerformanceReport.cpp"
  LIBRARIES awtrix_posix TIMEOUT 45)
add_awtrix_test(NAME linux-ble SOURCES test_ble.cpp LIBRARIES awtrix_ble SANITIZE)
add_awtrix_test(NAME linux-ble-radio SOURCES test_ble_radio.cpp LIBRARIES awtrix_ble SANITIZE)
target_link_options(linux-ble-radio-test PRIVATE
  -Wl,--wrap=socket -Wl,--wrap=bind -Wl,--wrap=listen -Wl,--wrap=setsockopt
  -Wl,--wrap=getsockopt -Wl,--wrap=connect -Wl,--wrap=accept4 -Wl,--wrap=write -Wl,--wrap=uname)
add_awtrix_test(NAME linux-iphone SOURCES test_iphone.cpp LIBRARIES awtrix_iphone SANITIZE)
add_awtrix_test(NAME linux-remote-images SOURCES test_remote_images.cpp LIBRARIES awtrix_images SANITIZE
  TIMEOUT 60)
add_awtrix_test(NAME linux-png SOURCES test_png.cpp LIBRARIES awtrix_images SANITIZE)
add_awtrix_test(NAME linux-jpeg SOURCES test_jpeg.cpp LIBRARIES awtrix_images JPEG::JPEG SANITIZE)
add_awtrix_test(NAME linux-ble-script SOURCES test_ble_script.cpp
  LIBRARIES awtrix_ble_script awtrix_host)
add_awtrix_test(NAME http-body-limit SOURCES test_http_body_limit.cpp
  LIBRARIES awtrix_host TIMEOUT 15)
add_awtrix_test(NAME linux-file-download SOURCES test_file_download.cpp LIBRARIES awtrix_host awtrix_posix
  TIMEOUT 20)
add_awtrix_test(NAME linux-tls SOURCES test_tls.cpp "${linux}/LinuxMqttTls.cpp" LIBRARIES awtrix_host SANITIZE TIMEOUT 60)
add_awtrix_test(NAME linux-button-webhook SOURCES test_button_webhook.cpp "${linux}/LinuxButtonWebhook.cpp"
  LIBRARIES awtrix_core Threads::Threads TIMEOUT 30)
add_awtrix_test(NAME httplib-header-parser SOURCES test_headers.cpp INCLUDES "${PROJECT_SOURCE_DIR}/src"
  LIBRARIES Threads::Threads SANITIZE TIMEOUT 60)

set(contract_arguments)
set(security_arguments)
if(AWTRIX_REQUIRE_INTEGRATION_SERVICES)
  set(contract_arguments --require-mqtt --require-tls)
  set(security_arguments --require-tools)
endif()
add_awtrix_runtime_test(NAME linux-contract SCRIPT test_contract.py ARGUMENTS ${contract_arguments} TIMEOUT 180)
add_awtrix_runtime_test(NAME app-config-contract SCRIPT test_app_config.py TIMEOUT 90)
add_awtrix_runtime_test(NAME linux-oauth-contract SCRIPT test_oauth_contract.py TIMEOUT 120)
add_awtrix_runtime_test(NAME linux-security SCRIPT test_security.py ARGUMENTS ${security_arguments} TIMEOUT 180
  LABELS needs-mosquitto PROPERTIES SKIP_RETURN_CODE 77)
add_awtrix_runtime_test(NAME linux-supervisor-channel SCRIPT test_supervisor_channel.py TIMEOUT 90)
add_awtrix_runtime_test(NAME linux-performance SCRIPT test_performance.py TIMEOUT 45)
add_awtrix_runtime_test(NAME http-header-contract SCRIPT test_headers.py TIMEOUT 60)
add_awtrix_runtime_test(NAME http-bodyless-post SCRIPT test_bodyless_post.py TIMEOUT 60)
add_awtrix_runtime_test(NAME linux-discovery SCRIPT test_discovery.py TIMEOUT 60 PROPERTIES RUN_SERIAL TRUE)
add_awtrix_runtime_test(NAME linux-icon-origins SCRIPT test_icon_origins.py TIMEOUT 60)
add_awtrix_runtime_test(NAME linux-provision SCRIPT test_provision.py TIMEOUT 180)
add_awtrix_runtime_test(NAME linux-web-update SCRIPT test_web_update.py TIMEOUT 180)
add_awtrix_runtime_test(NAME linux-ca-file SCRIPT test_ca_file.py TIMEOUT 180)
add_awtrix_runtime_test(NAME docs-panels SCRIPT test_docs_panels.py TIMEOUT 300)

add_awtrix_test(NAME linux-crypto-script SOURCES test_crypto_script.cpp
  LIBRARIES awtrix_script_ext awtrix_host SANITIZE)
add_awtrix_test(NAME linux-oauth-spec SOURCES test_oauth_spec.cpp LIBRARIES awtrix_oauth SANITIZE)
add_awtrix_test(NAME linux-oauth-flow SOURCES test_oauth_flow.cpp LIBRARIES awtrix_oauth SANITIZE)
add_awtrix_test(NAME linux-oauth-vault SOURCES test_oauth_vault.cpp LIBRARIES awtrix_oauth SANITIZE)
add_awtrix_test(NAME linux-oauth-service SOURCES test_oauth_service.cpp LIBRARIES awtrix_oauth SANITIZE TIMEOUT 60)
add_awtrix_test(NAME linux-oauth-api SOURCES test_oauth_api.cpp LIBRARIES awtrix_oauth SANITIZE)
add_awtrix_test(NAME linux-oauth-script SOURCES test_oauth_script.cpp
  LIBRARIES awtrix_oauth awtrix_host SANITIZE TIMEOUT 60)
add_awtrix_test(NAME linux-pow SOURCES test_pow.cpp LIBRARIES awtrix_script_ext SANITIZE TIMEOUT 120)
add_awtrix_test(NAME linux-pow-startup SOURCES test_pow_startup.cpp LIBRARIES awtrix_script_ext SANITIZE)
# Pull std::thread into this executable so the pthread_create fault-injection wrapper
# also intercepts calls originating in libstdc++ (a shared library bypasses --wrap).
target_link_options(linux-pow-startup-test PRIVATE -static-libstdc++ -Wl,--wrap=pthread_setschedparam -Wl,--wrap=pthread_create)
add_awtrix_test(NAME linux-crypto-mining SOURCES test_crypto_mining.cpp
  LIBRARIES awtrix_script_ext awtrix_host SANITIZE TIMEOUT 120)
add_awtrix_test(NAME linux-tcp SOURCES test_tcp.cpp LIBRARIES awtrix_script_ext SANITIZE TIMEOUT 45)
