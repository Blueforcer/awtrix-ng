/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef AWTRIX_PCM_H
#define AWTRIX_PCM_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define AWTRIX_PCM_DEVICE "/dev/awtrix_pcm"
#define AWTRIX_PCM_RATE 44100
#define AWTRIX_PCM_MIN_GAIN_DB (-64)

enum awtrix_pcm_state {
	AWTRIX_PCM_PREPARED = 1,
	AWTRIX_PCM_RUNNING = 2,
	AWTRIX_PCM_FAILED = 3
};

struct awtrix_pcm_status {
	__u32 state;
	__u32 buffer_bytes;
	__u32 period_bytes;
	__u32 queued_bytes;
	__u64 written_bytes;
	__u32 underruns;
	__s32 gain_db;
	__u32 muted;
	__s32 hal_error;
	__u32 measured_bytes_per_second;
	__u32 reserved;
};

#define AWTRIX_PCM_IOC_MAGIC 'W'
#define AWTRIX_PCM_SET_GAIN _IOW(AWTRIX_PCM_IOC_MAGIC, 0x41, __s32)
#define AWTRIX_PCM_SET_MUTE _IOW(AWTRIX_PCM_IOC_MAGIC, 0x42, __u32)
#define AWTRIX_PCM_DRAIN _IO(AWTRIX_PCM_IOC_MAGIC, 0x43)
#define AWTRIX_PCM_STOP _IO(AWTRIX_PCM_IOC_MAGIC, 0x44)
#define AWTRIX_PCM_GET_STATUS _IOR(AWTRIX_PCM_IOC_MAGIC, 0x45, struct awtrix_pcm_status)

#endif
