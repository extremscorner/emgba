/* 
 * Copyright (c) 2015-2022, Extrems' Corner.org
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef GBI_GX_H
#define GBI_GX_H

#include <stdint.h>
#include <paired.h>
#include <ogc/gx.h>
#include "video.h"

typedef struct {
	uint8_t planes;
	uint8_t slices;
	uint8_t shadows, shadow;
	uint32_t size;
	void **buf;
	void **lutbuf;
	GXTexObj *obj;
	GXTexRegion *region;
	GXTlutObj *lutobj;
	rect_t rect;
	bool dirty;
} gx_surface_t;

typedef union {
	uint32_t addr;
	uint32_t *ptr;
	struct {
		uint32_t   : 10;
		uint32_t y : 10;
		uint32_t x : 10;
		uint32_t   :  2;
	};
} gx_efb32_t;

typedef union {
	uint32_t addr;
	uint64_t *ptr;
	struct {
		uint32_t   :  9;
		uint32_t y : 10;
		uint32_t x : 10;
		uint32_t   :  3;
	};
} gx_efb64_t;

static inline float GXCast1u8f32(uint8_t inval)
{
	float outval;
	asm("psq_l%U1%X1 %0,%1,1,2" : "=f" (outval) : "m" (inval));
	return outval;
}

static inline float GXCast1u16f32(uint16_t inval)
{
	float outval;
	asm("psq_l%U1%X1 %0,%1,1,3" : "=f" (outval) : "m" (inval));
	return outval;
}

static inline vector float GXCast2u8f32(uint8_t inval[2])
{
	vector float outval;
	asm("psq_l%U1%X1 %0,%1,0,2" : "=f" (outval) : "m" (inval));
	return outval;
}

static inline vector float GXCast2u16f32(uint16_t inval[2])
{
	vector float outval;
	asm("psq_l%U1%X1 %0,%1,1,3" : "=f" (outval) : "m" (inval));
	return outval;
}

static inline uint8_t GXCast1f32u8(float inval)
{
	uint8_t outval;
	asm("psq_st%U0%X0 %1,%0,1,2" : "=m" (outval) : "f" (inval));
	return outval;
}

static inline uint16_t GXCast1f32u16(float inval)
{
	uint16_t outval;
	asm("psq_st%U0%X0 %1,%0,1,3" : "=m" (outval) : "f" (inval));
	return outval;
}

void GXInit(void);
void *GXAllocBuffer(uint32_t size);
void GXAllocSurface(gx_surface_t *surface, uint16_t width, uint16_t height, uint8_t format, uint8_t planes);
void GXAllocSurfaceSliced(gx_surface_t *surface, uint16_t width, uint16_t height, uint8_t format, uint8_t slices);
void GXPreloadSurface(gx_surface_t *surface, uint32_t tmem_even, uint32_t tmem_odd, uint8_t count);
void GXPreloadSurfacev(gx_surface_t *surface, uint32_t tmem_even[], uint32_t tmem_odd[], uint8_t count);
void GXCacheSurface(gx_surface_t *surface, uint32_t tmem_even, uint32_t tmem_odd, uint8_t count);
void GXCacheSurfacev(gx_surface_t *surface, uint32_t tmem_even[], uint32_t tmem_odd[], uint8_t count);
void GXSetSurfaceFilt(gx_surface_t *surface, uint8_t filter);
void GXSetSurfaceTlut(gx_surface_t *surface, uint32_t tlut);
void GXFreeSurface(gx_surface_t *surface);
void *GXOpenMem(void *buffer, int size);
void *GXOpenFile(const char *file);
rect_t GXReadRect(void);

void GXCursorDrawPoint(uint32_t index, float x, float y, float angle);
void GXCursorAllocState(void);
void GXCursorSetState(void);

void GXFontAllocState(void);
void GXFontSetState(void);

void GXOverlayDrawRect(rect_t rect);
void GXOverlayAllocState(void);
void GXOverlaySetState(void);
void GXOverlayReadMemEx(void *buffer, int size, int index);
void GXOverlayReadMem(void *buffer, int size, int index);
void GXOverlayReadFile(const char *file, int index);

void GXPackedApplyMix(gx_surface_t *dst, gx_surface_t *src);
void GXPackedApplyYUV(gx_surface_t *dst, gx_surface_t *src);

void GXPlanarApply(gx_surface_t *dst, gx_surface_t *src);
void GXPlanarApplyBlend(gx_surface_t *dst, gx_surface_t *src);
void GXPlanarApplyDeflicker(gx_surface_t *dst, gx_surface_t *src);
void GXPlanarApplyScale2xEx(gx_surface_t *dst, gx_surface_t *src, gx_surface_t *yuv);
void GXPlanarApplyScale2x(gx_surface_t *dst, gx_surface_t *src, bool blend);
void GXPlanarApplyEagle2x(gx_surface_t *dst, gx_surface_t *src);
void GXPlanarApplyScan2x(gx_surface_t *dst, gx_surface_t *src, bool field);
void GXPlanarAllocState(void);

void GXPrescaleApply(gx_surface_t *dst, gx_surface_t *src);
void GXPrescaleApplyDither(gx_surface_t *dst, gx_surface_t *src);
void GXPrescaleApplyDitherFast(gx_surface_t *dst, gx_surface_t *src);
void GXPrescaleApplyBlend(gx_surface_t *dst, gx_surface_t **src, uint8_t *alpha, uint32_t count);
void GXPrescaleApplyBlendDither(gx_surface_t *dst, gx_surface_t **src, uint8_t *alpha, uint32_t count);
void GXPrescaleApplyBlendDitherFast(gx_surface_t *dst, gx_surface_t **src, uint8_t *alpha, uint32_t count);
void GXPrescaleAllocState(void);
rect_t GXPrescaleGetRect(uint16_t width, uint16_t height);

void GXPreviewDrawRect(GXTexObj texobj[3], rect_t dst_rect, rect_t src_rect);
void GXPreviewAllocState(void);
void GXPreviewSetState(uint32_t index);

void GXSolidDrawRect(rect_t rect, uint32_t color[4]);
void GXSolidAllocState(void);
void GXSolidSetState(void);

#endif /* GBI_GX_H */
