add_awtrix_test(NAME tc002-wifi-units SOURCES test_wifi_units.cpp ${TC002_WIFI_CORE_SOURCES} LIBRARIES tc002d_base)
add_awtrix_test(NAME tc002-wifi-service SOURCES test_wifi_service.cpp ${TC002D_WIFI_SOURCES}
  LIBRARIES tc002d_base tc002_contract)
