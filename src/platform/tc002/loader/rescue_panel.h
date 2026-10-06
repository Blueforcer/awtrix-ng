#pragma once

#include <stddef.h>

/* The rescue message on the panel, "USB" over "RECOVERY", without the runtime: the frame goes over
 * the panel's SPI device with the GPIO35 latch the way Tc002Board sends it. -1 with why set when
 * the panel cannot be reached; the rescue goes on without it. */
int rescue_panel_show(const char* spi_path, const char* gpio_dir, char* why, size_t why_size);
