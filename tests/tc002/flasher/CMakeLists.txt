add_awtrix_test(NAME tc002-flasher SOURCES test_flasher.c ${TC002_FLASH_C_SOURCES} INCLUDES "${PROJECT_SOURCE_DIR}/src" SANITIZE TIMEOUT 20)

# The installer's lock tests run the host build of the helper.
awtrix_sanitize(awtrix-tc002-flash)
