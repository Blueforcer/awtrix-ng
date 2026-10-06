#include "rescue_panel.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/spi/spidev.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "../contract/tc002_layout.h"
#include "../contract/terminal_glyphs.h"

static const unsigned char kAmber[3] = {180, 100, 0};

static void sleep_ms(long ms) {
  struct timespec left = {ms / 1000, (ms % 1000) * 1000000L};
  while (nanosleep(&left, &left) && errno == EINTR) {
  }
}

static int fail(char* why, size_t size, const char* what) {
  snprintf(why, size, "%s: %s", what, strerror(errno));
  return -1;
}

static void draw_line(unsigned char* frame, const char* text, int y) {
  const int count = (int)strlen(text);
  int x = (TC002_PANEL_WIDTH - (count * (TC002_GLYPH_WIDTH + 1) - 1)) / 2;
  for (const char* c = text; *c; ++c, x += TC002_GLYPH_WIDTH + 1) {
    const unsigned char* rows = tc002_glyph_rows(*c);
    if (!rows) continue;
    for (int row = 0; row < TC002_GLYPH_HEIGHT; ++row)
      for (int column = 0; column < TC002_GLYPH_WIDTH; ++column)
        if (rows[row] & (1u << (TC002_GLYPH_WIDTH - 1 - column)))
          memcpy(frame + (size_t)(y + row) * TC002_PANEL_ROW_BYTES + (size_t)(x + column) * 3, kAmber, 3);
  }
}

static int write_text(const char* path, const char* text) {
  const int fd = open(path, O_WRONLY | O_CLOEXEC);
  if (fd < 0) return -1;
  const ssize_t written = write(fd, text, strlen(text));
  const int error = errno;
  close(fd);
  errno = error;
  return written == (ssize_t)strlen(text) ? 0 : -1;
}

static int latch(const char* direction, int high) { return write_text(direction, high ? "high" : "low"); }

int rescue_panel_show(const char* spi_path, const char* gpio_dir, char* why, size_t why_size) {
  char path[256], direction[256];
  snprintf(path, sizeof path, "%s/gpio" TC002_PANEL_LATCH_GPIO, gpio_dir);
  snprintf(direction, sizeof direction, "%s/gpio" TC002_PANEL_LATCH_GPIO "/direction", gpio_dir);
  struct stat info;
  if (stat(path, &info)) {
    char export_path[256];
    snprintf(export_path, sizeof export_path, "%s/export", gpio_dir);
    if (write_text(export_path, TC002_PANEL_LATCH_GPIO) && errno != EBUSY)
      return fail(why, why_size, "cannot export the panel latch");
  }
  if (latch(direction, 1)) return fail(why, why_size, "cannot raise the panel latch");
  const int spi = open(spi_path, O_RDWR | O_CLOEXEC);
  if (spi < 0) return fail(why, why_size, "cannot open the panel SPI");
  uint8_t mode = SPI_MODE_0, bits = 8, lsb = 0;
  uint32_t speed = TC002_PANEL_SPI_HZ;
  if (ioctl(spi, SPI_IOC_WR_MODE, &mode) < 0 || ioctl(spi, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0 ||
      ioctl(spi, SPI_IOC_WR_LSB_FIRST, &lsb) < 0 || ioctl(spi, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0) {
    fail(why, why_size, "cannot configure the panel SPI");
    close(spi);
    return -1;
  }
  unsigned char frame[TC002_PANEL_FRAME_BYTES];
  memset(frame, 0, sizeof frame);
  draw_line(frame, "USB", 3);
  draw_line(frame, "RECOVERY", 9);
  /* The panel shows the frame before the last one; the second transfer brings this one on. */
  for (int transfer = 0; transfer < 2; ++transfer) {
    if (latch(direction, 0)) {
      fail(why, why_size, "cannot lower the panel latch");
      close(spi);
      return -1;
    }
    sleep_ms(TC002_PANEL_LATCH_SETTLE_MS);
    const ssize_t written = write(spi, frame, sizeof frame);
    const int error = errno;
    sleep_ms(TC002_PANEL_LATCH_SETTLE_MS);
    if (latch(direction, 1) || written != (ssize_t)sizeof frame) {
      errno = written < 0 ? error : EIO;
      fail(why, why_size, "cannot send the rescue frame");
      close(spi);
      return -1;
    }
    sleep_ms(TC002_PANEL_FRAME_GAP_MS);
  }
  close(spi);
  return 0;
}
