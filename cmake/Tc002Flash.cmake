include_guard(GLOBAL)

set(TC002_FLASH_ROOT "${CMAKE_CURRENT_LIST_DIR}/../src/platform")
set(TC002_RELEASE_SLOT_SOURCE "${TC002_FLASH_ROOT}/tc002/contract/release_slot.c")
set(TC002_MTD_TABLE_SOURCE "${TC002_FLASH_ROOT}/tc002/flasher/mtd_table.c")
set(TC002_SLOT_C_SOURCES ${TC002_RELEASE_SLOT_SOURCE} "${TC002_FLASH_ROOT}/posix/sha256.c")
set(TC002_FLASH_C_SOURCES
  "${TC002_FLASH_ROOT}/tc002/flasher/flash_write.c"
  "${TC002_FLASH_ROOT}/tc002/flasher/slot_io.c"
  ${TC002_MTD_TABLE_SOURCE} ${TC002_SLOT_C_SOURCES})
