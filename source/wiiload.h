/* 
 * Copyright (c) 2015-2020, Extrems' Corner.org
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef GBI_WIILOAD_H
#define GBI_WIILOAD_H

#include <network.h>

typedef struct {
	struct {
		int sd;
		struct sockaddr_in sin;
		socklen_t sinlen;
	} sv;

	struct {
		int sd;
		struct sockaddr_in sin;
		socklen_t sinlen;
	} cl;

	struct {
		void *buf;
		uint32_t bufpos;
		uint32_t buflen;
		void *arg;
		uint32_t arglen;

		enum {
			TYPE_NONE = 0,
			TYPE_BIN,
			TYPE_DOL,
			TYPE_ELF,
			TYPE_GCI,
			TYPE_MB,
			TYPE_TPL,
			TYPE_ZIP,
		} type;
	} task;
} wiiload_state_t;

extern wiiload_state_t wiiload;

typedef struct {
	uint32_t magic;
	uint16_t version;
	uint16_t args_size;
	uint32_t deflate_size;
	uint32_t inflate_size;
} ATTRIBUTE_PACKED wiiload_header_t;

typedef struct {
	uint32_t text_offset[7];
	uint32_t data_offset[11];
	uint32_t text_address[7];
	uint32_t data_address[11];
	uint32_t text_size[7];
	uint32_t data_size[11];
	uint32_t bss_address;
	uint32_t bss_size;
	uint32_t entrypoint;
	uint32_t padding[7];
} ATTRIBUTE_PACKED dol_header_t;

typedef struct {
	uint8_t gamecode[4];
	uint8_t company[2];
	uint8_t padding0;
	uint8_t banner_format;
	uint8_t filename[32];
	uint32_t time;
	uint32_t icon_offset;
	uint16_t icon_format;
	uint16_t icon_speed;
	uint8_t permissions;
	uint8_t copies;
	uint16_t block;
	uint16_t length;
	uint16_t padding1;
	uint32_t comment_offset;
} ATTRIBUTE_PACKED gci_header_t;

typedef struct {
	uint32_t version;
	uint32_t count;
	uint32_t size;
} ATTRIBUTE_PACKED tpl_header_t;

void WIILOADInit(void);
void WIILOADLoad(void);
bool WIILOADReadFile(const char *file);

#endif /* GBI_WIILOAD_H */
