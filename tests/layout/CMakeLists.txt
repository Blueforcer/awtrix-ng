foreach(suite layout scriptlayout layoutcapabilities)
  add_awtrix_test(NAME linux-${suite} SOURCES test_${suite}.cpp
    LIBRARIES awtrix_layout unity SANITIZE)
endforeach()
