/* 
 * Copyright (c) 2015-2022, Extrems' Corner.org
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef GBI_NETWORK_H
#define GBI_NETWORK_H

#include <network.h>

typedef struct {
	int inited;
	struct in_addr address;
	struct in_addr gateway;
	struct in_addr netmask;
	bool use_dhcp;
	bool disabled;
} network_state_t;

extern network_state_t network;

int tcp_read(int socket, void *buffer, int size, int minsize);
int tcp_read_complete(int socket, void *buffer, int size);

void NetworkInit(void);

#endif /* GBI_NETWORK_H */
