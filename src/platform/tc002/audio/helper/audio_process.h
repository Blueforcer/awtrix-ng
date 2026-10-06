#ifndef AWTRIX_TC002_AUDIO_PROCESS_H
#define AWTRIX_TC002_AUDIO_PROCESS_H

/* Process frame of awtrix-tc002-audio-pcm: one inherited SOCK_SEQPACKET descriptor, an empty
 * environment and a watchdog around every device call. */

#include <signal.h>
#include <stddef.h>
#include <stdint.h>

#include "audio_helper.h"

enum {
  AH_EXIT_CLEAN = 0,
  AH_EXIT_FAILED = 1,
  AH_EXIT_USAGE = 2,
  AH_EXIT_PREFLIGHT = 3,
  AH_EXIT_DEVICE = 4,
  AH_EXIT_START = 5,
  AH_CALL_BUDGET_SECONDS = 2,
  AH_START_BUDGET_SECONDS = 5
};

int64_t ah_monotonic_ms(void);
/* SIGALRM exits with 124; SIGTERM and SIGINT stay blocked outside wait_mask and end ah_run. */
int ah_install_signals(sigset_t *wait_mask);
/* "--socket-fd N" naming an AF_UNIX SOCK_SEQPACKET socket, which becomes non-blocking and
 * close-on-exec; -1 otherwise. */
int ah_parse_socket(int argc, char **argv);
int ah_silence_stdio(void);
int ah_board_matches(void);
/* An ah_ops send over a non-blocking SOCK_SEQPACKET descriptor. */
int ah_send_datagram(int fd, const uint8_t *data, size_t size);
/* Sends the FAILED status of a helper that never started. */
void ah_report_failure(ah_state *s, int channel, uint8_t error, int32_t vendor_result);
/* Serves the socket until it closes or a stop signal arrives (AH_EXIT_CLEAN), or until the
 * helper or the socket fails (AH_EXIT_FAILED). Unless device is -1 it is watched for errors,
 * which device_failed turns into a FAILED state. */
int ah_run(ah_state *s, int channel, int device, void (*device_failed)(ah_state *),
           const sigset_t *wait_mask);

#endif
