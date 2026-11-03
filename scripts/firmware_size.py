"""Take what the firmware never uses out of the link.

Runs post: it edits the link line the framework builder produces. The compile flags and the
`-Wl,--wrap` list that go with it are in platformio.ini, the replacement functions in
src/system/SizeWraps.c. Figures are for the `awtrix` env at 1.2.3, against a 1 665 168 byte image.

  * LTO, 79 552 bytes of flash. The framework's link line says `-fno-lto`, so it has to go before
    `-flto` means anything. The prebuilt ESP-IDF libraries stay as they are: only what is compiled
    here takes part. Objects are built fat because PlatformIO archives with plain `ar`, whose
    symbol index does not see the slim ones.

    Every `__wrap_` function has to carry `__attribute__((used))`. Under LTO nothing the compiler
    can see refers to it, so it is dropped before the linker asks for it, and the link fails with
    an undefined reference to `__wrap_<name>`.

    HeapProbe.cpp stays out of LTO (scripts/heap_probe_opt.py). With `--wrap=malloc` the linker
    pointed its `__real_malloc` at free(), and the probe builds linked fine and then aborted on the
    first Berry allocation. After changing a wrap, check what the `__wrap_` functions really call
    in the disassembly.

  * The Bluetooth high-level interrupt, 4 832 bytes of flash and 4 444 of DRAM on the classic
    ESP32. `-u ld_include_hli_vectors_bt` pulls in hli_vectors.S with a 4 KB interrupt stack, and
    no firmware here has Bluetooth. Without it libfreertos' weak default `xt_highint4` is linked,
    and nothing enables level 4 interrupts. The panic path is `xt_highint5` and stays. The S3 has
    no such flag.

  * Exception tables, 12 656 bytes of flash together with `__wrap___cxa_throw`. Nothing is ever
    caught: the firmware builds with `-fno-exceptions`, and what libstdc++ throws ends in abort()
    either way. `operator new(size_t, nothrow)` is not affected. It calls malloc directly and
    returns null, and its one handler only guards a custom new-handler, which nothing installs.
    Check that by disassembly before moving to another toolchain, since newer libstdc++ builds it
    on the throwing `operator new`, and this edit would turn its null return into an abort.
"""

import os
import sys

Import("env")


def fail(message):
    sys.stderr.write("firmware_size.py: %s Fix this rather than shipping a larger image.\n" % message)
    sys.exit(1)


def without_hli(flags):
    kept = []
    found = False
    position = 0
    while position < len(flags):
        if flags[position] == "-u" and flags[position + 1:position + 2] == ["ld_include_hli_vectors_bt"]:
            found = True
            position += 2
            continue
        kept.append(flags[position])
        position += 1
    if not found:
        fail("'-u ld_include_hli_vectors_bt' is no longer on the link line: the framework changed.")
    return kept


def find_sections_script():
    for directory in env["LIBPATH"]:
        candidate = os.path.join(env.subst(str(directory)), "sections.ld")
        if os.path.isfile(candidate):
            return candidate
    fail("no sections.ld on the library path: the framework changed.")


def without_exception_tables(script):
    keep_rule = "KEEP(*(.eh_frame))"
    opening = "SECTIONS\n{"
    if keep_rule not in script or opening not in script:
        fail("sections.ld no longer has the expected .eh_frame rule: the framework changed.")
    discard = "\n  /DISCARD/ : { *(.eh_frame) *(.gcc_except_table .gcc_except_table.*) }\n"
    return script.replace(keep_rule, "").replace(opening, opening + discard, 1)


flags = [flag for flag in env["LINKFLAGS"] if flag != "-fno-lto"]
if env.BoardConfig().get("build.mcu") == "esp32":
    flags = without_hli(flags)
env.Replace(LINKFLAGS=flags + ["-flto", "-Os", "-fno-exceptions"])

with open(find_sections_script()) as handle:
    patched = without_exception_tables(handle.read())
directory = os.path.join(env.subst("$BUILD_DIR"), "ldscripts")
target = os.path.join(directory, "sections.ld")
os.makedirs(directory, exist_ok=True)
current = None
if os.path.isfile(target):
    with open(target) as handle:
        current = handle.read()
if current != patched:
    with open(target, "w") as handle:
        handle.write(patched)
env.Prepend(LIBPATH=[directory])
