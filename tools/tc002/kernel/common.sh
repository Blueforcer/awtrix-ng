KERNEL_REPO=https://github.com/linux-chenxing/linux-chenxing-vendor-slop.git
KERNEL_COMMIT=e53dccbcd926a883a2859695a6b8839e12daf321
KERNEL_BASE_DEFCONFIG=pioneer3_ssc021a_s01a_defconfig

TC002_KERNEL_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
. "$TC002_KERNEL_DIR/../toolchain/env.sh"
TC002_KMOD_WORK=${TC002_KMOD_WORK:-$TC002_CACHE/kernel}

KERNEL_SRC=$TC002_KMOD_WORK/src/vendor-slop-${KERNEL_COMMIT:0:8}
KERNEL_OUT=$TC002_KMOD_WORK/kernel-${KERNEL_COMMIT:0:8}

STOCK_VERMAGIC="4.9.84 SMP preempt mod_unload ARMv7 thumb2 p2v8 "
STOCK_THIS_MODULE_SIZE=0x1c0
STOCK_INIT_OFFSET=0xe8
STOCK_EXIT_OFFSET=0x180

kmake() {
	make -C "$KERNEL_OUT" -f Makefile ARCH=arm CROSS_COMPILE="$TC002_GLIBC_PREFIX" HOSTCFLAGS=-fcommon "$@"
}
