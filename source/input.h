/* 
 * Copyright (c) 2015-2024, Extrems' Corner.org
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef GBI_INPUT_H
#define GBI_INPUT_H

#include <stdint.h>
#include <ogc/n64.h>
#include <ogc/pad.h>
#include <ogc/si.h>
#include <ogc/si_steering.h>

#define MAT_BUTTON_MINUS         PAD_BUTTON_LEFT
#define MAT_BUTTON_BLUE_DOWN     PAD_BUTTON_RIGHT
#define MAT_BUTTON_BLUE_SQUARE   PAD_BUTTON_DOWN
#define MAT_BUTTON_BLUE_LEFT     PAD_BUTTON_UP
#define MAT_BUTTON_ORANGE_DOWN   PAD_BUTTON_Z
#define MAT_BUTTON_ORANGE_SQUARE PAD_BUTTON_A
#define MAT_BUTTON_ORANGE_UP     PAD_BUTTON_B
#define MAT_BUTTON_PLUS          PAD_BUTTON_X
#define MAT_BUTTON_ORANGE_RIGHT  PAD_BUTTON_Y
#define MAT_BUTTON_BLUE_UP       PAD_BUTTON_START

#define PAD_BUTTON_ALL    0x1F7F
#define PAD_COMBO_ORIGIN  (PAD_BUTTON_Y | PAD_BUTTON_X | PAD_BUTTON_START)
#define PAD_COMBO_RESET   (PAD_BUTTON_B | PAD_BUTTON_X | PAD_BUTTON_START)
#define PAD_COMBO_RESTART (PAD_BUTTON_R | PAD_BUTTON_Z | PAD_BUTTON_START)

#define SI_STEERING_BUTTON_ALL    PAD_BUTTON_ALL
#define SI_STEERING_COMBO_ORIGIN  PAD_COMBO_ORIGIN
#define SI_STEERING_COMBO_RESET   PAD_COMBO_RESET
#define SI_STEERING_COMBO_RESTART PAD_COMBO_RESTART

#define N64_BUTTON_ALL    0x3FFF
#define N64_COMBO_ORIGIN  (N64_BUTTON_START | N64_BUTTON_R | N64_BUTTON_L)
#define N64_COMBO_RESET   (N64_BUTTON_START | N64_BUTTON_Z | N64_BUTTON_B | N64_BUTTON_A | N64_BUTTON_R)
#define N64_COMBO_RESTART (N64_BUTTON_START | N64_BUTTON_Z | N64_BUTTON_R)

typedef struct {
	PADStatus status[SI_MAX_CHAN];

	struct {
		uint16_t held, last, down, up;
		struct { int8_t x, y; } stick;
		struct { int8_t x, y; } substick;
		struct { uint8_t l, r; } trigger;
		struct { uint8_t a, b; } button;
		bool barrel;
	} data[SI_MAX_CHAN];
} gc_controller_t;

typedef struct {
	SISteeringStatus status[SI_MAX_CHAN];

	struct {
		uint16_t held, last, down, up;
		uint8_t flag;
		int8_t wheel;
		struct { uint8_t l, r; } pedal;
		struct { uint8_t l, r; } paddle;
	} data[SI_MAX_CHAN];
} gc_steering_t;

typedef struct {
	N64Status status[SI_MAX_CHAN];

	struct {
		uint16_t held, last, down, up;
		struct { int8_t x, y; } stick;
	} data[SI_MAX_CHAN];
} n64_controller_t;

extern gc_controller_t gc_controller;
extern gc_steering_t gc_steering;
extern n64_controller_t n64_controller;

void InputRead(void);
void InputInit(void);

#endif /* GBI_INPUT_H */
