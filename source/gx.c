/* 
 * Copyright (c) 2015-2020, Extrems' Corner.org
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <malloc.h>
#include <math.h>
#include <stdio.h>
#include <gccore.h>
#include <ogc/machine/asm.h>
#include <zlib.h>
#include "gx.h"
#include "state.h"
#include "util.h"

static void *fifo;
static GXTexRegion texregion[24];
static GXTlutRegion tlutregion[20];

static GXTexRegion *texregion_cb(GXTexObj *texobj, uint8_t mapid)
{
	uint8_t format = GX_GetTexObjFmt(texobj);
	uint8_t mipmap = GX_GetTexObjMipMap(texobj);

	if (format == GX_TF_CI4 || format == GX_TF_CI8 || format == GX_TF_CI14)
		return &texregion[mapid];

	if (format == GX_TF_RGBA8) {
		if (mipmap)
			return &texregion[mapid + 16];
		else
			return &texregion[mapid + 8];
	} else {
		if (mipmap)
			return &texregion[mapid + 8];
		else
			return &texregion[mapid];
	}
}

static GXTlutRegion *tlutregion_cb(uint32_t tlut)
{
	return &tlutregion[tlut];
}

void GXInit(void)
{
	Mtx m;

	fifo = GXAllocBuffer(GX_FIFO_MINSIZE);
	GX_Init(fifo, GX_FIFO_MINSIZE);

	CAST_SetGQR2(GQR_TYPE_U8,   8);
	CAST_SetGQR3(GQR_TYPE_U16, 16);

	switch (lrintf(state.output_gamma * 10.)) {
		case 10: GX_SetDispCopyGamma(GX_GM_1_0); break;
		case 17: GX_SetDispCopyGamma(GX_GM_1_7); break;
		case 22: GX_SetDispCopyGamma(GX_GM_2_2); break;
	}

	for (int i = GX_TEXCOORD0; i < GX_MAXCOORD; i++)
		GX_SetTexCoordScaleManually(i, GX_TRUE, 1, 1);

	for (int i = 0; i < 10; i++) {
		float s = 1 << (i + 1);

		guMtxScale(m, s, s, s);
		GX_LoadTexMtxImm(m, GX_TEXMTX0 + i * 3, GX_MTX3x4);
	}

	guMtxTrans(m, 1./64., 1./64., 0);
	GX_LoadTexMtxImm(m, GX_DTTIDENTITY, GX_MTX3x4);

	for (int i = 0; i < 9; i++) {
		float x = i % 3 - 1;
		float y = i / 3 - 1;

		if (i % 2 == 0) {
			x /= 2;
			y /= 2;
		}

		x += 1./64.;
		y += 1./64.;

		guMtxTrans(m, x, y, 0);
		GX_LoadTexMtxImm(m, GX_DTTMTX1 + i * 3, GX_MTX3x4);
	}

	for (int i = 0; i < 4; i++) {
		GX_InitTexCacheRegion(&texregion[i +  0], GX_FALSE, 0x00000 + i * 0x10000, GX_TEXCACHE_32K, 0x08000 + i * 0x10000, GX_TEXCACHE_32K);
		GX_InitTexCacheRegion(&texregion[i +  8], GX_FALSE, 0x00000 + i * 0x10000, GX_TEXCACHE_32K, 0x80000 + i * 0x10000, GX_TEXCACHE_32K);
		GX_InitTexCacheRegion(&texregion[i + 16], GX_TRUE,  0x00000 + i * 0x10000, GX_TEXCACHE_32K, 0x80000 + i * 0x10000, GX_TEXCACHE_32K);
	}

	for (int i = 0; i < 4; i++) {
		GX_InitTexCacheRegion(&texregion[i +  4], GX_FALSE, 0x08000 + i * 0x10000, GX_TEXCACHE_32K, 0x00000 + i * 0x10000, GX_TEXCACHE_32K);
		GX_InitTexCacheRegion(&texregion[i + 12], GX_FALSE, 0x08000 + i * 0x10000, GX_TEXCACHE_32K, 0x88000 + i * 0x10000, GX_TEXCACHE_32K);
		GX_InitTexCacheRegion(&texregion[i + 20], GX_TRUE,  0x80000 + i * 0x10000, GX_TEXCACHE_32K, 0x00000 + i * 0x10000, GX_TEXCACHE_32K);
	}

	for (int i = 0; i < 16; i++)
		GX_InitTlutRegion(&tlutregion[i +  0], 0xC0000 + i * 0x2000, GX_TLUT_256);

	for (int i = 0; i < 4; i++)
		GX_InitTlutRegion(&tlutregion[i + 16], 0xC0000 + i * 0x8000, GX_TLUT_1K);

	GX_SetTexRegionCallback(texregion_cb);
	GX_SetTlutRegionCallback(tlutregion_cb);
}

void *GXAllocBuffer(uint32_t size)
{
	void *ptr = memalign(PPC_CACHE_ALIGNMENT, size);
	if (ptr) DCZeroRange(ptr, size);
	return ptr;
}

static double trc_gamma(int ch, double f) {
	double V = fabs(f);
	double L = pow(V, state.input_gamma[ch]);
	return copysign(L, f);
}

static double trc_itu709(int ch, double f) {
	double V = fabs(f);
	double L = V <= .081 ? V / 4.5 : pow((V + .099) / 1.099, 1/.45);
	return copysign(L, f);
}

static double trc_smpte240(int ch, double f) {
	double V = fabs(f);
	double L = V <= .0912 ? V / 4. : pow((V + .1115) / 1.1115, 1/.45);
	return copysign(L, f);
}

static double trc_linear(int ch, double f) {
	return f;
}

static double (*trc_funcs[])(int, double) = {
	trc_gamma,
	trc_itu709,
	trc_smpte240,
	trc_linear
};

static void fill_lut(int ch, double (*func)(int, double), hword_t *lut)
{
	double a = func(ch, state.contrast[ch]), b = state.brightness[ch] / state.contrast[ch];

	for (int i = 0; i < 256; i++) {
		double f = a * func(ch, i / 255. + b);

		if (state.dither) {
			lut[i].u16 = f * 65535. + .5;
		} else {
			lut[i].u8[0] = f * 255. + .5;
			lut[i].u8[1] = 0;
		}
	}

	DCStoreRange(lut, 256 * sizeof(hword_t));
}

void GXAllocSurface(gx_surface_t *surface, uint16_t width, uint16_t height, uint8_t format, uint8_t planes)
{
	uint32_t size = GX_GetTexBufferSize(width, height, format, GX_FALSE, 0);

	surface->planes = planes;
	surface->slices = 0;
	surface->size = size;

	surface->buf = calloc(planes, sizeof(void *));
	surface->lutbuf = calloc(planes, sizeof(void *));

	surface->obj = calloc(planes, sizeof(GXTexObj));
	surface->lutobj = calloc(planes, sizeof(GXTlutObj));

	for (int i = 0; i < surface->planes; i++) {
		surface->buf[i] = GXAllocBuffer(surface->size);

		switch (format) {
			case GX_TF_CI4:
				break;
			case GX_TF_CI8:
				surface->lutbuf[i] = GXAllocBuffer(256 * sizeof(hword_t));
				fill_lut(i, trc_funcs[state.input_trc], surface->lutbuf[i]);

				GX_InitTlutObj(&surface->lutobj[i], surface->lutbuf[i], GX_TL_IA8, 256);
				GX_InitTexObjCI(&surface->obj[i], surface->buf[i], width, height, format, GX_CLAMP, GX_CLAMP, GX_FALSE, GX_TLUT0 + i);
				break;
			case GX_TF_CI14:
				break;
			default:
				GX_InitTexObj(&surface->obj[i], surface->buf[i], width, height, format, GX_CLAMP, GX_CLAMP, GX_FALSE);
		}

		GX_InitTexObjUserData(&surface->obj[i], surface);
	}
}

void GXAllocSurfaceSliced(gx_surface_t *surface, uint16_t width, uint16_t height, uint8_t format, uint8_t slices)
{
	uint32_t size = GX_GetTexBufferSize(width, height, format, GX_FALSE, 0);

	surface->planes = 1;
	surface->slices = slices;
	surface->size = size;

	surface->buf = calloc(slices, sizeof(void *));
	surface->lutbuf = calloc(1, sizeof(void *));

	surface->obj = calloc(1 + slices, sizeof(GXTexObj));
	surface->lutobj = calloc(1, sizeof(GXTlutObj));

	*surface->buf = GXAllocBuffer(surface->size * surface->slices);

	switch (format) {
		case GX_TF_CI4:
			break;
		case GX_TF_CI8:
			break;
		case GX_TF_CI14:
			break;
		default:
			GX_InitTexObj(&surface->obj[slices], *surface->buf, width, height * slices, format, GX_CLAMP, GX_CLAMP, GX_FALSE);
	}

	GX_InitTexObjUserData(&surface->obj[slices], surface);

	for (int i = 0; i < surface->slices; i++) {
		surface->buf[i] = *surface->buf + i * surface->size;

		switch (format) {
			case GX_TF_CI4:
				break;
			case GX_TF_CI8:
				break;
			case GX_TF_CI14:
				break;
			default:
				GX_InitTexObj(&surface->obj[i], surface->buf[i], width, height, format, GX_CLAMP, GX_CLAMP, GX_FALSE);
		}

		GX_InitTexObjUserData(&surface->obj[i], surface);
	}
}

void GXPreloadSurface(gx_surface_t *surface, uint32_t tmem_even, uint32_t tmem_odd, uint8_t count)
{
	surface->shadows = count;
	surface->shadow = 0;

	surface->region = calloc(count, sizeof(GXTexRegion));

	if (tmem_even == tmem_odd) {
		for (int i = 0; i < count; i++) {
			GX_InitTexPreloadRegion(&surface->region[i], tmem_even, surface->size, 0x00000, 0);
			GX_PreloadEntireTexture(&surface->obj[0], &surface->region[i]);
			tmem_even += surface->size;
		}
	} else {
		for (int i = 0; i < count; i++) {
			GX_InitTexPreloadRegion(&surface->region[i], tmem_even, surface->size / 2, tmem_odd, surface->size / 2);
			GX_PreloadEntireTexture(&surface->obj[0], &surface->region[i]);
			tmem_even += surface->size / 2;
			tmem_odd  += surface->size / 2;
		}
	}
}

void GXPreloadSurfacev(gx_surface_t *surface, uint32_t tmem_even[], uint32_t tmem_odd[], uint8_t count)
{
	surface->shadows = count;
	surface->shadow = 0;

	surface->region = calloc(count, sizeof(GXTexRegion));

	if (!tmem_odd) {
		for (int i = 0; i < count; i++) {
			GX_InitTexPreloadRegion(&surface->region[i], tmem_even[i], surface->size, 0x00000, 0);
			GX_PreloadEntireTexture(&surface->obj[0], &surface->region[i]);
		}
	} else {
		for (int i = 0; i < count; i++) {
			GX_InitTexPreloadRegion(&surface->region[i], tmem_even[i], surface->size / 2, tmem_odd[i], surface->size / 2);
			GX_PreloadEntireTexture(&surface->obj[0], &surface->region[i]);
		}
	}
}

void GXCacheSurface(gx_surface_t *surface, uint32_t tmem_even, uint32_t tmem_odd, uint8_t count)
{
	surface->shadows = count;
	surface->shadow = 0;

	surface->region = calloc(count, sizeof(GXTexRegion));

	if (tmem_even == tmem_odd) {
		for (int i = 0; i < count; i++) {
			GX_InitTexCacheRegion(&surface->region[i], GX_FALSE, tmem_even, GX_TEXCACHE_32K, 0x00000, GX_TEXCACHE_NONE);
			GX_InvalidateTexRegion(&surface->region[i]);
			tmem_even += 0x10000;
		}
	} else {
		for (int i = 0; i < count; i++) {
			GX_InitTexCacheRegion(&surface->region[i], GX_FALSE, tmem_even, GX_TEXCACHE_32K, tmem_odd, GX_TEXCACHE_32K);
			GX_InvalidateTexRegion(&surface->region[i]);
			tmem_even += 0x10000;
			tmem_odd  += 0x10000;
		}
	}
}

void GXCacheSurfacev(gx_surface_t *surface, uint32_t tmem_even[], uint32_t tmem_odd[], uint8_t count)
{
	surface->shadows = count;
	surface->shadow = 0;

	surface->region = calloc(count, sizeof(GXTexRegion));

	if (!tmem_odd) {
		for (int i = 0; i < count; i++) {
			GX_InitTexCacheRegion(&surface->region[i], GX_FALSE, tmem_even[i], GX_TEXCACHE_32K, 0x00000, GX_TEXCACHE_NONE);
			GX_InvalidateTexRegion(&surface->region[i]);
		}
	} else {
		for (int i = 0; i < count; i++) {
			GX_InitTexCacheRegion(&surface->region[i], GX_FALSE, tmem_even[i], GX_TEXCACHE_32K, tmem_odd[i], GX_TEXCACHE_32K);
			GX_InvalidateTexRegion(&surface->region[i]);
		}
	}
}

void GXSetSurfaceFilt(gx_surface_t *surface, uint8_t filter)
{
	switch (filter) {
		case GX_NEAR:
			for (int i = 0; i < surface->planes + surface->slices; i++)
				GX_InitTexObjLOD(&surface->obj[i], GX_NEAR, GX_NEAR, 0., 0., 0., GX_FALSE, GX_FALSE, GX_ANISO_1);
			break;
		case GX_LINEAR:
			for (int i = 0; i < surface->planes + surface->slices; i++)
				GX_InitTexObjLOD(&surface->obj[i], GX_LINEAR, GX_LINEAR, 0., 0., 0., GX_TRUE, GX_TRUE, GX_ANISO_4);
			break;
	}
}

void GXSetSurfaceTlut(gx_surface_t *surface, uint32_t tlut)
{
	for (int i = 0; i < surface->planes + surface->slices; i++)
		GX_InitTexObjTlut(&surface->obj[i], tlut);
}

void GXFreeSurface(gx_surface_t *surface)
{
	bulk_free(surface->buf, surface->planes);
	bulk_free(surface->lutbuf, surface->planes);

	free(surface->buf);
	free(surface->lutbuf);

	free(surface->obj);
	free(surface->region);
	free(surface->lutobj);

	surface->planes = 0;
	surface->slices = 0;
	surface->size = 0;

	surface->buf = NULL;
	surface->lutbuf = NULL;

	surface->obj = NULL;
	surface->region = NULL;
	surface->lutobj = NULL;
}

void *GXOpenMem(void *buffer, int size)
{
	return fmemopen(buffer, size, "rb");
}

void *GXOpenFile(const char *file)
{
	gzFile zfp = gzopen(file, "rb");

	return funopen(zfp,
		(int (*)(void *, char *, int))gzread,
		(int (*)(void *, const char *, int))gzwrite,
		(fpos_t (*)(void *, fpos_t, int))gzseek,
		(int (*)(void *))gzclose);
}

rect_t GXReadRect(void)
{
	rect_t rect;
	uint16_t top, bottom, left, right;

	GX_ReadBoundingBox(&top, &bottom, &left, &right);

	rect.x = viewport.x + left;
	rect.y = viewport.y + top;
	rect.w = right - left + 1;
	rect.h = bottom - top + 1;

	return rect;
}
