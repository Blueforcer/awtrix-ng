add_library(mqtt-parser-pubsubclient STATIC "${PROJECT_SOURCE_DIR}/lib/PubSubClient/src/PubSubClient.cpp")
target_include_directories(mqtt-parser-pubsubclient SYSTEM PUBLIC "${PROJECT_SOURCE_DIR}/lib/PubSubClient/src")
target_include_directories(mqtt-parser-pubsubclient PUBLIC "${PROJECT_SOURCE_DIR}/src/platform/linux/host/compat"
  "${PROJECT_SOURCE_DIR}/src/platform/linux/include" "${PROJECT_SOURCE_DIR}/src")
awtrix_sanitize(mqtt-parser-pubsubclient)
add_awtrix_test(NAME mqtt-parser SOURCES test_parser.cpp LIBRARIES mqtt-parser-pubsubclient Threads::Threads SANITIZE)
