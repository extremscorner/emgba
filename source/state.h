/* 
 * Copyright (c) 2015-2022, Extrems' Corner.org
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef GBI_STATE_H
#define GBI_STATE_H

#include <stdint.h>

#define KEY_QUIT     0x01
#define KEY_RESET    0x02
#define KEY_POWEROFF 0x04

typedef struct {
	const char *path;
	bool draw_osd;
	bool draw_wait;
	volatile int reset, quit;

	struct { float w, h; } aspect;
	struct { float x, y; } offset;
	struct { float x, y; } zoom;
	bool zoom_auto;
	float zoom_ratio;
	float rotation;
	unsigned scale;

	unsigned poll;

	const char *cursor;
	const char *overlay;
	unsigned overlay_id;
	struct { float x, y; } overlay_scale;

	enum {
		FILTER_NONE = 0,
		FILTER_BLEND,
		FILTER_DEFLICKER,
		FILTER_ACCUMULATE,
		FILTER_SCALE2XEX,
		FILTER_SCALE2XPLUS,
		FILTER_SCALE2X,
		FILTER_EAGLE2X,
		FILTER_SCAN2X,
		FILTER_NORMAL2X,
		FILTER_MAX
	} filter;

	float filter_weight[3];
	bool filter_prescale;

	enum {
		DITHER_NONE = 0,
		DITHER_THRESHOLD,
		DITHER_BAYER8x8,
		DITHER_BAYER4x4,
		DITHER_BAYER2x2,
		DITHER_CLUSTER8x8,
		DITHER_CLUSTER4x4,
		DITHER_MAX
	} dither;

	enum {
		SCALER_NEAREST = 0,
		SCALER_BILINEAR,
		SCALER_AREA,
		SCALER_BOX,
		SCALER_MAX
	} scaler;

	enum {
		MATRIX_IDENTITY = 0,
		MATRIX_GBA,
		MATRIX_GBC = MATRIX_GBA,
		MATRIX_GBI,
		MATRIX_NDS,
		MATRIX_PALM,
		MATRIX_PSP,
		MATRIX_VBA,
		MATRIX_GBC_DEV = MATRIX_VBA,
		MATRIX_MAX
	} matrix;

	enum {
		TRC_GAMMA = 0,
		TRC_ITU709,
		TRC_SMPTE240,
		TRC_LINEAR,
		TRC_MAX
	} input_trc;

	float input_gamma[3];
	float output_gamma;
	float brightness[3];
	float contrast[3];

	unsigned retrace, field;

	enum {
		STATE_INIT = 0,
		STATE_PREVIEW,
		STATE_PREVIEW_LUMA,
		STATE_OVERLAY,
		STATE_SOLID,
		STATE_FONT,
		STATE_CURSOR,
	} current;
} state_t;

extern state_t default_state, state;

enum {
	SM_DEFAULT = 0,
	SM_NORMAL,
	SM_FULL,
	SM_STRETCH,
	SM_MAX
};

#endif /* GBI_STATE_H */
