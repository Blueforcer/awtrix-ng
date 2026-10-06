# TC002 corresponding-source companion

Generate this companion after building a clean committed bundle:

```bash
python3 tools/tc002/install/source_package.py \
  --commit FULL_APPLICATION_COMMIT --bundle-manifest BUNDLE/manifest.json \
  --downloads DOWNLOAD_CACHE --kernel-source PINNED_KERNEL_GIT_CHECKOUT \
  --aic-source PINNED_AIC_GIT_CHECKOUT --musl-buildroot-recipe BUILDROOT/package/musl \
  --output NEW_OUTPUT_DIRECTORY/tc002-sources.tar.gz
```

The output parent must exist and the archive must not exist. `--repo` optionally
selects the application repository; the default is the repository containing the
tool. `--check-only` verifies all source pins, the actual binary files and their
commit/counter binding without creating an archive. No source is downloaded.
Creation uses a fresh private staging directory, fixed archive timestamps and
normalized archive ownership/modes. Its JSON result includes the archive SHA-256.
Pass that digest and the explicit intended HTTPS release-asset URL to
`browser_package.py --source-sha256 ... --source-url ...`; neither tool publishes.

This source set accompanies the exact binary manifest in binary-manifest.json.
Publish it alongside that binary release; this tool does not upload either artifact.
Source licenses remain those in each archive. The AWTRIX font-data licensing is in
the exact application source archive and does not change the application's license.

Included: AWTRIX sources and build/install scripts at the manifest commit; the
complete pinned kernel source tree used to build the shipped modules; the two
complete AIC driver directories that the module recipe exports; original BusyBox,
wpa_supplicant and libnl sources; OpenSSL, liblzma/XZ, libjpeg-turbo, musl, base64 and
Mozilla certificate sources. The actual toolchain's musl patches are retained in
musl-buildroot-recipe. The application archive carries AIC patches, awtrix_pcm, the
loop.ko recipe, kernel configuration, compiler setup recipes and all component configurations.
The AIC archive deliberately excludes fw/ and unused driver/deployment files.
See AIC-SOURCE-NOTICE.txt for the exact selection and the available license evidence.
The MCU patch helper uses the included liblzma/XZ source. The release contains the
MIT MCU extension binary with its recipe and licence. Original and combined MCU images are generated locally and
are never part of this source companion.
No TC002 stock partition/kernel image, AIC radio firmware or proprietary MHAL
binary is included. The complete public upstream kernel tree does retain its
historical firmware/*.ihex payloads and their upstream firmware/WHENCE and license
evidence. Those unrelated upstream files are preserved as part of the pinned
kernel source archive; no claim is made that every such file has GPL terms.
The original GPL/LGPL/BSD terms apply to the respective components; the AWTRIX
application's noncommercial terms must not be applied to those separate works.

Rebuilding / relinking:
1. Extract the application source. Read docs/developers/tc002/install-tools.md and the
   toolchain/env.sh and kernel/common.sh definitions for the required compilers.
2. Put the eight original release archives in TC002_DL_DIR; the build recipes
   validate their pinned SHA-256 values before using them.
3. The original prepare-tree.sh and build_aic8800.sh require Git objects. A tar
   archive cannot be passed to those Git fetch/archive steps unchanged. The
   concrete extraction equivalents below replace those steps and preserve the
   original configure/build/patch/symbol/strip operations. The full kernel tree
   has no gitlinks; no kernel submodule content was omitted by git archive.
4. tools/tc002/wpa_supplicant/build.sh builds both libnl static libraries and the
   complete BSD-licensed wpa_supplicant application, using the included sources.
   Use the recipe ONCE to configure/extract the trees, then modify libnl and use
   the relink commands below. Rerunning the recipe itself would delete and
   re-extract libnl, discarding those modifications. The full wpa_supplicant
   source is the source-code alternative to supplying its object files under
   LGPL-2.1 section 6(a); no proprietary wpa_supplicant object is withheld.
   Its internal TLS/crypto/LibTomMath implementations are in that source archive.
5. tools/tc002/install/build_bundle.sh records the complete program build and
   installation recipe. It can use an extracted source checkout via --working-tree;
   the source-only archive has no Git metadata, so set SOURCE_DATE_EPOCH to the
   original manifest counter. Its dirty/unknown version label then intentionally
   differs from the original release. The bundle recipe still calls the original
   Git-based module steps; use the archive equivalents below when rebuilding the
   modules without network access. A complete offline bundle orchestration has
   not been tested. No reconstructed Git history is claimed. Installation/deployment tools are provided in the application source;
   this companion does not perform deployment. TC002 update packages use integrity hashes
   and release counters, without signing keys.

Archive equivalents for the module recipes (bash, configured pinned toolchains):

```bash
set -euo pipefail
export COMPANION=/absolute/path/to/this-directory
export APP=/absolute/path/to/extracted/awtrix-ng
export TC002_KMOD_WORK=/absolute/path/to/new-kernel-work
export TC002_CACHE=/absolute/path/to/new-tc002-work
export TC002_DL_DIR="$COMPANION"
# Set TC002_GLIBC_PREFIX and TC002_MUSL_PREFIX using toolchain/env.sh instructions.
. "$APP/tools/tc002/kernel/common.sh"
tc002_need_glibc
test ! -e "$KERNEL_SRC" && test ! -e "$KERNEL_OUT"
mkdir -p "$KERNEL_SRC" "$KERNEL_OUT"
for target in "$KERNEL_SRC" "$KERNEL_OUT"; do
  tar -xzf "$COMPANION/linux-chenxing-e53dccbcd926a883a2859695a6b8839e12daf321.tar.gz" --strip-components=1 -C "$target"
done
cat "$KERNEL_OUT/arch/arm/configs/$KERNEL_BASE_DEFCONFIG" "$TC002_KERNEL_DIR/tc002_stock.fragment" > "$KERNEL_OUT/.config"
kmake olddefconfig
diff -u "$TC002_KERNEL_DIR/tc002_stock.config" "$KERNEL_OUT/.config"
kmake -j6 vmlinux
kmake modules_prepare

out="$TC002_CACHE/aic8800-from-source/out"
test ! -e "$out"
mkdir -p "$out"
tar -xzf "$COMPANION/aic8800-dd73e713829ab31029e371bc3fb67b367b8ae2eb.tar.gz" --strip-components=1 -C "$out"
for patch in 0001-register-tdls-mgmt.patch 0002-tc002-power-and-defaults.patch 0004-fdrv-fwlog-en-is-bool.patch 0003-never-log-key-material.patch; do
  patch -p1 --fuzz=0 -d "$out" < "$APP/src/platform/tc002/kmod/aic8800/$patch"
done
printf 'obj-m += aic8800_bsp/ aic8800_fdrv/\n' > "$out/Kbuild"
for symbol in sunxi_wlan_get_bus_index sunxi_wlan_set_power sigmastar_mmc_rescan_card; do
  printf '0x00000000\t%s\tvmlinux\tEXPORT_SYMBOL\n' "$symbol"
done > "$out/tc002-kernel-extra.symvers"
kmake M="$out" KBUILD_EXTRA_SYMBOLS="$out/tc002-kernel-extra.symvers" \
  KCFLAGS="-ffile-prefix-map=$out/=drivers/net/wireless/aic8800/ -ffile-prefix-map=$KERNEL_OUT/=" \
  CONFIG_PLATFORM_TC002=y CONFIG_AIC_FW_PATH=/lib/firmware/aic8800DC \
  CONFIG_USE_P2P0=y CONFIG_PREALLOC_RX_SKB=y modules
for module in aic8800_bsp aic8800_fdrv; do
  "${TC002_GLIBC_PREFIX}objcopy" -R .comment -R .note.gnu.build-id -R .note.GNU-stack \
    -x -G __this_module --strip-unneeded "$out/$module/$module.ko" "$out/$module.ko"
  bash "$TC002_KERNEL_DIR/check-ko.sh" "$out/$module.ko"
done
bash "$TC002_KERNEL_DIR/build-awtrix-pcm.sh" "$TC002_CACHE/awtrix_pcm-from-source"
bash "$TC002_KERNEL_DIR/build-loop.sh" "$TC002_CACHE/loop-from-source"
```

Libnl modification/relink workflow (bash; no device access):

```bash
set -euo pipefail
export APP=/absolute/path/to/extracted/awtrix-ng
export TC002_DL_DIR=/absolute/path/to/this-directory
export WPA_WORK=/absolute/path/to/new-wpa-work
# Set TC002_MUSL_PREFIX to the pinned compiler before running this.
WPA_VARIANT=tc002 WPA_STDOUT_DEBUG=0 bash "$APP/tools/tc002/wpa_supplicant/build.sh" "$WPA_WORK" "$WPA_WORK/out"
```

Now edit the Library in `$WPA_WORK/libnl-3.11.0`, retain the complete source and
configuration, then relink without re-extracting either source tree:

```bash
set -euo pipefail
LIBNL_SRC="$WPA_WORK/libnl-3.11.0"
WPA_SRC="$WPA_WORK/wpa_supplicant-2.12"
export SOURCE_DATE_EPOCH=0 LC_ALL=C TZ=UTC
SIZE_FLAGS="-Os -ffunction-sections -fdata-sections"
make -C "$LIBNL_SRC" -j6 lib/libnl-3.la lib/libnl-genl-3.la
CC="${TC002_MUSL_PREFIX}gcc" \
  CFLAGS="$SIZE_FLAGS -Wall -ffile-prefix-map=$WPA_SRC=wpa_supplicant-2.12" \
  LDFLAGS="-static -Wl,--gc-sections -L$LIBNL_SRC/lib/.libs" \
  LIBNL_INC="$LIBNL_SRC/include" EXTRALIBS="-lnl-3" \
  make -C "$WPA_SRC/wpa_supplicant" -B -j6 QUIET=1 wpa_supplicant
"${TC002_MUSL_PREFIX}strip" --strip-all -o "$WPA_WORK/out/wpa_supplicant-modified" "$WPA_SRC/wpa_supplicant/wpa_supplicant"
```

LGPL-2.1 section 6 also requires the applicable notices/license and terms allowing
the user's own modifications and reverse engineering for debugging them. The
bundle includes the libnl notice/license, and wpa_supplicant has BSD terms; this
companion does not add a noncommercial restriction to either. Preserve those
permissions when publishing or writing any enclosing release terms. Compiler
packages and the Buildroot bootstrap/download cache are not included here; the
application archive records their pinned acquisition/build recipes. This is not
a self-contained offline development environment.

The inventory covers the source inputs and build/install recipes identified for
the shipped GPL/LGPL programs and modules. It is not a legal clearance for every
upstream file, a bootable image, or a hardware-qualified TC002 system.
The exact committed build is tested separately; the extraction substitutions
described above have not been run as a second offline build of this companion.
Publishing the binaries requires publishing this companion at the same download
location, or choosing another valid source-delivery mechanism under the licenses.
Do not present merely upstream links as a substitute for this prepared source set.
Reference terms: GPL-2.0 section 3 and LGPL-2.1 section 6:
https://www.gnu.org/licenses/old-licenses/gpl-2.0.html
https://www.gnu.org/licenses/old-licenses/lgpl-2.1.html
