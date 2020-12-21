/* 
 * Copyright (c) 2015-2020, Extrems' Corner.org
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef GBI_UTIL_H
#define GBI_UTIL_H

#include <math.h>
#include <stdint.h>

#define ARRAY_ELEMS(a) (sizeof(a) / sizeof((a)[0]))
#define SWAP(a, b) { typeof(a) tmp = b; b = a; a = tmp; }

#define LERP(a, b, c, d) ((a * (d - (c)) + b * (c)) / d)
#define LSHIFT(a, b) ((b) >= 0 ? (a) * (1 << (b)) : (a) / (1 << -(b)))

typedef union {
	uint8_t u8[2];
	int8_t s8[2];
	uint16_t u16;
	int16_t s16;
} hword_t;

typedef union {
	uint8_t u8[4];
	int8_t s8[4];
	uint16_t u16[2];
	int8_t s16[2];
	uint32_t u32;
	int32_t s32;
	float_t f32;
} word_t;

typedef union {
	uint8_t u8[8];
	int8_t s8[8];
	uint16_t u16[4];
	int16_t s16[4];
	uint32_t u32[2];
	int32_t s32[2];
	float_t f32[2];
	uint64_t u64;
	int64_t s64;
	double_t f64;
} dword_t;

#endif /* GBI_UTIL_H */
