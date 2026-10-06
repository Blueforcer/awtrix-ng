// SPDX-License-Identifier: GPL-2.0
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/math64.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/uaccess.h>
#include <linux/wait.h>

#include "awtrix_pcm.h"
#include "mhal_audio_abi.h"

#define PCM_CLOSED 0
#define PCM_GUARD_BYTES 256u
#define PCM_DRAIN_MARGIN_MS 250u
#define PCM_LEVEL_SETTLE_TRIES 10

static ushort ao_dev;
module_param(ao_dev, ushort, 0444);
MODULE_PARM_DESC(ao_dev, "MHAL audio output device (0: line out, the TC002 speaker)");

static uint channels = 1;
module_param(channels, uint, 0444);
MODULE_PARM_DESC(channels, "DMA channels: 1 mono, 2 mono duplicated into stereo frames");

static uint buffer_bytes = 16384;
module_param(buffer_bytes, uint, 0444);
MODULE_PARM_DESC(buffer_bytes, "DMA ring size in DMA bytes, multiple of 16");

static uint period_bytes = 2048;
module_param(period_bytes, uint, 0444);
MODULE_PARM_DESC(period_bytes, "Transfer unit in DMA bytes, multiple of 16");

static uint start_bytes = 4096;
module_param(start_bytes, uint, 0444);
MODULE_PARM_DESC(start_bytes, "Queued DMA bytes that start playback, multiple of 16");

static int initial_gain_db = -28;
module_param(initial_gain_db, int, 0444);
MODULE_PARM_DESC(initial_gain_db, "Output gain in dB after open");

static int max_gain_db = -3;
module_param(max_gain_db, int, 0444);
MODULE_PARM_DESC(max_gain_db, "Highest output gain in dB a client may set, at most 0");

struct pcm_device {
	struct mutex lock;
	wait_queue_head_t wait;
	struct timer_list wake_timer;
	unsigned long in_use;
	unsigned long wait_jiffies;
	bool ready;
	bool hal_open;
	bool level_logged;
	u32 state;
	u8 *dma_area;
	dma_addr_t dma_handle;
	u32 dma_miu_addr;
	u8 *staging;
	u8 *frames;
	u32 staged;
	u32 prepared_level;
	u32 silence_tail;
	bool stall_armed;
	u32 stall_level;
	unsigned long stall_since;
	s32 gain_db;
	bool muted;
	u64 written;
	u64 stream_base;
	ktime_t stream_start;
	u32 underruns;
	s32 hal_error;
	u32 measured_rate;
};

static struct pcm_device pcm;

static u32 input_align(void)
{
	return MHAL_AUDIO_MIU_WORD_BYTES / channels;
}

static u32 staging_capacity(void)
{
	return period_bytes / channels;
}

static u32 dma_bytes_per_second(void)
{
	return AWTRIX_PCM_RATE * 2 * channels;
}

static u32 buffer_ms(void)
{
	return buffer_bytes * 1000u / dma_bytes_per_second();
}

static s32 clamp_gain(s32 db)
{
	return clamp_t(s32, db, MHAL_AUDIO_DPGA_MIN_DB, max_gain_db);
}

static s32 audible_gain(const struct pcm_device *p)
{
	return p->muted ? MHAL_AUDIO_DPGA_MIN_DB : p->gain_db;
}

static int hal_fail(struct pcm_device *p, const char *call, s32 ret)
{
	p->hal_error = ret;
	p->state = AWTRIX_PCM_FAILED;
	pr_err("awtrix_pcm: %s failed (%d), output disabled until close\n", call, ret);
	return -EIO;
}

static int hal_level(struct pcm_device *p, u32 *level)
{
	s32 ret;

	*level = 0;
	ret = MHAL_AUDIO_GetPcmOutCurrDataLen(ao_dev, level);
	if (ret)
		return hal_fail(p, "GetPcmOutCurrDataLen", ret);
	return 0;
}

static int hal_set_gain(struct pcm_device *p, s32 db)
{
	s32 ret;
	s8 channel;

	for (channel = 0; channel < 2; channel++) {
		ret = MHAL_AUDIO_SetGainOut(ao_dev, (s16)db, channel,
					    MHAL_AUDIO_GAIN_FADING_64_SAMPLES);
		if (ret) {
			p->hal_error = ret;
			pr_warn("awtrix_pcm: SetGainOut(%d dB, channel %d) failed (%d)\n",
				db, channel, ret);
			return -EIO;
		}
	}
	return 0;
}

static void hal_quiet(struct pcm_device *p)
{
	hal_set_gain(p, MHAL_AUDIO_DPGA_MIN_DB);
	usleep_range(2000, 3000);
}

static u32 free_dma_bytes(u32 level)
{
	u32 used = level + PCM_GUARD_BYTES;

	if (used >= buffer_bytes)
		return 0;
	return rounddown(buffer_bytes - used, MHAL_AUDIO_MIU_WORD_BYTES);
}

static void stream_measure(struct pcm_device *p, u32 level)
{
	s64 elapsed_us;
	u64 consumed;

	if (p->state != AWTRIX_PCM_RUNNING || level <= MHAL_AUDIO_MIU_WORD_BYTES * 4)
		return;
	elapsed_us = ktime_us_delta(ktime_get(), p->stream_start);
	consumed = p->written - p->stream_base;
	if (elapsed_us < 50000 || consumed <= level)
		return;
	p->measured_rate = (u32)div64_u64((consumed - level) * USEC_PER_SEC, (u64)elapsed_us);
}

static unsigned long stall_timeout(void)
{
	return msecs_to_jiffies(buffer_ms() + PCM_DRAIN_MARGIN_MS);
}

static int stream_start(struct pcm_device *p)
{
	s32 ret;

	ret = hal_set_gain(p, audible_gain(p));
	if (ret) {
		p->state = AWTRIX_PCM_FAILED;
		return ret;
	}
	ret = MHAL_AUDIO_StartPcmOut(ao_dev);
	if (ret)
		return hal_fail(p, "StartPcmOut", ret);
	p->stream_base = p->written - p->prepared_level;
	p->stream_start = ktime_get();
	p->state = AWTRIX_PCM_RUNNING;
	p->stall_armed = false;
	return 0;
}

static int stream_prepare(struct pcm_device *p)
{
	u8 ret;

	if (p->state == AWTRIX_PCM_RUNNING)
		hal_quiet(p);
	ret = MHAL_AUDIO_PrepareToRestartPcmOut(ao_dev);
	if (ret)
		return hal_fail(p, "PrepareToRestartPcmOut", (s8)ret);
	p->state = AWTRIX_PCM_PREPARED;
	p->prepared_level = 0;
	return 0;
}

static int check_prepared_level(struct pcm_device *p)
{
	u32 level = 0;
	int tries;

	for (tries = 0; tries < PCM_LEVEL_SETTLE_TRIES; tries++) {
		if (hal_level(p, &level))
			return -EIO;
		if (level == p->prepared_level)
			break;
		udelay(20);
	}
	if (!p->level_logged || level != p->prepared_level) {
		pr_info("awtrix_pcm: DMA level %u after %u prepared bytes\n",
			level, p->prepared_level);
		p->level_logged = true;
	}
	if (level != p->prepared_level) {
		p->state = AWTRIX_PCM_FAILED;
		pr_err("awtrix_pcm: DMA level does not match the written bytes, not starting\n");
		return -EIO;
	}
	return 0;
}

static int hal_write(struct pcm_device *p, u8 *input, u32 input_bytes)
{
	u32 bytes = input_bytes * channels;
	u8 *frames = input;
	int attempt;
	s32 ret;

	if (p->muted) {
		memset(p->frames, 0, bytes);
		frames = p->frames;
	} else if (channels == 2) {
		const u16 *mono = (const u16 *)input;
		u16 *stereo = (u16 *)p->frames;
		u32 i;

		for (i = 0; i < input_bytes / 2; i++)
			stereo[2 * i] = stereo[2 * i + 1] = mono[i];
		frames = p->frames;
	}

	for (attempt = 0;; attempt++) {
		ret = MHAL_AUDIO_WriteDataOut(ao_dev, frames, bytes, 0);
		if (ret == (s32)bytes)
			break;
		if (ret >= 0 || ret == MHAL_AUDIO_WRITE_FULL ||
		    p->state != AWTRIX_PCM_RUNNING || attempt)
			return hal_fail(p, "WriteDataOut", ret);
		p->underruns++;
		if (stream_prepare(p))
			return -EIO;
	}

	p->written += bytes;
	if (p->state != AWTRIX_PCM_PREPARED)
		return 0;
	p->prepared_level += bytes;
	if (check_prepared_level(p))
		return -EIO;
	if (p->prepared_level >= start_bytes)
		return stream_start(p);
	return 0;
}

static int detect_stall(struct pcm_device *p, u32 level)
{
	if (p->state != AWTRIX_PCM_RUNNING)
		return 0;
	if (!p->stall_armed || level != p->stall_level) {
		p->stall_armed = true;
		p->stall_level = level;
		p->stall_since = jiffies;
		return 0;
	}
	if (!time_after(jiffies, p->stall_since + stall_timeout()))
		return 0;
	pr_warn("awtrix_pcm: DMA level stuck at %u bytes, restarting the stream\n", level);
	p->stall_armed = false;
	p->underruns++;
	return stream_prepare(p);
}

static ssize_t push_input(struct pcm_device *p, const char __user *src, size_t len)
{
	u32 level, room, take, total, emit;
	int ret;

	if (p->state == AWTRIX_PCM_FAILED)
		return -EIO;
	ret = hal_level(p, &level);
	if (ret)
		return ret;
	stream_measure(p, level);
	room = min(free_dma_bytes(level) / channels, staging_capacity());
	if (room < input_align())
		return detect_stall(p, level);
	p->stall_armed = false;

	take = min_t(size_t, len, room - p->staged);
	if (copy_from_user(p->staging + p->staged, src, take))
		return -EFAULT;
	total = p->staged + take;
	emit = rounddown(total, input_align());
	p->staged = total - emit;
	if (emit) {
		ret = hal_write(p, p->staging, emit);
		if (ret)
			return ret;
		if (p->staged)
			memmove(p->staging, p->staging + emit, p->staged);
	}
	return take;
}

static int flush_with_silence(struct pcm_device *p)
{
	u32 level, room;
	int ret;

	ret = hal_level(p, &level);
	if (ret)
		return ret;
	room = min(free_dma_bytes(level) / channels, staging_capacity());
	if (room < input_align()) {
		p->staged = 0;
		return 0;
	}
	memset(p->staging + p->staged, 0, room - p->staged);
	p->silence_tail = (room - p->staged) * channels;
	p->staged = 0;
	return hal_write(p, p->staging, room);
}

static void log_stream(const struct pcm_device *p, const char *event)
{
	pr_info("awtrix_pcm: %s: %llu bytes written, %u underruns, measured %u B/s (nominal %u), hal error %d\n",
		event, p->written, p->underruns, p->measured_rate, dma_bytes_per_second(),
		p->hal_error);
}

static int pcm_drain(struct pcm_device *p, bool interruptible)
{
	unsigned long deadline;
	u32 level, drained_level;
	int ret = 0;

	mutex_lock(&p->lock);
	p->silence_tail = 0;
	if (p->state == AWTRIX_PCM_FAILED)
		ret = -EIO;
	else if (p->state == AWTRIX_PCM_RUNNING || p->prepared_level || p->staged)
		ret = flush_with_silence(p);
	if (!ret && p->state == AWTRIX_PCM_PREPARED && p->prepared_level)
		ret = stream_start(p);
	drained_level = min(p->silence_tail, period_bytes / 2);
	mutex_unlock(&p->lock);
	if (ret)
		return ret;

	deadline = jiffies + stall_timeout();
	for (;;) {
		mutex_lock(&p->lock);
		if (p->state != AWTRIX_PCM_RUNNING) {
			ret = p->state == AWTRIX_PCM_FAILED ? -EIO : 0;
			mutex_unlock(&p->lock);
			return ret;
		}
		ret = hal_level(p, &level);
		if (!ret) {
			stream_measure(p, level);
			if (level <= drained_level || time_after(jiffies, deadline)) {
				if (level > drained_level)
					pr_warn("awtrix_pcm: drain timed out with %u bytes queued\n", level);
				ret = stream_prepare(p);
				mutex_unlock(&p->lock);
				return ret;
			}
		}
		mutex_unlock(&p->lock);
		if (ret)
			return ret;
		if (!interruptible) {
			msleep(5);
			continue;
		}
		msleep_interruptible(5);
		if (signal_pending(current))
			return -ERESTARTSYS;
	}
}

static int pcm_stop(struct pcm_device *p)
{
	int ret;

	mutex_lock(&p->lock);
	p->staged = 0;
	ret = p->state == AWTRIX_PCM_FAILED ? -EIO : stream_prepare(p);
	mutex_unlock(&p->lock);
	return ret;
}

static int open_hal(struct pcm_device *p)
{
	struct mhal_audio_pcm_cfg cfg;
	u32 level = 0;
	s32 ret;

	if (!p->ready)
		return -ENODEV;
	p->staged = 0;
	p->prepared_level = 0;
	p->written = 0;
	p->stream_base = 0;
	p->underruns = 0;
	p->hal_error = 0;
	p->measured_rate = 0;
	p->level_logged = false;
	p->stall_armed = false;
	p->muted = false;
	p->gain_db = clamp_gain(initial_gain_db);

	ret = MHAL_AUDIO_Init(NULL);
	if (ret) {
		p->hal_error = ret;
		pr_err("awtrix_pcm: Init failed (%d)\n", ret);
		return -EIO;
	}
	if (hal_set_gain(p, MHAL_AUDIO_DPGA_MIN_DB))
		return -ENODEV;

	memset(&cfg, 0, sizeof(cfg));
	cfg.rate = MHAL_AUDIO_RATE_44K;
	cfg.bit_width = MHAL_AUDIO_BITWIDTH_16;
	cfg.channels = channels;
	cfg.interleaved = 1;
	cfg.dma_area = p->dma_area;
	cfg.dma_miu_addr = p->dma_miu_addr;
	cfg.buffer_bytes = buffer_bytes;
	cfg.period_bytes = period_bytes;
	cfg.start_threshold = start_bytes;
	ret = MHAL_AUDIO_ConfigPcmOut(ao_dev, &cfg);
	if (ret) {
		p->hal_error = ret;
		pr_err("awtrix_pcm: ConfigPcmOut refused (%d): output %u busy or configuration rejected\n",
		       ret, ao_dev);
		return -EBUSY;
	}
	ret = MHAL_AUDIO_OpenPcmOut(ao_dev);
	if (ret) {
		p->hal_error = ret;
		pr_err("awtrix_pcm: OpenPcmOut failed (%d)\n", ret);
		MHAL_AUDIO_ClosePcmOut(ao_dev);
		return -EIO;
	}
	p->hal_open = true;
	p->state = AWTRIX_PCM_PREPARED;
	if (hal_level(p, &level) || level) {
		pr_err("awtrix_pcm: DMA level %u after open, expected 0\n", level);
		p->state = AWTRIX_PCM_FAILED;
	}
	return 0;
}

static void close_hal(struct pcm_device *p)
{
	s32 stop_ret, close_ret;

	if (!p->hal_open) {
		p->state = PCM_CLOSED;
		return;
	}
	hal_quiet(p);
	stop_ret = MHAL_AUDIO_StopPcmOut(ao_dev);
	close_ret = MHAL_AUDIO_ClosePcmOut(ao_dev);
	p->hal_open = false;
	p->state = PCM_CLOSED;
	log_stream(p, "closed");
	if (stop_ret || close_ret)
		pr_warn("awtrix_pcm: StopPcmOut %d, ClosePcmOut %d\n", stop_ret, close_ret);
}

static int pcm_open(struct inode *inode, struct file *file)
{
	struct pcm_device *p = &pcm;
	int err;

	if (test_and_set_bit(0, &p->in_use))
		return -EBUSY;
	mutex_lock(&p->lock);
	err = open_hal(p);
	if (!err && p->state == AWTRIX_PCM_FAILED) {
		close_hal(p);
		err = -EIO;
	}
	mutex_unlock(&p->lock);
	if (err) {
		clear_bit(0, &p->in_use);
		return err;
	}
	return nonseekable_open(inode, file);
}

static int pcm_release(struct inode *inode, struct file *file)
{
	struct pcm_device *p = &pcm;

	if (!fatal_signal_pending(current))
		pcm_drain(p, false);
	del_timer_sync(&p->wake_timer);
	mutex_lock(&p->lock);
	close_hal(p);
	mutex_unlock(&p->lock);
	clear_bit(0, &p->in_use);
	return 0;
}

static ssize_t pcm_write(struct file *file, const char __user *buf, size_t count,
			 loff_t *ppos)
{
	struct pcm_device *p = &pcm;
	size_t done = 0;
	ssize_t n = 0;

	if (count & 1)
		return -EINVAL;
	while (done < count) {
		mutex_lock(&p->lock);
		n = push_input(p, buf + done, count - done);
		mutex_unlock(&p->lock);
		if (n < 0)
			break;
		done += n;
		if (n)
			continue;
		if (file->f_flags & O_NONBLOCK) {
			n = -EAGAIN;
			break;
		}
		schedule_timeout_interruptible(p->wait_jiffies);
		if (signal_pending(current)) {
			n = -ERESTARTSYS;
			break;
		}
	}
	return done ? (ssize_t)done : n;
}

static unsigned int pcm_poll(struct file *file, poll_table *wait)
{
	struct pcm_device *p = &pcm;
	unsigned int mask = 0;
	u32 level;

	poll_wait(file, &p->wait, wait);
	mutex_lock(&p->lock);
	if (p->state == AWTRIX_PCM_FAILED || hal_level(p, &level))
		mask = POLLERR;
	else if (free_dma_bytes(level) >= period_bytes)
		mask = POLLOUT | POLLWRNORM;
	else
		mod_timer(&p->wake_timer, jiffies + p->wait_jiffies);
	mutex_unlock(&p->lock);
	return mask;
}

static int pcm_get_status(struct pcm_device *p, struct awtrix_pcm_status __user *out)
{
	struct awtrix_pcm_status status;
	u32 level = 0;

	memset(&status, 0, sizeof(status));
	mutex_lock(&p->lock);
	if (p->state != AWTRIX_PCM_FAILED)
		hal_level(p, &level);
	status.state = p->state;
	status.buffer_bytes = buffer_bytes;
	status.period_bytes = period_bytes;
	status.queued_bytes = level + p->staged * channels;
	status.written_bytes = p->written;
	status.underruns = p->underruns;
	status.gain_db = p->gain_db;
	status.muted = p->muted;
	status.hal_error = p->hal_error;
	status.measured_bytes_per_second = p->measured_rate;
	mutex_unlock(&p->lock);
	return copy_to_user(out, &status, sizeof(status)) ? -EFAULT : 0;
}

static long pcm_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct pcm_device *p = &pcm;
	s32 gain;
	u32 muted;
	int ret = 0;

	switch (cmd) {
	case AWTRIX_PCM_SET_GAIN:
		if (get_user(gain, (s32 __user *)arg))
			return -EFAULT;
		mutex_lock(&p->lock);
		p->gain_db = clamp_gain(gain);
		if (p->state == AWTRIX_PCM_RUNNING)
			ret = hal_set_gain(p, audible_gain(p));
		if (ret)
			p->state = AWTRIX_PCM_FAILED;
		mutex_unlock(&p->lock);
		return ret;
	case AWTRIX_PCM_SET_MUTE:
		if (get_user(muted, (u32 __user *)arg))
			return -EFAULT;
		mutex_lock(&p->lock);
		p->muted = muted != 0;
		if (p->state == AWTRIX_PCM_RUNNING)
			ret = hal_set_gain(p, audible_gain(p));
		if (ret)
			p->state = AWTRIX_PCM_FAILED;
		mutex_unlock(&p->lock);
		return ret;
	case AWTRIX_PCM_DRAIN:
		return pcm_drain(p, true);
	case AWTRIX_PCM_STOP:
		return pcm_stop(p);
	case AWTRIX_PCM_GET_STATUS:
		return pcm_get_status(p, (struct awtrix_pcm_status __user *)arg);
	default:
		return -ENOTTY;
	}
}

static void pcm_wake(unsigned long data)
{
	wake_up_interruptible(&pcm.wait);
}

static const struct file_operations pcm_fops = {
	.owner = THIS_MODULE,
	.open = pcm_open,
	.release = pcm_release,
	.write = pcm_write,
	.poll = pcm_poll,
	.unlocked_ioctl = pcm_ioctl,
	.llseek = no_llseek,
};

static struct miscdevice pcm_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "awtrix_pcm",
	.fops = &pcm_fops,
	.mode = 0600,
};

static int check_params(void)
{
	if (channels != 1 && channels != 2)
		return -EINVAL;
	if (buffer_bytes % MHAL_AUDIO_MIU_WORD_BYTES || buffer_bytes < 4096 ||
	    buffer_bytes > 65536)
		return -EINVAL;
	if (period_bytes % MHAL_AUDIO_MIU_WORD_BYTES || period_bytes < 256 ||
	    period_bytes * 2 + PCM_GUARD_BYTES > buffer_bytes)
		return -EINVAL;
	if (start_bytes % MHAL_AUDIO_MIU_WORD_BYTES || start_bytes < period_bytes ||
	    start_bytes + PCM_GUARD_BYTES > buffer_bytes)
		return -EINVAL;
	if (max_gain_db < MHAL_AUDIO_DPGA_MIN_DB || max_gain_db > 0)
		return -EINVAL;
	return 0;
}

static int __init pcm_init(void)
{
	struct device *dev;
	u64 miu;
	int err;

	BUILD_BUG_ON(!MHAL_AUDIO_PCM_CFG_LAYOUT_OK);
	err = check_params();
	if (err) {
		pr_err("awtrix_pcm: invalid module parameters\n");
		return err;
	}

	mutex_init(&pcm.lock);
	init_waitqueue_head(&pcm.wait);
	setup_timer(&pcm.wake_timer, pcm_wake, 0);
	pcm.wait_jiffies = max(1UL, msecs_to_jiffies(period_bytes * 500u / dma_bytes_per_second()));
	pcm.staging = kmalloc(staging_capacity(), GFP_KERNEL);
	pcm.frames = kmalloc(period_bytes, GFP_KERNEL);
	if (!pcm.staging || !pcm.frames) {
		err = -ENOMEM;
		goto free_buffers;
	}

	err = misc_register(&pcm_misc);
	if (err)
		goto free_buffers;
	dev = pcm_misc.this_device;
	dev->coherent_dma_mask = DMA_BIT_MASK(32);
	dev->dma_mask = &dev->coherent_dma_mask;
	pcm.dma_area = dma_alloc_coherent(dev, buffer_bytes, &pcm.dma_handle, GFP_KERNEL);
	if (!pcm.dma_area) {
		err = -ENOMEM;
		goto deregister;
	}
	miu = (u64)pcm.dma_handle - MHAL_AUDIO_MIU0_BUS_BASE;
	if (pcm.dma_handle < MHAL_AUDIO_MIU0_BUS_BASE || miu >= 0x80000000ull ||
	    miu % MHAL_AUDIO_MIU_WORD_BYTES) {
		pr_err("awtrix_pcm: DMA buffer at %pad is outside MIU0\n", &pcm.dma_handle);
		err = -ENOMEM;
		goto free_dma;
	}
	pcm.dma_miu_addr = (u32)miu;

	mutex_lock(&pcm.lock);
	pcm.ready = true;
	mutex_unlock(&pcm.lock);
	pr_info("awtrix_pcm: ready: ao_dev %u, %u channel(s), buffer %u, period %u, start %u bytes, dma %pad (MIU 0x%08x)\n",
		ao_dev, channels, buffer_bytes, period_bytes, start_bytes, &pcm.dma_handle,
		pcm.dma_miu_addr);
	return 0;

free_dma:
	dma_free_coherent(dev, buffer_bytes, pcm.dma_area, pcm.dma_handle);
deregister:
	misc_deregister(&pcm_misc);
free_buffers:
	kfree(pcm.frames);
	kfree(pcm.staging);
	return err;
}

static void __exit pcm_exit(void)
{
	mutex_lock(&pcm.lock);
	pcm.ready = false;
	mutex_unlock(&pcm.lock);
	dma_free_coherent(pcm_misc.this_device, buffer_bytes, pcm.dma_area, pcm.dma_handle);
	misc_deregister(&pcm_misc);
	kfree(pcm.frames);
	kfree(pcm.staging);
	pr_info("awtrix_pcm: unloaded\n");
}

module_init(pcm_init);
module_exit(pcm_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("TC002 speaker PCM output on top of the SigmaStar MHAL audio exports");
