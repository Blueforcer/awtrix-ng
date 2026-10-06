// SPDX-License-Identifier: GPL-2.0
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include "../awtrix_pcm.h"

static int64_t now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int print_status(int fd, const char *label)
{
	struct awtrix_pcm_status s;

	if (ioctl(fd, AWTRIX_PCM_GET_STATUS, &s)) {
		fprintf(stderr, "GET_STATUS: %s\n", strerror(errno));
		return -1;
	}
	printf("%s: state=%u written=%llu queued=%u underruns=%u gain=%d muted=%u hal_error=%d "
	       "measured=%u B/s buffer=%u period=%u\n",
	       label, s.state, (unsigned long long)s.written_bytes, s.queued_bytes, s.underruns,
	       s.gain_db, s.muted, s.hal_error, s.measured_bytes_per_second, s.buffer_bytes,
	       s.period_bytes);
	return 0;
}

static int write_all(int fd, const uint8_t *data, size_t length, int64_t stop_at)
{
	size_t done = 0;

	while (done < length) {
		struct pollfd pfd = {.fd = fd, .events = POLLOUT};
		ssize_t n;
		int ready;

		if (stop_at && now_ms() >= stop_at)
			return 1;
		n = write(fd, data + done, length - done);
		if (n > 0) {
			done += (size_t)n;
			continue;
		}
		if (n < 0 && errno != EAGAIN) {
			fprintf(stderr, "write: %s\n", strerror(errno));
			return -1;
		}
		ready = poll(&pfd, 1, 1000);
		if (ready < 0 && errno != EINTR) {
			fprintf(stderr, "poll: %s\n", strerror(errno));
			return -1;
		}
		if (ready == 0) {
			fprintf(stderr, "poll: no space for 1 s\n");
			return -1;
		}
		if (ready > 0 && (pfd.revents & (POLLERR | POLLHUP))) {
			fprintf(stderr, "poll: device reported an error\n");
			return -1;
		}
	}
	return 0;
}

static uint8_t *read_file(const char *path, size_t *length)
{
	FILE *file = fopen(path, "rb");
	uint8_t *data;
	long size;

	if (!file)
		return NULL;
	if (fseek(file, 0, SEEK_END) || (size = ftell(file)) < 0 || fseek(file, 0, SEEK_SET)) {
		fclose(file);
		return NULL;
	}
	data = malloc(size ? (size_t)size : 1);
	if (data && fread(data, 1, (size_t)size, file) != (size_t)size) {
		free(data);
		data = NULL;
	}
	fclose(file);
	*length = (size_t)size & ~(size_t)1;
	return data;
}

int main(int argc, char **argv)
{
	const char *device = getenv("AWTRIX_PCM_DEVICE");
	int32_t gain = -28;
	int64_t started, stop_at = 0;
	size_t length = 0;
	uint8_t *pcm;
	int fd, result;

	if (argc < 3 || strcmp(argv[1], "play")) {
		fprintf(stderr, "usage: %s play FILE [GAIN_DB [STOP_AFTER_MS]]\n", argv[0]);
		return 2;
	}
	if (argc > 3)
		gain = (int32_t)strtol(argv[3], NULL, 10);
	pcm = read_file(argv[2], &length);
	if (!pcm) {
		fprintf(stderr, "%s: cannot read\n", argv[2]);
		return 1;
	}
	if (!device || !*device)
		device = AWTRIX_PCM_DEVICE;
	fd = open(device, O_WRONLY | O_NONBLOCK | O_CLOEXEC);
	if (fd < 0) {
		fprintf(stderr, "open %s: %s\n", device, strerror(errno));
		free(pcm);
		return 1;
	}
	if (ioctl(fd, AWTRIX_PCM_SET_GAIN, &gain)) {
		fprintf(stderr, "SET_GAIN: %s\n", strerror(errno));
		free(pcm);
		close(fd);
		return 1;
	}
	started = now_ms();
	if (argc > 4)
		stop_at = started + strtol(argv[4], NULL, 10);
	result = write_all(fd, pcm, length, stop_at);
	print_status(fd, "written");
	if (result == 1) {
		if (ioctl(fd, AWTRIX_PCM_STOP))
			fprintf(stderr, "STOP: %s\n", strerror(errno));
		else
			printf("stopped after %lld ms\n", (long long)(now_ms() - started));
	} else if (result == 0) {
		if (ioctl(fd, AWTRIX_PCM_DRAIN))
			fprintf(stderr, "DRAIN: %s\n", strerror(errno));
		else
			printf("drained after %lld ms\n", (long long)(now_ms() - started));
	}
	print_status(fd, "final");
	free(pcm);
	if (close(fd)) {
		fprintf(stderr, "close: %s\n", strerror(errno));
		return 1;
	}
	return result < 0 ? 1 : 0;
}
