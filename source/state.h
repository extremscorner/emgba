/* 
 * Copyright (c) 2015-2025, Extrems' Corner.org
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
		INTENT_PERCEPTUAL = 0,
		INTENT_RELATIVE_COLORIMETRIC,
		INTENT_SATURATION,
		INTENT_ABSOLUTE_COLORIMETRIC,
	} profile_intent;

	enum {
		PROFILE_SRGB = 0,
		PROFILE_GAMBATTE,
		PROFILE_GBA,
		PROFILE_GBASP,
		PROFILE_GBC,
		PROFILE_GBI,
		PROFILE_HICOLOUR,
		PROFILE_HIGAN,
		PROFILE_NDS,
		PROFILE_PALM,
		PROFILE_PSP,
		PROFILE_MAX
	} profile;

	enum {
		MATRIX_IDENTITY = 0,
		MATRIX_GAMBATTE,
		MATRIX_GBA,
		MATRIX_GBASP,
		MATRIX_GBASP_D65,
		MATRIX_GBI,
		MATRIX_HIGAN,
		MATRIX_NDS,
		MATRIX_NDS_D65,
		MATRIX_PALM,
		MATRIX_PALM_D65,
		MATRIX_PSP,
		MATRIX_PSP_D65,
		MATRIX_VBA,
		MATRIX_MAX,
		MATRIX_GBC = MATRIX_GBA,
		MATRIX_HICOLOUR = MATRIX_VBA,
	} input_matrix;

	enum {
		TRC_LINEAR = 0,
		TRC_GAMMA,
		TRC_PIECEWISE,
		TRC_GAMMA22,
		TRC_IEC61966,
		TRC_ITU709,
		TRC_SMPTE240,
		TRC_MAX
	} input_trc;

	float input_gamma[3];
	float input_alpha[3];
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
