/* 
 * Copyright (c) 2015-2020, Extrems' Corner.org
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <ogc/n64.h>
#include <ogc/pad.h>
#include <ogc/si.h>
#include <ogc/si_steering.h>
#include <ogc/system.h>
#include <wiiuse/wpad.h>
#include "input.h"
#include "video.h"

gc_controller_t gc_controller;
gc_steering_t gc_steering;
n64_controller_t n64_controller;

#ifdef HW_RVL
static void power_cb(void)
{
	state.quit |= KEY_POWEROFF;
}
#endif

static void reset_cb(void)
{
	if (state.draw_osd) state.quit |= KEY_RESET;
	else state.reset = true;
	PAD_Recalibrate(PAD_CHAN0_BIT | PAD_CHAN1_BIT | PAD_CHAN2_BIT | PAD_CHAN3_BIT);
}

static void poll_cb()
{
	PAD_Read(gc_controller.status);

	for (int chan = 0; chan < SI_MAX_CHAN; chan++)
		SI_ReadSteering(chan, &gc_steering.status[chan]);
}

void InputRead(void)
{
	for (int chan = 0; chan < PAD_CHANMAX; chan++) {
		gc_controller.data[chan].last = gc_controller.data[chan].held;

		if (gc_controller.status[chan].err != PAD_ERR_TRANSFER) {
			gc_controller.data[chan].barrel = PAD_IsBarrel(chan);

			gc_controller.data[chan].held = gc_controller.status[chan].button;
			gc_controller.data[chan].stick.x = gc_controller.status[chan].stickX;
			gc_controller.data[chan].stick.y = gc_controller.status[chan].stickY;
			gc_controller.data[chan].substick.x = gc_controller.status[chan].substickX;
			gc_controller.data[chan].substick.y = gc_controller.status[chan].substickY;
			gc_controller.data[chan].trigger.l = gc_controller.status[chan].triggerL;
			gc_controller.data[chan].trigger.r = gc_controller.status[chan].triggerR;
			gc_controller.data[chan].button.a = gc_controller.status[chan].analogA;
			gc_controller.data[chan].button.b = gc_controller.status[chan].analogB;
		}

		gc_controller.data[chan].down = gc_controller.data[chan].held & ~gc_controller.data[chan].last;
		gc_controller.data[chan].up = ~gc_controller.data[chan].held & gc_controller.data[chan].last;
	}

	for (int chan = 0; chan < SI_MAX_CHAN; chan++) {
		gc_steering.data[chan].last = gc_steering.data[chan].held;

		if (gc_steering.status[chan].err != SI_STEERING_ERR_TRANSFER) {
			gc_steering.data[chan].held = gc_steering.status[chan].button;
			gc_steering.data[chan].flag = gc_steering.status[chan].flag;
			gc_steering.data[chan].wheel = gc_steering.status[chan].wheel;
			gc_steering.data[chan].pedal.l = gc_steering.status[chan].pedalL;
			gc_steering.data[chan].pedal.r = gc_steering.status[chan].pedalR;
			gc_steering.data[chan].paddle.l = gc_steering.status[chan].paddleL;
			gc_steering.data[chan].paddle.r = gc_steering.status[chan].paddleR;
		}

		gc_steering.data[chan].down = gc_steering.data[chan].held & ~gc_steering.data[chan].last;
		gc_steering.data[chan].up = ~gc_steering.data[chan].held & gc_steering.data[chan].last;
	}

	for (int chan = 0; chan < SI_MAX_CHAN; chan++) {
		N64_Read(chan, &n64_controller.status[chan]);

		n64_controller.data[chan].last = n64_controller.data[chan].held;

		if (n64_controller.status[chan].err != N64_ERR_TRANSFER) {
			n64_controller.data[chan].held = n64_controller.status[chan].button;
			n64_controller.data[chan].stick.x = n64_controller.status[chan].stickX;
			n64_controller.data[chan].stick.y = n64_controller.status[chan].stickY;
		}

		n64_controller.data[chan].down = n64_controller.data[chan].held & ~n64_controller.data[chan].last;
		n64_controller.data[chan].up = ~n64_controller.data[chan].held & n64_controller.data[chan].last;
	}
}

void InputInit(void)
{
	#ifdef HW_RVL
	SYS_SetPowerCallback(power_cb);
	#endif
	SYS_SetResetCallback(reset_cb);

	SI_SetSamplingRate(state.poll);
	SI_RegisterPollingHandler(poll_cb);

	PAD_Init();
	SI_InitSteering();
	N64_Init();

	for (int chan = 0; chan < SI_MAX_CHAN; chan++) {
		gc_controller.status[chan].err = PAD_ERR_NO_CONTROLLER;
		gc_steering.status[chan].err = SI_ResetSteering(chan);
		n64_controller.status[chan].err = N64_ERR_NO_CONTROLLER;
	}

	#ifdef HW_RVL
	WPAD_Init();
	WPAD_SetDataFormat(WPAD_CHAN_ALL, WPAD_FMT_BTNS_ACC_IR);
	WPAD_SetVRes(WPAD_CHAN_ALL, screen.w, screen.h);
	#endif
}
