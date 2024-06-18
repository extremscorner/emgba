/* 
 * Copyright (c) 2015-2024, Extrems' Corner.org
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef GBI_VIDEO_H
#define GBI_VIDEO_H

#include <stdint.h>
#include <ogc/gx_struct.h>
#include "clock.h"

typedef struct {
	int16_t x, y;
	uint16_t w, h;
} rect_t;

extern GXRModeObj rmode;
extern rect_t viewport, screen;
extern timing_t viclock;

void VideoSetup(uint32_t tvMode, uint32_t viMode, uint32_t xfbMode);
void VideoBlackOut(void);
void *VideoGetFramebuffer(uint32_t *index);
void VideoSetFramebuffer(uint32_t index);

#endif /* GBI_VIDEO_H */
