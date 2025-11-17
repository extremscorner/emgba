/* 
 * Copyright (c) 2015-2025, Extrems' Corner.org
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <gccore.h>
#include "network.h"
#include "sntp.h"
#include "state.h"

static lwp_t thread = LWP_THREAD_NULL;

sntp_state_t sntp = {
	.sv.sd = INVALID_SOCKET,

	.sv.sin.sin_family = AF_INET,
	.sv.sin.sin_port = 123,
	.sv.sin.sin_addr.s_addr = INADDR_ANY,

	.sv.tv.tv_sec  = 1,
	.sv.tv.tv_usec = 0,
};

extern uint32_t __SYS_SetRTC(uint32_t time);

static bool sntp_read(int socket)
{
	sntp_packet_t packet;

	if (net_read(socket, &packet, sizeof(packet)) < sizeof(packet))
		return false;
	if (!__SYS_SetRTC((packet.transmit_time >> 32) - DIFF_SEC_1900_2000))
		return false;

	return true;
}

static void *thread_func(void *arg)
{
	sntp.sv.sd = net_socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);

	if (sntp.sv.sd == INVALID_SOCKET)
		goto fail;
	if (net_bind(sntp.sv.sd, &sntp.sv.sa, sizeof(sntp.sv.sin)) < 0)
		goto fail;

	do {
		#ifdef HW_DOL
		fd_set readset;
		FD_ZERO(&readset);
		FD_SET(sntp.sv.sd, &readset);
		net_select(FD_SETSIZE, &readset, NULL, NULL, &sntp.sv.tv);

		if (FD_ISSET(sntp.sv.sd, &readset))
		#else
		struct pollsd psd = {sntp.sv.sd, POLLIN, 0};
		net_poll(&psd, 1, 1000);

		if (psd.revents & POLLIN)
		#endif
			sntp_read(sntp.sv.sd);
	} while (!state.quit);

fail:
	net_close(sntp.sv.sd);
	sntp.sv.sd = INVALID_SOCKET;
	return NULL;
}

void SNTPInit(void)
{
	if (!LWP_CreateThread(&thread, thread_func, NULL, NULL, 0, LWP_PRIO_NORMAL))
		LWP_DetachThread(thread);
}
