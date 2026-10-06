/* SPDX-License-Identifier: GPL-2.0 */
#ifndef AWTRIX_PCM_MHAL_AUDIO_ABI_H
#define AWTRIX_PCM_MHAL_AUDIO_ABI_H

#include <linux/stddef.h>
#include <linux/types.h>

typedef u16 mhal_audio_dev;

#define MHAL_AUDIO_RATE_44K 44000
#define MHAL_AUDIO_BITWIDTH_16 0
#define MHAL_AUDIO_GAIN_FADING_64_SAMPLES 7
#define MHAL_AUDIO_WRITE_FULL (-2)
#define MHAL_AUDIO_MIU0_BUS_BASE 0x20000000u
#define MHAL_AUDIO_MIU_WORD_BYTES 16u
#define MHAL_AUDIO_DPGA_MIN_DB (-64)

struct mhal_audio_pcm_cfg {
	u32 rate;
	u32 bit_width;
	u16 channels;
	u8 interleaved;
	u8 *dma_area;
	u64 dma_miu_addr;
	u32 buffer_bytes;
	u32 period_bytes;
	u32 start_threshold;
};

#define MHAL_AUDIO_PCM_CFG_LAYOUT_OK                                        \
	(sizeof(struct mhal_audio_pcm_cfg) == 40 &&                         \
	 offsetof(struct mhal_audio_pcm_cfg, channels) == 8 &&              \
	 offsetof(struct mhal_audio_pcm_cfg, interleaved) == 10 &&          \
	 offsetof(struct mhal_audio_pcm_cfg, dma_area) == 12 &&             \
	 offsetof(struct mhal_audio_pcm_cfg, dma_miu_addr) == 16 &&         \
	 offsetof(struct mhal_audio_pcm_cfg, buffer_bytes) == 24 &&         \
	 offsetof(struct mhal_audio_pcm_cfg, period_bytes) == 28 &&         \
	 offsetof(struct mhal_audio_pcm_cfg, start_threshold) == 32)

s32 MHAL_AUDIO_Init(void *platform_data);
s32 MHAL_AUDIO_ConfigPcmOut(mhal_audio_dev dev, struct mhal_audio_pcm_cfg *cfg);
s32 MHAL_AUDIO_OpenPcmOut(mhal_audio_dev dev);
s32 MHAL_AUDIO_ClosePcmOut(mhal_audio_dev dev);
s32 MHAL_AUDIO_StartPcmOut(mhal_audio_dev dev);
s32 MHAL_AUDIO_StopPcmOut(mhal_audio_dev dev);
s32 MHAL_AUDIO_WriteDataOut(mhal_audio_dev dev, void *pcm, u32 bytes, u8 block);
u8 MHAL_AUDIO_PrepareToRestartPcmOut(mhal_audio_dev dev);
s32 MHAL_AUDIO_SetGainOut(mhal_audio_dev dev, s16 gain_db, s8 channel, int fading);
s32 MHAL_AUDIO_GetPcmOutCurrDataLen(mhal_audio_dev dev, u32 *bytes);

#endif
