/* 
 * Copyright (c) 2015-2024, Extrems' Corner.org
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef GBI_SNTP_H
#define GBI_SNTP_H

#include <network.h>

#define DIFF_SEC_1900_2000 3155673600UL

typedef struct {
	struct {
		int sd;
		struct sockaddr_in sin;
		socklen_t sinlen;
	} sv;
} sntp_state_t;

extern sntp_state_t sntp;

typedef struct {
	uint8_t flags;
	uint8_t stratum;
	int8_t poll;
	int8_t precision;
	uint32_t root_delay;
	uint32_t root_dispersion;
	uint32_t reference_id;
	uint64_t reference_time;
	uint64_t origin_time;
	uint64_t receive_time;
	uint64_t transmit_time;
} ATTRIBUTE_PACKED sntp_packet_t;

void SNTPInit(void);

#endif /* GBI_SNTP_H */
