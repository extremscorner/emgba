/* 
 * Copyright (c) 2015-2025, Extrems' Corner.org
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <stdio.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <math.h>
#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <ogc/machine/processor.h>
#include <asndlib.h>
#include <wiiuse/wpad.h>
#include <fat.h>
#include "3ds.h"
#include "clock.h"
#include "gba.h"
#include "gbp.h"
#include "gx.h"
#include "input.h"
#include "network.h"
#include "state.h"
#include "sysconf.h"
#include "video.h"
#include "wiiload.h"

#include <mgba/flags.h>

#include <mgba/core/core.h>
#include "feature/gui/gui-runner.h"
#include <mgba/internal/gba/gba.h>
#include <mgba/internal/gba/input.h>
#include <mgba-util/gui/font.h>
#include <mgba-util/gui/menu.h>
#include <mgba-util/vfs.h>

static void *displist[4];
static uint32_t dispsize[4];

static gx_surface_t convert_surface, packed_surface;
static gx_surface_t planar_surface, prescale_surface;

state_t default_state, state = {
	.draw_osd       = true,
	.draw_wait      = true,
	.aspect         = { 4., 3. },
	.zoom           = { 2., 2. },
	.zoom_auto      = true,
	.zoom_ratio     = .75,
	.scale          = 1,
	.poll           = 1,
	.cursor         = "point.tpl.gz",
	.overlay        = "frame.tpl.gz",
	.filter         = FILTER_NONE,
	.filter_weight  = { 141./255., 141./255., 141./255. },
	.dither         = DITHER_THRESHOLD,
	.scaler         = SCALER_AREA,
	.profile_intent = INTENT_PERCEPTUAL,
	.profile        = PROFILE_GBI,
	.matrix         = MATRIX_GBI,
	.input_trc      = TRC_GAMMA,
	.input_gamma    = { 2.2, 2.2, 2.2 },
	.output_gamma   = 2.2,
	.contrast       = { 1., 1., 1. },
};

#ifdef HW_DOL
timing_t aiclock = { .reset = true, .hz = 54000000. / 1124. };
#else
timing_t aiclock = { .reset = true, .hz = 48000. };
#endif
timing_t gxclock = { .reset = true, .hz = NAN };

static void asndlib_cb(void)
{
	ClockTick(&aiclock, 1024);
}

static sem_t semaphore[2] = { LWP_SEM_NULL, LWP_SEM_NULL };
static syswd_t watchdog;

static void alarm_cb(syswd_t alarm, void *arg)
{
	LWP_SemPost(semaphore[0]);
}

static void vsync_cb(uint32_t retrace)
{
	LWP_SemPost(semaphore[0]);
}

static void drawsync_cb(uint16_t token)
{
	VideoSetFramebuffer(token);
	//ClockTick(&gxclock, 1);
	LWP_SemPost(semaphore[1]);
}

static void _drawStart(void)
{
	if (state.draw_wait)
		LWP_SemWait(semaphore[0]);
	LWP_SemWait(semaphore[1]);

	state.retrace = VIDEO_GetRetraceCount() + 1;
	state.field   = VIDEO_GetCurrentField() ^ 1;

	state.current = STATE_INIT;

	dispsize[0] = 
	dispsize[1] = 
	dispsize[2] = 
	dispsize[3] = 0;

	GX_SetMisc(GX_MT_DL_SAVE_CTX, GX_DISABLE);

	GX_InvalidateTexAll();
}

static void _drawEnd(void)
{
	uint32_t xfb_index;
	uint16_t (*xfb)[rmode.fbWidth] = VideoGetFramebuffer(&xfb_index);

	GX_SetFieldMask(GX_TRUE, GX_TRUE);
	GX_SetFieldMode(rmode.field_rendering, rmode.xfbHeight * 2 == rmode.viHeight);

	GX_SetPixelFmt(GX_PF_RGB8_Z24, GX_ZC_LINEAR);
	GX_SetCopyFilter(rmode.aa, rmode.sample_pattern, GX_TRUE, rmode.vfilter);

	for (int y = 0; y < rmode.xfbHeight; y += rmode.efbHeight) {
		uint8_t clamp = GX_CLAMP_NONE;

		if (y == 0)
			clamp |= GX_CLAMP_TOP;
		if (y + rmode.efbHeight == rmode.xfbHeight)
			clamp |= GX_CLAMP_BOTTOM;

		GX_SetCopyClamp(clamp);

		for (int x = 0; x < rmode.fbWidth; x += 640) {
			uint16_t efbWidth = MIN(rmode.fbWidth - x, 640);

			GX_SetScissor(viewport.x + x, viewport.y + y - 2, efbWidth, rmode.efbHeight + 4);
			GX_SetScissorBoxOffset(viewport.x + x, viewport.y + y - 2);
			GX_ClearBoundingBox();

			if (dispsize[0]) {
				GXPreviewSetState(state.reset);
				GX_CallDispList(displist[0], dispsize[0]);
			}

			if (dispsize[1]) {
				GXOverlaySetState();
				GX_CallDispList(displist[1], dispsize[1]);
			}

			if (dispsize[2]) {
				GXFontSetState();
				GX_CallDispList(displist[2], dispsize[2]);
			}

			if (dispsize[3]) {
				GXCursorSetState();
				GX_CallDispList(displist[3], dispsize[3]);
			}

			GX_SetDispCopyFrame2Field(GX_COPY_PROGRESSIVE);
			GX_SetDispCopySrc(0, 2, efbWidth, rmode.efbHeight);
			GX_SetDispCopyDst(rmode.fbWidth, rmode.efbHeight);

			GX_CopyDisp(&xfb[y][x], GX_TRUE);

			GX_SetDispCopyFrame2Field(GX_COPY_NONE);
			GX_SetDispCopySrc(0, 0, efbWidth, 2);
			GX_SetDispCopyDst(0, 0);

			GX_CopyDisp(&xfb[y][x], GX_TRUE);

			GX_SetDispCopyFrame2Field(GX_COPY_NONE);
			GX_SetDispCopySrc(0, 2 + rmode.efbHeight, efbWidth, 2);
			GX_SetDispCopyDst(0, 0);

			GX_CopyDisp(&xfb[y][x], GX_TRUE);
		}
	}

	GX_SetDrawSyncCallback(drawsync_cb);
	GX_SetDrawSync(xfb_index);
}

static bool _pollRunning(void)
{
	return !state.quit;
}

static uint32_t _pollInput(const struct mInputMap *map)
{
	uint32_t keys = 0;

	InputRead();

	for (int chan = 0; chan < SI_MAX_CHAN; chan++) {
		if (gc_controller.data[chan].barrel) {
			if (gc_controller.data[chan].trigger.l == 0xFF &&
				gc_controller.data[chan].trigger.r == 0xFF) {
				keys |= mInputMapKeyBits(map, 'act\0', gc_controller.data[chan].held, 0);
				continue;
			} else if (gc_controller.data[chan].trigger.l == 0x00) {
				keys |= mInputMapKeyBits(map, 'dk\0\0', gc_controller.data[chan].held, 0);
				keys |= 1 << mInputMapAxis(map, 'dk\0\0', 0, gc_controller.data[chan].trigger.r);
				continue;
			}
		}

		keys |= mInputMapKeyBits(map, 'gc\0\0', gc_controller.data[chan].held, 0);
		keys |= 1 << mInputMapAxis(map, 'gc\0\0', 0, gc_controller.data[chan].stick.x);
		keys |= 1 << mInputMapAxis(map, 'gc\0\0', 1, gc_controller.data[chan].stick.y);
		keys |= 1 << mInputMapAxis(map, 'gc\0\0', 2, gc_controller.data[chan].substick.x);
		keys |= 1 << mInputMapAxis(map, 'gc\0\0', 3, gc_controller.data[chan].substick.y);
		keys |= 1 << mInputMapAxis(map, 'gc\0\0', 4, gc_controller.data[chan].trigger.l);
		keys |= 1 << mInputMapAxis(map, 'gc\0\0', 5, gc_controller.data[chan].trigger.r);
		keys |= 1 << mInputMapAxis(map, 'gc\0\0', 6, gc_controller.data[chan].button.a);
		keys |= 1 << mInputMapAxis(map, 'gc\0\0', 7, gc_controller.data[chan].button.b);
	}

	for (int chan = 0; chan < SI_MAX_CHAN; chan++) {
		keys |= mInputMapKeyBits(map, 'logi', gc_steering.data[chan].held, 0);
		keys |= 1 << mInputMapAxis(map, 'logi', 0, gc_steering.data[chan].wheel);
		keys |= 1 << mInputMapAxis(map, 'logi', 1, gc_steering.data[chan].pedal.l);
		keys |= 1 << mInputMapAxis(map, 'logi', 2, gc_steering.data[chan].pedal.r);
		keys |= 1 << mInputMapAxis(map, 'logi', 3, gc_steering.data[chan].paddle.l);
		keys |= 1 << mInputMapAxis(map, 'logi', 4, gc_steering.data[chan].paddle.r);
	}

	for (int chan = 0; chan < SI_MAX_CHAN; chan++) {
		keys |= mInputMapKeyBits(map, 'n64\0', n64_controller.data[chan].held, 0);
		keys |= 1 << mInputMapAxis(map, 'n64\0', 0, n64_controller.data[chan].stick.x);
		keys |= 1 << mInputMapAxis(map, 'n64\0', 1, n64_controller.data[chan].stick.y);
	}

	#ifdef HW_RVL
	WPAD_ScanPads();

	for (int chan = 0; chan < WPAD_MAX_WIIMOTES; chan++) {
		WPADData *data = WPAD_Data(chan);

		if (!data->ir.raw_valid && data->exp.type == WPAD_EXP_NONE) {
			if (data->orient.roll <= 0.)
				keys |= mInputMapKeyBits(map, 'wm\0\0', data->btns_h & 0xFFFF, 0);
			else
				keys |= mInputMapKeyBits(map, 'wmr\0', data->btns_h & 0xFFFF, 0);
		} else {
			keys |= mInputMapKeyBits(map, 'wmnc', data->btns_h & 0xFFFF, 0);

			switch (data->exp.type) {
				case WPAD_EXP_NUNCHUK:
					keys |= mInputMapKeyBits(map, 'wmnc', data->btns_h, 16);
					keys |= 1 << mInputMapAxis(map, 'wmnc', 0, data->exp.nunchuk.js.pos.x - data->exp.nunchuk.js.center.x);
					keys |= 1 << mInputMapAxis(map, 'wmnc', 1, data->exp.nunchuk.js.pos.y - data->exp.nunchuk.js.center.y);
					break;
				case WPAD_EXP_CLASSIC:
					keys |= mInputMapKeyBits(map, 'wmcc', data->exp.classic.btns, 0);
					keys |= 1 << mInputMapAxis(map, 'wmcc', 0, data->exp.classic.ljs.pos.x - data->exp.classic.ljs.center.x);
					keys |= 1 << mInputMapAxis(map, 'wmcc', 1, data->exp.classic.ljs.pos.y - data->exp.classic.ljs.center.y);
					keys |= 1 << mInputMapAxis(map, 'wmcc', 2, data->exp.classic.rjs.pos.x - data->exp.classic.rjs.center.x);
					keys |= 1 << mInputMapAxis(map, 'wmcc', 3, data->exp.classic.rjs.pos.y - data->exp.classic.rjs.center.y);
					keys |= 1 << mInputMapAxis(map, 'wmcc', 4, data->exp.classic.ls_raw);
					keys |= 1 << mInputMapAxis(map, 'wmcc', 5, data->exp.classic.rs_raw);
					break;
				case WPAD_EXP_WIIUPRO:
					keys |= mInputMapKeyBits(map, 'wupc', data->exp.wup.btns, 0);
					keys |= 1 << mInputMapAxis(map, 'wupc', 0, data->exp.wup.ljs.pos.x - data->exp.wup.ljs.center.x);
					keys |= 1 << mInputMapAxis(map, 'wupc', 1, data->exp.wup.ljs.pos.y - data->exp.wup.ljs.center.y);
					keys |= 1 << mInputMapAxis(map, 'wupc', 2, data->exp.wup.rjs.pos.x - data->exp.wup.rjs.center.x);
					keys |= 1 << mInputMapAxis(map, 'wupc', 3, data->exp.wup.rjs.pos.y - data->exp.wup.rjs.center.y);
					break;
				case WPAD_EXP_NES:
					keys |= mInputMapKeyBits(map, 'nes\0', data->exp.nes.btns, 0);
					break;
				case WPAD_EXP_SNES:
					keys |= mInputMapKeyBits(map, 'snes', data->exp.snes.btns, 0);
					break;
				case WPAD_EXP_N64:
					keys |= mInputMapKeyBits(map, 'n64\0', data->exp.n64.btns, 0);
					keys |= 1 << mInputMapAxis(map, 'n64\0', 0, data->exp.n64.js.pos.x - data->exp.n64.js.center.x);
					keys |= 1 << mInputMapAxis(map, 'n64\0', 1, data->exp.n64.js.pos.y - data->exp.n64.js.center.y);
					break;
				case WPAD_EXP_GC:
					keys |= mInputMapKeyBits(map, 'gc\0\0', data->exp.gc.btns, 0);
					keys |= 1 << mInputMapAxis(map, 'gc\0\0', 0, data->exp.gc.ljs.pos.x - data->exp.gc.ljs.center.x);
					keys |= 1 << mInputMapAxis(map, 'gc\0\0', 1, data->exp.gc.ljs.pos.y - data->exp.gc.ljs.center.y);
					keys |= 1 << mInputMapAxis(map, 'gc\0\0', 2, data->exp.gc.rjs.pos.x - data->exp.gc.rjs.center.x);
					keys |= 1 << mInputMapAxis(map, 'gc\0\0', 3, data->exp.gc.rjs.pos.y - data->exp.gc.rjs.center.y);
					keys |= 1 << mInputMapAxis(map, 'gc\0\0', 4, data->exp.gc.ls_raw);
					keys |= 1 << mInputMapAxis(map, 'gc\0\0', 5, data->exp.gc.rs_raw);
					break;
			}
		}
	}
	#endif

	CTRScanPads();

	keys |= mInputMapKeyBits(map, '3ds\0', ctr.data.held, 0);
	keys |= 1 << mInputMapAxis(map, '3ds\0', 0, ctr.data.stick.x);
	keys |= 1 << mInputMapAxis(map, '3ds\0', 1, ctr.data.stick.y);
	keys |= 1 << mInputMapAxis(map, '3ds\0', 2, ctr.data.substick.x);
	keys |= 1 << mInputMapAxis(map, '3ds\0', 3, ctr.data.substick.y);

	if (ctr.data.held & CTR_TOUCH) {
		keys |= 1 << mInputMapAxis(map, '3ds\0', 4, ctr.data.touch.x);
		keys |= 1 << mInputMapAxis(map, '3ds\0', 5, ctr.data.touch.y);
	}

	return keys;
}

#ifdef HW_RVL
static enum GUICursorState _pollCursor(unsigned *x, unsigned *y)
{
	for (int chan = 0; chan < WPAD_MAX_WIIMOTES; chan++) {
		WPADData *data = WPAD_Data(chan);

		if (data->ir.valid) {
			*x = (data->ir.x - screen.w / 2) + (GBA_VIDEO_HORIZONTAL_PIXELS * state.zoom.x / 2);
			*y = (data->ir.y - screen.h / 2) + (GBA_VIDEO_VERTICAL_PIXELS   * state.zoom.y / 2);
			return GUI_CURSOR_UP;
		}
	}

	return GUI_CURSOR_NOT_PRESENT;
}

static int _batteryState(void)
{
	int charge, state;

	for (int chan = 0; chan < WPAD_MAX_WIIMOTES; chan++) {
		WPADData *data = WPAD_Data(chan);

		if (data->data_present & WPAD_DATA_BUTTONS) {
			if (data->exp.type == WPAD_EXP_WIIUPRO)
				charge = data->exp.wup.battery_level * 25;
			else
				charge = data->battery_level / 2.55 * 2.46 - 1.3;

			charge = MIN(MAX(charge, BATTERY_EMPTY), BATTERY_FULL);
			state = (charge & BATTERY_VALUE) | BATTERY_PERCENTAGE_VALID;

			if (data->exp.type == WPAD_EXP_WIIUPRO && (data->exp.wup.btns & WIIU_PRO_CTRL_BUTTON_CHARGING))
				state |= BATTERY_CHARGING;
			return state;
		}
	}

	return BATTERY_NOT_PRESENT;
}
#endif

static void _guiPrepare(void)
{
	GX_BeginDispList(displist[2], GX_FIFO_MINSIZE);

	GX_SetViewportJitter(viewport.x + state.offset.x, viewport.y + state.offset.y, viewport.w, viewport.h, 0., 1.,
		rmode.field_rendering ? state.field : viewport.h % 2);

	Mtx viewmodel;
	guMtxTrans(viewmodel,
		-GBA_VIDEO_HORIZONTAL_PIXELS * state.zoom.x / 2,
		-GBA_VIDEO_VERTICAL_PIXELS   * state.zoom.y / 2, 0);
	GX_LoadPosMtxImm(viewmodel, GX_PNMTX1);
}

static void _guiFinish(void)
{
	dispsize[2] = GX_EndDispList();

	GX_BeginDispList(displist[3], GX_FIFO_MINSIZE);

	#ifdef HW_RVL
	for (int chan = 0; chan < WPAD_MAX_WIIMOTES; chan++) {
		WPADData *data = WPAD_Data(chan);

		if (data->ir.valid) {
			GXCursorDrawPoint(chan, data->ir.x, data->ir.y, data->ir.angle);
			break;
		}
	}
	#endif

	dispsize[3] = GX_EndDispList();
}

static struct mStereoSample audioBuffer[2][16384] ATTRIBUTE_ALIGN(32);
static unsigned audioBufferIndex;
static unsigned audioBufferSize;
static unsigned audioRate;
static double fpsRatio;

static void _audioRateChanged(struct mAVStream *stream, unsigned rate)
{
	audioRate = rate * fpsRatio;
	audioBufferSize = (audioRate * 1024 + (48000 - 1)) / 48000;
	ASND_ChangePitchVoice(0, audioRate);
}

static void _postAudioBuffer(struct mAVStream *stream, struct mAudioBuffer *buffer)
{
	uint32_t level;

	if (ASND_TestVoiceBufferReady(0) == SND_OK) {
		size_t available = mAudioBufferAvailable(buffer) / audioBufferSize * audioBufferSize;
		mAudioBufferRead(buffer, (int16_t *)audioBuffer[audioBufferIndex], available);

		_CPU_ISR_Disable(level);

		if (ASND_StatusVoice(0) == SND_UNUSED) {
			if (ASND_SetVoice(0, VOICE_STEREO_16BIT, audioRate, 0, audioBuffer[audioBufferIndex], available * 4, MAX_VOLUME, MAX_VOLUME, NULL) == SND_OK)
				audioBufferIndex ^= 1;
		} else {
			if (ASND_AddVoice(0, audioBuffer[audioBufferIndex], available * 4) == SND_OK)
				audioBufferIndex ^= 1;
		}

		_CPU_ISR_Restore(level);
	} else {
		if (mAudioBufferAvailable(buffer) == mAudioBufferCapacity(buffer))
			mAudioBufferClear(buffer);
	}
}

static struct mAVStream stream = {
	.audioRateChanged = _audioRateChanged,
	.postAudioBuffer = _postAudioBuffer,
};

static void _setRumble(struct mRumble *rumble, bool enable, uint32_t sinceLast)
{
	PAD_ControlMotor(PAD_CHAN0, enable);
	#ifdef HW_RVL
	WPAD_Rumble(WPAD_CHAN_0, enable);
	#endif
}

static struct mRumble rumble = {
	.setRumble = _setRumble,
};

static void _sampleRotation(struct mRotationSource *source)
{
	state.rotation = RadToDeg(ctr.data.orient.roll);

	#ifdef HW_RVL
	for (int chan = 0; chan < WPAD_MAX_WIIMOTES; chan++) {
		WPADData *data = WPAD_Data(chan);

		if (data->data_present & WPAD_DATA_ACCEL) {
			if (!data->ir.raw_valid && data->exp.type == WPAD_EXP_NONE)
				state.rotation = data->orient.roll <= 0. ? -data->orient.pitch : data->orient.pitch;
			else
				state.rotation = data->ir.raw_valid ? data->ir.angle : data->orient.roll;
			break;
		}
	}
	#endif
}

static int32_t _readTiltX(struct mRotationSource *source)
{
	float tiltX = ctr.data.gforce.x;

	#ifdef HW_RVL
	for (int chan = 0; chan < WPAD_MAX_WIIMOTES; chan++) {
		WPADData *data = WPAD_Data(chan);

		if (data->data_present & WPAD_DATA_ACCEL) {
			if (!data->ir.raw_valid && data->exp.type == WPAD_EXP_NONE)
				tiltX = data->orient.roll <= 0. ? -data->gforce.y : data->gforce.y;
			else
				tiltX = data->gforce.x;
			break;
		}
	}
	#endif

	return tiltX * 0xE0p21;
}

static int32_t _readTiltY(struct mRotationSource *source)
{
	float tiltY = ctr.data.gforce.y;

	#ifdef HW_RVL
	for (int chan = 0; chan < WPAD_MAX_WIIMOTES; chan++) {
		WPADData *data = WPAD_Data(chan);

		if (data->data_present & WPAD_DATA_ACCEL) {
			if (!data->ir.raw_valid && data->exp.type == WPAD_EXP_NONE)
				tiltY = data->orient.roll <= 0. ? -data->gforce.x : data->gforce.x;
			else
				tiltY = data->gforce.y;
			break;
		}
	}
	#endif

	return tiltY * 0xE0p21;
}

static struct mRotationSource rotation = {
	.sample = _sampleRotation,
	.readTiltX = _readTiltX,
	.readTiltY = _readTiltY,
};

static void _updateScreenMode(struct GUIParams *params, unsigned mode)
{
	switch (mode) {
		case SM_NORMAL:
		{
			float dst = hypotf(screen.w, screen.h) * .74;
			float src = hypotf(GBA_VIDEO_HORIZONTAL_PIXELS, GBA_VIDEO_VERTICAL_PIXELS);
			state.zoom.x = 
			state.zoom.y = rintf((dst / src) * 8.) / 8.;
			break;
		}
		case SM_FULL:
		{
			float dst = hypotf(screen.w, screen.h) * .84;
			float src = hypotf(GBA_VIDEO_HORIZONTAL_PIXELS, GBA_VIDEO_VERTICAL_PIXELS);
			state.zoom.x = 
			state.zoom.y = rintf((dst / src) * 8.) / 8.;
			break;
		}
		case SM_STRETCH:
		{
			float dst[2] = {screen.w * .94, screen.h * .94};
			float src[2] = {GBA_VIDEO_HORIZONTAL_PIXELS, GBA_VIDEO_VERTICAL_PIXELS};
			state.zoom.x = rintf((dst[0] / src[0]) * 8.) / 8.;
			state.zoom.y = rintf((dst[1] / src[1]) * 8.) / 8.;
			break;
		}
		default:
		{
			float dst[2] = {screen.w * state.zoom_ratio, screen.h * state.zoom_ratio};
			float src[2] = {GBA_VIDEO_HORIZONTAL_PIXELS, GBA_VIDEO_VERTICAL_PIXELS};

			if (state.zoom_auto) {
				if (dst[0] / dst[1] < src[0] / src[1]) {
					state.zoom.x = 
					state.zoom.y = dst[0] / src[0];
				} else {
					state.zoom.x = 
					state.zoom.y = dst[1] / src[1];
				}
			} else
				state.zoom = default_state.zoom;
		}
	}

	params->width = GBA_VIDEO_HORIZONTAL_PIXELS * state.zoom.x;
	params->height = GBA_VIDEO_VERTICAL_PIXELS * state.zoom.y;
}

static void *outputBuffer;

static void _setup(struct mGUIRunner *runner)
{
	unsigned mode;

	if (mCoreConfigGetUIntValue(&runner->config, "screenMode", &mode))
		_updateScreenMode(&runner->params, mode);

	mCoreConfigSetDefaultFloatValue(&runner->config, "fpsTarget", (float)GBA_ARM7TDMI_FREQUENCY / VIDEO_TOTAL_LENGTH);

	mInputBindKey(&runner->params.keyMap, 'gc\0\0', __builtin_ctz(PAD_BUTTON_A), GUI_INPUT_SELECT);
	mInputBindKey(&runner->params.keyMap, 'gc\0\0', __builtin_ctz(PAD_BUTTON_B), GUI_INPUT_BACK);
	mInputBindKey(&runner->params.keyMap, 'gc\0\0', __builtin_ctz(PAD_BUTTON_X), GUI_INPUT_CANCEL);
	mInputBindKey(&runner->params.keyMap, 'gc\0\0', __builtin_ctz(PAD_BUTTON_UP), GUI_INPUT_UP);
	mInputBindKey(&runner->params.keyMap, 'gc\0\0', __builtin_ctz(PAD_BUTTON_DOWN), GUI_INPUT_DOWN);
	mInputBindKey(&runner->params.keyMap, 'gc\0\0', __builtin_ctz(PAD_BUTTON_LEFT), GUI_INPUT_LEFT);
	mInputBindKey(&runner->params.keyMap, 'gc\0\0', __builtin_ctz(PAD_BUTTON_RIGHT), GUI_INPUT_RIGHT);
	mInputBindKey(&runner->params.keyMap, 'gc\0\0', __builtin_ctz(PAD_BUTTON_Y), mGUI_INPUT_FAST_FORWARD_HELD);

	mInputBindAxis(&runner->params.keyMap, 'gc\0\0', 0, &(struct mInputAxis){GUI_INPUT_RIGHT, GUI_INPUT_LEFT, +50, -50});
	mInputBindAxis(&runner->params.keyMap, 'gc\0\0', 1, &(struct mInputAxis){GUI_INPUT_UP, GUI_INPUT_DOWN, +50, -50});
	mInputBindAxis(&runner->params.keyMap, 'gc\0\0', 3, &(struct mInputAxis){mGUI_INPUT_INCREASE_BRIGHTNESS, mGUI_INPUT_DECREASE_BRIGHTNESS, +50, -50});

	mInputBindKey(&runner->params.keyMap, 'logi', __builtin_ctz(SI_STEERING_BUTTON_A), GUI_INPUT_SELECT);
	mInputBindKey(&runner->params.keyMap, 'logi', __builtin_ctz(SI_STEERING_BUTTON_B), GUI_INPUT_BACK);
	mInputBindKey(&runner->params.keyMap, 'logi', __builtin_ctz(SI_STEERING_BUTTON_X), GUI_INPUT_CANCEL);
	mInputBindKey(&runner->params.keyMap, 'logi', __builtin_ctz(SI_STEERING_BUTTON_UP), GUI_INPUT_UP);
	mInputBindKey(&runner->params.keyMap, 'logi', __builtin_ctz(SI_STEERING_BUTTON_DOWN), GUI_INPUT_DOWN);
	mInputBindKey(&runner->params.keyMap, 'logi', __builtin_ctz(SI_STEERING_BUTTON_LEFT), GUI_INPUT_LEFT);
	mInputBindKey(&runner->params.keyMap, 'logi', __builtin_ctz(SI_STEERING_BUTTON_RIGHT), GUI_INPUT_RIGHT);
	mInputBindKey(&runner->params.keyMap, 'logi', __builtin_ctz(SI_STEERING_BUTTON_Y), mGUI_INPUT_FAST_FORWARD_HELD);

	mInputBindAxis(&runner->params.keyMap, 'logi', 0, &(struct mInputAxis){GUI_INPUT_RIGHT, GUI_INPUT_LEFT, +25, -25});

	#ifdef HW_RVL
	mInputBindKey(&runner->params.keyMap, 'wm\0\0', __builtin_ctz(WPAD_BUTTON_2), GUI_INPUT_SELECT);
	mInputBindKey(&runner->params.keyMap, 'wm\0\0', __builtin_ctz(WPAD_BUTTON_1), GUI_INPUT_BACK);
	mInputBindKey(&runner->params.keyMap, 'wm\0\0', __builtin_ctz(WPAD_BUTTON_HOME), GUI_INPUT_CANCEL);
	mInputBindKey(&runner->params.keyMap, 'wm\0\0', __builtin_ctz(WPAD_BUTTON_RIGHT), GUI_INPUT_UP);
	mInputBindKey(&runner->params.keyMap, 'wm\0\0', __builtin_ctz(WPAD_BUTTON_LEFT), GUI_INPUT_DOWN);
	mInputBindKey(&runner->params.keyMap, 'wm\0\0', __builtin_ctz(WPAD_BUTTON_UP), GUI_INPUT_LEFT);
	mInputBindKey(&runner->params.keyMap, 'wm\0\0', __builtin_ctz(WPAD_BUTTON_DOWN), GUI_INPUT_RIGHT);

	mInputBindKey(&runner->params.keyMap, 'wmr\0', __builtin_ctz(WPAD_BUTTON_2), GUI_INPUT_SELECT);
	mInputBindKey(&runner->params.keyMap, 'wmr\0', __builtin_ctz(WPAD_BUTTON_1), GUI_INPUT_BACK);
	mInputBindKey(&runner->params.keyMap, 'wmr\0', __builtin_ctz(WPAD_BUTTON_HOME), GUI_INPUT_CANCEL);
	mInputBindKey(&runner->params.keyMap, 'wmr\0', __builtin_ctz(WPAD_BUTTON_LEFT), GUI_INPUT_UP);
	mInputBindKey(&runner->params.keyMap, 'wmr\0', __builtin_ctz(WPAD_BUTTON_RIGHT), GUI_INPUT_DOWN);
	mInputBindKey(&runner->params.keyMap, 'wmr\0', __builtin_ctz(WPAD_BUTTON_DOWN), GUI_INPUT_LEFT);
	mInputBindKey(&runner->params.keyMap, 'wmr\0', __builtin_ctz(WPAD_BUTTON_UP), GUI_INPUT_RIGHT);

	mInputBindKey(&runner->params.keyMap, 'wmnc', __builtin_ctz(WPAD_BUTTON_A), GUI_INPUT_SELECT);
	mInputBindKey(&runner->params.keyMap, 'wmnc', __builtin_ctz(WPAD_BUTTON_B), GUI_INPUT_BACK);
	mInputBindKey(&runner->params.keyMap, 'wmnc', __builtin_ctz(WPAD_BUTTON_1), GUI_INPUT_CANCEL);
	mInputBindKey(&runner->params.keyMap, 'wmnc', __builtin_ctz(WPAD_BUTTON_UP), GUI_INPUT_UP);
	mInputBindKey(&runner->params.keyMap, 'wmnc', __builtin_ctz(WPAD_BUTTON_DOWN), GUI_INPUT_DOWN);
	mInputBindKey(&runner->params.keyMap, 'wmnc', __builtin_ctz(WPAD_BUTTON_LEFT), GUI_INPUT_LEFT);
	mInputBindKey(&runner->params.keyMap, 'wmnc', __builtin_ctz(WPAD_BUTTON_RIGHT), GUI_INPUT_RIGHT);
	mInputBindKey(&runner->params.keyMap, 'wmnc', __builtin_ctz(WPAD_BUTTON_2), mGUI_INPUT_FAST_FORWARD_HELD);

	mInputBindAxis(&runner->params.keyMap, 'wmnc', 0, &(struct mInputAxis){GUI_INPUT_RIGHT, GUI_INPUT_LEFT, +50, -50});
	mInputBindAxis(&runner->params.keyMap, 'wmnc', 1, &(struct mInputAxis){GUI_INPUT_UP, GUI_INPUT_DOWN, +50, -50});

	mInputBindKey(&runner->params.keyMap, 'wmcc', __builtin_ctz(CLASSIC_CTRL_BUTTON_A), GUI_INPUT_SELECT);
	mInputBindKey(&runner->params.keyMap, 'wmcc', __builtin_ctz(CLASSIC_CTRL_BUTTON_B), GUI_INPUT_BACK);
	mInputBindKey(&runner->params.keyMap, 'wmcc', __builtin_ctz(CLASSIC_CTRL_BUTTON_X), GUI_INPUT_CANCEL);
	mInputBindKey(&runner->params.keyMap, 'wmcc', __builtin_ctz(CLASSIC_CTRL_BUTTON_UP), GUI_INPUT_UP);
	mInputBindKey(&runner->params.keyMap, 'wmcc', __builtin_ctz(CLASSIC_CTRL_BUTTON_DOWN), GUI_INPUT_DOWN);
	mInputBindKey(&runner->params.keyMap, 'wmcc', __builtin_ctz(CLASSIC_CTRL_BUTTON_LEFT), GUI_INPUT_LEFT);
	mInputBindKey(&runner->params.keyMap, 'wmcc', __builtin_ctz(CLASSIC_CTRL_BUTTON_RIGHT), GUI_INPUT_RIGHT);
	mInputBindKey(&runner->params.keyMap, 'wmcc', __builtin_ctz(CLASSIC_CTRL_BUTTON_Y), mGUI_INPUT_FAST_FORWARD_HELD);

	mInputBindAxis(&runner->params.keyMap, 'wmcc', 0, &(struct mInputAxis){GUI_INPUT_RIGHT, GUI_INPUT_LEFT, +15, -15});
	mInputBindAxis(&runner->params.keyMap, 'wmcc', 1, &(struct mInputAxis){GUI_INPUT_UP, GUI_INPUT_DOWN, +15, -15});
	mInputBindAxis(&runner->params.keyMap, 'wmcc', 3, &(struct mInputAxis){mGUI_INPUT_INCREASE_BRIGHTNESS, mGUI_INPUT_DECREASE_BRIGHTNESS, +7, -7});

	mInputBindKey(&runner->params.keyMap, 'wupc', __builtin_ctz(WIIU_PRO_CTRL_BUTTON_A), GUI_INPUT_SELECT);
	mInputBindKey(&runner->params.keyMap, 'wupc', __builtin_ctz(WIIU_PRO_CTRL_BUTTON_B), GUI_INPUT_BACK);
	mInputBindKey(&runner->params.keyMap, 'wupc', __builtin_ctz(WIIU_PRO_CTRL_BUTTON_X), GUI_INPUT_CANCEL);
	mInputBindKey(&runner->params.keyMap, 'wupc', __builtin_ctz(WIIU_PRO_CTRL_BUTTON_UP), GUI_INPUT_UP);
	mInputBindKey(&runner->params.keyMap, 'wupc', __builtin_ctz(WIIU_PRO_CTRL_BUTTON_DOWN), GUI_INPUT_DOWN);
	mInputBindKey(&runner->params.keyMap, 'wupc', __builtin_ctz(WIIU_PRO_CTRL_BUTTON_LEFT), GUI_INPUT_LEFT);
	mInputBindKey(&runner->params.keyMap, 'wupc', __builtin_ctz(WIIU_PRO_CTRL_BUTTON_RIGHT), GUI_INPUT_RIGHT);
	mInputBindKey(&runner->params.keyMap, 'wupc', __builtin_ctz(WIIU_PRO_CTRL_BUTTON_Y), mGUI_INPUT_FAST_FORWARD_HELD);

	mInputBindAxis(&runner->params.keyMap, 'wupc', 0, &(struct mInputAxis){GUI_INPUT_RIGHT, GUI_INPUT_LEFT, +500, -500});
	mInputBindAxis(&runner->params.keyMap, 'wupc', 1, &(struct mInputAxis){GUI_INPUT_UP, GUI_INPUT_DOWN, +500, -500});
	mInputBindAxis(&runner->params.keyMap, 'wupc', 3, &(struct mInputAxis){mGUI_INPUT_INCREASE_BRIGHTNESS, mGUI_INPUT_DECREASE_BRIGHTNESS, +500, -500});

	mInputBindKey(&runner->params.keyMap, 'nes\0', __builtin_ctz(EXTENMOTE_NES_BUTTON_A), GUI_INPUT_SELECT);
	mInputBindKey(&runner->params.keyMap, 'nes\0', __builtin_ctz(EXTENMOTE_NES_BUTTON_B), GUI_INPUT_BACK);
	mInputBindKey(&runner->params.keyMap, 'nes\0', __builtin_ctz(EXTENMOTE_NES_BUTTON_UP), GUI_INPUT_UP);
	mInputBindKey(&runner->params.keyMap, 'nes\0', __builtin_ctz(EXTENMOTE_NES_BUTTON_DOWN), GUI_INPUT_DOWN);
	mInputBindKey(&runner->params.keyMap, 'nes\0', __builtin_ctz(EXTENMOTE_NES_BUTTON_LEFT), GUI_INPUT_LEFT);
	mInputBindKey(&runner->params.keyMap, 'nes\0', __builtin_ctz(EXTENMOTE_NES_BUTTON_RIGHT), GUI_INPUT_RIGHT);

	mInputBindKey(&runner->params.keyMap, 'snes', __builtin_ctz(EXTENMOTE_SNES_BUTTON_A), GUI_INPUT_SELECT);
	mInputBindKey(&runner->params.keyMap, 'snes', __builtin_ctz(EXTENMOTE_SNES_BUTTON_B), GUI_INPUT_BACK);
	mInputBindKey(&runner->params.keyMap, 'snes', __builtin_ctz(EXTENMOTE_SNES_BUTTON_X), GUI_INPUT_CANCEL);
	mInputBindKey(&runner->params.keyMap, 'snes', __builtin_ctz(EXTENMOTE_SNES_BUTTON_UP), GUI_INPUT_UP);
	mInputBindKey(&runner->params.keyMap, 'snes', __builtin_ctz(EXTENMOTE_SNES_BUTTON_DOWN), GUI_INPUT_DOWN);
	mInputBindKey(&runner->params.keyMap, 'snes', __builtin_ctz(EXTENMOTE_SNES_BUTTON_LEFT), GUI_INPUT_LEFT);
	mInputBindKey(&runner->params.keyMap, 'snes', __builtin_ctz(EXTENMOTE_SNES_BUTTON_RIGHT), GUI_INPUT_RIGHT);
	mInputBindKey(&runner->params.keyMap, 'snes', __builtin_ctz(EXTENMOTE_SNES_BUTTON_Y), mGUI_INPUT_FAST_FORWARD_HELD);
	#endif

	mInputBindKey(&runner->params.keyMap, 'n64\0', __builtin_ctz(N64_BUTTON_A), GUI_INPUT_SELECT);
	mInputBindKey(&runner->params.keyMap, 'n64\0', __builtin_ctz(N64_BUTTON_B), GUI_INPUT_BACK);
	mInputBindKey(&runner->params.keyMap, 'n64\0', __builtin_ctz(N64_BUTTON_C_LEFT), GUI_INPUT_CANCEL);
	mInputBindKey(&runner->params.keyMap, 'n64\0', __builtin_ctz(N64_BUTTON_UP), GUI_INPUT_UP);
	mInputBindKey(&runner->params.keyMap, 'n64\0', __builtin_ctz(N64_BUTTON_DOWN), GUI_INPUT_DOWN);
	mInputBindKey(&runner->params.keyMap, 'n64\0', __builtin_ctz(N64_BUTTON_LEFT), GUI_INPUT_LEFT);
	mInputBindKey(&runner->params.keyMap, 'n64\0', __builtin_ctz(N64_BUTTON_RIGHT), GUI_INPUT_RIGHT);
	mInputBindKey(&runner->params.keyMap, 'n64\0', __builtin_ctz(N64_BUTTON_C_UP), mGUI_INPUT_INCREASE_BRIGHTNESS);
	mInputBindKey(&runner->params.keyMap, 'n64\0', __builtin_ctz(N64_BUTTON_C_DOWN), mGUI_INPUT_DECREASE_BRIGHTNESS);
	mInputBindKey(&runner->params.keyMap, 'n64\0', __builtin_ctz(N64_BUTTON_C_RIGHT), mGUI_INPUT_FAST_FORWARD_HELD);

	mInputBindAxis(&runner->params.keyMap, 'n64\0', 0, &(struct mInputAxis){GUI_INPUT_RIGHT, GUI_INPUT_LEFT, +40, -40});
	mInputBindAxis(&runner->params.keyMap, 'n64\0', 1, &(struct mInputAxis){GUI_INPUT_UP, GUI_INPUT_DOWN, +40, -40});

	mInputBindKey(&runner->params.keyMap, '3ds\0', __builtin_ctz(CTR_BUTTON_A), GUI_INPUT_SELECT);
	mInputBindKey(&runner->params.keyMap, '3ds\0', __builtin_ctz(CTR_BUTTON_B), GUI_INPUT_BACK);
	mInputBindKey(&runner->params.keyMap, '3ds\0', __builtin_ctz(CTR_BUTTON_X), GUI_INPUT_CANCEL);
	mInputBindKey(&runner->params.keyMap, '3ds\0', __builtin_ctz(CTR_BUTTON_UP), GUI_INPUT_UP);
	mInputBindKey(&runner->params.keyMap, '3ds\0', __builtin_ctz(CTR_BUTTON_DOWN), GUI_INPUT_DOWN);
	mInputBindKey(&runner->params.keyMap, '3ds\0', __builtin_ctz(CTR_BUTTON_LEFT), GUI_INPUT_LEFT);
	mInputBindKey(&runner->params.keyMap, '3ds\0', __builtin_ctz(CTR_BUTTON_RIGHT), GUI_INPUT_RIGHT);
	mInputBindKey(&runner->params.keyMap, '3ds\0', __builtin_ctz(CTR_BUTTON_Y), mGUI_INPUT_FAST_FORWARD_HELD);

	mInputBindAxis(&runner->params.keyMap, '3ds\0', 0, &(struct mInputAxis){GUI_INPUT_RIGHT, GUI_INPUT_LEFT, +40, -40});
	mInputBindAxis(&runner->params.keyMap, '3ds\0', 1, &(struct mInputAxis){GUI_INPUT_UP, GUI_INPUT_DOWN, +40, -40});
	mInputBindAxis(&runner->params.keyMap, '3ds\0', 3, &(struct mInputAxis){mGUI_INPUT_INCREASE_BRIGHTNESS, mGUI_INPUT_DECREASE_BRIGHTNESS, +40, -40});

	LWP_SemInit(&semaphore[0], 1, 1);
	LWP_SemInit(&semaphore[1], 1, 1);
	SYS_CreateAlarm(&watchdog);
	VIDEO_SetPostRetraceCallback(vsync_cb);
}

static void _teardown(struct mGUIRunner *runner)
{
	state.quit |= KEY_QUIT;

	VIDEO_SetPostRetraceCallback(NULL);
	SYS_RemoveAlarm(watchdog);
	LWP_SemDestroy(semaphore[0]);
	LWP_SemDestroy(semaphore[1]);
	watchdog = SYS_WD_NULL;
	semaphore[0] = LWP_SEM_NULL;
	semaphore[1] = LWP_SEM_NULL;
}

static void _gameLoaded(struct mGUIRunner *runner)
{
	while (isnan(aiclock.hz) || isnan(viclock.hz)) {
		runner->params.drawStart();
		if (runner->params.guiPrepare)
			runner->params.guiPrepare();
		GUIFontPrint(runner->params.font, runner->params.width / 2, (GUIFontHeight(runner->params.font) + runner->params.height) / 2, GUI_ALIGN_HCENTER, 0xFFFFFFFF, "Calibrating...");
		if (runner->params.guiFinish)
			runner->params.guiFinish();
		runner->params.drawEnd();
	}

	unsigned width, height;
	runner->core->currentVideoSize(runner->core, &width, &height);
	outputBuffer = GXAllocBuffer(width * height * BYTES_PER_PIXEL);
	runner->core->setVideoBuffer(runner->core, outputBuffer, width);

	GXAllocSurface(&convert_surface, width, height, GX_TF_RGB5A3, 1);
	GXPreloadSurfacev(&convert_surface, (uint32_t[]){0x40000, 0x60000, 0xE0000}, NULL, 3);
	GXSetSurfaceFilt(&convert_surface, GX_NEAR);

	if (state.filter == FILTER_ACCUMULATE ||
		state.filter == FILTER_SCALE2XEX) {
		GXAllocSurface(&packed_surface, width, height, GX_TF_RGBA8, 1);
		GXPreloadSurface(&packed_surface, 0x60000, 0xE0000, 1);
		GXSetSurfaceFilt(&packed_surface, GX_NEAR);
	}

	GXAllocSurface(&planar_surface, width * state.scale, height * state.scale, GX_TF_CI8, 3);
	if (planar_surface.size * 3 > 0x40000)
		GXPreloadSurfacev(&planar_surface, (uint32_t[]){0x00000, 0x00000, 0x00000}, NULL, 3);
	else GXPreloadSurface(&planar_surface, 0x00000, 0x00000, 3);
	GXSetSurfaceFilt(&planar_surface, GX_NEAR);

	if (state.filter_prescale)
		GXAllocSurface(&prescale_surface, width * 4, height * MIN(rmode.xfbHeight * 4 / rmode.viHeight, 4), GX_TF_I8, 3);
	else GXAllocSurface(&prescale_surface, width * state.scale, height * state.scale, GX_TF_I8, 3);
	GXSetSurfaceFilt(&prescale_surface, state.scaler == SCALER_NEAREST ? GX_NEAR : GX_LINEAR);

	runner->core->setAudioBufferSize(runner->core, 1024 * 3);

	runner->core->setAVStream(runner->core, &stream);

	runner->core->setPeripheral(runner->core, mPERIPH_ROTATION, &rotation);
	runner->core->setPeripheral(runner->core, mPERIPH_RUMBLE, &rumble);

	mInputBindKey(&runner->core->inputMap, 'dk\0\0', __builtin_ctz(PAD_BUTTON_X), GBA_KEY_A);
	mInputBindKey(&runner->core->inputMap, 'dk\0\0', __builtin_ctz(PAD_BUTTON_Y), GBA_KEY_B);
	mInputBindKey(&runner->core->inputMap, 'dk\0\0', __builtin_ctz(PAD_BUTTON_START), GBA_KEY_START);
	mInputBindKey(&runner->core->inputMap, 'dk\0\0', __builtin_ctz(PAD_BUTTON_B), GBA_KEY_LEFT);
	mInputBindKey(&runner->core->inputMap, 'dk\0\0', __builtin_ctz(PAD_BUTTON_A), GBA_KEY_DOWN);

	mInputBindAxis(&runner->core->inputMap, 'dk\0\0', 0, &(struct mInputAxis){GBA_KEY_RIGHT, GBA_KEY_NONE, 30, 0});

	mInputBindKey(&runner->core->inputMap, 'gc\0\0', __builtin_ctz(PAD_BUTTON_A), GBA_KEY_A);
	mInputBindKey(&runner->core->inputMap, 'gc\0\0', __builtin_ctz(PAD_BUTTON_B), GBA_KEY_B);
	mInputBindKey(&runner->core->inputMap, 'gc\0\0', __builtin_ctz(PAD_BUTTON_Z), GBA_KEY_SELECT);
	mInputBindKey(&runner->core->inputMap, 'gc\0\0', __builtin_ctz(PAD_BUTTON_START), GBA_KEY_START);
	mInputBindKey(&runner->core->inputMap, 'gc\0\0', __builtin_ctz(PAD_BUTTON_RIGHT), GBA_KEY_RIGHT);
	mInputBindKey(&runner->core->inputMap, 'gc\0\0', __builtin_ctz(PAD_BUTTON_LEFT), GBA_KEY_LEFT);
	mInputBindKey(&runner->core->inputMap, 'gc\0\0', __builtin_ctz(PAD_BUTTON_UP), GBA_KEY_UP);
	mInputBindKey(&runner->core->inputMap, 'gc\0\0', __builtin_ctz(PAD_BUTTON_DOWN), GBA_KEY_DOWN);
	mInputBindKey(&runner->core->inputMap, 'gc\0\0', __builtin_ctz(PAD_BUTTON_R), GBA_KEY_R);
	mInputBindKey(&runner->core->inputMap, 'gc\0\0', __builtin_ctz(PAD_BUTTON_L), GBA_KEY_L);

	mInputBindAxis(&runner->core->inputMap, 'gc\0\0', 0, &(struct mInputAxis){GBA_KEY_RIGHT, GBA_KEY_LEFT, +50, -50});
	mInputBindAxis(&runner->core->inputMap, 'gc\0\0', 1, &(struct mInputAxis){GBA_KEY_UP, GBA_KEY_DOWN, +50, -50});
	mInputBindAxis(&runner->core->inputMap, 'gc\0\0', 4, &(struct mInputAxis){GBA_KEY_L, GBA_KEY_NONE, 100, 0});
	mInputBindAxis(&runner->core->inputMap, 'gc\0\0', 5, &(struct mInputAxis){GBA_KEY_R, GBA_KEY_NONE, 100, 0});

	mInputBindKey(&runner->core->inputMap, 'logi', __builtin_ctz(SI_STEERING_BUTTON_A), GBA_KEY_A);
	mInputBindKey(&runner->core->inputMap, 'logi', __builtin_ctz(SI_STEERING_BUTTON_B), GBA_KEY_B);
	mInputBindKey(&runner->core->inputMap, 'logi', __builtin_ctz(SI_STEERING_BUTTON_Z), GBA_KEY_SELECT);
	mInputBindKey(&runner->core->inputMap, 'logi', __builtin_ctz(SI_STEERING_BUTTON_START), GBA_KEY_START);
	mInputBindKey(&runner->core->inputMap, 'logi', __builtin_ctz(SI_STEERING_BUTTON_RIGHT), GBA_KEY_RIGHT);
	mInputBindKey(&runner->core->inputMap, 'logi', __builtin_ctz(SI_STEERING_BUTTON_LEFT), GBA_KEY_LEFT);
	mInputBindKey(&runner->core->inputMap, 'logi', __builtin_ctz(SI_STEERING_BUTTON_UP), GBA_KEY_UP);
	mInputBindKey(&runner->core->inputMap, 'logi', __builtin_ctz(SI_STEERING_BUTTON_DOWN), GBA_KEY_DOWN);
	mInputBindKey(&runner->core->inputMap, 'logi', __builtin_ctz(SI_STEERING_BUTTON_R), GBA_KEY_R);
	mInputBindKey(&runner->core->inputMap, 'logi', __builtin_ctz(SI_STEERING_BUTTON_L), GBA_KEY_L);

	mInputBindAxis(&runner->core->inputMap, 'logi', 0, &(struct mInputAxis){GBA_KEY_RIGHT, GBA_KEY_LEFT, +25, -25});
	mInputBindAxis(&runner->core->inputMap, 'logi', 1, &(struct mInputAxis){GBA_KEY_A, GBA_KEY_NONE, 50, 0});
	mInputBindAxis(&runner->core->inputMap, 'logi', 2, &(struct mInputAxis){GBA_KEY_B, GBA_KEY_NONE, 50, 0});
	mInputBindAxis(&runner->core->inputMap, 'logi', 3, &(struct mInputAxis){GBA_KEY_L, GBA_KEY_NONE, 50, 0});
	mInputBindAxis(&runner->core->inputMap, 'logi', 4, &(struct mInputAxis){GBA_KEY_R, GBA_KEY_NONE, 50, 0});

	#ifdef HW_RVL
	mInputBindKey(&runner->core->inputMap, 'wm\0\0', __builtin_ctz(WPAD_BUTTON_2), GBA_KEY_A);
	mInputBindKey(&runner->core->inputMap, 'wm\0\0', __builtin_ctz(WPAD_BUTTON_1), GBA_KEY_B);
	mInputBindKey(&runner->core->inputMap, 'wm\0\0', __builtin_ctz(WPAD_BUTTON_MINUS), GBA_KEY_SELECT);
	mInputBindKey(&runner->core->inputMap, 'wm\0\0', __builtin_ctz(WPAD_BUTTON_PLUS), GBA_KEY_START);
	mInputBindKey(&runner->core->inputMap, 'wm\0\0', __builtin_ctz(WPAD_BUTTON_DOWN), GBA_KEY_RIGHT);
	mInputBindKey(&runner->core->inputMap, 'wm\0\0', __builtin_ctz(WPAD_BUTTON_UP), GBA_KEY_LEFT);
	mInputBindKey(&runner->core->inputMap, 'wm\0\0', __builtin_ctz(WPAD_BUTTON_RIGHT), GBA_KEY_UP);
	mInputBindKey(&runner->core->inputMap, 'wm\0\0', __builtin_ctz(WPAD_BUTTON_LEFT), GBA_KEY_DOWN);
	mInputBindKey(&runner->core->inputMap, 'wm\0\0', __builtin_ctz(WPAD_BUTTON_A), GBA_KEY_R);
	mInputBindKey(&runner->core->inputMap, 'wm\0\0', __builtin_ctz(WPAD_BUTTON_B), GBA_KEY_L);

	mInputBindKey(&runner->core->inputMap, 'wmr\0', __builtin_ctz(WPAD_BUTTON_2), GBA_KEY_A);
	mInputBindKey(&runner->core->inputMap, 'wmr\0', __builtin_ctz(WPAD_BUTTON_1), GBA_KEY_B);
	mInputBindKey(&runner->core->inputMap, 'wmr\0', __builtin_ctz(WPAD_BUTTON_MINUS), GBA_KEY_SELECT);
	mInputBindKey(&runner->core->inputMap, 'wmr\0', __builtin_ctz(WPAD_BUTTON_PLUS), GBA_KEY_START);
	mInputBindKey(&runner->core->inputMap, 'wmr\0', __builtin_ctz(WPAD_BUTTON_DOWN), GBA_KEY_LEFT);
	mInputBindKey(&runner->core->inputMap, 'wmr\0', __builtin_ctz(WPAD_BUTTON_UP), GBA_KEY_RIGHT);
	mInputBindKey(&runner->core->inputMap, 'wmr\0', __builtin_ctz(WPAD_BUTTON_RIGHT), GBA_KEY_DOWN);
	mInputBindKey(&runner->core->inputMap, 'wmr\0', __builtin_ctz(WPAD_BUTTON_LEFT), GBA_KEY_UP);
	mInputBindKey(&runner->core->inputMap, 'wmr\0', __builtin_ctz(WPAD_BUTTON_A), GBA_KEY_R);
	mInputBindKey(&runner->core->inputMap, 'wmr\0', __builtin_ctz(WPAD_BUTTON_B), GBA_KEY_L);

	mInputBindKey(&runner->core->inputMap, 'wmnc', __builtin_ctz(WPAD_BUTTON_A), GBA_KEY_A);
	mInputBindKey(&runner->core->inputMap, 'wmnc', __builtin_ctz(WPAD_BUTTON_B), GBA_KEY_B);
	mInputBindKey(&runner->core->inputMap, 'wmnc', __builtin_ctz(WPAD_BUTTON_MINUS), GBA_KEY_SELECT);
	mInputBindKey(&runner->core->inputMap, 'wmnc', __builtin_ctz(WPAD_BUTTON_PLUS), GBA_KEY_START);
	mInputBindKey(&runner->core->inputMap, 'wmnc', __builtin_ctz(WPAD_BUTTON_RIGHT), GBA_KEY_RIGHT);
	mInputBindKey(&runner->core->inputMap, 'wmnc', __builtin_ctz(WPAD_BUTTON_LEFT), GBA_KEY_LEFT);
	mInputBindKey(&runner->core->inputMap, 'wmnc', __builtin_ctz(WPAD_BUTTON_UP), GBA_KEY_UP);
	mInputBindKey(&runner->core->inputMap, 'wmnc', __builtin_ctz(WPAD_BUTTON_DOWN), GBA_KEY_DOWN);
	mInputBindKey(&runner->core->inputMap, 'wmnc', __builtin_ctz(WPAD_NUNCHUK_BUTTON_Z), GBA_KEY_R);
	mInputBindKey(&runner->core->inputMap, 'wmnc', __builtin_ctz(WPAD_NUNCHUK_BUTTON_C), GBA_KEY_L);

	mInputBindAxis(&runner->core->inputMap, 'wmnc', 0, &(struct mInputAxis){GBA_KEY_RIGHT, GBA_KEY_LEFT, +50, -50});
	mInputBindAxis(&runner->core->inputMap, 'wmnc', 1, &(struct mInputAxis){GBA_KEY_UP, GBA_KEY_DOWN, +50, -50});

	mInputBindKey(&runner->core->inputMap, 'wmcc', __builtin_ctz(CLASSIC_CTRL_BUTTON_A), GBA_KEY_A);
	mInputBindKey(&runner->core->inputMap, 'wmcc', __builtin_ctz(CLASSIC_CTRL_BUTTON_B), GBA_KEY_B);
	mInputBindKey(&runner->core->inputMap, 'wmcc', __builtin_ctz(CLASSIC_CTRL_BUTTON_MINUS), GBA_KEY_SELECT);
	mInputBindKey(&runner->core->inputMap, 'wmcc', __builtin_ctz(CLASSIC_CTRL_BUTTON_PLUS), GBA_KEY_START);
	mInputBindKey(&runner->core->inputMap, 'wmcc', __builtin_ctz(CLASSIC_CTRL_BUTTON_RIGHT), GBA_KEY_RIGHT);
	mInputBindKey(&runner->core->inputMap, 'wmcc', __builtin_ctz(CLASSIC_CTRL_BUTTON_LEFT), GBA_KEY_LEFT);
	mInputBindKey(&runner->core->inputMap, 'wmcc', __builtin_ctz(CLASSIC_CTRL_BUTTON_UP), GBA_KEY_UP);
	mInputBindKey(&runner->core->inputMap, 'wmcc', __builtin_ctz(CLASSIC_CTRL_BUTTON_DOWN), GBA_KEY_DOWN);
	mInputBindKey(&runner->core->inputMap, 'wmcc', __builtin_ctz(CLASSIC_CTRL_BUTTON_ZR), GBA_KEY_R);
	mInputBindKey(&runner->core->inputMap, 'wmcc', __builtin_ctz(CLASSIC_CTRL_BUTTON_ZL), GBA_KEY_L);

	mInputBindAxis(&runner->core->inputMap, 'wmcc', 0, &(struct mInputAxis){GBA_KEY_RIGHT, GBA_KEY_LEFT, +15, -15});
	mInputBindAxis(&runner->core->inputMap, 'wmcc', 1, &(struct mInputAxis){GBA_KEY_UP, GBA_KEY_DOWN, +15, -15});
	mInputBindAxis(&runner->core->inputMap, 'wmcc', 4, &(struct mInputAxis){GBA_KEY_L, GBA_KEY_NONE, 30, 0});
	mInputBindAxis(&runner->core->inputMap, 'wmcc', 5, &(struct mInputAxis){GBA_KEY_R, GBA_KEY_NONE, 30, 0});

	mInputBindKey(&runner->core->inputMap, 'wupc', __builtin_ctz(WIIU_PRO_CTRL_BUTTON_A), GBA_KEY_A);
	mInputBindKey(&runner->core->inputMap, 'wupc', __builtin_ctz(WIIU_PRO_CTRL_BUTTON_B), GBA_KEY_B);
	mInputBindKey(&runner->core->inputMap, 'wupc', __builtin_ctz(WIIU_PRO_CTRL_BUTTON_MINUS), GBA_KEY_SELECT);
	mInputBindKey(&runner->core->inputMap, 'wupc', __builtin_ctz(WIIU_PRO_CTRL_BUTTON_PLUS), GBA_KEY_START);
	mInputBindKey(&runner->core->inputMap, 'wupc', __builtin_ctz(WIIU_PRO_CTRL_BUTTON_RIGHT), GBA_KEY_RIGHT);
	mInputBindKey(&runner->core->inputMap, 'wupc', __builtin_ctz(WIIU_PRO_CTRL_BUTTON_LEFT), GBA_KEY_LEFT);
	mInputBindKey(&runner->core->inputMap, 'wupc', __builtin_ctz(WIIU_PRO_CTRL_BUTTON_UP), GBA_KEY_UP);
	mInputBindKey(&runner->core->inputMap, 'wupc', __builtin_ctz(WIIU_PRO_CTRL_BUTTON_DOWN), GBA_KEY_DOWN);
	mInputBindKey(&runner->core->inputMap, 'wupc', __builtin_ctz(WIIU_PRO_CTRL_BUTTON_ZR), GBA_KEY_R);
	mInputBindKey(&runner->core->inputMap, 'wupc', __builtin_ctz(WIIU_PRO_CTRL_BUTTON_ZL), GBA_KEY_L);

	mInputBindAxis(&runner->core->inputMap, 'wupc', 0, &(struct mInputAxis){GBA_KEY_RIGHT, GBA_KEY_LEFT, +500, -500});
	mInputBindAxis(&runner->core->inputMap, 'wupc', 1, &(struct mInputAxis){GBA_KEY_UP, GBA_KEY_DOWN, +500, -500});

	mInputBindKey(&runner->core->inputMap, 'nes\0', __builtin_ctz(EXTENMOTE_NES_BUTTON_A), GBA_KEY_A);
	mInputBindKey(&runner->core->inputMap, 'nes\0', __builtin_ctz(EXTENMOTE_NES_BUTTON_B), GBA_KEY_B);
	mInputBindKey(&runner->core->inputMap, 'nes\0', __builtin_ctz(EXTENMOTE_NES_BUTTON_SELECT), GBA_KEY_SELECT);
	mInputBindKey(&runner->core->inputMap, 'nes\0', __builtin_ctz(EXTENMOTE_NES_BUTTON_START), GBA_KEY_START);
	mInputBindKey(&runner->core->inputMap, 'nes\0', __builtin_ctz(EXTENMOTE_NES_BUTTON_RIGHT), GBA_KEY_RIGHT);
	mInputBindKey(&runner->core->inputMap, 'nes\0', __builtin_ctz(EXTENMOTE_NES_BUTTON_LEFT), GBA_KEY_LEFT);
	mInputBindKey(&runner->core->inputMap, 'nes\0', __builtin_ctz(EXTENMOTE_NES_BUTTON_UP), GBA_KEY_UP);
	mInputBindKey(&runner->core->inputMap, 'nes\0', __builtin_ctz(EXTENMOTE_NES_BUTTON_DOWN), GBA_KEY_DOWN);

	mInputBindKey(&runner->core->inputMap, 'snes', __builtin_ctz(EXTENMOTE_SNES_BUTTON_A), GBA_KEY_A);
	mInputBindKey(&runner->core->inputMap, 'snes', __builtin_ctz(EXTENMOTE_SNES_BUTTON_B), GBA_KEY_B);
	mInputBindKey(&runner->core->inputMap, 'snes', __builtin_ctz(EXTENMOTE_SNES_BUTTON_SELECT), GBA_KEY_SELECT);
	mInputBindKey(&runner->core->inputMap, 'snes', __builtin_ctz(EXTENMOTE_SNES_BUTTON_START), GBA_KEY_START);
	mInputBindKey(&runner->core->inputMap, 'snes', __builtin_ctz(EXTENMOTE_SNES_BUTTON_RIGHT), GBA_KEY_RIGHT);
	mInputBindKey(&runner->core->inputMap, 'snes', __builtin_ctz(EXTENMOTE_SNES_BUTTON_LEFT), GBA_KEY_LEFT);
	mInputBindKey(&runner->core->inputMap, 'snes', __builtin_ctz(EXTENMOTE_SNES_BUTTON_UP), GBA_KEY_UP);
	mInputBindKey(&runner->core->inputMap, 'snes', __builtin_ctz(EXTENMOTE_SNES_BUTTON_DOWN), GBA_KEY_DOWN);
	mInputBindKey(&runner->core->inputMap, 'snes', __builtin_ctz(EXTENMOTE_SNES_BUTTON_R), GBA_KEY_R);
	mInputBindKey(&runner->core->inputMap, 'snes', __builtin_ctz(EXTENMOTE_SNES_BUTTON_L), GBA_KEY_L);
	#endif

	mInputBindKey(&runner->core->inputMap, 'n64\0', __builtin_ctz(N64_BUTTON_A), GBA_KEY_A);
	mInputBindKey(&runner->core->inputMap, 'n64\0', __builtin_ctz(N64_BUTTON_B), GBA_KEY_B);
	mInputBindKey(&runner->core->inputMap, 'n64\0', __builtin_ctz(N64_BUTTON_Z), GBA_KEY_SELECT);
	mInputBindKey(&runner->core->inputMap, 'n64\0', __builtin_ctz(N64_BUTTON_START), GBA_KEY_START);
	mInputBindKey(&runner->core->inputMap, 'n64\0', __builtin_ctz(N64_BUTTON_RIGHT), GBA_KEY_RIGHT);
	mInputBindKey(&runner->core->inputMap, 'n64\0', __builtin_ctz(N64_BUTTON_LEFT), GBA_KEY_LEFT);
	mInputBindKey(&runner->core->inputMap, 'n64\0', __builtin_ctz(N64_BUTTON_UP), GBA_KEY_UP);
	mInputBindKey(&runner->core->inputMap, 'n64\0', __builtin_ctz(N64_BUTTON_DOWN), GBA_KEY_DOWN);
	mInputBindKey(&runner->core->inputMap, 'n64\0', __builtin_ctz(N64_BUTTON_R), GBA_KEY_R);
	mInputBindKey(&runner->core->inputMap, 'n64\0', __builtin_ctz(N64_BUTTON_L), GBA_KEY_L);

	mInputBindAxis(&runner->core->inputMap, 'n64\0', 0, &(struct mInputAxis){GBA_KEY_RIGHT, GBA_KEY_LEFT, +40, -40});
	mInputBindAxis(&runner->core->inputMap, 'n64\0', 1, &(struct mInputAxis){GBA_KEY_UP, GBA_KEY_DOWN, +40, -40});

	mInputBindKey(&runner->core->inputMap, '3ds\0', __builtin_ctz(CTR_BUTTON_A), GBA_KEY_A);
	mInputBindKey(&runner->core->inputMap, '3ds\0', __builtin_ctz(CTR_BUTTON_B), GBA_KEY_B);
	mInputBindKey(&runner->core->inputMap, '3ds\0', __builtin_ctz(CTR_BUTTON_SELECT), GBA_KEY_SELECT);
	mInputBindKey(&runner->core->inputMap, '3ds\0', __builtin_ctz(CTR_BUTTON_START), GBA_KEY_START);
	mInputBindKey(&runner->core->inputMap, '3ds\0', __builtin_ctz(CTR_BUTTON_RIGHT), GBA_KEY_RIGHT);
	mInputBindKey(&runner->core->inputMap, '3ds\0', __builtin_ctz(CTR_BUTTON_LEFT), GBA_KEY_LEFT);
	mInputBindKey(&runner->core->inputMap, '3ds\0', __builtin_ctz(CTR_BUTTON_UP), GBA_KEY_UP);
	mInputBindKey(&runner->core->inputMap, '3ds\0', __builtin_ctz(CTR_BUTTON_DOWN), GBA_KEY_DOWN);
	mInputBindKey(&runner->core->inputMap, '3ds\0', __builtin_ctz(CTR_BUTTON_R), GBA_KEY_R);
	mInputBindKey(&runner->core->inputMap, '3ds\0', __builtin_ctz(CTR_BUTTON_L), GBA_KEY_L);

	mInputBindAxis(&runner->core->inputMap, '3ds\0', 0, &(struct mInputAxis){GBA_KEY_RIGHT, GBA_KEY_LEFT, +40, -40});
	mInputBindAxis(&runner->core->inputMap, '3ds\0', 1, &(struct mInputAxis){GBA_KEY_UP, GBA_KEY_DOWN, +40, -40});
}

static void _gameUnloaded(struct mGUIRunner *runner)
{
	free(outputBuffer);
	outputBuffer = NULL;

	GX_DrawDone();

	GXFreeSurface(&convert_surface);
	GXFreeSurface(&packed_surface);
	GXFreeSurface(&planar_surface);
	GXFreeSurface(&prescale_surface);
}

static void _prepareForFrame(struct mGUIRunner *runner)
{
	state.rotation = default_state.rotation;

	if (state.reset) {
		state.reset = SYS_ResetButtonDown();

		if (!state.reset) {
			if (wiiload.task.type == TYPE_MB)
				runner->core->loadROM(runner->core, VFileFromMemory(wiiload.task.buf, wiiload.task.buflen));
			runner->core->reset(runner->core);
		}
	}
}

static void _drawFrame(struct mGUIRunner *runner, bool faded)
{
	unsigned width, height;
	runner->core->currentVideoSize(runner->core, &width, &height);

	rect_t planar_src   = {0, 0, width, height};
	rect_t prescale_src = {0, 0, planar_src.w * state.scale, planar_src.h * state.scale};
	rect_t prescale_dst = GXPrescaleGetRect(prescale_src.w, prescale_src.h);

	prescale_surface.rect = state.filter_prescale ? prescale_dst : prescale_src;
	packed_surface.rect = planar_surface.rect = prescale_src;
	convert_surface.rect = planar_src;

	GBAVideoConvertBGR5(*convert_surface.buf, outputBuffer, width, height);
	convert_surface.dirty = true;

	switch (state.filter) {
		case FILTER_BLEND:
			GXPlanarApplyBlend(&planar_surface, &convert_surface);
			break;
		case FILTER_DEFLICKER:
			GXPlanarApplyDeflicker(&planar_surface, &convert_surface);
			break;
		case FILTER_ACCUMULATE:
			GXPackedApplyMix(&packed_surface, &convert_surface);
			GXPlanarApply(&planar_surface, &packed_surface);
			break;
		case FILTER_SCALE2XEX:
			GXPackedApplyYUV(&packed_surface, &convert_surface);
			GXPlanarApplyScale2xEx(&planar_surface, &convert_surface, &packed_surface);
			break;
		case FILTER_SCALE2XPLUS:
			GXPlanarApplyScale2x(&planar_surface, &convert_surface, true);
			break;
		case FILTER_SCALE2X:
			GXPlanarApplyScale2x(&planar_surface, &convert_surface, false);
			break;
		case FILTER_EAGLE2X:
			GXPlanarApplyEagle2x(&planar_surface, &convert_surface);
			break;
		case FILTER_SCAN2X:
			GXPlanarApplyScan2x(&planar_surface, &convert_surface, false);
			break;
		default:
			GXPlanarApply(&planar_surface, &convert_surface);
	}

	if (faded) {
		gx_surface_t *planar_surfaces[GX_MAX_TEXMAP] = {&planar_surface};
		uint8_t planar_alpha[GX_MAX_TEXMAP] = {0xC0};
		uint32_t planar_count = 1;

		switch (state.dither) {
			case DITHER_NONE:
				GXPrescaleApplyBlend(&prescale_surface, planar_surfaces, planar_alpha, planar_count);
				break;
			case DITHER_THRESHOLD:
			case DITHER_BAYER2x2:
				GXPrescaleApplyBlendDitherFast(&prescale_surface, planar_surfaces, planar_alpha, planar_count);
				break;
			default:
				GXPrescaleApplyBlendDither(&prescale_surface, planar_surfaces, planar_alpha, planar_count);
		}
	} else {
		switch (state.dither) {
			case DITHER_NONE:
				GXPrescaleApply(&prescale_surface, &planar_surface);
				break;
			case DITHER_THRESHOLD:
			case DITHER_BAYER2x2:
				GXPrescaleApplyDitherFast(&prescale_surface, &planar_surface);
				break;
			default:
				GXPrescaleApplyDither(&prescale_surface, &planar_surface);
		}
	}

	GX_BeginDispList(displist[0], GX_FIFO_MINSIZE);

	GX_SetViewportJitter(viewport.x + state.offset.x, viewport.y + state.offset.y, viewport.w, viewport.h, 0., 1.,
		rmode.field_rendering ? state.field : viewport.h % 2);

	GXPreviewDrawRect(prescale_surface.obj, convert_surface.rect, prescale_surface.rect);

	dispsize[0] = GX_EndDispList();

	GX_BeginDispList(displist[1], GX_FIFO_MINSIZE);

	GXOverlayDrawRect((rect_t){0, 0, GBA_VIDEO_HORIZONTAL_PIXELS, GBA_VIDEO_VERTICAL_PIXELS});

	dispsize[1] = GX_EndDispList();
}

static void _drawScreenshot(struct mGUIRunner *runner, const mColor *pixels, unsigned width, unsigned height, bool faded)
{
	rect_t planar_src   = {0, 0, width, height};
	rect_t prescale_src = {0, 0, planar_src.w * state.scale, planar_src.h * state.scale};
	rect_t prescale_dst = GXPrescaleGetRect(prescale_src.w, prescale_src.h);

	prescale_surface.rect = state.filter_prescale ? prescale_dst : prescale_src;
	packed_surface.rect = planar_surface.rect = prescale_src;
	convert_surface.rect = planar_src;

	GBAVideoConvertBGR5(*convert_surface.buf, (void *)pixels, width, height);
	convert_surface.dirty = true;

	switch (state.filter) {
		case FILTER_BLEND:
			GXPlanarApplyBlend(&planar_surface, &convert_surface);
			break;
		case FILTER_DEFLICKER:
			GXPlanarApplyDeflicker(&planar_surface, &convert_surface);
			break;
		case FILTER_ACCUMULATE:
			GXPackedApplyMix(&packed_surface, &convert_surface);
			GXPlanarApply(&planar_surface, &packed_surface);
			break;
		case FILTER_SCALE2XEX:
			GXPackedApplyYUV(&packed_surface, &convert_surface);
			GXPlanarApplyScale2xEx(&planar_surface, &convert_surface, &packed_surface);
			break;
		case FILTER_SCALE2XPLUS:
			GXPlanarApplyScale2x(&planar_surface, &convert_surface, true);
			break;
		case FILTER_SCALE2X:
			GXPlanarApplyScale2x(&planar_surface, &convert_surface, false);
			break;
		case FILTER_EAGLE2X:
			GXPlanarApplyEagle2x(&planar_surface, &convert_surface);
			break;
		case FILTER_SCAN2X:
			GXPlanarApplyScan2x(&planar_surface, &convert_surface, false);
			break;
		default:
			GXPlanarApply(&planar_surface, &convert_surface);
	}

	if (faded) {
		gx_surface_t *planar_surfaces[GX_MAX_TEXMAP] = {&planar_surface};
		uint8_t planar_alpha[GX_MAX_TEXMAP] = {0xC0};
		uint32_t planar_count = 1;

		switch (state.dither) {
			case DITHER_NONE:
				GXPrescaleApplyBlend(&prescale_surface, planar_surfaces, planar_alpha, planar_count);
				break;
			case DITHER_THRESHOLD:
			case DITHER_BAYER2x2:
				GXPrescaleApplyBlendDitherFast(&prescale_surface, planar_surfaces, planar_alpha, planar_count);
				break;
			default:
				GXPrescaleApplyBlendDither(&prescale_surface, planar_surfaces, planar_alpha, planar_count);
		}
	} else {
		switch (state.dither) {
			case DITHER_NONE:
				GXPrescaleApply(&prescale_surface, &planar_surface);
				break;
			case DITHER_THRESHOLD:
			case DITHER_BAYER2x2:
				GXPrescaleApplyDitherFast(&prescale_surface, &planar_surface);
				break;
			default:
				GXPrescaleApplyDither(&prescale_surface, &planar_surface);
		}
	}

	GX_BeginDispList(displist[0], GX_FIFO_MINSIZE);

	GX_SetViewportJitter(viewport.x + state.offset.x, viewport.y + state.offset.y, viewport.w, viewport.h, 0., 1.,
		rmode.field_rendering ? state.field : viewport.h % 2);

	GXPreviewDrawRect(prescale_surface.obj, convert_surface.rect, prescale_surface.rect);

	dispsize[0] = GX_EndDispList();

	GX_BeginDispList(displist[1], GX_FIFO_MINSIZE);

	GXOverlayDrawRect((rect_t){0, 0, GBA_VIDEO_HORIZONTAL_PIXELS, GBA_VIDEO_VERTICAL_PIXELS});

	dispsize[1] = GX_EndDispList();
}

static void _paused(struct mGUIRunner *runner)
{
	state.draw_osd = true;

	SYS_CancelAlarm(watchdog);
	VIDEO_SetPostRetraceCallback(vsync_cb);

	gxclock.reset = true;
	gxclock.hz = viclock.hz;
}

static void _unpaused(struct mGUIRunner *runner)
{
	unsigned mode;
	float fps;
	bool sync, interframeBlending;

	state.draw_osd = false;

	if (mCoreConfigGetUIntValue(&runner->config, "screenMode", &mode))
		_updateScreenMode(&runner->params, mode);

	if (mCoreConfigGetBoolValue(&runner->config, "interframeBlending", &interframeBlending))
		if (state.filter < FILTER_DEFLICKER)
			state.filter = interframeBlending;

	if (mCoreConfigGetBoolValue(&runner->config, "videoSync", &sync) && !sync) {
		if (mCoreConfigGetFloatValue(&runner->config, "fpsTarget", &fps)) {
			struct timespec tv;

			tv.tv_sec  = 0;
			tv.tv_nsec = TB_NSPERSEC / fps;

			gxclock.reset = true;
			gxclock.hz = fps;

			VIDEO_SetPostRetraceCallback(NULL);
			SYS_SetPeriodicAlarm(watchdog, &tv, &tv, alarm_cb, NULL);
		}
	} else {
		gxclock.reset = true;
		gxclock.hz = viclock.hz;
	}

	fpsRatio = ASND_GetAudioRate() / (mCoreCalculateFramerateRatio(runner->core, gxclock.hz) * aiclock.hz);
	_audioRateChanged(&stream, runner->core->audioSampleRate(runner->core));
}

static void _incrementScreenMode(struct mGUIRunner *runner)
{
	unsigned mode = 0;

	mCoreConfigGetUIntValue(&runner->config, "screenMode", &mode);
	mode = (mode + 1) % SM_MAX;
	_updateScreenMode(&runner->params, mode);

	mCoreConfigSetUIntValue(&runner->config, "screenMode", mode);
}

static void _setFrameLimiter(struct mGUIRunner *runner, bool limit)
{
	state.draw_wait = limit;
}

static uint16_t _pollGameInput(struct mGUIRunner *runner)
{
	uint16_t keys = 0;

	for (int chan = 0; chan < SI_MAX_CHAN; chan++) {
		if (gc_controller.data[chan].barrel) {
			if (gc_controller.data[chan].trigger.l == 0xFF &&
				gc_controller.data[chan].trigger.r == 0xFF) {
				keys |= mInputMapKeyBits(&runner->core->inputMap, 'act\0', gc_controller.data[chan].held, 0);
				continue;
			} else if (gc_controller.data[chan].trigger.l == 0x00) {
				keys |= mInputMapKeyBits(&runner->core->inputMap, 'dk\0\0', gc_controller.data[chan].held, 0);
				keys |= 1 << mInputMapAxis(&runner->core->inputMap, 'dk\0\0', 0, gc_controller.data[chan].trigger.r);
				continue;
			}
		}

		keys |= mInputMapKeyBits(&runner->core->inputMap, 'gc\0\0', gc_controller.data[chan].held, 0);
		keys |= 1 << mInputMapAxis(&runner->core->inputMap, 'gc\0\0', 0, gc_controller.data[chan].stick.x);
		keys |= 1 << mInputMapAxis(&runner->core->inputMap, 'gc\0\0', 1, gc_controller.data[chan].stick.y);
		keys |= 1 << mInputMapAxis(&runner->core->inputMap, 'gc\0\0', 2, gc_controller.data[chan].substick.x);
		keys |= 1 << mInputMapAxis(&runner->core->inputMap, 'gc\0\0', 3, gc_controller.data[chan].substick.y);
		keys |= 1 << mInputMapAxis(&runner->core->inputMap, 'gc\0\0', 4, gc_controller.data[chan].trigger.l);
		keys |= 1 << mInputMapAxis(&runner->core->inputMap, 'gc\0\0', 5, gc_controller.data[chan].trigger.r);
		keys |= 1 << mInputMapAxis(&runner->core->inputMap, 'gc\0\0', 6, gc_controller.data[chan].button.a);
		keys |= 1 << mInputMapAxis(&runner->core->inputMap, 'gc\0\0', 7, gc_controller.data[chan].button.b);
	}

	for (int chan = 0; chan < SI_MAX_CHAN; chan++) {
		keys |= mInputMapKeyBits(&runner->core->inputMap, 'logi', gc_steering.data[chan].held, 0);
		keys |= 1 << mInputMapAxis(&runner->core->inputMap, 'logi', 0, gc_steering.data[chan].wheel);
		keys |= 1 << mInputMapAxis(&runner->core->inputMap, 'logi', 1, gc_steering.data[chan].pedal.l);
		keys |= 1 << mInputMapAxis(&runner->core->inputMap, 'logi', 2, gc_steering.data[chan].pedal.r);
		keys |= 1 << mInputMapAxis(&runner->core->inputMap, 'logi', 3, gc_steering.data[chan].paddle.l);
		keys |= 1 << mInputMapAxis(&runner->core->inputMap, 'logi', 4, gc_steering.data[chan].paddle.r);
	}

	for (int chan = 0; chan < SI_MAX_CHAN; chan++) {
		keys |= mInputMapKeyBits(&runner->core->inputMap, 'n64\0', n64_controller.data[chan].held, 0);
		keys |= 1 << mInputMapAxis(&runner->core->inputMap, 'n64\0', 0, n64_controller.data[chan].stick.x);
		keys |= 1 << mInputMapAxis(&runner->core->inputMap, 'n64\0', 1, n64_controller.data[chan].stick.y);
	}

	#ifdef HW_RVL
	for (int chan = 0; chan < WPAD_MAX_WIIMOTES; chan++) {
		WPADData *data = WPAD_Data(chan);

		if (!data->ir.raw_valid && data->exp.type == WPAD_EXP_NONE) {
			if (data->orient.roll <= 0.)
				keys |= mInputMapKeyBits(&runner->core->inputMap, 'wm\0\0', data->btns_h & 0xFFFF, 0);
			else
				keys |= mInputMapKeyBits(&runner->core->inputMap, 'wmr\0', data->btns_h & 0xFFFF, 0);
		} else {
			keys |= mInputMapKeyBits(&runner->core->inputMap, 'wmnc', data->btns_h & 0xFFFF, 0);

			switch (data->exp.type) {
				case WPAD_EXP_NUNCHUK:
					keys |= mInputMapKeyBits(&runner->core->inputMap, 'wmnc', data->btns_h, 16);
					keys |= 1 << mInputMapAxis(&runner->core->inputMap, 'wmnc', 0, data->exp.nunchuk.js.pos.x - data->exp.nunchuk.js.center.x);
					keys |= 1 << mInputMapAxis(&runner->core->inputMap, 'wmnc', 1, data->exp.nunchuk.js.pos.y - data->exp.nunchuk.js.center.y);
					break;
				case WPAD_EXP_CLASSIC:
					keys |= mInputMapKeyBits(&runner->core->inputMap, 'wmcc', data->exp.classic.btns, 0);
					keys |= 1 << mInputMapAxis(&runner->core->inputMap, 'wmcc', 0, data->exp.classic.ljs.pos.x - data->exp.classic.ljs.center.x);
					keys |= 1 << mInputMapAxis(&runner->core->inputMap, 'wmcc', 1, data->exp.classic.ljs.pos.y - data->exp.classic.ljs.center.y);
					keys |= 1 << mInputMapAxis(&runner->core->inputMap, 'wmcc', 2, data->exp.classic.rjs.pos.x - data->exp.classic.rjs.center.x);
					keys |= 1 << mInputMapAxis(&runner->core->inputMap, 'wmcc', 3, data->exp.classic.rjs.pos.y - data->exp.classic.rjs.center.y);
					keys |= 1 << mInputMapAxis(&runner->core->inputMap, 'wmcc', 4, data->exp.classic.ls_raw);
					keys |= 1 << mInputMapAxis(&runner->core->inputMap, 'wmcc', 5, data->exp.classic.rs_raw);
					break;
				case WPAD_EXP_WIIUPRO:
					keys |= mInputMapKeyBits(&runner->core->inputMap, 'wupc', data->exp.wup.btns, 0);
					keys |= 1 << mInputMapAxis(&runner->core->inputMap, 'wupc', 0, data->exp.wup.ljs.pos.x - data->exp.wup.ljs.center.x);
					keys |= 1 << mInputMapAxis(&runner->core->inputMap, 'wupc', 1, data->exp.wup.ljs.pos.y - data->exp.wup.ljs.center.y);
					keys |= 1 << mInputMapAxis(&runner->core->inputMap, 'wupc', 2, data->exp.wup.rjs.pos.x - data->exp.wup.rjs.center.x);
					keys |= 1 << mInputMapAxis(&runner->core->inputMap, 'wupc', 3, data->exp.wup.rjs.pos.y - data->exp.wup.rjs.center.y);
					break;
				case WPAD_EXP_NES:
					keys |= mInputMapKeyBits(&runner->core->inputMap, 'nes\0', data->exp.nes.btns, 0);
					break;
				case WPAD_EXP_SNES:
					keys |= mInputMapKeyBits(&runner->core->inputMap, 'snes', data->exp.snes.btns, 0);
					break;
				case WPAD_EXP_N64:
					keys |= mInputMapKeyBits(&runner->core->inputMap, 'n64\0', data->exp.n64.btns, 0);
					keys |= 1 << mInputMapAxis(&runner->core->inputMap, 'n64\0', 0, data->exp.n64.js.pos.x - data->exp.n64.js.center.x);
					keys |= 1 << mInputMapAxis(&runner->core->inputMap, 'n64\0', 1, data->exp.n64.js.pos.y - data->exp.n64.js.center.y);
					break;
				case WPAD_EXP_GC:
					keys |= mInputMapKeyBits(&runner->core->inputMap, 'gc\0\0', data->exp.gc.btns, 0);
					keys |= 1 << mInputMapAxis(&runner->core->inputMap, 'gc\0\0', 0, data->exp.gc.ljs.pos.x - data->exp.gc.ljs.center.x);
					keys |= 1 << mInputMapAxis(&runner->core->inputMap, 'gc\0\0', 1, data->exp.gc.ljs.pos.y - data->exp.gc.ljs.center.y);
					keys |= 1 << mInputMapAxis(&runner->core->inputMap, 'gc\0\0', 2, data->exp.gc.rjs.pos.x - data->exp.gc.rjs.center.x);
					keys |= 1 << mInputMapAxis(&runner->core->inputMap, 'gc\0\0', 3, data->exp.gc.rjs.pos.y - data->exp.gc.rjs.center.y);
					keys |= 1 << mInputMapAxis(&runner->core->inputMap, 'gc\0\0', 4, data->exp.gc.ls_raw);
					keys |= 1 << mInputMapAxis(&runner->core->inputMap, 'gc\0\0', 5, data->exp.gc.rs_raw);
					break;
			}
		}
	}
	#endif

	keys |= mInputMapKeyBits(&runner->core->inputMap, '3ds\0', ctr.data.held, 0);
	keys |= 1 << mInputMapAxis(&runner->core->inputMap, '3ds\0', 0, ctr.data.stick.x);
	keys |= 1 << mInputMapAxis(&runner->core->inputMap, '3ds\0', 1, ctr.data.stick.y);
	keys |= 1 << mInputMapAxis(&runner->core->inputMap, '3ds\0', 2, ctr.data.substick.x);
	keys |= 1 << mInputMapAxis(&runner->core->inputMap, '3ds\0', 3, ctr.data.substick.y);

	if (ctr.data.held & CTR_TOUCH) {
		keys |= 1 << mInputMapAxis(&runner->core->inputMap, '3ds\0', 4, ctr.data.touch.x);
		keys |= 1 << mInputMapAxis(&runner->core->inputMap, '3ds\0', 5, ctr.data.touch.y);
	}

	return keys;
}

static void preinit(int argc, char **argv)
{
	for (int chan = 0; chan < EXI_CHANNEL_2; chan++)
		CON_EnableGecko(chan, false);

	puts("Enhanced mGBA © 2015-2025 Extrems' Corner.org");

	if (fatInitDefault()) {
		mkdir("/mGBA", 0755);
		chdir("/mGBA");
	}

	ASND_SetCallback(asndlib_cb);
	ASND_Init();
	VIDEO_Init();

	uint32_t tvMode = VIDEO_GetCurrentTvMode(), viMode = VIDEO_GetCurrentViMode(), xfbMode;

	switch (SYSCONF_GetVideoMode()) {
		case SYSCONF_VIDEO_NTSC:
			tvMode = VI_NTSC;
			break;
		case SYSCONF_VIDEO_PAL:
			tvMode = VI_EURGB60;
			break;
		case SYSCONF_VIDEO_MPAL:
			tvMode = VIDEO_HaveComponentCable() ? VI_NTSC : VI_MPAL;
			break;
	}

	if ((SYSCONF_GetProgressiveScan() && VIDEO_HaveComponentCable()) ||
		viMode == (VI_MONO | VI_NON_INTERLACE | VI_ENHANCED | VI_CLOCK_54MHZ)) {
		viMode  =  VI_MONO | VI_NON_INTERLACE | VI_ENHANCED | VI_CLOCK_54MHZ;
		xfbMode =  VI_XFBMODE_SF;
		state.aspect.w = 16.;
		state.aspect.h = 9.;
	#ifdef HW_DOL
	} else if (VIDEO_HaveComponentCable()) {
		viMode  = VI_STEREO | VI_INTERLACE | VI_STANDARD | VI_CLOCK_54MHZ;
		xfbMode = VI_XFBMODE_DF;
	#endif
	} else {
		viMode  = VI_MONO | VI_INTERLACE | VI_STANDARD | VI_CLOCK_27MHZ;
		xfbMode = VI_XFBMODE_DF;
	}

	#ifdef HW_RVL
	switch (CONF_GetAspectRatio()) {
		case CONF_ASPECT_4_3:
			VIDEO_SetAspectRatio(VI_DISPLAY_GAMEPAD, VI_ASPECT_3_4);
			VIDEO_SetAspectRatio(VI_DISPLAY_TV,      VI_ASPECT_1_1);
			state.aspect.w = 4.;
			state.aspect.h = 3.;
			break;
		case CONF_ASPECT_16_9:
			VIDEO_SetAspectRatio(VI_DISPLAY_BOTH, VI_ASPECT_1_1);
			state.aspect.w = 16.;
			state.aspect.h = 9.;
			break;
	}
	#endif

	state.offset.x = SYSCONF_GetDisplayOffsetH();
	state.zoom_ratio = GBPGetScreenSize() ? .875 : .75;
	state.overlay_id = GBPGetFrame();
	state.filter = GBPGetScreenFilter();

	enum {
		OPT_ASPECT = 0x80,
		OPT_OFFSET,
		OPT_ZOOM,
		OPT_ZOOM_AUTO,
		OPT_ROTATE,
		OPT_POLL,
		OPT_CURSOR,
		OPT_NO_CURSOR,
		OPT_OVERLAY,
		OPT_NO_OVERLAY,
		OPT_OVERLAY_ID,
		OPT_OVERLAY_SCALE,
		OPT_FILTER,
		OPT_DITHER,
		OPT_SCALER,
		OPT_PROFILE_INTENT,
		OPT_PROFILE,
		OPT_MATRIX,
		OPT_INPUT_GAMMA,
		OPT_INPUT_ALPHA,
		OPT_OUTPUT_GAMMA,
		OPT_BRIGHTNESS,
		OPT_CONTRAST,
		OPT_FORMAT,
		OPT_SCAN_MODE,
		OPT_IPV4_ADDRESS,
		OPT_IPV4_GATEWAY,
		OPT_IPV4_NETMASK,
		OPT_NETWORK,
		OPT_NO_NETWORK,
	};
	int optc, longind;
	static struct option longopts[] = {
		{ "aspect",          required_argument, NULL, OPT_ASPECT        },
		{ "offset",          required_argument, NULL, OPT_OFFSET        },
		{ "zoom",            required_argument, NULL, OPT_ZOOM          },
		{ "zoom-auto",       optional_argument, NULL, OPT_ZOOM_AUTO     },
		{ "rotate",          required_argument, NULL, OPT_ROTATE        },
		{ "poll",            required_argument, NULL, OPT_POLL          },
		{ "cursor",          required_argument, NULL, OPT_CURSOR        },
		{ "no-cursor",       no_argument,       NULL, OPT_NO_CURSOR     },
		{ "overlay",         required_argument, NULL, OPT_OVERLAY       },
		{ "no-overlay",      no_argument,       NULL, OPT_NO_OVERLAY    },
		{ "overlay-id",      required_argument, NULL, OPT_OVERLAY_ID    },
		{ "overlay-scale",   required_argument, NULL, OPT_OVERLAY_SCALE },
		{ "filter",          required_argument, NULL, OPT_FILTER        },
		{ "dither",          required_argument, NULL, OPT_DITHER        },
		{ "scaler",          required_argument, NULL, OPT_SCALER        },
		{ "profile-intent",  required_argument, NULL, OPT_PROFILE_INTENT  },
		{ "profile",         required_argument, NULL, OPT_PROFILE       },
		{ "matrix",          required_argument, NULL, OPT_MATRIX          },
		{ "input-gamma",     required_argument, NULL, OPT_INPUT_GAMMA   },
		{ "input-alpha",     required_argument, NULL, OPT_INPUT_ALPHA     },
		{ "output-gamma",    required_argument, NULL, OPT_OUTPUT_GAMMA  },
		{ "brightness",      required_argument, NULL, OPT_BRIGHTNESS    },
		{ "contrast",        required_argument, NULL, OPT_CONTRAST      },
		{ "format",          required_argument, NULL, OPT_FORMAT        },
		{ "scan-mode",       required_argument, NULL, OPT_SCAN_MODE     },
		{ "ipv4-address",    required_argument, NULL, OPT_IPV4_ADDRESS  },
		{ "ipv4-gateway",    required_argument, NULL, OPT_IPV4_GATEWAY  },
		{ "ipv4-netmask",    required_argument, NULL, OPT_IPV4_NETMASK  },
		{ "network",         no_argument,       NULL, OPT_NETWORK       },
		{ "no-network",      no_argument,       NULL, OPT_NO_NETWORK    },
		{ NULL }
	};
	while ((optc = getopt_long(argc, argv, "-", longopts, &longind)) != EOF) {
		switch (optc) {
			case 1:
				state.path = optarg;
				break;
			case OPT_ASPECT:
				switch (sscanf(optarg, "%g:%g",
							&state.aspect.w, &state.aspect.h)) {
					case 1: state.aspect.h = 1.;
				}
				break;
			case OPT_OFFSET:
				switch (sscanf(optarg, "%g:%g",
							&state.offset.x, &state.offset.y)) {
					case 1: state.offset.y = 0.;
				}
				break;
			case OPT_ZOOM:
				switch (sscanf(optarg, "%g:%g",
							&state.zoom.x, &state.zoom.y)) {
					case 1: state.zoom.y = state.zoom.x;
					case 2: state.zoom_auto = false;
				}
				break;
			case OPT_ZOOM_AUTO:
				if (optarg)
					state.zoom_ratio = strtod(optarg, NULL);
				state.zoom_auto = true;
				break;
			case OPT_ROTATE:
				state.rotation = strtod(optarg, NULL);
				break;
			case OPT_POLL:
				state.poll = strtoul(optarg, NULL, 10);
				break;
			case OPT_CURSOR:
				state.cursor = optarg;
				break;
			case OPT_NO_CURSOR:
				state.cursor = NULL;
				break;
			case OPT_OVERLAY:
				state.overlay = optarg;
				break;
			case OPT_NO_OVERLAY:
				state.overlay = NULL;
				break;
			case OPT_OVERLAY_ID:
				state.overlay_id = strtoul(optarg, NULL, 10);
				break;
			case OPT_OVERLAY_SCALE:
				switch (sscanf(optarg, "%g:%g",
							&state.overlay_scale.x, &state.overlay_scale.y)) {
					case 1: state.overlay_scale.y = state.overlay_scale.x;
				}
				break;
			case OPT_FILTER:
			{
				enum {
					FILTER_PRESCALE = FILTER_MAX,
					FILTER_NO_PRESCALE,
				};
				char *options = optarg, *value;
				static char *tokens[] = {
					[FILTER_NONE]        = "none",
					[FILTER_BLEND]       = "blend",
					[FILTER_DEFLICKER]   = "deflicker",
					[FILTER_ACCUMULATE]  = "accumulate",
					[FILTER_SCALE2XEX]   = "scale2xex",
					[FILTER_SCALE2XPLUS] = "scale2xplus",
					[FILTER_SCALE2X]     = "scale2x",
					[FILTER_EAGLE2X]     = "eagle2x",
					[FILTER_SCAN2X]      = "scan2x",
					[FILTER_NORMAL2X]    = "normal2x",
					[FILTER_PRESCALE]    = "prescale",
					[FILTER_NO_PRESCALE] = "no-prescale",
					NULL
				};
				while (*options) {
					switch (getsubopt(&options, tokens, &value)) {
						case FILTER_NONE:
							state.scale  = 1;
							state.filter = FILTER_NONE;
							break;
						case FILTER_BLEND:
							if (value) {
								switch (sscanf(value, "%g:%g:%g",
											&state.filter_weight[0], &state.filter_weight[1], &state.filter_weight[2])) {
									case 1: state.filter_weight[1] = state.filter_weight[0];
									case 2: state.filter_weight[2] = state.filter_weight[0];
								}
							}
							state.scale  = 1;
							state.filter = FILTER_BLEND;
							break;
						case FILTER_DEFLICKER:
							if (value) {
								switch (sscanf(value, "%g:%g:%g",
											&state.filter_weight[0], &state.filter_weight[1], &state.filter_weight[2])) {
									case 1: state.filter_weight[1] = state.filter_weight[0];
									case 2: state.filter_weight[2] = state.filter_weight[0];
								}
							}
							state.scale  = 1;
							state.filter = FILTER_DEFLICKER;
							break;
						case FILTER_ACCUMULATE:
							if (value) {
								switch (sscanf(value, "%g:%g:%g",
											&state.filter_weight[0], &state.filter_weight[1], &state.filter_weight[2])) {
									case 1: state.filter_weight[1] = state.filter_weight[0];
									case 2: state.filter_weight[2] = state.filter_weight[0];
								}
							}
							state.scale  = 1;
							state.filter = FILTER_ACCUMULATE;
							break;
						case FILTER_SCALE2XEX:
							state.scale  = 2;
							state.filter = FILTER_SCALE2XEX;
							break;
						case FILTER_SCALE2XPLUS:
							state.scale  = 2;
							state.filter = FILTER_SCALE2XPLUS;
							break;
						case FILTER_SCALE2X:
							state.scale  = 2;
							state.filter = FILTER_SCALE2X;
							break;
						case FILTER_EAGLE2X:
							state.scale  = 2;
							state.filter = FILTER_EAGLE2X;
							break;
						case FILTER_SCAN2X:
							state.scale  = 2;
							state.filter = FILTER_SCAN2X;
							break;
						case FILTER_NORMAL2X:
							state.scale  = 2;
							state.filter = FILTER_NORMAL2X;
							break;
						case FILTER_PRESCALE:
							state.filter_prescale = true;
							break;
						case FILTER_NO_PRESCALE:
							state.filter_prescale = false;
							break;
					}
				}
				break;
			}
			case OPT_DITHER:
				if (strcmp(optarg, "none") == 0)
					state.dither = DITHER_NONE;
				else if (strcmp(optarg, "threshold") == 0)
					state.dither = DITHER_THRESHOLD;
				else if (strcmp(optarg, "bayer8x8") == 0)
					state.dither = DITHER_BAYER8x8;
				else if (strcmp(optarg, "bayer4x4") == 0)
					state.dither = DITHER_BAYER4x4;
				else if (strcmp(optarg, "bayer2x2") == 0)
					state.dither = DITHER_BAYER2x2;
				else if (strcmp(optarg, "cluster8x8") == 0)
					state.dither = DITHER_CLUSTER8x8;
				else if (strcmp(optarg, "cluster4x4") == 0)
					state.dither = DITHER_CLUSTER4x4;
				break;
			case OPT_SCALER:
				if (strcmp(optarg, "nearest") == 0)
					state.scaler = SCALER_NEAREST;
				else if (strcmp(optarg, "bilinear") == 0)
					state.scaler = SCALER_BILINEAR;
				else if (strcmp(optarg, "area") == 0)
					state.scaler = SCALER_AREA;
				else if (strcmp(optarg, "box") == 0)
					state.scaler = SCALER_BOX;
				break;
			case OPT_PROFILE_INTENT:
				if (strcmp(optarg, "perceptual") == 0)
					state.profile_intent = INTENT_PERCEPTUAL;
				else if (strcmp(optarg, "relative") == 0)
					state.profile_intent = INTENT_RELATIVE_COLORIMETRIC;
				else if (strcmp(optarg, "saturation") == 0)
					state.profile_intent = INTENT_SATURATION;
				else if (strcmp(optarg, "absolute") == 0)
					state.profile_intent = INTENT_ABSOLUTE_COLORIMETRIC;
				break;
			case OPT_PROFILE:
				if (strcmp(optarg, "srgb") == 0) {
					state.profile = PROFILE_SRGB;
					state.matrix = MATRIX_IDENTITY;
					state.input_trc = TRC_LINEAR;
					state.input_gamma[0] = 
					state.input_gamma[1] = 
					state.input_gamma[2] = 1.;
					state.input_alpha[0] = 
					state.input_alpha[1] = 
					state.input_alpha[2] = 0.;
					state.output_gamma = 1.;
					state.brightness[0] = 
					state.brightness[1] = 
					state.brightness[2] = 0.;
					state.contrast[0] = 
					state.contrast[1] = 
					state.contrast[2] = 1.;
				} else if (strcmp(optarg, "gambatte") == 0) {
					state.profile = PROFILE_GAMBATTE;
					if (state.profile_intent == INTENT_SATURATION)
						state.matrix = MATRIX_IDENTITY;
					else state.matrix = MATRIX_GAMBATTE;
					state.input_trc = TRC_LINEAR;
					state.input_gamma[0] = 
					state.input_gamma[1] = 
					state.input_gamma[2] = 1.;
					state.input_alpha[0] = 
					state.input_alpha[1] = 
					state.input_alpha[2] = 0.;
					state.output_gamma = 1.;
					state.brightness[0] = 
					state.brightness[1] = 
					state.brightness[2] = 0.;
					state.contrast[0] = 
					state.contrast[1] = 
					state.contrast[2] = 1.;
				} else if (strcmp(optarg, "gba") == 0) {
					state.profile = PROFILE_GBA;
					if (state.profile_intent == INTENT_SATURATION)
						state.matrix = MATRIX_IDENTITY;
					else state.matrix = MATRIX_GBA;
					state.input_trc = TRC_GAMMA;
					state.input_gamma[0] = 
					state.input_gamma[1] = 
					state.input_gamma[2] = 4.;
					state.input_alpha[0] = 
					state.input_alpha[1] = 
					state.input_alpha[2] = 0.;
					state.output_gamma = 2.2;
					state.brightness[0] = 
					state.brightness[1] = 
					state.brightness[2] = powf(1./250., 1./4.);

					switch (state.profile_intent) {
						case INTENT_PERCEPTUAL:
							state.contrast[0] = 
							state.contrast[1] = 
							state.contrast[2] = powf(1./1.075, 1./4.);
							break;
						default:
							state.contrast[0] = 
							state.contrast[1] = 
							state.contrast[2] = 1.;
					}

					state.contrast[0] -= state.brightness[0];
					state.contrast[1] -= state.brightness[1];
					state.contrast[2] -= state.brightness[2];
				} else if (strcmp(optarg, "gbasp") == 0) {
					state.profile = PROFILE_GBASP;

					switch (state.profile_intent) {
						case INTENT_SATURATION:            state.matrix = MATRIX_IDENTITY;  break;
						case INTENT_ABSOLUTE_COLORIMETRIC: state.matrix = MATRIX_GBASP;     break;
						default:                           state.matrix = MATRIX_GBASP_D65;
					}

					state.input_trc = TRC_GAMMA;
					state.input_gamma[0] = 
					state.input_gamma[1] = 
					state.input_gamma[2] = 2.2;
					state.input_alpha[0] = 
					state.input_alpha[1] = 
					state.input_alpha[2] = 0.;
					state.output_gamma = 2.2;
					state.brightness[0] = 
					state.brightness[1] = 
					state.brightness[2] = powf(1./600., 1./2.2);

					switch (state.profile_intent) {
						case INTENT_PERCEPTUAL:
							state.contrast[0] = 
							state.contrast[1] = 
							state.contrast[2] = powf(1./1.065 * 1.0275, 1./2.2);
							break;
						default:
							state.contrast[0] = 
							state.contrast[1] = 
							state.contrast[2] = 1.;
					}

					state.contrast[0] -= state.brightness[0];
					state.contrast[1] -= state.brightness[1];
					state.contrast[2] -= state.brightness[2];
				} else if (strcmp(optarg, "gbc") == 0) {
					state.profile = PROFILE_GBC;
					if (state.profile_intent == INTENT_SATURATION)
						state.matrix = MATRIX_IDENTITY;
					else state.matrix = MATRIX_GBC;
					state.input_trc = TRC_GAMMA;
					state.input_gamma[0] = 
					state.input_gamma[1] = 
					state.input_gamma[2] = 2.2;
					state.input_alpha[0] = 
					state.input_alpha[1] = 
					state.input_alpha[2] = 0.;
					state.output_gamma = 2.2;
					state.brightness[0] = 
					state.brightness[1] = 
					state.brightness[2] = powf(1./75., 1./2.2);

					switch (state.profile_intent) {
						case INTENT_PERCEPTUAL:
							state.contrast[0] = 
							state.contrast[1] = 
							state.contrast[2] = powf(1./1.075, 1./2.2);
							break;
						default:
							state.contrast[0] = 
							state.contrast[1] = 
							state.contrast[2] = 1.;
					}

					state.contrast[0] -= state.brightness[0];
					state.contrast[1] -= state.brightness[1];
					state.contrast[2] -= state.brightness[2];
				} else if (strcmp(optarg, "gbi") == 0) {
					state.profile = PROFILE_GBI;
					if (state.profile_intent == INTENT_SATURATION)
						state.matrix = MATRIX_IDENTITY;
					else state.matrix = MATRIX_GBI;
					state.input_trc = TRC_SMPTE240;
					state.input_gamma[0] = 
					state.input_gamma[1] = 
					state.input_gamma[2] = 1/.45;
					state.input_alpha[0] = 
					state.input_alpha[1] = 
					state.input_alpha[2] = .1115;
					state.output_gamma = 2.2;
					state.brightness[0] = 
					state.brightness[1] = 
					state.brightness[2] = 0.;
					state.contrast[0] = 
					state.contrast[1] = 
					state.contrast[2] = 1.;
				} else if (strcmp(optarg, "hicolour") == 0) {
					state.profile = PROFILE_HICOLOUR;
					if (state.profile_intent == INTENT_SATURATION)
						state.matrix = MATRIX_IDENTITY;
					else state.matrix = MATRIX_HICOLOUR;
					state.input_trc = TRC_GAMMA;
					state.input_gamma[0] = 
					state.input_gamma[1] = 
					state.input_gamma[2] = 1.;
					state.input_alpha[0] = 
					state.input_alpha[1] = 
					state.input_alpha[2] = 0.;
					state.output_gamma = 1.7;
					state.brightness[0] = 
					state.brightness[1] = 
					state.brightness[2] = 0.;
					state.contrast[0] = 
					state.contrast[1] = 
					state.contrast[2] = 1.12;
				} else if (strcmp(optarg, "higan") == 0) {
					state.profile = PROFILE_HIGAN;
					if (state.profile_intent == INTENT_SATURATION)
						state.matrix = MATRIX_IDENTITY;
					else state.matrix = MATRIX_HIGAN;
					state.input_trc = TRC_GAMMA;
					state.input_gamma[0] = 
					state.input_gamma[1] = 
					state.input_gamma[2] = 4.;
					state.input_alpha[0] = 
					state.input_alpha[1] = 
					state.input_alpha[2] = 0.;
					state.output_gamma = 2.2;
					state.brightness[0] = 
					state.brightness[1] = 
					state.brightness[2] = 0.;
					state.contrast[0] = 
					state.contrast[1] = 
					state.contrast[2] = powf(255./280., 2.2/4.);
				} else if (strcmp(optarg, "nds") == 0) {
					state.profile = PROFILE_NDS;

					switch (state.profile_intent) {
						case INTENT_SATURATION:            state.matrix = MATRIX_IDENTITY; break;
						case INTENT_ABSOLUTE_COLORIMETRIC: state.matrix = MATRIX_NDS;      break;
						default:                           state.matrix = MATRIX_NDS_D65;
					}

					state.input_trc = TRC_GAMMA;
					state.input_gamma[0] = 
					state.input_gamma[1] = 
					state.input_gamma[2] = 2.2;
					state.input_alpha[0] = 
					state.input_alpha[1] = 
					state.input_alpha[2] = 0.;
					state.output_gamma = 2.2;
					state.brightness[0] = 
					state.brightness[1] = 
					state.brightness[2] = powf(1./600., 1./2.2);

					switch (state.profile_intent) {
						case INTENT_PERCEPTUAL:
							state.contrast[0] = 
							state.contrast[1] = 
							state.contrast[2] = powf(1./1.09, 1./2.2);
							break;
						default:
							state.contrast[0] = 
							state.contrast[1] = 
							state.contrast[2] = 1.;
					}

					state.contrast[0] -= state.brightness[0];
					state.contrast[1] -= state.brightness[1];
					state.contrast[2] -= state.brightness[2];
				} else if (strcmp(optarg, "palm") == 0) {
					state.profile = PROFILE_PALM;

					switch (state.profile_intent) {
						case INTENT_SATURATION:            state.matrix = MATRIX_IDENTITY; break;
						case INTENT_ABSOLUTE_COLORIMETRIC: state.matrix = MATRIX_PALM;     break;
						default:                           state.matrix = MATRIX_PALM_D65;
					}

					state.input_trc = TRC_GAMMA;
					state.input_gamma[0] = 
					state.input_gamma[1] = 
					state.input_gamma[2] = 2.2;
					state.input_alpha[0] = 
					state.input_alpha[1] = 
					state.input_alpha[2] = 0.;
					state.output_gamma = 2.2;
					state.brightness[0] = 
					state.brightness[1] = 
					state.brightness[2] = powf(1./75., 1./2.2);

					switch (state.profile_intent) {
						case INTENT_PERCEPTUAL:
							state.contrast[0] = 
							state.contrast[1] = 
							state.contrast[2] = powf(1./1.125, 1./2.2);
							break;
						default:
							state.contrast[0] = 
							state.contrast[1] = 
							state.contrast[2] = 1.;
					}

					state.contrast[0] -= state.brightness[0];
					state.contrast[1] -= state.brightness[1];
					state.contrast[2] -= state.brightness[2];
				} else if (strcmp(optarg, "psp") == 0) {
					state.profile = PROFILE_PSP;

					switch (state.profile_intent) {
						case INTENT_SATURATION:            state.matrix = MATRIX_IDENTITY; break;
						case INTENT_ABSOLUTE_COLORIMETRIC: state.matrix = MATRIX_PSP;      break;
						default:                           state.matrix = MATRIX_PSP_D65;
					}

					state.input_trc = TRC_GAMMA;
					state.input_gamma[0] = 
					state.input_gamma[1] = 
					state.input_gamma[2] = 2.2;
					state.input_alpha[0] = 
					state.input_alpha[1] = 
					state.input_alpha[2] = 0.;
					state.output_gamma = 2.2;
					state.brightness[0] = 
					state.brightness[1] = 
					state.brightness[2] = powf(1./750., 1./2.2);

					switch (state.profile_intent) {
						case INTENT_PERCEPTUAL:
							state.contrast[0] = 
							state.contrast[1] = 
							state.contrast[2] = powf(1./1.15, 1./2.2);
							break;
						default:
							state.contrast[0] = 
							state.contrast[1] = 
							state.contrast[2] = 1.;
					}

					state.contrast[0] -= state.brightness[0];
					state.contrast[1] -= state.brightness[1];
					state.contrast[2] -= state.brightness[2];
				}
				break;
			case OPT_MATRIX:
				if (strcmp(optarg, "identity") == 0)
					state.matrix = MATRIX_IDENTITY;
				else if (strcmp(optarg, "gambatte") == 0)
					state.matrix = MATRIX_GAMBATTE;
				else if (strcmp(optarg, "gba") == 0)
					state.matrix = MATRIX_GBA;
				else if (strcmp(optarg, "gbasp") == 0)
					state.matrix = MATRIX_GBASP_D65;
				else if (strcmp(optarg, "gbc") == 0)
					state.matrix = MATRIX_GBC;
				else if (strcmp(optarg, "gbi") == 0)
					state.matrix = MATRIX_GBI;
				else if (strcmp(optarg, "hicolour") == 0)
					state.matrix = MATRIX_HICOLOUR;
				else if (strcmp(optarg, "higan") == 0)
					state.matrix = MATRIX_HIGAN;
				else if (strcmp(optarg, "nds") == 0)
					state.matrix = MATRIX_NDS_D65;
				else if (strcmp(optarg, "palm") == 0)
					state.matrix = MATRIX_PALM_D65;
				else if (strcmp(optarg, "psp") == 0)
					state.matrix = MATRIX_PSP_D65;
				else if (strcmp(optarg, "vba") == 0)
					state.matrix = MATRIX_VBA;
				break;
			case OPT_INPUT_GAMMA:
				switch (sscanf(optarg, "%g:%g:%g",
							&state.input_gamma[0], &state.input_gamma[1], &state.input_gamma[2])) {
					case 1: state.input_gamma[1] = state.input_gamma[0];
					case 2: state.input_gamma[2] = state.input_gamma[0];
					case 3: state.input_trc = TRC_GAMMA;
				}
				break;
			case OPT_INPUT_ALPHA:
				switch (sscanf(optarg, "%g:%g:%g",
							&state.input_alpha[0], &state.input_alpha[1], &state.input_alpha[2])) {
					case 1: state.input_alpha[1] = state.input_alpha[0];
					case 2: state.input_alpha[2] = state.input_alpha[0];
					case 3: state.input_trc = TRC_PIECEWISE;
				}
				break;
			case OPT_OUTPUT_GAMMA:
				state.output_gamma = strtod(optarg, NULL);
				break;
			case OPT_BRIGHTNESS:
				switch (sscanf(optarg, "%g:%g:%g",
							&state.brightness[0], &state.brightness[1], &state.brightness[2])) {
					case 1: state.brightness[1] = state.brightness[0];
					case 2: state.brightness[2] = state.brightness[0];
				}
				break;
			case OPT_CONTRAST:
				switch (sscanf(optarg, "%g:%g:%g",
							&state.contrast[0], &state.contrast[1], &state.contrast[2])) {
					case 1: state.contrast[1] = state.contrast[0];
					case 2: state.contrast[2] = state.contrast[0];
				}
				break;
			case OPT_FORMAT:
				if (strcmp(optarg, "ntsc") == 0)
					tvMode = VI_NTSC;
				else if (strcmp(optarg, "pal") == 0)
					tvMode = VI_PAL;
				else if (strcmp(optarg, "pal-m") == 0)
					tvMode = VI_MPAL;
				else if (strcmp(optarg, "ntsc-50") == 0)
					tvMode = VI_DEBUG_PAL;
				else if (strcmp(optarg, "pal-60") == 0)
					tvMode = VI_EURGB60;
				else if (strcmp(optarg, "custom") == 0)
					tvMode = VI_CUSTOM;
				else if (strcmp(optarg, "custom-m") == 0)
					tvMode = VI_MCUSTOM;
				else if (strcmp(optarg, "hd60") == 0)
					tvMode = VI_HD60;
				else if (strcmp(optarg, "hd50") == 0)
					tvMode = VI_HD50;
				else if (strcmp(optarg, "hd48") == 0)
					tvMode = VI_HD48;
				else if (strcmp(optarg, "hdcustom") == 0)
					tvMode = VI_HDCUSTOM;
				break;
			case OPT_SCAN_MODE:
			{
				enum {
					MODE_INTERLACE = 0,
					MODE_QUASI_INTERLACE,
					MODE_NON_INTERLACE,
					MODE_NON_PROGRESSIVE,
					MODE_PROGRESSIVE,
					MODE_CLOCK2X,
					MODE_NO_CLOCK2X,
					MODE_SIZE2X,
					MODE_NO_SIZE2X,
				};
				char *options = optarg, *value;
				static char *tokens[] = {
					[MODE_INTERLACE]       = "interlace",
					[MODE_QUASI_INTERLACE] = "quasi-interlace",
					[MODE_NON_INTERLACE]   = "non-interlace",
					[MODE_NON_PROGRESSIVE] = "non-progressive",
					[MODE_PROGRESSIVE]     = "progressive",
					[MODE_CLOCK2X]         = "clock2x",
					[MODE_NO_CLOCK2X]      = "no-clock2x",
					[MODE_SIZE2X]          = "size2x",
					[MODE_NO_SIZE2X]       = "no-size2x",
					NULL
				};
				while (*options) {
					switch (getsubopt(&options, tokens, &value)) {
						case MODE_INTERLACE:
							#ifdef HW_DOL
							viMode  = VIDEO_HaveComponentCable() ? VI_STEREO | VI_INTERLACE | VI_STANDARD | VI_CLOCK_54MHZ
							                                     : VI_MONO   | VI_INTERLACE | VI_STANDARD | VI_CLOCK_27MHZ;
							#else
							viMode  = VI_MONO | VI_INTERLACE | VI_STANDARD | VI_CLOCK_27MHZ;
							#endif
							xfbMode = VI_XFBMODE_DF;
							break;
						case MODE_QUASI_INTERLACE:
							#ifdef HW_DOL
							viMode  = VIDEO_HaveComponentCable() ? VI_STEREO | VI_INTERLACE | VI_STANDARD | VI_CLOCK_54MHZ
							                                     : VI_MONO   | VI_INTERLACE | VI_STANDARD | VI_CLOCK_27MHZ;
							#else
							viMode  = VI_MONO | VI_INTERLACE | VI_STANDARD | VI_CLOCK_27MHZ;
							#endif
							xfbMode = VI_XFBMODE_PSF;
							break;
						case MODE_NON_INTERLACE:
							#ifdef HW_DOL
							viMode  = VIDEO_HaveComponentCable() ? VI_STEREO | VI_NON_INTERLACE | VI_STANDARD | VI_CLOCK_54MHZ
							                                     : VI_MONO   | VI_NON_INTERLACE | VI_STANDARD | VI_CLOCK_27MHZ;
							#else
							viMode  = VI_MONO | VI_NON_INTERLACE | VI_STANDARD | VI_CLOCK_27MHZ;
							#endif
							xfbMode = VI_XFBMODE_SF;
							state.zoom_auto = false;
							state.zoom_ratio = 1.;
							break;
						case MODE_NON_PROGRESSIVE:
							viMode  = VI_MONO | VI_INTERLACE | VI_ENHANCED | VI_CLOCK_54MHZ;
							xfbMode = VI_XFBMODE_SF;
							break;
						case MODE_PROGRESSIVE:
							viMode  = VI_MONO | VI_NON_INTERLACE | VI_ENHANCED | VI_CLOCK_54MHZ;
							xfbMode = VI_XFBMODE_SF;
							break;
						case MODE_CLOCK2X:
							viMode |=  VI_CLOCK_54MHZ;
							break;
						case MODE_NO_CLOCK2X:
							viMode &= ~VI_CLOCK_54MHZ;
							break;
						case MODE_SIZE2X:
							viMode |=  VI_STEREO;
							break;
						case MODE_NO_SIZE2X:
							viMode &= ~VI_STEREO;
							break;
					}
				}
				break;
			}
			case OPT_IPV4_ADDRESS:
				network.use_dhcp = inet_aton(optarg, &network.address) == 0;
				break;
			case OPT_IPV4_GATEWAY:
				network.use_dhcp = inet_aton(optarg, &network.gateway) == 0;
				break;
			case OPT_IPV4_NETMASK:
				network.use_dhcp = inet_aton(optarg, &network.netmask) == 0;
				break;
			case OPT_NETWORK:
				network.disabled = false;
				break;
			case OPT_NO_NETWORK:
				network.disabled = true;
				break;
		}
	}

	default_state = state;

	VideoSetup(tvMode, viMode, xfbMode);
}

int main(int argc, char **argv)
{
	preinit(argc, argv);

	GXInit();
	GXPlanarAllocState();
	GXPrescaleAllocState();
	GXPreviewAllocState();
	GXOverlayAllocState();
	GXFontAllocState();
	GXCursorAllocState();

	displist[0] = GXAllocBuffer(GX_FIFO_MINSIZE);
	displist[1] = GXAllocBuffer(GX_FIFO_MINSIZE);
	displist[2] = GXAllocBuffer(GX_FIFO_MINSIZE);
	displist[3] = GXAllocBuffer(GX_FIFO_MINSIZE);

	GXOverlayReadFile(state.overlay, state.overlay_id);

	InputInit();
	GBAInit();
	NetworkInit();

	struct mGUIRunner runner = {
		.params = {
			.width = GBA_VIDEO_HORIZONTAL_PIXELS * state.zoom.x,
			.height = GBA_VIDEO_VERTICAL_PIXELS * state.zoom.y,
			.basePath = "",
			.drawStart = _drawStart,
			.drawEnd = _drawEnd,
			.pollRunning = _pollRunning,
			.pollInput = _pollInput,
			#ifdef HW_RVL
			.pollCursor = _pollCursor,
			.batteryState = _batteryState,
			#endif
			.guiPrepare = _guiPrepare,
			.guiFinish = _guiFinish
		},
		.configExtra = (struct GUIMenuItem[]) {
			{
				.title = "Screen mode",
				.data = GUI_V_S("screenMode"),
				.state = SM_DEFAULT,
				.validStates = (const char *[]) {
					"Default", "Normal", "Full", "Stretch"
				},
				.nStates = 4
			},
			{
				.title = "Sync to video",
				.data = GUI_V_S("videoSync"),
				.state = true,
				.validStates = (const char *[]) {
					"Off", "On"
				},
				.nStates = 2
			}
		},
		.nConfigExtra = 2,
		.keySources = (struct GUIInputKeys[]) {
			{
				.name = "3DS Controller",
				.id = '3ds\0',
				.keyNames = (const char *[]) {
					"A",
					"B",
					"Select",
					"Start",
					"Right",
					"Left",
					"Up",
					"Down",
					"R",
					"L",
					"X",
					"Y",
					NULL,
					NULL,
					"ZL",
					"ZR"
				},
				.nKeys = 16
			},
			{
				.name = "Active Life Mat",
				.id = 'act\0',
				.keyNames = (const char *[]) {
					"-",
					"Blue Down",
					"Blue Square",
					"Blue Left",
					"Orange Down",
					NULL,
					NULL,
					NULL,
					"Orange Square",
					"Orange Up",
					"+",
					"Orange Right",
					"Blue Up"
				},
				.nKeys = 13
			},
			{
				.name = "DK Bongos",
				.id = 'dk\0\0',
				.keyNames = (const char *[]) {
					NULL,
					NULL,
					NULL,
					NULL,
					NULL,
					NULL,
					NULL,
					NULL,
					"Bottom Right",
					"Bottom Left",
					"Top Right",
					"Top Left",
					"Start"
				},
				.nKeys = 13
			},
			{
				.name = "GameCube Controller",
				.id = 'gc\0\0',
				.keyNames = (const char *[]) {
					"Left",
					"Right",
					"Down",
					"Up",
					"Z",
					"R",
					"L",
					NULL,
					"A",
					"B",
					"X",
					"Y",
					"Start"
				},
				.nKeys = 13
			},
			{
				.name = "Logitech Speed Force",
				.id = 'logi',
				.keyNames = (const char *[]) {
					"Left",
					"Right",
					"Down",
					"Up",
					"Z",
					"R",
					"L",
					NULL,
					"A",
					"B",
					"X",
					"Y",
					"Start"
				},
				.nKeys = 13
			},
			{
				.name = "N64 Controller",
				.id = 'n64\0',
				.keyNames = (const char *[]) {
					"Right",
					"Left",
					"Down",
					"Up",
					"Start",
					"Z",
					"B",
					"A",
					"C Right",
					"C Left",
					"C Down",
					"C Up",
					"R",
					"L"
				},
				.nKeys = 14
			},
			#ifdef HW_RVL
			{
				.name = "NES Controller",
				.id = 'nes\0',
				.keyNames = (const char *[]) {
					"Right",
					"Left",
					"Down",
					"Up",
					"Start",
					"Select",
					"B",
					"A"
				},
				.nKeys = 8
			},
			{
				.name = "SNES Controller",
				.id = 'snes',
				.keyNames = (const char *[]) {
					"Right",
					"Left",
					"Down",
					"Up",
					"Start",
					"Select",
					"Y",
					"B",
					NULL,
					NULL,
					NULL,
					NULL,
					"R",
					"L",
					"X",
					"A"
				},
				.nKeys = 16
			},
			{
				.name = "Wii Classic Controller",
				.id = 'wmcc',
				.keyNames = (const char *[]) {
					NULL,
					"R",
					"+",
					"Home",
					"-",
					"L",
					"Down",
					"Right",
					"Up",
					"Left",
					"ZR",
					"X",
					"A",
					"Y",
					"B",
					"ZL"
				},
				.nKeys = 16
			},
			{
				.name = "Wii Nunchuk",
				.id = 'wmnc',
				.keyNames = (const char *[]) {
					"Left",
					"Right",
					"Down",
					"Up",
					"+",
					NULL,
					NULL,
					NULL,
					"2",
					"1",
					"B",
					"A",
					"-",
					NULL,
					NULL,
					"Home",
					"Z",
					"C"
				},
				.nKeys = 18
			},
			{
				.name = "Wii Remote (L)",
				.id = 'wm\0\0',
				.keyNames = (const char *[]) {
					"Left",
					"Right",
					"Down",
					"Up",
					"+",
					NULL,
					NULL,
					NULL,
					"2",
					"1",
					"B",
					"A",
					"-",
					NULL,
					NULL,
					"Home"
				},
				.nKeys = 16
			},
			{
				.name = "Wii Remote (R)",
				.id = 'wmr\0',
				.keyNames = (const char *[]) {
					"Left",
					"Right",
					"Down",
					"Up",
					"+",
					NULL,
					NULL,
					NULL,
					"2",
					"1",
					"B",
					"A",
					"-",
					NULL,
					NULL,
					"Home"
				},
				.nKeys = 16
			},
			{
				.name = "Wii U Pro Controller",
				.id = 'wupc',
				.keyNames = (const char *[]) {
					NULL,
					"R",
					"+",
					"Home",
					"-",
					"L",
					"Down",
					"Right",
					"Up",
					"Left",
					"ZR",
					"X",
					"A",
					"Y",
					"B",
					"ZL",
					"RS",
					"LS"
				},
				.nKeys = 18
			},
			#endif
			{ NULL }
		},
		.setup = _setup,
		.teardown = _teardown,
		.gameLoaded = _gameLoaded,
		.gameUnloaded = _gameUnloaded,
		.prepareForFrame = _prepareForFrame,
		.drawFrame = _drawFrame,
		.drawScreenshot = _drawScreenshot,
		.paused = _paused,
		.unpaused = _unpaused,
		.incrementScreenMode = _incrementScreenMode,
		.setFrameLimiter = _setFrameLimiter,
		.pollGameInput = _pollGameInput
	};

	mGUIInit(&runner, "gc");
	if (state.path == NULL) mGUIRunloop(&runner);
	else mGUIRun(&runner, state.path);
	mGUIDeinit(&runner);

	VideoBlackOut();

	#ifdef HW_RVL
	if (state.quit & KEY_POWEROFF)
		SYS_ResetSystem(SYS_POWEROFF);
	else if (state.quit & KEY_RESET)
		SYS_ResetSystem(SYS_HOTRESET);
	#endif

	WIILOADLoad();

	return EXIT_SUCCESS;
}
