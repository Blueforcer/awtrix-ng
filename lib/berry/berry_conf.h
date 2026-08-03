/*
 * AWTRIX NG build configuration for the vendored Berry interpreter.
 *
 * Provenance
 * ----------
 * Vendored from https://github.com/berry-lang/berry at tag v1.1.0
 * (commit b5ede66721937533fbf5c286ef44a5111ea30c75). Everything under
 * lib/berry/src/ and lib/berry/default/ is upstream code, unmodified EXCEPT
 * one memory-safety fix (marked "AWTRIX (candidate for upstream)"): be_vm.c
 * and be_exec.c nil-fill freshly allocated/grown VM stack memory, because
 * premark_stack scans up to vm->top and precall raises top over slots no
 * instruction has written yet -- malloc garbage there that happens to look
 * like a GC object makes the mark phase dereference a wild pointer. Found
 * 2026-07-31 via a page-guard allocator on the host suite.
 * This file is ours: it is a copy of upstream's default/berry_conf.h with
 * AWTRIX values substituted. Deviations from upstream are marked "AWTRIX:".
 *
 * How this file is found
 * ----------------------
 * Berry v1.1.0 has NO BE_USER_CONF indirection: src/berry.h does a plain
 * `#include "berry_conf.h"`. Configuration is therefore selected purely by
 * include-path order. lib/berry/library.json puts the library root (`-I.`)
 * on the include path, and upstream's own default/berry_conf.h has been
 * DELETED from the vendored tree, so this file is the only berry_conf.h
 * that exists. That is deliberate: a stray second copy could silently
 * re-enable the os/file modules disabled below.
 *
 * Generated tables
 * ----------------
 * Berry's build requires a code-generation pass (upstream tools/coc/coc,
 * a Python script) that emits lib/berry/generate/*.h from the sources AND
 * from this config. Those headers are checked in. If you change any
 * BE_USE_*_MODULE value here you MUST regenerate them:
 *
 *   python <berry-checkout>/tools/coc/coc \
 *       -o lib/berry/generate lib/berry/src lib/berry/default \
 *       -c lib/berry/berry_conf.h
 *
 * Execution hook (relied on by the script sandbox)
 * ------------------------------------------------
 * Berry v1.1.0 exposes an "observability hook", not a per-instruction
 * debug callback:
 *
 *   typedef void (*bobshook)(bvm *vm, int event, ...);   src/berry.h:413
 *   BERRY_API void be_set_obs_hook(bvm *vm, bobshook);   src/berry.h:563
 *                                                        src/be_vm.c:1305
 *
 * The VM's dispatch loop invokes it with BE_OBS_VM_HEARTBEAT via the
 * VM_HEARTBEAT() macro (src/be_vm.c:58-66), passing the running
 * instruction count (vm->counter_ins). The whole mechanism -- the counter
 * and the heartbeat -- is compiled out unless BE_USE_PERF_COUNTERS is 1,
 * so we keep it on below.
 *
 * Note the actual firing period is 2^(BE_VM_OBSERVABILITY_SAMPLING - 1)
 * instructions, not 2^BE_VM_OBSERVABILITY_SAMPLING as upstream's comment
 * claims; the mask in be_vm.c is ((1 << (SAMPLING - 1)) - 1).
 *
 * The other hook upstream offers is the debug line hook
 * (be_setntvhook/be_sethook, BE_HOOK_LINE, gated on BE_USE_DEBUG_HOOK,
 * dispatched by do_linehook in src/be_debug.c). We do not use it, but it
 * is left compiled in.
 */
#ifndef BERRY_CONF_H
#define BERRY_CONF_H

#include <assert.h>

/* Macro: BE_DEBUG
 * Berry interpreter debug switch.
 * Default: 0
 **/
#ifndef BE_DEBUG
#define BE_DEBUG                        0
#endif

/* Macro: BE_LONGLONG_INT
 * Select integer length.
 * If the value is 0, use an integer of type int, use a long
 * integer type when the value is 1, and use a long long integer
 * type when the value is 2.
 * Default: 2
 */
#define BE_INTGER_TYPE                  2

/* Macro: BE_USE_SINGLE_FLOAT
 * Select floating point precision.
 * Use double-precision floating-point numbers when the value
 * is 0 (default), otherwise use single-precision floating-point
 * numbers.
 * Default: 0
 * AWTRIX: 1 -- the ESP32 has a single-precision FPU; doubles are
 * emulated in software and cost both cycles and flash.
 **/
#define BE_USE_SINGLE_FLOAT             1

/* Macro: BE_USE_PRECOMPILED_OBJECT
 * Use precompiled objects to avoid creating these objects at
 * runtime. Enable this macro can greatly optimize RAM usage.
 * Default: 1
 **/
/* AWTRIX: #ifndef-guarded so the solidify generator (tools/solidify) can build
 * the vendored tree in runtime-object mode (-DBE_USE_PRECOMPILED_OBJECT=0),
 * where be_solidifylib.c registers its module without the coc-generated const
 * tables. The device build leaves this at 1. */
#ifndef BE_USE_PRECOMPILED_OBJECT
#define BE_USE_PRECOMPILED_OBJECT       1
#endif

/* AWTRIX (berry master rebase): macros the newer tree adds. Master defaults,
 * except BE_DEBUG_SOURCE_FILE.
 *
 * BE_DEBUG_SOURCE_FILE is 0 (master: 1): drops the bstring* source slot from
 * every runtime-compiled bproto. Every chunk this firmware compiles is named
 * "script", so the field carried no information. Structured script errors are
 * unaffected: compile errors take their "script:NN:" prefix from the lexer's
 * fname (be_lexer.c, independent of this macro) and runtime error VALUES never
 * carried a location -- only the serial-only traceback loses its "script"
 * prefix per frame. Solidified protos are layout-safe either way: the
 * be_nested_proto macro swallows the emitted source argument when this is 0. */
#ifndef BE_BYTES_MAX_SIZE
#define BE_BYTES_MAX_SIZE               (32*1024)
#endif
#ifndef BE_DEBUG_SOURCE_FILE
#define BE_DEBUG_SOURCE_FILE            1
#endif
/* AWTRIX: 1 on the classic ESP32 (master default: 0). After a script compiles,
 * be_parser moves each proto's bytecode, constant table and line info through
 * be_move_to_aligned() into memory obtained from berry_malloc32() -- provided
 * by the script heap seam (src/system/ScriptHeapEsp32.cpp) from the IRAM-only
 * heap, which allows 32-bit access only and is otherwise dead weight. Only
 * arrays whose every access is a core 32-bit load may live there:
 * binstruction is uint32_t and blineinfo is two ints (which is why
 * BE_DEBUG_RUNTIME_INFO below is 1 whenever this flag is on). ktab must NOT
 * move -- real constants are read with FPU loads (LSI), which fault on IRAM;
 * be_parser.c carries that deviation. Host builds and the S3 keep the flag
 * off: no IRAM-only heap there, and the S3's Berry heap already lives in
 * PSRAM. */
#ifndef BE_USE_MEM_ALIGNED
#if defined(ESP_PLATFORM) && !defined(AWTRIX_SOC_ESP32S3)
#define BE_USE_MEM_ALIGNED              1
#else
#define BE_USE_MEM_ALIGNED              0
#endif
#endif

/* Macro: BE_DEBUG_RUNTIME_INFO
 * Set runtime error debugging information.
 * 0: unable to output source file and line number at runtime.
 * 1: output source file and line number information at runtime.
 * 2: the information use uint16_t type (save space).
 * Default: 1
 * AWTRIX: 2 (uint16_t) is the DRAM-lean choice, but 16-bit loads fault in the
 * IRAM-only region be_move_to_aligned() targets, so the BE_USE_MEM_ALIGNED
 * build uses 1: twice the bytes per entry, in memory nothing else could use.
 **/
#if BE_USE_MEM_ALIGNED
#define BE_DEBUG_RUNTIME_INFO           1
#else
#define BE_DEBUG_RUNTIME_INFO           2
#endif

/* Macro: BE_DEBUG_VAR_INFO
 * Set variable debugging tracking information.
 * 0: disable variable debugging tracking information at runtime.
 * 1: enable variable debugging tracking information at runtime.
 * Default: 1
 **/
#define BE_DEBUG_VAR_INFO               0

/* Macro: BE_USE_PERF_COUNTERS
 * Use the obshook function to report low-level actions.
 * Default: 1
 * AWTRIX: must stay 1. This is what compiles in vm->counter_ins and the
 * BE_OBS_VM_HEARTBEAT callback that the script sandbox uses to bound
 * runaway scripts. See the header comment above.
 **/
#define BE_USE_PERF_COUNTERS            1

/* Macro: BE_VM_OBSERVABILITY_SAMPLING
 * If BE_USE_PERF_COUNTERS == 1
 * then the observability hook is called regularly in the VM loop
 * allowing to stop infinite loops or too-long running code.
 * The value is a power of 2.
 * Default: 20 - which upstream documents as 2^20 or ~1 million
 * instructions, but the mask in be_vm.c makes the real period
 * 2^(value-1), so 20 actually means ~500k. See the preamble note.
 * AWTRIX: 11, i.e. the heartbeat fires every 2^10 = 1024 instructions.
 * Upstream's default only checks in every ~500k instructions, which is
 * far too coarse to keep a misbehaving script from stalling the display
 * refresh. 1024 keeps the granularity of the instruction budget useful
 * while the per-check cost stays under 0.1% of dispatch.
 **/
#define BE_VM_OBSERVABILITY_SAMPLING    11

/* Macro: BE_STACK_TOTAL_MAX
 * Set the maximum total stack size.
 * Default: 20000
 * AWTRIX: 2000 -- caps runaway recursion well inside the ESP32 heap.
 **/
#define BE_STACK_TOTAL_MAX              2000

/* Macro: BE_STACK_FREE_MIN
 * Set the minimum free count of the stack. The stack idles will
 * be checked when a function is called, and the stack will be
 * expanded if the number of free is less than BE_STACK_FREE_MIN.
 * Default: 10
 **/
#define BE_STACK_FREE_MIN               10

/* Macro: BE_STACK_START
 * Set the starting size of the stack at VM creation.
 * Default: 50
 **/
#define BE_STACK_START                  50

/* Macro: BE_CONST_SEARCH_SIZE
 * Constants in function are limited to 255. However the compiler
 * will look for a maximum of pre-existing constants to avoid
 * performance degradation. This may cause the number of constants
 * to be higher than required.
 * Increase is you need to solidify functions.
 * Default: 50
 **/
#define BE_CONST_SEARCH_SIZE            50

/* Macro: BE_STACK_FREE_MIN
 * The short string will hold the hash value when the value is
 * true. It may be faster but requires more RAM.
 * Default: 0
 **/
#define BE_USE_STR_HASH_CACHE           0

/* Macro: BE_USE_FILE_SYSTEM
 * The file system interface will be used when this macro is true
 * or when using the OS module. Otherwise the file system interface
 * will not be used.
 * Default: 1 (upstream's comment here says 0, but default/berry_conf.h
 * at b5ede66 ships 1).
 * AWTRIX: 0. Verified against the vendored source, this macro guards
 * exactly one thing: the directory/path helpers in default/be_port.c
 * (be_isdir, be_getcwd, be_chdir, be_mkdir, be_unlink, be_dirfirst/next/
 * close). Turning it off drops those and, on the host builds, the
 * <dirent.h>/<sys/stat.h> dependency. be_oslib.c #errors if the os module
 * is on while this is off, which is consistent -- both are off here.
 *
 * NOT a complete file sandbox: the `open()` builtin lives in
 * src/be_filelib.c, which upstream compiles and registers into the
 * builtin table UNCONDITIONALLY (see the m_builtin vartab in
 * src/be_baselib.c). Scripts can therefore still open/read/write files
 * by path. Removing it belongs to the BerryVM wrapper, which should drop
 * the `open` global after be_vm_new(); excluding be_filelib.c from the
 * build instead would leave be_nfunc_open undefined at link time.
 **/
#define BE_USE_FILE_SYSTEM              0

/* Macro: BE_USE_SCRIPT_COMPILER
 * Enable compiler when BE_USE_SCRIPT_COMPILER is not 0, otherwise
 * disable the compiler.
 * Default: 1
 * AWTRIX: 1 -- we ship source, not bytecode; be_loadstring needs this.
 **/
#define BE_USE_SCRIPT_COMPILER          1

/* Macro: BE_USE_BYTECODE_SAVER
 * Enable save bytecode to file when BE_USE_BYTECODE_SAVER is not 0,
 * otherwise disable the feature.
 * Default: 1
 * AWTRIX: 0 -- writes .bec files, needs BE_USE_FILE_SYSTEM.
 **/
#define BE_USE_BYTECODE_SAVER           0

/* Macro: BE_USE_BYTECODE_LOADER
 * Enable load bytecode from file when BE_USE_BYTECODE_LOADER is not 0,
 * otherwise disable the feature.
 * Default: 1
 * AWTRIX: 0 -- reads .bec files, needs BE_USE_FILE_SYSTEM. Loading
 * attacker-supplied bytecode also bypasses the compiler entirely.
 **/
#define BE_USE_BYTECODE_LOADER          0

/* Macro: BE_USE_SHARED_LIB
 * Enable shared library  when BE_USE_SHARED_LIB is not 0,
 * otherwise disable the feature.
 * Default: 1
 * AWTRIX: 0 (sandbox) -- dlopen()s native code named by the script.
 * Meaningless on the ESP32 and a full sandbox escape on the host builds.
 **/
#define BE_USE_SHARED_LIB               0

/* Macro: BE_USE_OVERLOAD_HASH
 * Allows instances to overload hash methods for use in the
 * built-in Map class. Disable this feature to crop the code
 * size.
 * Default: 1
 **/
#define BE_USE_OVERLOAD_HASH            1

/* Macro: BE_USE_DEBUG_HOOK
 * Berry debug hook switch.
 * Default: 1 (upstream's comment here says 0, but default/berry_conf.h
 * at b5ede66 ships 1).
 * AWTRIX: left at upstream's 1. This is the per-line debug hook
 * (be_setntvhook), a different mechanism from the observability
 * heartbeat we use for the instruction budget.
 **/
#define BE_USE_DEBUG_HOOK               0

/* Macro: BE_USE_DEBUG_GC
 * Enable GC debug mode. This causes an actual gc after each
 * allocation. It's much slower and should not be used
 * in production code.
 * Default: 0
 **/
#define BE_USE_DEBUG_GC                  0

/* Macro: BE_USE_DEBUG_STACK
 * Enable Stack Resize debug mode. At each function call
 * the stack is reallocated at a different memory location
 * and the previous location is cleared with toxic data.
 * Default: 0
 **/
#define BE_USE_DEBUG_STACK               0

/* Macro: BE_USE_XXX_MODULE
 * These macros control whether the related module is compiled.
 * When they are true, they will enable related modules. At this
 * point you can use the import statement to import the module.
 * They will not compile related modules when they are false.
 *
 * AWTRIX: os/sys/debug/time are off -- see the per-line notes. Changing
 * any value here requires regenerating lib/berry/generate/ (see above).
 **/
#define BE_USE_STRING_MODULE            1
#define BE_USE_JSON_MODULE              1
#define BE_USE_MATH_MODULE              1
/* AWTRIX: 0 -- we expose our own NTP-backed time bindings instead. */
#define BE_USE_TIME_MODULE              0
/* AWTRIX: 0 (sandbox) -- file access, chdir, system(), exit(). */
#define BE_USE_OS_MODULE                0
#define BE_USE_GLOBAL_MODULE            1
/* AWTRIX: 0 (sandbox) -- sys.path manipulation and module loading. */
#define BE_USE_SYS_MODULE               0
/* AWTRIX: 0 -- exposes the VM internals (traceback, counters, gc probes).
 * #ifndef-guarded so the solidify generator can turn it on (solidify needs
 * be_print_inst, declared only under this module); the device keeps it 0. */
#ifndef BE_USE_DEBUG_MODULE
#define BE_USE_DEBUG_MODULE             0
#endif
#define BE_USE_GC_MODULE                1
/* AWTRIX: 0. A build-time developer tool (dumps compiled functions as C
 * source for embedding); nothing on the device needs it. It also cannot be
 * built with BE_USE_DEBUG_MODULE off: src/be_solidifylib.c calls
 * be_print_inst() unguarded, but src/be_debug.h only declares it under
 * `#if BE_USE_DEBUG_MODULE`. That is an upstream v1.1.0 defect -- leave it
 * to upstream rather than patching the vendored tree. */
#ifndef BE_USE_SOLIDIFY_MODULE
#define BE_USE_SOLIDIFY_MODULE          0
#endif
/* AWTRIX: 0 (sandbox) -- `introspect.fromptr(n)` casts an arbitrary integer to
 * a bgcobject* and dereferences it, so four lines of user script segfault the
 * firmware with no error frame and no latch. toptr/get/set/members are the same
 * hazard by other routes. Note this CANNOT be closed the way `open` is: a
 * module is not reachable as a global, so shadowing the name with nil does
 * nothing -- `import` binds the name itself and resolves against
 * be_module_table. Dropping it from that table is the only fix.
 *
 * This is also why the generate/ tables did not need the coc pass documented
 * above: be_introspectlib.c compiles to nothing and its entry disappears from
 * be_modtab.c, while generate/be_fixed_introspect.h simply stops being
 * included. The string constants it contributed to be_const_strtab.h are inert.
 * Verified by test_vm_module_sandbox_inventory plus a full ulanzi build. */
#define BE_USE_INTROSPECT_MODULE        0
#define BE_USE_STRICT_MODULE            1

/* Macro: BE_EXPLICIT_XXX
 * If these macros are defined, the corresponding function will
 * use the version defined by these macros. These macro definitions
 * are not required.
 * The default is to use the functions in the standard library.
 **/
#define BE_EXPLICIT_ABORT               abort
#define BE_EXPLICIT_EXIT                exit

/* AWTRIX NG: the VM allocates through the host application's script heap seam
 * (src/core/script/ScriptHeap.h) rather than through the C library directly.
 * On a board with usable PSRAM that seam places the VM heap there, which is
 * the only way Berry ever reaches it -- its objects are far too small to cross
 * the allocator's own internal/external threshold.
 *
 * be_mem.c redefines malloc/free/realloc onto these names file-wide, so the
 * gc16/gc32 small-object pools are carried along too and the seam only ever
 * sees pool-sized blocks. Nothing else in this library changes.
 *
 * This header is included from both C and C++ (be_mem.c and BerryVM.cpp), so
 * the declarations need explicit C linkage or the two would not refer to the
 * same symbols. */
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
void* awtrix_script_heap_alloc(size_t size);
void* awtrix_script_heap_realloc(void* ptr, size_t size);
void awtrix_script_heap_free(void* ptr);
/* IRAM-only (32-bit access) allocation for be_move_to_aligned(); may return
 * NULL, in which case the block simply stays where it is. Only referenced when
 * BE_USE_MEM_ALIGNED is 1; upstream's be_mem.c calls it without declaring it. */
void* berry_malloc32(size_t size);
#ifdef __cplusplus
}
#endif

#define BE_EXPLICIT_MALLOC              awtrix_script_heap_alloc
#define BE_EXPLICIT_FREE                awtrix_script_heap_free
#define BE_EXPLICIT_REALLOC             awtrix_script_heap_realloc

/* Macro: be_assert
 * Berry debug assertion. Only enabled when BE_DEBUG is active.
 * Default: use the assert() function of the standard library.
 **/
#define be_assert(expr)                 assert(expr)

#endif
