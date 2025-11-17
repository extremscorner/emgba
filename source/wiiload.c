/* 
 * Copyright (c) 2015-2025, Extrems' Corner.org
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <malloc.h>
#include <string.h>
#include <ctype.h>
#include <gccore.h>
#include <fcntl.h>
#include <zlib.h>
#include "gba_mb.h"
#include "gx.h"
#include "network.h"
#include "state.h"
#include "stub.h"
#include "wiiload.h"

#ifdef HW_DOL
#define STUB_ADDR  0x80001000
#define STUB_STACK 0x80003000
#else
#define STUB_ADDR  0x90000000
#define STUB_STACK 0x90000800
#endif

static lwp_t thread = LWP_THREAD_NULL;

wiiload_state_t wiiload = {
	.sv.sd = INVALID_SOCKET,

	.sv.sin.sin_family = AF_INET,
	.sv.sin.sin_port = 4299,
	.sv.sin.sin_addr.s_addr = INADDR_ANY,

	.sv.tv.tv_sec  = 1,
	.sv.tv.tv_usec = 0,

	.cl.sd = INVALID_SOCKET,
};

static bool wiiload_read_file(int fd, int size)
{
	void *buf = wiiload.task.buf;
	wiiload.task.buf    = NULL;
	wiiload.task.bufpos = 0;
	wiiload.task.buflen = size;
	buf = realloc(buf, size);

	if (!buf)
		goto fail;
	if (read(fd, buf, size) < size)
		goto fail;

	wiiload.task.buf = buf;
	return true;

fail:
	free(buf);
	return false;
}

static bool wiiload_read(int sd, int insize, int outsize)
{
	Byte inbuf[4096];
	z_stream zstream = {0};
	int ret, pos = 0;

	void *buf = wiiload.task.buf;
	wiiload.task.buf    = NULL;
	wiiload.task.bufpos = 0;
	wiiload.task.buflen = outsize;
	buf = realloc(buf, outsize);

	if (!buf)
		goto fail;
	if (inflateInit(&zstream) < 0)
		goto fail;

	zstream.next_out  = buf;
	zstream.avail_out = outsize;

	while (pos < insize) {
		ret = tcp_read(sd, inbuf, MIN(insize - pos, sizeof(inbuf)), 1);
		if (ret < 0) goto fail;
		else pos += ret;

		zstream.next_in  = inbuf;
		zstream.avail_in = ret;
		ret = inflate(&zstream, Z_NO_FLUSH);
		wiiload.task.bufpos = zstream.total_out;
		if (ret < 0) goto fail;
	}

	inflateEnd(&zstream);
	wiiload.task.buf = buf;
	return true;

fail:
	inflateEnd(&zstream);
	free(buf);
	return false;
}

static bool wiiload_read_args(int sd, int size)
{
	void *arg = wiiload.task.arg;
	wiiload.task.arg    = NULL;
	wiiload.task.arglen = size;
	arg = realloc(arg, size);

	if (!arg)
		goto fail;
	if (tcp_read_complete(sd, arg, size) < size)
		goto fail;

	wiiload.task.arg = arg;
	return true;

fail:
	free(arg);
	return false;
}

static bool is_type_tpl(void *buffer, int size)
{
	tpl_header_t *header = buffer;

	if (size < sizeof(*header))
		return false;
	if (header->version != 2142000)
		return false;
	if (header->count == 0)
		return false;
	if (header->size != sizeof(*header))
		return false;

	return true;
}

static bool is_type_mb(void *buffer, int size)
{
	if (size < 0xC0 || size > 0x40000)
		return false;
	if (memcmp(buffer + 0x04, gba_mb + 0x04, 0x9C))
		return false;

	return true;
}

static bool is_type_gci(void *buffer, int size)
{
	gci_header_t *header = buffer;

	if (size < sizeof(*header))
		return false;
	if (size != sizeof(*header) + header->length * 8192)
		return false;
	if (header->length < 1 || header->length > 2043)
		return false;
	if (header->padding0 != 0xFF || header->padding1 != 0xFFFF)
		return false;
	if (header->icon_offset != -1 && header->icon_offset > 512)
		return false;
	if (header->comment_offset != -1 && header->comment_offset > 8128)
		return false;

	for (int i = 0; i < 4; i++)
		if (!isalnum(header->gamecode[i]))
			return false;

	for (int i = 0; i < 2; i++)
		if (!isalnum(header->company[i]))
			return false;

	return true;
}

static bool is_type_dol(void *buffer, int size)
{
	dol_header_t *header = buffer;

	if (size < sizeof(*header))
		return false;

	for (int i = 0; i < 7; i++)
		if (header->padding[i])
			return false;

	for (int i = 0; i < 7; i++) {
		if (header->text_size[i]) {
			if (header->text_offset[i] < sizeof(*header))
				return false;
			if ((header->text_address[i] & SYS_BASE_UNCACHED) != SYS_BASE_CACHED)
				return false;
		}
	}

	for (int i = 0; i < 11; i++) {
		if (header->data_size[i]) {
			if (header->data_offset[i] < sizeof(*header))
				return false;
			if ((header->data_address[i] & SYS_BASE_UNCACHED) != SYS_BASE_CACHED)
				return false;
		}
	}

	if (header->bss_size) {
		if ((header->bss_address & SYS_BASE_UNCACHED) != SYS_BASE_CACHED)
			return false;
	}

	if ((header->entrypoint & SYS_BASE_UNCACHED) != SYS_BASE_CACHED)
		return false;

	return true;
}

static bool wiiload_handler(int sd)
{
	wiiload_header_t header;

	if (tcp_read_complete(sd, &header, sizeof(header)) < sizeof(header))
		return false;
	if (header.magic != 'HAXX')
		return false;
	if (header.version != 5)
		return false;

	wiiload.task.type = TYPE_NONE;

	if (wiiload_read(sd, header.deflate_size, header.inflate_size) &&
		wiiload_read_args(sd, header.args_size)) {

		if (is_type_tpl(wiiload.task.buf, wiiload.task.buflen)) {
			wiiload.task.type = TYPE_TPL;
			GXOverlayReadMem(wiiload.task.buf, wiiload.task.buflen, state.overlay_id);
		} else if (is_type_mb(wiiload.task.buf, wiiload.task.buflen)) {
			wiiload.task.type = TYPE_MB;
			if (state.draw_osd) state.reset = true;
		} else if (is_type_gci(wiiload.task.buf, wiiload.task.buflen)) {
			wiiload.task.type = TYPE_GCI;
		} else if (is_type_dol(wiiload.task.buf, wiiload.task.buflen)) {
			wiiload.task.type = TYPE_DOL;
			if (state.draw_osd) state.quit |= KEY_QUIT;
		}
	}

	return true;
}

static void *thread_func(void *arg)
{
	wiiload.sv.sd = net_socket(AF_INET, SOCK_STREAM, IPPROTO_IP);

	if (wiiload.sv.sd == INVALID_SOCKET)
		goto fail;
	if (net_bind(wiiload.sv.sd, &wiiload.sv.sa, sizeof(wiiload.sv.sin)) < 0)
		goto fail;
	if (net_listen(wiiload.sv.sd, 0) < 0)
		goto fail;

	do {
		#ifdef HW_DOL
		fd_set readset;
		FD_ZERO(&readset);
		FD_SET(wiiload.sv.sd, &readset);
		net_select(FD_SETSIZE, &readset, NULL, NULL, &wiiload.sv.tv);

		if (FD_ISSET(wiiload.sv.sd, &readset)) {
		#else
		struct pollsd psd = {wiiload.sv.sd, POLLIN, 0};
		net_poll(&psd, 1, 1000);

		if (psd.revents & POLLIN) {
		#endif
			socklen_t addrlen = sizeof(wiiload.cl.sin);
			wiiload.cl.sd = net_accept(wiiload.sv.sd, &wiiload.cl.sa, &addrlen);

			if (wiiload.cl.sd != INVALID_SOCKET) {
				wiiload_handler(wiiload.cl.sd);
				net_close(wiiload.cl.sd);
				wiiload.cl.sd = INVALID_SOCKET;
			}
		}
	} while (!state.quit);

fail:
	net_close(wiiload.sv.sd);
	wiiload.sv.sd = INVALID_SOCKET;
	return NULL;
}

void WIILOADInit(void)
{
	LWP_CreateThread(&thread, thread_func, NULL, NULL, 0, LWP_PRIO_NORMAL);
}

void WIILOADLoad(void)
{
	LWP_JoinThread(thread, NULL);
	thread = LWP_THREAD_NULL;

	#ifdef HW_DOL
	if (wiiload.task.type != TYPE_DOL)
		WIILOADReadFile("/AUTOEXEC.DOL");
	#endif

	if (wiiload.task.type == TYPE_DOL) {
		memcpy((void *)STUB_ADDR, stub, stub_size);
		DCStoreRange((void *)STUB_ADDR, stub_size);

		SYS_ResetSystem(SYS_SHUTDOWN);
		SYS_SwitchFiber((intptr_t)wiiload.task.buf, wiiload.task.buflen,
		                (intptr_t)wiiload.task.arg, wiiload.task.arglen,
		                STUB_ADDR, STUB_STACK);
	}
}

bool WIILOADReadFile(const char *file)
{
	int fd = open(file, O_RDONLY);
	struct stat st;

	if (fd < 0)
		return false;
	if (fstat(fd, &st) < 0)
		return false;

	wiiload.task.type = TYPE_NONE;

	if (wiiload_read_file(fd, st.st_size))
		wiiload.task.type = TYPE_DOL;

	close(fd);
	return true;
}
