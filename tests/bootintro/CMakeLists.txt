add_awtrix_test(NAME boot-intro-wide SOURCES test_boot_intro_wide.cpp ${TC002_BOOT_SCREEN_SOURCES}
  LIBRARIES awtrix_core TIMEOUT 60)
add_awtrix_test(NAME boot-info-wide SOURCES test_boot_info_wide.cpp ${TC002_BOOT_SCREEN_SOURCES}
  LIBRARIES awtrix_core TIMEOUT 60)
add_awtrix_test(NAME linux-boot-intro-unit SOURCES test_linux_boot_intro.cpp
  "${TC002_RUNTIME_DIR}/Tc002BootIntro.cpp" ${TC002_BOOT_SCREEN_SOURCES} LIBRARIES awtrix_core TIMEOUT 60)
add_awtrix_test(NAME tc002-boot-sound SOURCES test_boot_sound.cpp LIBRARIES tc002_audio
  COMMAND "${PROJECT_SOURCE_DIR}/assets/tc002/boot.mp3" TIMEOUT 60)
add_awtrix_runtime_test(NAME linux-boot-intro SCRIPT test_boot_intro.py TIMEOUT 120)
