/* 
 * Copyright (c) 2015-2021, Extrems' Corner.org
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <string.h>
#include <gccore.h>
#include <ogc/machine/processor.h>
#include "gba.h"
#include "gba_mb.h"
#include "input.h"

static lwpq_t queue = LWP_TQUEUE_NULL;
static lwp_t thread = LWP_THREAD_NULL;

static void transfer_cb(int32_t chan, uint32_t type)
{
	LWP_ThreadSignal(queue);
}

uint32_t GBAJoyResetCommand(int32_t chan)
{
	uint32_t level;
	uint8_t outbuf[1] = {0xFF};
	uint8_t inbuf[3] = {0x00};

	_CPU_ISR_Disable(level);

	if (SI_Transfer(chan, outbuf, 1, inbuf, 3, transfer_cb, 65))
		LWP_ThreadSleep(queue);

	_CPU_ISR_Restore(level);

	return inbuf[0] << 24 | inbuf[1] << 16 | inbuf[2] << 8;
}

uint32_t GBAJoyStatusCommand(int32_t chan)
{
	uint32_t level;
	uint8_t outbuf[1] = {0x00};
	uint8_t inbuf[3] = {0x00};

	_CPU_ISR_Disable(level);

	if (SI_Transfer(chan, outbuf, 1, inbuf, 3, transfer_cb, 65))
		LWP_ThreadSleep(queue);

	_CPU_ISR_Restore(level);

	return inbuf[0] << 24 | inbuf[1] << 16 | inbuf[2] << 8;
}

uint32_t GBAJoyReadCommand(int32_t chan)
{
	uint32_t level;
	uint8_t outbuf[1] = {0x14};
	uint8_t inbuf[5] = {0x00};

	_CPU_ISR_Disable(level);

	if (SI_Transfer(chan, outbuf, 1, inbuf, 5, transfer_cb, 65))
		LWP_ThreadSleep(queue);

	_CPU_ISR_Restore(level);

	return inbuf[3] << 24 | inbuf[2] << 16 | inbuf[1] << 8 | inbuf[0];
}

void GBAJoyWriteCommand(int32_t chan, uint32_t val)
{
	uint32_t level;
	uint8_t outbuf[5] = {0x15, val, val >> 8, val >> 16, val >> 24};
	uint8_t inbuf[1] = {0x00};

	_CPU_ISR_Disable(level);

	if (SI_Transfer(chan, outbuf, 5, inbuf, 1, transfer_cb, 65))
		LWP_ThreadSleep(queue);

	_CPU_ISR_Restore(level);
}

uint32_t GBAJoyChecksum(uint32_t crc, uint32_t val)
{
	crc ^= val;

	for (int bit = 0; bit < 32; bit++)
		crc = crc & 1 ? crc >> 1 ^ 0xA1C1 : crc >> 1;

	return crc;
}

uint32_t GBAJoyEncrypt(uint32_t addr, uint32_t val, uint32_t *key)
{
	*key = *key * bswap32('Kawa') + 1;
	val ^= *key;
	val ^= ~addr + 1;
	val ^= bswap32(' by ');
	return val;
}

uint32_t GBAJoyGetKey(uint32_t size)
{
	uint32_t key;
	key  = (size - 0x200) >> 3;
	key  = (key & 0x7F) | (key & 0x3F80) << 1 | (key & 0x4000) << 2 | 0x700000;
	key |= (key + (key >> 8) + (key >> 16)) << 24 | 0x80808080;

	if ((key & 0x200) == 0x200)
		return key ^ bswap32('sedo');
	else
		return key ^ bswap32('Kawa');
}

static void *thread_func(void *arg)
{
	do {
		uint32_t type, reset = 0;

		for (int chan = 0; chan < SI_MAX_CHAN; chan++) {
			switch (SI_Probe(chan)) {
				case SI_GC_CONTROLLER:
				case SI_GC_WAVEBIRD:
					if (gc_controller.status[chan].err == PAD_ERR_NO_CONTROLLER) {
						gc_controller.status[chan].err =  PAD_ERR_NOT_READY;
						reset |= SI_CHAN_BIT(chan);
					}
					break;
				case SI_GC_STEERING:
					if (gc_steering.status[chan].err == SI_STEERING_ERR_NO_CONTROLLER)
						gc_steering.status[chan].err =  SI_ResetSteering(chan);
					break;
				case SI_GBA:
					type = GBAJoyResetCommand(chan);
					type = GBAJoyStatusCommand(chan);

					if ((type & 0x3000) == 0x1000) {
						uint32_t off, size = gba_mb_size < 0x200 ? 0x200 : (gba_mb_size + 7) & ~7;
						uint32_t crc = 0x15A0, key = bswap32('sedo'), val;

						key ^= GBAJoyReadCommand(chan);
						GBAJoyWriteCommand(chan, GBAJoyGetKey(size));

						for (off = 0; off < 0xC0; off += 4) {
							val = __lwbrx(gba_mb, off);
							GBAJoyWriteCommand(chan, val);
						}

						for (off = 0xC0; off < size; off += 4) {
							val = __lwbrx(gba_mb, off);
							crc = GBAJoyChecksum(crc, val);
							val = GBAJoyEncrypt(0x02000000 + off, val, &key);
							GBAJoyWriteCommand(chan, val);
						}

						crc |= size << 16;
						crc  = GBAJoyEncrypt(0x02000000 + off, crc, &key);
						GBAJoyWriteCommand(chan, crc);
						GBAJoyReadCommand(chan);
					}
				default:
					gc_controller.status[chan].err = PAD_ERR_NO_CONTROLLER;
					gc_steering.status[chan].err = SI_STEERING_ERR_NO_CONTROLLER;
					break;
			}
		}

		if (reset) PAD_Reset(reset);
		VIDEO_WaitVSync();
	} while (!state.quit);

	return NULL;
}

void GBAJoyInit(void)
{
	LWP_InitQueue(&queue);
	LWP_CreateThread(&thread, thread_func, NULL, NULL, 0, LWP_PRIO_NORMAL);
}
