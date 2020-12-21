/* 
 * Copyright (c) 2015-2020, Extrems' Corner.org
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <gccore.h>
#include "gbp.h"

extern syssramex *__SYS_LockSramEx();
extern uint32_t __SYS_UnlockSramEx(uint32_t write);

static bool is_valid(syssramex *sramex)
{
	if (!sramex)
		return false;
	if ((sramex->gbs & 0x8000) >> 15 == __builtin_parity(sramex->gbs & ~0x8000))
		return false;

	return true;
}

int GBPGetController(void)
{
	int value = 0;
	syssramex *sramex = __SYS_LockSramEx();

	if (is_valid(sramex))
		value = (sramex->gbs & 0x100) >> 8;

	__SYS_UnlockSramEx(0);

	return value;
}

int GBPGetScreenSize(void)
{
	int value = 0;
	syssramex *sramex = __SYS_LockSramEx();

	if (is_valid(sramex))
		value = (sramex->gbs & 0x200) >> 9;

	__SYS_UnlockSramEx(0);

	return value;
}

int GBPGetFrame(void)
{
	int value = 0;
	syssramex *sramex = __SYS_LockSramEx();

	if (is_valid(sramex))
		value = (sramex->gbs & 0x7C00) >> 10;

	__SYS_UnlockSramEx(0);

	return value;
}

int GBPGetTimer(void)
{
	int value = 0;
	syssramex *sramex = __SYS_LockSramEx();

	if (is_valid(sramex))
		value = sramex->gbs & 0x3F;

	__SYS_UnlockSramEx(0);

	return value;
}

int GBPGetScreenFilter(void)
{
	int value = 0;
	syssramex *sramex = __SYS_LockSramEx();

	if (is_valid(sramex))
		value = (sramex->gbs & 0xC0) >> 6;

	__SYS_UnlockSramEx(0);

	return value;
}
