/* 
 * Copyright (c) 2015-2025, Extrems' Corner.org
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <gccore.h>
#include <mgba-util/memory.h>
#include "vm/vm.h"

uint32_t *romBuffer;
size_t romBufferSize;

static void __attribute__((constructor)) allocateRomBuffer(void)
{
	#ifdef HW_DOL
	romBufferSize = 32 << 20;
	romBuffer = SYS_AllocArenaMem1Hi(romBufferSize, 32);

	if (!romBuffer) {
		AR_Init(NULL, 0);
		ARQ_Init();

		romBufferSize = AR_GetSize();
		romBuffer = VM_Init(romBufferSize, 8 << 20);
	}
	#else
	romBufferSize = 32 << 20;
	romBuffer = SYS_AllocArenaMem2Lo(romBufferSize, 32);
	#endif
}

void *anonymousMemoryMap(size_t size)
{
	return malloc(size);
}

void mappedMemoryFree(void *memory, size_t size)
{
	free(memory);
}
