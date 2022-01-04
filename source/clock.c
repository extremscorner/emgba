/* 
 * Copyright (c) 2015-2022, Extrems' Corner.org
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <ogc/lwp_watchdog.h>
#include "clock.h"

void ClockTick(timing_t *clock, uint32_t step)
{
	uint64_t diff, curr;

	curr = gettime();
	diff = diff_ticks(clock->start, curr);

	if (clock->reset) {
		clock->reset = false;
		clock->count = 0;
		clock->time  = clock->start = curr;
		clock->delta = 0;
	} else {
		clock->delta  = diff_ticks(clock->time, curr);
		clock->time   = curr;
		clock->count += step;

		if (diff >= secs_to_ticks(1)) {
			clock->hz = (double)secs_to_ticks(1) * clock->count / diff;
			clock->start = curr;
			clock->count = 0;
		}
	}
}
