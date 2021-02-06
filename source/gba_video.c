/* 
 * Copyright (c) 2015-2021, Extrems' Corner.org
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <gccore.h>
#include "gba.h"

void GBAVideoConvertBGR5(void *dst, void *src, int width, int height)
{
	GX_RedirectWriteGatherPipe(dst);

	uint16_t *src0 = src - 4;
	uint16_t *src1 = src0 + width;
	uint16_t *src2 = src1 + width;
	uint16_t *src3 = src2 + width;

	register int reg00, reg01;
	register int reg10, reg11;
	register int reg20, reg21;
	register int reg30, reg31;

	int lines = height >> 2;

	while (lines--) {
		int tiles = width >> 2;

		do {
			asm volatile (
				"lwz     %00, 4 (%08) \n"
				"lwzu    %01, 8 (%08) \n"
				"lwz     %02, 4 (%09) \n"
				"lwzu    %03, 8 (%09) \n"
				"lwz     %04, 4 (%10) \n"
				"lwzu    %05, 8 (%10) \n"
				"lwz     %06, 4 (%11) \n"
				"lwzu    %07, 8 (%11) \n"

				"or      %00, %00, %12 \n"
				"or      %01, %01, %12 \n"
				"or      %02, %02, %12 \n"
				"or      %03, %03, %12 \n"
				"or      %04, %04, %12 \n"
				"or      %05, %05, %12 \n"
				"or      %06, %06, %12 \n"
				"or      %07, %07, %12 \n"

				"stw     %00, 0 (%13) \n"
				"stw     %01, 0 (%13) \n"
				"stw     %02, 0 (%13) \n"
				"stw     %03, 0 (%13) \n"
				"stw     %04, 0 (%13) \n"
				"stw     %05, 0 (%13) \n"
				"stw     %06, 0 (%13) \n"
				"stw     %07, 0 (%13) \n"
				: "=r" (reg00), "=r" (reg01),
				  "=r" (reg10), "=r" (reg11),
				  "=r" (reg20), "=r" (reg21),
				  "=r" (reg30), "=r" (reg31),
				  "+b" (src0), "+b" (src1), "+b" (src2), "+b" (src3)
				: "r" (0x80008000),
				  "b" (wgPipe)
				: "memory"
			);
		} while (--tiles);

		src0 += width * 3;
		src1 += width * 3;
		src2 += width * 3;
		src3 += width * 3;
	}

	GX_RestoreWriteGatherPipe();
}
