// Link-time replacements that take unused weight out of the prebuilt framework libraries. The
// -Wl,--wrap list is in platformio.ini, the reasons and the LTO rules in scripts/firmware_size.py.

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include "esp_attr.h"
#include "multi_heap.h"

#define LINK_WRAP __attribute__((used))

// WiFiClientSecure calls this for log_e, which is compiled out, and for lastError(), which nothing
// here calls. mbedtls_strerror itself is 15 KB of flash, nearly all of it messages.
LINK_WRAP void __wrap_mbedtls_strerror(int ret, char* buf, size_t buflen) {
  if (buf && buflen) snprintf(buf, buflen, "-0x%04X", (unsigned)(ret < 0 ? -ret : ret));
}

// No handler exists to unwind to, so a throw only has to stop the device. It also gets the panic
// backtrace straight from the throw site.
LINK_WRAP void __wrap___cxa_throw(void* object, void* type, void (*destructor)(void*)) {
  (void)object;
  (void)type;
  (void)destructor;
  abort();
}

// The Arduino libraries are built with CONFIG_HEAP_POISONING_LIGHT, which adds a 12-byte head and
// tail to every block on the heap and checks them on free. multi_heap_poisoning.c is only a layer
// over these _impl functions, and with poisoning off in sdkconfig the plain names are aliases of
// them. Going straight to them is that configuration, without rebuilding the libraries. A
// corrupted block is not reported at free(); take this list out of platformio.ini to hunt for one.
// These stay in IRAM like the originals, because allocations can run with the flash cache off.
void* multi_heap_malloc_impl(multi_heap_handle_t heap, size_t size);
void multi_heap_free_impl(multi_heap_handle_t heap, void* p);
void* multi_heap_realloc_impl(multi_heap_handle_t heap, void* p, size_t size);
void* multi_heap_aligned_alloc_impl(multi_heap_handle_t heap, size_t size, size_t alignment);
size_t multi_heap_get_allocated_size_impl(multi_heap_handle_t heap, void* p);
void multi_heap_get_info_impl(multi_heap_handle_t heap, multi_heap_info_t* info);
size_t multi_heap_free_size_impl(multi_heap_handle_t heap);
size_t multi_heap_minimum_free_size_impl(multi_heap_handle_t heap);

LINK_WRAP IRAM_ATTR void* __wrap_multi_heap_malloc(multi_heap_handle_t heap, size_t size) {
  return multi_heap_malloc_impl(heap, size);
}

LINK_WRAP IRAM_ATTR void __wrap_multi_heap_free(multi_heap_handle_t heap, void* p) {
  multi_heap_free_impl(heap, p);
}

LINK_WRAP IRAM_ATTR void* __wrap_multi_heap_realloc(multi_heap_handle_t heap, void* p, size_t size) {
  return multi_heap_realloc_impl(heap, p, size);
}

LINK_WRAP IRAM_ATTR void* __wrap_multi_heap_aligned_alloc(multi_heap_handle_t heap, size_t size,
                                                          size_t alignment) {
  return multi_heap_aligned_alloc_impl(heap, size, alignment);
}

LINK_WRAP IRAM_ATTR size_t __wrap_multi_heap_get_allocated_size(multi_heap_handle_t heap, void* p) {
  return multi_heap_get_allocated_size_impl(heap, p);
}

LINK_WRAP IRAM_ATTR void __wrap_multi_heap_get_info(multi_heap_handle_t heap, multi_heap_info_t* info) {
  multi_heap_get_info_impl(heap, info);
}

LINK_WRAP IRAM_ATTR size_t __wrap_multi_heap_free_size(multi_heap_handle_t heap) {
  return multi_heap_free_size_impl(heap);
}

LINK_WRAP IRAM_ATTR size_t __wrap_multi_heap_minimum_free_size(multi_heap_handle_t heap) {
  return multi_heap_minimum_free_size_impl(heap);
}

#if !defined(AWTRIX_SOC_ESP32S3)
// Classic ESP32 only, reasons in platformio.ini. The startup code calls the first, the panic handler
// the second, and carries on to the reboot when it returns. What keeps the panic output and the
// reset reason is the handler, not this.
LINK_WRAP void __wrap_esp_core_dump_init(void) {}

LINK_WRAP void __wrap_esp_core_dump_to_flash(void* info) { (void)info; }
#endif

// heap_caps_check_integrity walks every used block through this; there is no canary left to read.
LINK_WRAP IRAM_ATTR bool __wrap_multi_heap_internal_check_block_poisoning(void* start, size_t size,
                                                                          bool is_free, bool print_errors) {
  (void)start;
  (void)size;
  (void)is_free;
  (void)print_errors;
  return true;
}
