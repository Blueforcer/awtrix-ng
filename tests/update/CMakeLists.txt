add_library(awtrix_update_test_faults SHARED test_faults.cpp)
target_link_libraries(awtrix_update_test_faults PRIVATE ${CMAKE_DL_LIBS} awtrix_warnings)
add_awtrix_test(NAME update-package-contract
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_update.py"
          --verifier "$<TARGET_FILE:awtrix-update-verify>" --faults "$<TARGET_FILE:awtrix_update_test_faults>"
  TIMEOUT 120)
add_awtrix_test(NAME update-state-policy SOURCES test_state.cpp LIBRARIES awtrix_update_state
  COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/target_vectors.json" SANITIZE TIMEOUT 60)
