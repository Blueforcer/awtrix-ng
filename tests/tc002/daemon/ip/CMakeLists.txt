add_library(tc002d_ip_under_test STATIC ${TC002D_IP_SOURCES})
target_link_libraries(tc002d_ip_under_test PUBLIC tc002d_base PRIVATE awtrix_warnings)
foreach(suite units controller)
  add_awtrix_test(NAME tc002d-ip-${suite} SOURCES test_ip_${suite}.cpp LIBRARIES tc002d_ip_under_test TIMEOUT 20)
endforeach()

if(AWTRIX_TC002_PROCESS_TESTS)
  add_executable(tc002d-ip-fake-udhcpc fake_udhcpc.cpp)
  target_link_libraries(tc002d-ip-fake-udhcpc PRIVATE awtrix_warnings)
  add_awtrix_test(NAME tc002d-ip-system SOURCES test_ip_system.cpp LIBRARIES tc002d_ip_under_test
    COMMAND "$<TARGET_FILE:tc002d-ip-fake-udhcpc>" "$<TARGET_FILE:awtrix-tc002-dhcp-callback>"
            ${AWTRIX_TC002_TEST_BUSYBOX}
    TIMEOUT 90 LABELS needs-root PROPERTIES SKIP_RETURN_CODE 77)
endif()
