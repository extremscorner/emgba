/* 
 * Copyright (c) 2015-2021, Extrems' Corner.org
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <math.h>
#include <gccore.h>
#include "state.h"
#include "util.h"
#include "video.h"

static void *xfb[3];
static uint32_t xfb_index;

GXRModeObj rmode;
rect_t viewport, screen;
timing_t viclock = { .reset = true, .hz = NAN };

static void vblank_cb(uint32_t retrace)
{
	ClockTick(&viclock, 1);
}

void VideoSetup(uint32_t tvMode, uint32_t viMode, uint32_t xfbMode)
{
	rmode.viTVMode = VI_TVMODE(tvMode, viMode);

	rmode.viXOrigin = 0;
	rmode.viYOrigin = 0;

	switch (tvMode) {
		case VI_HDCUSTOM:
		case VI_HD48:
		case VI_HD50:
		case VI_HD60:
			if ((viMode & VI_ENHANCED) == VI_ENHANCED) {
				rmode.viWidth  = VI_MAX_WIDTH_FHD;
				rmode.viHeight = VI_MAX_HEIGHT_FHD;
			} else {
				rmode.viWidth  = VI_MAX_WIDTH_HD;
				rmode.viHeight = VI_MAX_HEIGHT_HD;
			}
			break;
		case VI_DEBUG_PAL:
		case VI_PAL:
			rmode.viWidth  = VI_MAX_WIDTH_PAL;
			rmode.viHeight = VI_MAX_HEIGHT_PAL;
			break;
		default:
			rmode.viWidth  = VI_MAX_WIDTH_NTSC;
			rmode.viHeight = VI_MAX_HEIGHT_NTSC;
	}

	rmode.fbWidth   = rmode.viWidth;
	rmode.efbHeight = 
	rmode.xfbHeight = rmode.viHeight;

	if ((viMode & VI_STEREO) == VI_STEREO) {
		rmode.viXOrigin *= 2;
		rmode.viWidth   *= 2;

		if (rmode.fbWidth <= 682)
			rmode.fbWidth *= 2;
	}

	if (xfbMode > VI_XFBMODE_SF) {
		rmode.efbHeight *= 2;
		rmode.xfbHeight *= 2;
	}

	if ((viMode & VI_ENHANCED) == VI_STANDARD) {
		rmode.efbHeight /= 2;
		rmode.xfbHeight /= 2;
	}

	viewport.x = (684 - rmode.fbWidth)   / 4 * 2;
	viewport.y = (684 - rmode.efbHeight) / 4 * 2;

	viewport.w = rmode.fbWidth;
	viewport.h = rmode.efbHeight;

	rmode.efbHeight = rmode.efbHeight / (1 + rmode.efbHeight / 525);

	rmode.xfbMode = xfbMode;
	rmode.field_rendering = (viMode & VI_NON_INTERLACE) == VI_INTERLACE && xfbMode == VI_XFBMODE_SF;
	rmode.aa = GX_FALSE;

	if (tvMode < VI_HD60 && (viMode & VI_NON_INTERLACE) == VI_INTERLACE && xfbMode == VI_XFBMODE_DF) {
		rmode.vfilter[0] = 8;
		rmode.vfilter[1] = 8;
		rmode.vfilter[2] = 10;
		rmode.vfilter[3] = 12;
		rmode.vfilter[4] = 10;
		rmode.vfilter[5] = 8;
		rmode.vfilter[6] = 8;
	} else {
		rmode.vfilter[0] = 0;
		rmode.vfilter[1] = 0;
		rmode.vfilter[2] = 21;
		rmode.vfilter[3] = 22;
		rmode.vfilter[4] = 21;
		rmode.vfilter[5] = 0;
		rmode.vfilter[6] = 0;
	}
	#ifdef HW_RVL
	if (viMode == (VI_MONO | VI_NON_INTERLACE | VI_STANDARD | VI_CLOCK_27MHZ))
		VIDEO_SetTrapFilter(true);
	#endif
	VIDEO_SetAdjustingValues(0, 0);
	VIDEO_Configure(&rmode);
	rmode.fbWidth = VIDEO_PadFramebufferWidth(rmode.fbWidth);

	for (int i = 0; i < ARRAY_ELEMS(xfb); i++)
		xfb[i] = SYS_AllocateFramebuffer(&rmode);
	VIDEO_SetNextFramebuffer(xfb[xfb_index]);

	VIDEO_SetBlack(false);
	VIDEO_Flush();
	VIDEO_WaitVSync();
	VIDEO_WaitVSync();

	switch (VIDEO_GetCurrentTvMode()) {
		case VI_HDCUSTOM:
		case VI_HD48:
		case VI_HD50:
		case VI_HD60:
			if (state.aspect.h > state.aspect.w) {
				screen.w = 540;
				screen.h = 540 * state.aspect.h / state.aspect.w;
			} else {
				screen.w = 540 * state.aspect.w / state.aspect.h;
				screen.h = 540;
			}
			break;
		case VI_DEBUG_PAL:
		case VI_PAL:
			if (state.aspect.h > state.aspect.w) {
				screen.w = 576;
				screen.h = 576 * state.aspect.h / state.aspect.w;
			} else {
				screen.w = 576 * state.aspect.w / state.aspect.h;
				screen.h = 576;
			}
			break;
		default:
			if (state.aspect.h > state.aspect.w) {
				screen.w = 480;
				screen.h = 486 * state.aspect.h / state.aspect.w;
			} else {
				screen.w = 480 * state.aspect.w / state.aspect.h;
				screen.h = 486;
			}
	}

	screen.w = (screen.w + 1) & ~1;
	screen.h = (screen.h + 1) & ~1;

	VIDEO_SetPreRetraceCallback(vblank_cb);
}

void VideoBlackOut(void)
{
	VIDEO_SetBlack(true);
	VIDEO_Flush();
	VIDEO_WaitVSync();
	VIDEO_WaitVSync();
}

void *VideoGetFramebuffer(uint32_t *index)
{
	do {
		xfb_index = (xfb_index + 1) % ARRAY_ELEMS(xfb);
	} while (xfb[xfb_index] == VIDEO_GetCurrentFramebuffer());

	if (index)
		*index = xfb_index;
	return xfb[xfb_index];
}

void VideoSetFramebuffer(uint32_t index)
{
	VIDEO_SetNextFramebuffer(xfb[index % ARRAY_ELEMS(xfb)]);
	VIDEO_Flush();
}
