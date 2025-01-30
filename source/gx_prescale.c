/* 
 * Copyright (c) 2015-2025, Extrems' Corner.org
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <malloc.h>
#include <math.h>
#include <gccore.h>
#include "gx.h"
#include "state.h"
#include "util.h"

static float texmtx[][3][4] ATTRIBUTE_ALIGN(32) = {
	{
		{ +1., +0., +0., +1./64. },
		{ +0., +1., +0., +1./64. },
		{ +0., +0., +1., +0.     }
	}, {
		{ +0., +1., +0., +1./64. },
		{ -1., +0., +0., +1./64. },
		{ +0., +0., +1., +0.     }
	}, {
		{ -1., +0., +0., +1./64. },
		{ +0., -1., +0., +1./64. },
		{ +0., +0., +1., +0.     }
	}, {
		{ +0., -1., +0., +1./64. },
		{ +1., +0., +0., +1./64. },
		{ +0., +0., +1., +0.     }
	},
};

static uint8_t texdata[][8 * 8] ATTRIBUTE_ALIGN(32) = {
	{
		0x60, 0x90, 0x6C, 0x9C, 0x63, 0x93, 0x6F, 0x9F,
		0x80, 0x70, 0x8C, 0x7C, 0x83, 0x73, 0x8F, 0x7F,
		0x68, 0x98, 0x64, 0x94, 0x6B, 0x9B, 0x67, 0x97,
		0x88, 0x78, 0x84, 0x74, 0x8B, 0x7B, 0x87, 0x77,
		0x62, 0x92, 0x6E, 0x9E, 0x61, 0x91, 0x6D, 0x9D,
		0x82, 0x72, 0x8E, 0x7E, 0x81, 0x71, 0x8D, 0x7D,
		0x6A, 0x9A, 0x66, 0x96, 0x69, 0x99, 0x65, 0x95,
		0x8A, 0x7A, 0x86, 0x76, 0x89, 0x79, 0x85, 0x75,
	}, {
		0x78, 0x6A, 0x6C, 0x7A, 0x83, 0x8F, 0x91, 0x85,
		0x68, 0x60, 0x62, 0x6E, 0x8D, 0x9B, 0x9D, 0x93,
		0x76, 0x66, 0x64, 0x70, 0x8B, 0x99, 0x9F, 0x95,
		0x7E, 0x74, 0x72, 0x7C, 0x81, 0x89, 0x97, 0x87,
		0x82, 0x8E, 0x90, 0x84, 0x79, 0x6B, 0x6D, 0x7B,
		0x8C, 0x9A, 0x9C, 0x92, 0x69, 0x61, 0x63, 0x6F,
		0x8A, 0x98, 0x9E, 0x94, 0x77, 0x67, 0x65, 0x71,
		0x80, 0x88, 0x96, 0x86, 0x7F, 0x75, 0x73, 0x7D,
	}
};

static GXTexObj texobj;

static uint16_t tlutdata[GX_MAX_TEXMAP][3][256] ATTRIBUTE_ALIGN(32);
static GXTlutObj tlutobj[GX_MAX_TEXMAP][3];

static void GXPrescaleCopyChannel(GXTexObj texobj, rect_t dst_rect, rect_t src_rect, uint8_t channel)
{
	void *ptr;
	uint16_t width, height;
	uint8_t format, wrap_s, wrap_t, mipmap;

	GX_GetTexObjAll(&texobj, &ptr, &width, &height, &format, &wrap_s, &wrap_t, &mipmap);

	if (channel == GX_CH_RED) {
		GX_SetScissor(0, 0, width << mipmap, height << mipmap);
		GX_SetScissorBoxOffset(0, 0);
		GX_ClearBoundingBox();
	}

	GX_Begin(GX_QUADS, GX_VTXFMT0, 4);

	GX_Position2s16(dst_rect.x, dst_rect.y);
	GX_TexCoord2s16(src_rect.x, src_rect.y);

	GX_Position2s16(dst_rect.x + dst_rect.w, dst_rect.y);
	GX_TexCoord2s16(src_rect.x + src_rect.w, src_rect.y);

	GX_Position2s16(dst_rect.x + dst_rect.w, dst_rect.y + dst_rect.h);
	GX_TexCoord2s16(src_rect.x + src_rect.w, src_rect.y + src_rect.h);

	GX_Position2s16(dst_rect.x, dst_rect.y + dst_rect.h);
	GX_TexCoord2s16(src_rect.x, src_rect.y + src_rect.h);

	GX_SetTexCopySrc(0, 0, width << mipmap, height << mipmap);
	GX_SetTexCopyDst(width, height, GX_CTF_R8, mipmap);

	GX_CopyTex(ptr, channel == GX_CH_BLUE ? GX_TRUE : GX_FALSE);
}

void GXPrescaleApply(gx_surface_t *dst, gx_surface_t *src)
{
	Mtx44 projection;
	guOrtho(projection, 0., 1024., 0., 1024., 0., 1.);

	GX_SetBlendMode(GX_BM_NONE, GX_BL_ZERO, GX_BL_ZERO, GX_LO_CLEAR);
	GX_SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
	GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
	GX_SetZCompLoc(GX_FALSE);

	GX_SetNumChans(0);
	GX_SetNumTexGens(1);
	GX_SetNumIndStages(0);
	GX_SetNumTevStages(1);

	GX_SetTevSwapModeTable(GX_TEV_SWAP0, GX_CH_RED, GX_CH_GREEN, GX_CH_BLUE, GX_CH_ALPHA);

	GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLORNULL);
	GX_SetTevColorIn(GX_TEVSTAGE0, GX_CC_TEXA, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO);
	GX_SetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
	GX_SetTevDirect(GX_TEVSTAGE0);

	GX_ClearVtxDesc();
	GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XY, GX_S16, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_S16, 0);

	GX_LoadProjectionMtx(projection, GX_ORTHOGRAPHIC);
	GX_SetCurrentMtx(GX_PNMTX0);
	GX_SetViewport(0., 0., 1024., 1024., 0., 1.);

	GX_SetFieldMask(GX_TRUE, GX_TRUE);
	GX_SetFieldMode(GX_FALSE, GX_FALSE);

	GX_SetPixelFmt(GX_PF_Y8, GX_ZC_LINEAR);
	GX_SetCopyFilter(GX_FALSE, NULL, GX_FALSE, NULL);

	GX_LoadTlut(&src->lutobj[GX_CH_RED],   GX_TLUT0);
	GX_LoadTlut(&src->lutobj[GX_CH_GREEN], GX_TLUT1);
	GX_LoadTlut(&src->lutobj[GX_CH_BLUE],  GX_TLUT2);

	for (int ch = GX_CH_RED; ch <= GX_CH_BLUE; ch++) {
		if (src->dirty) GX_PreloadEntireTexture(&src->obj[ch], &src->region[ch]);
		GX_LoadTexObjPreloaded(&src->obj[ch], &src->region[ch], GX_TEXMAP0);

		GXPrescaleCopyChannel(dst->obj[ch], dst->rect, src->rect, ch);
	}

	dst->dirty = true; src->dirty = false;
}

void GXPrescaleApplyDither(gx_surface_t *dst, gx_surface_t *src)
{
	Mtx44 projection;
	guOrtho(projection, 0., 1024., 0., 1024., 0., 1.);

	GX_SetBlendMode(GX_BM_NONE, GX_BL_ZERO, GX_BL_ZERO, GX_LO_CLEAR);
	GX_SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
	GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
	GX_SetZCompLoc(GX_FALSE);

	GX_SetNumChans(0);
	GX_SetNumTexGens(2);
	GX_SetNumIndStages(0);
	GX_SetNumTevStages(3);

	GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
	GX_SetTexCoordGen(GX_TEXCOORD1, GX_TG_MTX2x4, GX_TG_POS, GX_IDENTITY);

	GX_SetTevSwapModeTable(GX_TEV_SWAP0, GX_CH_RED, GX_CH_GREEN, GX_CH_BLUE, GX_CH_ALPHA);

	GX_SetTevKColor(GX_KCOLOR0, (GXColor){4, 4, 4, 4});

	GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLORNULL);
	GX_SetTevKColorSel(GX_TEVSTAGE0, GX_TEV_KCSEL_1_4);
	GX_SetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_TEXC, GX_CC_KONST, GX_CC_ZERO);
	GX_SetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_FALSE, GX_TEVPREV);
	GX_SetTevAlphaIn(GX_TEVSTAGE0, GX_CA_TEXA, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO);
	GX_SetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_FALSE, GX_TEVPREV);
	GX_SetTevDirect(GX_TEVSTAGE0);

	GX_SetTevOrder(GX_TEVSTAGE1, GX_TEXCOORD1, GX_TEXMAP7, GX_COLORNULL);
	GX_SetTevColorIn(GX_TEVSTAGE1, GX_CC_TEXA, GX_CC_ZERO, GX_CC_ZERO, GX_CC_CPREV);
	GX_SetTevColorOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_SUBHALF, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
	GX_SetTevAlphaIn(GX_TEVSTAGE1, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_APREV);
	GX_SetTevAlphaOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
	GX_SetTevDirect(GX_TEVSTAGE1);

	GX_SetTevOrder(GX_TEVSTAGE2, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLORNULL);
	GX_SetTevKColorSel(GX_TEVSTAGE2, GX_TEV_KCSEL_K0);
	GX_SetTevColorIn(GX_TEVSTAGE2, GX_CC_ZERO, GX_CC_CPREV, GX_CC_KONST, GX_CC_APREV);
	GX_SetTevColorOp(GX_TEVSTAGE2, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
	GX_SetTevDirect(GX_TEVSTAGE2);

	GX_ClearVtxDesc();
	GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XY, GX_S16, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_S16, 0);

	GX_LoadProjectionMtx(projection, GX_ORTHOGRAPHIC);
	GX_SetCurrentMtx(GX_PNMTX0);
	GX_SetViewport(0., 0., 1024., 1024., 0., 1.);

	GX_SetFieldMask(GX_TRUE, GX_TRUE);
	GX_SetFieldMode(GX_FALSE, GX_FALSE);

	GX_SetPixelFmt(GX_PF_Y8, GX_ZC_LINEAR);
	GX_SetCopyFilter(GX_FALSE, NULL, GX_FALSE, NULL);

	GX_LoadTlut(&src->lutobj[GX_CH_RED],   GX_TLUT0);
	GX_LoadTlut(&src->lutobj[GX_CH_GREEN], GX_TLUT1);
	GX_LoadTlut(&src->lutobj[GX_CH_BLUE],  GX_TLUT2);

	for (int ch = GX_CH_RED; ch <= GX_CH_BLUE; ch++) {
		if (src->dirty) GX_PreloadEntireTexture(&src->obj[ch], &src->region[ch]);
		GX_LoadTexObjPreloaded(&src->obj[ch], &src->region[ch], GX_TEXMAP0);

		GX_LoadTexObj(&texobj, GX_TEXMAP7);

		GXPrescaleCopyChannel(dst->obj[ch], dst->rect, src->rect, ch);
	}

	dst->dirty = true; src->dirty = false;
}

void GXPrescaleApplyDitherFast(gx_surface_t *dst, gx_surface_t *src)
{
	Mtx44 projection;
	guOrtho(projection, 0., 1024., 0., 1024., 0., 1.);

	GX_SetBlendMode(GX_BM_NONE, GX_BL_ZERO, GX_BL_ZERO, GX_LO_CLEAR);
	GX_SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
	GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
	GX_SetZCompLoc(GX_FALSE);

	GX_SetNumChans(0);
	GX_SetNumTexGens(2);
	GX_SetNumIndStages(0);
	GX_SetNumTevStages(2);

	GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
	GX_SetTexCoordGen2(GX_TEXCOORD1, GX_TG_MTX2x4, GX_TG_POS, GX_IDENTITY, GX_FALSE, GX_DTTMTX0);

	GX_SetTevSwapModeTable(GX_TEV_SWAP0, GX_CH_RED, GX_CH_GREEN, GX_CH_BLUE, GX_CH_ALPHA);

	GX_SetTevKColor(GX_KCOLOR0, (GXColor){4, 4, 4, 4});

	GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLORNULL);
	GX_SetTevKColorSel(GX_TEVSTAGE0, GX_TEV_KCSEL_1_4);
	GX_SetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_TEXC, GX_CC_KONST, GX_CC_A0);
	GX_SetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
	GX_SetTevAlphaIn(GX_TEVSTAGE0, GX_CA_TEXA, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO);
	GX_SetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
	GX_SetTevDirect(GX_TEVSTAGE0);

	if (state.dither > DITHER_THRESHOLD) {
		GX_SetTevOrder(GX_TEVSTAGE1, GX_TEXCOORD1, GX_TEXMAP7, GX_COLORNULL);
		GX_SetTevKColorSel(GX_TEVSTAGE1, GX_TEV_KCSEL_K0);
		GX_SetTevColorIn(GX_TEVSTAGE1, GX_CC_ZERO, GX_CC_CPREV, GX_CC_KONST, GX_CC_APREV);
		GX_SetTevColorOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
		GX_SetTevAlphaIn(GX_TEVSTAGE1, GX_CA_TEXA, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO);
		GX_SetTevAlphaOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_SUBHALF, GX_CS_SCALE_1, GX_FALSE, GX_TEVREG0);
		GX_SetTevDirect(GX_TEVSTAGE1);
	} else {
		GX_SetTevOrder(GX_TEVSTAGE1, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLORNULL);
		GX_SetTevKColorSel(GX_TEVSTAGE1, GX_TEV_KCSEL_K0);
		GX_SetTevColorIn(GX_TEVSTAGE1, GX_CC_ZERO, GX_CC_CPREV, GX_CC_KONST, GX_CC_APREV);
		GX_SetTevColorOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
		GX_SetTevAlphaIn(GX_TEVSTAGE1, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_A0);
		GX_SetTevAlphaOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_FALSE, GX_TEVREG0);
		GX_SetTevDirect(GX_TEVSTAGE1);
	}

	GX_ClearVtxDesc();
	GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XY, GX_S16, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_S16, 0);
	GX_SetArray(GX_TEXMTXARRAY, texmtx, sizeof(texmtx[0]));

	GX_LoadProjectionMtx(projection, GX_ORTHOGRAPHIC);
	GX_SetCurrentMtx(GX_PNMTX0);
	GX_SetViewport(0., 0., 1024., 1024., 0., 1.);

	GX_SetFieldMask(GX_TRUE, GX_TRUE);
	GX_SetFieldMode(GX_FALSE, GX_FALSE);

	GX_SetPixelFmt(GX_PF_Y8, GX_ZC_LINEAR);
	GX_SetCopyFilter(GX_FALSE, NULL, GX_FALSE, NULL);

	GX_LoadTlut(&src->lutobj[GX_CH_RED],   GX_TLUT0);
	GX_LoadTlut(&src->lutobj[GX_CH_GREEN], GX_TLUT1);
	GX_LoadTlut(&src->lutobj[GX_CH_BLUE],  GX_TLUT2);

	for (int ch = GX_CH_RED; ch <= GX_CH_BLUE; ch++) {
		int idx = state.retrace + ch;

		if (src->dirty) GX_PreloadEntireTexture(&src->obj[ch], &src->region[ch]);
		GX_LoadTexObjPreloaded(&src->obj[ch], &src->region[ch], GX_TEXMAP0);

		if (state.dither > DITHER_THRESHOLD) {
			GX_LoadTexObj(&texobj, GX_TEXMAP7);
			GX_LoadTexMtxIdx(idx % 4, GX_DTTMTX0, GX_MTX3x4);
		}

		GX_SetTevColorS10(GX_TEVREG0, idx % 2 ? (GXColorS10){-32, -32, -32, -32}
		                                      : (GXColorS10){+15, +15, +15, +15});

		GXPrescaleCopyChannel(dst->obj[ch], dst->rect, src->rect, ch);
	}

	dst->dirty = true; src->dirty = false;
}

void GXPrescaleApplyBlend(gx_surface_t *dst, gx_surface_t **src, uint8_t *alpha, uint32_t count)
{
	Mtx44 projection;
	guOrtho(projection, 0., 1024., 0., 1024., 0., 1.);

	GX_SetBlendMode(GX_BM_NONE, GX_BL_ZERO, GX_BL_ZERO, GX_LO_CLEAR);
	GX_SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
	GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
	GX_SetZCompLoc(GX_FALSE);

	GX_SetNumChans(0);
	GX_SetNumTexGens(1);
	GX_SetNumIndStages(0);
	GX_SetNumTevStages(count);

	GX_SetTevSwapModeTable(GX_TEV_SWAP0, GX_CH_RED, GX_CH_GREEN, GX_CH_BLUE, GX_CH_ALPHA);

	GX_SetTevKColor(GX_KCOLOR0, (GXColor){alpha[0], alpha[4]});
	GX_SetTevKColor(GX_KCOLOR1, (GXColor){alpha[1], alpha[5]});
	GX_SetTevKColor(GX_KCOLOR2, (GXColor){alpha[2], alpha[6]});
	GX_SetTevKColor(GX_KCOLOR3, (GXColor){alpha[3], alpha[7]});

	for (int i = 0; i < count; i++) {
		GX_SetTevOrder(GX_TEVSTAGE0 + i, GX_TEXCOORD0, GX_TEXMAP0 + i, GX_COLORNULL);
		GX_SetTevKColorSel(GX_TEVSTAGE0 + i, GX_TEV_KCSEL_K0_R + i);
		GX_SetTevColorIn(GX_TEVSTAGE0 + i, GX_CC_TEXA, i == 0 ? GX_CC_ZERO : GX_CC_CPREV, GX_CC_KONST, GX_CC_ZERO);
		GX_SetTevColorOp(GX_TEVSTAGE0 + i, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
		GX_SetTevDirect(GX_TEVSTAGE0 + i);
	}

	GX_ClearVtxDesc();
	GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XY, GX_S16, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_S16, 0);

	GX_LoadProjectionMtx(projection, GX_ORTHOGRAPHIC);
	GX_SetCurrentMtx(GX_PNMTX0);
	GX_SetViewport(0., 0., 1024., 1024., 0., 1.);

	GX_SetFieldMask(GX_TRUE, GX_TRUE);
	GX_SetFieldMode(GX_FALSE, GX_FALSE);

	GX_SetPixelFmt(GX_PF_Y8, GX_ZC_LINEAR);
	GX_SetCopyFilter(GX_FALSE, NULL, GX_FALSE, NULL);

	for (int ch = GX_CH_RED; ch <= GX_CH_BLUE; ch++) {
		for (int i = 0; i < count; i++) {
			GX_LoadTlut(&src[i]->lutobj[ch], GX_GetTexObjTlut(&src[i]->obj[ch]));
			GX_LoadTexObj(&src[i]->obj[ch], GX_TEXMAP0 + i);
		}

		GXPrescaleCopyChannel(dst->obj[ch], dst->rect, src[0]->rect, ch);
	}

	dst->dirty = true; src[0]->dirty = false;
}

static void GXPrescaleTlut(gx_surface_t **surfaces, uint8_t *alpha, uint32_t count)
{
	float next = 1.;

	for (int i = count - 1; i >= 0; i--) {
		float curr = GXCast1u8f32(alpha[i]);
		float scale = LERP(next, 0., curr, 1.);

		next *= curr;

		for (int ch = GX_CH_RED; ch <= GX_CH_BLUE; ch++) {
			void *dst = tlutdata[i][ch];
			void *src = surfaces[i]->lutbuf[ch];

			for (int j = 0; j < 32; j++) {
				register float rega, regb, regc, regd;
				asm volatile (
					"psq_l    %0,  0 (%4), 0, 5 \n"
					"psq_l    %1,  4 (%4), 0, 5 \n"
					"psq_l    %2,  8 (%4), 0, 5 \n"
					"psq_l    %3, 12 (%4), 0, 5 \n"

					"ps_madd  %0, %0, %6, %7 \n"
					"ps_madd  %1, %1, %6, %7 \n"
					"ps_madd  %2, %2, %6, %7 \n"
					"ps_madd  %3, %3, %6, %7 \n"

					"psq_st   %0,  0 (%5), 0, 5 \n"
					"psq_st   %1,  4 (%5), 0, 5 \n"
					"psq_st   %2,  8 (%5), 0, 5 \n"
					"psq_st   %3, 12 (%5), 0, 5 \n"

					"addi     %4, %4, 16 \n"
					"addi     %5, %5, 16 \n"
					: "=f" (rega), "=f" (regb), "=f" (regc), "=f" (regd),
					  "+b" (src), "+b" (dst)
					: "f" (scale), "f" (0.5f)
				);
			}
		}
	}

	DCStoreRange(tlutdata, sizeof(tlutdata[0]) * count);
}

void GXPrescaleApplyBlendDither(gx_surface_t *dst, gx_surface_t **src, uint8_t *alpha, uint32_t count)
{
	count = MIN(count, 7);

	Mtx44 projection;
	guOrtho(projection, 0., 1024., 0., 1024., 0., 1.);

	GX_SetBlendMode(GX_BM_NONE, GX_BL_ZERO, GX_BL_ZERO, GX_LO_CLEAR);
	GX_SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
	GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
	GX_SetZCompLoc(GX_FALSE);

	GX_SetNumChans(0);
	GX_SetNumTexGens(2);
	GX_SetNumIndStages(0);
	GX_SetNumTevStages(count + 2);

	GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
	GX_SetTexCoordGen(GX_TEXCOORD1, GX_TG_MTX2x4, GX_TG_POS, GX_IDENTITY);

	GX_SetTevSwapModeTable(GX_TEV_SWAP0, GX_CH_RED, GX_CH_GREEN, GX_CH_BLUE, GX_CH_ALPHA);

	GX_SetTevKColor(GX_KCOLOR0, (GXColor){4, 4, 4, 4});

	for (int i = 0; i < count; i++) {
		GX_SetTevOrder(GX_TEVSTAGE0 + i, GX_TEXCOORD0, GX_TEXMAP0 + i, GX_COLORNULL);
		GX_SetTevKColorSel(GX_TEVSTAGE0 + i, GX_TEV_KCSEL_1_4);
		GX_SetTevColorIn(GX_TEVSTAGE0 + i, GX_CC_ZERO, GX_CC_TEXC, GX_CC_KONST, i == 0 ? GX_CC_ZERO : GX_CC_CPREV);
		GX_SetTevColorOp(GX_TEVSTAGE0 + i, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_FALSE, GX_TEVPREV);
		GX_SetTevAlphaIn(GX_TEVSTAGE0 + i, GX_CA_TEXA, GX_CA_ZERO, GX_CA_ZERO, i == 0 ? GX_CA_ZERO : GX_CA_APREV);
		GX_SetTevAlphaOp(GX_TEVSTAGE0 + i, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_FALSE, GX_TEVPREV);
		GX_SetTevDirect(GX_TEVSTAGE0 + i);
	}

	GX_SetTevOrder(GX_TEVSTAGE0 + count, GX_TEXCOORD1, GX_TEXMAP7, GX_COLORNULL);
	GX_SetTevColorIn(GX_TEVSTAGE0 + count, GX_CC_TEXA, GX_CC_ZERO, GX_CC_ZERO, GX_CC_CPREV);
	GX_SetTevColorOp(GX_TEVSTAGE0 + count, GX_TEV_ADD, GX_TB_SUBHALF, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
	GX_SetTevAlphaIn(GX_TEVSTAGE0 + count, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_APREV);
	GX_SetTevAlphaOp(GX_TEVSTAGE0 + count, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
	GX_SetTevDirect(GX_TEVSTAGE0 + count);

	GX_SetTevOrder(GX_TEVSTAGE1 + count, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLORNULL);
	GX_SetTevKColorSel(GX_TEVSTAGE1 + count, GX_TEV_KCSEL_K0);
	GX_SetTevColorIn(GX_TEVSTAGE1 + count, GX_CC_ZERO, GX_CC_CPREV, GX_CC_KONST, GX_CC_APREV);
	GX_SetTevColorOp(GX_TEVSTAGE1 + count, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
	GX_SetTevDirect(GX_TEVSTAGE1 + count);

	GX_ClearVtxDesc();
	GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XY, GX_S16, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_S16, 0);

	GX_LoadProjectionMtx(projection, GX_ORTHOGRAPHIC);
	GX_SetCurrentMtx(GX_PNMTX0);
	GX_SetViewport(0., 0., 1024., 1024., 0., 1.);

	GX_SetFieldMask(GX_TRUE, GX_TRUE);
	GX_SetFieldMode(GX_FALSE, GX_FALSE);

	GX_SetPixelFmt(GX_PF_Y8, GX_ZC_LINEAR);
	GX_SetCopyFilter(GX_FALSE, NULL, GX_FALSE, NULL);

	GXPrescaleTlut(src, alpha, count);

	for (int ch = GX_CH_RED; ch <= GX_CH_BLUE; ch++) {
		for (int i = 0; i < count; i++) {
			GX_LoadTlut(&tlutobj[i][ch], GX_GetTexObjTlut(&src[i]->obj[ch]));
			GX_LoadTexObj(&src[i]->obj[ch], GX_TEXMAP0 + i);
		}

		GX_LoadTexObj(&texobj, GX_TEXMAP7);

		GXPrescaleCopyChannel(dst->obj[ch], dst->rect, src[0]->rect, ch);
	}

	dst->dirty = true; src[0]->dirty = false;
}

void GXPrescaleApplyBlendDitherFast(gx_surface_t *dst, gx_surface_t **src, uint8_t *alpha, uint32_t count)
{
	Mtx44 projection;
	guOrtho(projection, 0., 1024., 0., 1024., 0., 1.);

	GX_SetBlendMode(GX_BM_NONE, GX_BL_ZERO, GX_BL_ZERO, GX_LO_CLEAR);
	GX_SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
	GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
	GX_SetZCompLoc(GX_FALSE);

	GX_SetNumChans(0);
	GX_SetNumTexGens(2);
	GX_SetNumIndStages(0);
	GX_SetNumTevStages(count + 1);

	GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
	GX_SetTexCoordGen2(GX_TEXCOORD1, GX_TG_MTX2x4, GX_TG_POS, GX_IDENTITY, GX_FALSE, GX_DTTMTX0);

	GX_SetTevSwapModeTable(GX_TEV_SWAP0, GX_CH_RED, GX_CH_GREEN, GX_CH_BLUE, GX_CH_ALPHA);

	GX_SetTevKColor(GX_KCOLOR0, (GXColor){4, 4, 4, 4});

	for (int i = 0; i < count; i++) {
		GX_SetTevOrder(GX_TEVSTAGE0 + i, GX_TEXCOORD0, GX_TEXMAP0 + i, GX_COLORNULL);
		GX_SetTevKColorSel(GX_TEVSTAGE0 + i, GX_TEV_KCSEL_1_4);
		GX_SetTevColorIn(GX_TEVSTAGE0 + i, GX_CC_ZERO, GX_CC_TEXC, GX_CC_KONST, i == 0 ? GX_CC_A0 : GX_CC_CPREV);
		GX_SetTevColorOp(GX_TEVSTAGE0 + i, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, i == count - 1 ? GX_TRUE : GX_FALSE, GX_TEVPREV);
		GX_SetTevAlphaIn(GX_TEVSTAGE0 + i, GX_CA_TEXA, GX_CA_ZERO, GX_CA_ZERO, i == 0 ? GX_CA_ZERO : GX_CA_APREV);
		GX_SetTevAlphaOp(GX_TEVSTAGE0 + i, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, i == count - 1 ? GX_TRUE : GX_FALSE, GX_TEVPREV);
		GX_SetTevDirect(GX_TEVSTAGE0 + i);
	}

	if (state.dither > DITHER_THRESHOLD && count < GX_MAX_TEXMAP) {
		GX_SetTevOrder(GX_TEVSTAGE0 + count, GX_TEXCOORD1, GX_TEXMAP7, GX_COLORNULL);
		GX_SetTevKColorSel(GX_TEVSTAGE0 + count, GX_TEV_KCSEL_K0);
		GX_SetTevColorIn(GX_TEVSTAGE0 + count, GX_CC_ZERO, GX_CC_CPREV, GX_CC_KONST, GX_CC_APREV);
		GX_SetTevColorOp(GX_TEVSTAGE0 + count, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
		GX_SetTevAlphaIn(GX_TEVSTAGE0 + count, GX_CA_TEXA, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO);
		GX_SetTevAlphaOp(GX_TEVSTAGE0 + count, GX_TEV_ADD, GX_TB_SUBHALF, GX_CS_SCALE_1, GX_FALSE, GX_TEVREG0);
		GX_SetTevDirect(GX_TEVSTAGE0 + count);
	} else {
		GX_SetTevOrder(GX_TEVSTAGE0 + count, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLORNULL);
		GX_SetTevKColorSel(GX_TEVSTAGE0 + count, GX_TEV_KCSEL_K0);
		GX_SetTevColorIn(GX_TEVSTAGE0 + count, GX_CC_ZERO, GX_CC_CPREV, GX_CC_KONST, GX_CC_APREV);
		GX_SetTevColorOp(GX_TEVSTAGE0 + count, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
		GX_SetTevAlphaIn(GX_TEVSTAGE0 + count, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_A0);
		GX_SetTevAlphaOp(GX_TEVSTAGE0 + count, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_FALSE, GX_TEVREG0);
		GX_SetTevDirect(GX_TEVSTAGE0 + count);
	}

	GX_ClearVtxDesc();
	GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XY, GX_S16, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_S16, 0);
	GX_SetArray(GX_TEXMTXARRAY, texmtx, sizeof(texmtx[0]));

	GX_LoadProjectionMtx(projection, GX_ORTHOGRAPHIC);
	GX_SetCurrentMtx(GX_PNMTX0);
	GX_SetViewport(0., 0., 1024., 1024., 0., 1.);

	GX_SetFieldMask(GX_TRUE, GX_TRUE);
	GX_SetFieldMode(GX_FALSE, GX_FALSE);

	GX_SetPixelFmt(GX_PF_Y8, GX_ZC_LINEAR);
	GX_SetCopyFilter(GX_FALSE, NULL, GX_FALSE, NULL);

	GXPrescaleTlut(src, alpha, count);

	for (int ch = GX_CH_RED; ch <= GX_CH_BLUE; ch++) {
		int idx = state.retrace + ch;

		for (int i = 0; i < count; i++) {
			GX_LoadTlut(&tlutobj[i][ch], GX_GetTexObjTlut(&src[i]->obj[ch]));
			GX_LoadTexObj(&src[i]->obj[ch], GX_TEXMAP0 + i);
		}

		if (state.dither > DITHER_THRESHOLD && count < GX_MAX_TEXMAP) {
			GX_LoadTexObj(&texobj, GX_TEXMAP7);
			GX_LoadTexMtxIdx(idx % 4, GX_DTTMTX0, GX_MTX3x4);
		}

		GX_SetTevColorS10(GX_TEVREG0, idx % 2 ? (GXColorS10){-32, -32, -32, -32}
		                                      : (GXColorS10){+15, +15, +15, +15});

		GXPrescaleCopyChannel(dst->obj[ch], dst->rect, src[0]->rect, ch);
	}

	dst->dirty = true; src[0]->dirty = false;
}

void GXPrescaleAllocState(void)
{
	switch (state.dither) {
		case DITHER_BAYER8x8:
			GX_InitTexObj(&texobj, texdata[0], 8, 8, GX_TF_I8, GX_REPEAT, GX_REPEAT, GX_FALSE);
			GX_InitTexObjFilterMode(&texobj, GX_NEAR, GX_NEAR);
			break;
		case DITHER_BAYER4x4:
			GX_InitTexObj(&texobj, texdata[0], 4, 4, GX_TF_I8, GX_REPEAT, GX_REPEAT, GX_FALSE);
			GX_InitTexObjFilterMode(&texobj, GX_NEAR, GX_NEAR);
			break;
		case DITHER_BAYER2x2:
			GX_InitTexObj(&texobj, texdata[0], 2, 2, GX_TF_I8, GX_REPEAT, GX_REPEAT, GX_FALSE);
			GX_InitTexObjFilterMode(&texobj, GX_NEAR, GX_NEAR);
			break;
		case DITHER_CLUSTER8x8:
			GX_InitTexObj(&texobj, texdata[1], 8, 8, GX_TF_I8, GX_REPEAT, GX_REPEAT, GX_FALSE);
			GX_InitTexObjFilterMode(&texobj, GX_NEAR, GX_NEAR);
			break;
		case DITHER_CLUSTER4x4:
			GX_InitTexObj(&texobj, texdata[1], 4, 4, GX_TF_I8, GX_REPEAT, GX_REPEAT, GX_FALSE);
			GX_InitTexObjFilterMode(&texobj, GX_NEAR, GX_NEAR);
			break;
		default:
			break;
	}

	for (int i = GX_TEXMAP0; i < GX_MAX_TEXMAP; i++)
		for (int ch = GX_CH_RED; ch <= GX_CH_BLUE; ch++)
			GX_InitTlutObj(&tlutobj[i][ch], tlutdata[i][ch], GX_TL_IA8, 256);
}

rect_t GXPrescaleGetRect(uint16_t width, uint16_t height)
{
	rect_t rect;

	float x = (viewport.w * state.zoom.x) / (state.scale * screen.w);
	float y = (viewport.h * state.zoom.y) / (state.scale * screen.h);
	float r = DegToRad(state.rotation);

	int X = lrintf(x * fabsf(cosf(r)) + x * fabsf(sinf(r)));
	int Y = lrintf(y * fabsf(cosf(r)) + y * fabsf(sinf(r)));

	rect.x = 0;
	rect.y = 0;
	rect.w = width  * MIN(MAX(X, 1), 1024 / width);
	rect.h = height * MIN(MAX(Y, 1),  640 / height);

	return rect;
}
