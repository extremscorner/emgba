/* 
 * Copyright (c) 2015-2024, Extrems' Corner.org
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <stdio.h>
#include <gccore.h>
#include "3ds.h"
#include "network.h"
#include "sntp.h"
#include "wiiload.h"

static lwp_t thread = LWP_THREAD_NULL;

network_state_t network = {
	.inited   = (-1),
	.use_dhcp = true
};

int tcp_read(int socket, void *buffer, int size, int minsize)
{
	int ret, len = 0;

	while (len < minsize) {
		ret = net_read(socket, buffer + len, size - len);
		if (ret < 1) return ret < 0 ? ret : len;
		else len += ret;
	}

	return len;
}

int tcp_read_complete(int socket, void *buffer, int size)
{
	return tcp_read(socket, buffer, size, size);
}

static void *thread_func(void *arg)
{
	network.inited = if_configex(&network.address, &network.gateway, &network.netmask, network.use_dhcp);

	if (network.inited < 0) {
		network.disabled = true;
		return NULL;
	}

	CTRInit();
	SNTPInit();
	WIILOADInit();

	return NULL;
}

void NetworkInit(void)
{
	if (network.disabled) return;
	if (LWP_CreateThread(&thread, thread_func, NULL, NULL, 0, LWP_PRIO_NORMAL) < 0)
		network.disabled = true;
}
