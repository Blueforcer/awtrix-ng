"""Link newlib's nano formatted I/O instead of the full one.

The Arduino prebuilt sdkconfig ships with `CONFIG_NEWLIB_NANO_FORMAT` off, so the
image carries both printf families at full size -- vfprintf, svfprintf, svfiprintf,
vfiprintf and svfiscanf together are 58 546 bytes of flash. The nano equivalents
cover the same call sites in 5 127, plus 1 594 for the float conversion below.
It costs no heap: nano printf uses less stack than the full one, not more.

Switching is a link-order change, exactly as ESP-IDF implements the Kconfig option:
`libc_nano.a` ships with the toolchain and the linker resolves each symbol from the
first archive that defines it, so scanning the nano archive ahead of `-lc` is enough.
`-lc` stays behind it: anything nano does not define still comes from the full libc.

**The archive is linked through a copy named `libc.a`, and that name is load-bearing.**
The Arduino sdkconfig enables the PSRAM cache workaround, so the generated
`esp32.project.ld` pins most of libc into IRAM with rules that match on the archive
*file name*:

    *libc.a:lib_a-memset.*(.literal .literal.* .text .text.*)   <- inside .iram0.text

`libc_nano.a` matches none of them. Scanning it under its own name therefore moves
`memset`, `memcpy`, `memmove`, `strlen` and ~85 more objects out of IRAM into flash,
where SPI-flash driver code that runs with the cache disabled cannot call them. The
symptom is a "Cache disabled but cached memory region accessed" panic in
`read_id_core` during `esp_flash_init_default_chip()`, i.e. a boot that dies in
`do_core_init` and reboots -- reproducible on the first boot after an OTA, which is
why `resetReason` read `panic` after every HTTP update. Copying the archive to
`$BUILD_DIR/newlib-nano/libc.a` makes every existing placement rule apply to it: the
member names are identical to libc.a's, so the IRAM set stays in IRAM and only the
nano-specific members (`lib_a-nano-vfprintf*.o`, which no rule mentions) land in
flash. Do not "simplify" this back to a bare `-lc_nano`.

Two consequences the firmware has to live with:

  * nano's `_printf_i` has no 64-bit integer conversion -- it does not even reference
    the libgcc division helpers `%lld` would need. The firmware formats no 64-bit
    integers; JsonWriter::appendDigits does that by hand. Do not reintroduce `%lld`
    or `%llu`.
  * `_printf_float` is a weak symbol nothing references, so `%f` and `%g` need
    `-u _printf_float` to pull it in. `%.*f` and `%.6g` are live in the JSON writers
    and the sensor formatter, so it is not optional.

`-u _scanf_float` is deliberately absent: no scanf call site converts a float.
"""

import os
import shutil
import subprocess
import sys

Import("env")

libs = env["LIBS"]
try:
    position = libs.index("-lc")
except ValueError:
    sys.stderr.write(
        "newlib_nano.py: '-lc' is no longer in LIBS - the framework changed its link "
        "line. Fix the insertion point rather than shipping a 55 KB larger image.\n"
    )
    sys.exit(1)

selectors = [
    flag
    for flag in list(env["CCFLAGS"]) + list(env["CXXFLAGS"]) + list(env["LINKFLAGS"])
    if isinstance(flag, str) and (flag.startswith("-m") or flag in ("-fno-rtti", "-frtti"))
]
cxx = env.WhereIs(env.subst("$CXX")) or env.subst("$CXX")
nano = subprocess.run(
    [cxx] + selectors + ["-print-file-name=libc_nano.a"],
    stdout=subprocess.PIPE,
    universal_newlines=True,
).stdout.strip()
if not nano or not os.path.isfile(nano):
    sys.stderr.write(
        "newlib_nano.py: the toolchain does not ship libc_nano.a for this multilib "
        "(asked %s, got %r).\n" % (cxx, nano)
    )
    sys.exit(1)

linked = os.path.join(env.subst("$BUILD_DIR"), "newlib-nano", "libc.a")
if not os.path.isfile(linked) or os.path.getmtime(linked) < os.path.getmtime(nano) or (
    os.path.getsize(linked) != os.path.getsize(nano)
):
    os.makedirs(os.path.dirname(linked), exist_ok=True)
    shutil.copy2(nano, linked)

libs.insert(position, env.File(linked))
env.Append(LINKFLAGS=["-u", "_printf_float"])
