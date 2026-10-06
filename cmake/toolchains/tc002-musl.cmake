# The TC002 programs: the static musl armv7 hard-float toolchain whose prefix TC002_MUSL_PREFIX
# names, as a cache variable or in the environment (tools/tc002/toolchain/env.sh). The ARM
# contracts run under qemu-arm when it is found (TC002_QEMU_ARM).
set(TC002_PREFIX_VARIABLE TC002_MUSL_PREFIX)
include("${CMAKE_CURRENT_LIST_DIR}/tc002-common.cmake")
# Cortex-A7 with NEON and VFPv4, hard-float ARM code.
set(CMAKE_C_FLAGS_INIT "-mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard -marm")
set(CMAKE_CXX_FLAGS_INIT "${CMAKE_C_FLAGS_INIT}")

find_program(TC002_QEMU_ARM qemu-arm)
if(TC002_QEMU_ARM)
  set(CMAKE_CROSSCOMPILING_EMULATOR "${TC002_QEMU_ARM};-cpu;cortex-a7")
endif()
