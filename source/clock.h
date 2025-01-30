/* 
 * Copyright (c) 2015-2025, Extrems' Corner.org
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef GBI_CLOCK_H
#define GBI_CLOCK_H

#include <stdint.h>

typedef struct {
	bool reset;
	uint32_t count;
	uint64_t start;
	uint64_t time;
	uint64_t delta;
	double hz;
} timing_t;

void ClockTick(timing_t *clock, uint32_t step);

#endif /* GBI_CLOCK_H */
