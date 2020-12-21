/* 
 * Copyright (c) 2015-2020, Extrems' Corner.org
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef GBI_GBA_H
#define GBI_GBA_H

#include <stdint.h>

#define GBA_BUTTON_A      0x0100
#define GBA_BUTTON_B      0x0200
#define GBA_BUTTON_SELECT 0x0400
#define GBA_BUTTON_START  0x0800
#define GBA_BUTTON_RIGHT  0x1000
#define GBA_BUTTON_LEFT   0x2000
#define GBA_BUTTON_UP     0x4000
#define GBA_BUTTON_DOWN   0x8000
#define GBA_BUTTON_R      0x0001
#define GBA_BUTTON_L      0x0002
#define GBA_BUTTON_RESET  0x0004

uint32_t GBAJoyResetCommand(int32_t chan);
uint32_t GBAJoyStatusCommand(int32_t chan);
uint32_t GBAJoyReadCommand(int32_t chan);
void GBAJoyWriteCommand(int32_t chan, uint32_t val);
void GBAJoyInit(void);

void GBAVideoConvertBGR5(void *dst, void *src, int width, int height);

#endif /* GBI_GBA_H */
