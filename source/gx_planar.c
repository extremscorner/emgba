/* 
 * Copyright (c) 2015-2022, Extrems' Corner.org
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <malloc.h>
#include <gccore.h>
#include "gx.h"
#include "state.h"

static float indtexmtx[][2][3] = {
	{
		{ +.5, +.0, +.0 },
		{ +.0, +.5, +.0 }
	}, {
		{ +.0, +.0, +.0 },
		{ +.0, +.5, +.0 }
	}, {
		{ +.5, +.0, +.0 },
		{ +.0, +.0, +.0 }
	}
};

static uint16_t indtexdata[][4 * 4] ATTRIBUTE_ALIGN(32) = {
	{
		0x7E7E, 0x817E, 0x7E7E, 0x817E,
		0x7E81, 0x8181, 0x7E81, 0x8181,
		0x7E7E, 0x817E, 0x7E7E, 0x817E,
		0x7E81, 0x8181, 0x7E81, 0x8181,
	}, {
		0x7E80, 0x807E, 0x7E80, 0x807E,
		0x8081, 0x8180, 0x8081, 0x8180,
		0x7E80, 0x807E, 0x7E80, 0x807E,
		0x8081, 0x8180, 0x8081, 0x8180,
	}, {
		0x807E, 0x8180, 0x807E, 0x8180,
		0x7E80, 0x8081, 0x7E80, 0x8081,
		0x807E, 0x8180, 0x807E, 0x8180,
		0x7E80, 0x8081, 0x7E80, 0x8081,
	}
};

static GXTexObj indtexobj[3];

static void GXPlanarCopyChannel(GXTexObj texobj, rect_t dst_rect, rect_t src_rect, uint8_t channel)
{
	void *ptr;
	uint16_t width, height;
	uint8_t format, wrap_s, wrap_t, mipmap;

	GX_GetTexObjAll(&texobj, &ptr, &width, &height, &format, &wrap_s, &wrap_t, &mipmap);

	if (channel == GX_CH_RED) {
		GX_SetScissor(0, 0, width << mipmap, height << mipmap);
		GX_SetScissorBoxOffset(0, 0);
		GX_ClearBoundingBox();

		GX_Begin(GX_QUADS, GX_VTXFMT0, 4);

		GX_Position2s16(dst_rect.x, dst_rect.y);
		GX_TexCoord2s16(src_rect.x, src_rect.y);

		GX_Position2s16(dst_rect.x + dst_rect.w, dst_rect.y);
		GX_TexCoord2s16(src_rect.x + src_rect.w, src_rect.y);

		GX_Position2s16(dst_rect.x + dst_rect.w, dst_rect.y + dst_rect.h);
		GX_TexCoord2s16(src_rect.x + src_rect.w, src_rect.y + src_rect.h);

		GX_Position2s16(dst_rect.x, dst_rect.y + dst_rect.h);
		GX_TexCoord2s16(src_rect.x, src_rect.y + src_rect.h);
	}

	GX_SetTexCopySrc(0, 0, width << mipmap, height << mipmap);
	GX_SetTexCopyDst(width, height, GX_CTF_R8 + channel, mipmap);

	GX_CopyTex(ptr, channel == GX_CH_BLUE ? GX_TRUE : GX_FALSE);
}

void GXPlanarApply(gx_surface_t *dst, gx_surface_t *src)
{
	Mtx44 projection;
	guOrtho(projection, 0., 1024., 0., 1024., 0., 1.);

	GX_SetBlendMode(GX_BM_NONE, GX_BL_ZERO, GX_BL_ZERO, GX_LO_CLEAR);
	GX_SetAlphaCompare(GX_GREATER, 0, GX_AOP_AND, GX_GREATER, 0);
	GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
	GX_SetZCompLoc(GX_FALSE);

	GX_SetNumChans(0);
	GX_SetNumTexGens(1);
	GX_SetNumIndStages(0);
	GX_SetNumTevStages(1);

	GX_SetTevSwapModeTable(GX_TEV_SWAP0, GX_CH_BLUE, GX_CH_GREEN, GX_CH_RED, GX_CH_ALPHA);

	GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLORNULL);
	GX_SetTevColorIn(GX_TEVSTAGE0, GX_CC_TEXC, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO);
	GX_SetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
	GX_SetTevKAlphaSel(GX_TEVSTAGE0, GX_TEV_KASEL_1);
	GX_SetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_KONST, GX_CA_ZERO);
	GX_SetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_COMP_BGR24_GT, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
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

	GX_SetPixelFmt(GX_PF_RGB8_Z24, GX_ZC_LINEAR);
	GX_SetCopyFilter(GX_FALSE, NULL, GX_FALSE, NULL);

	if (src->dirty) GX_PreloadEntireTexture(&src->obj[0], &src->region[0]);
	GX_LoadTexObjPreloaded(&src->obj[0], &src->region[0], GX_TEXMAP0);

	for (int ch = GX_CH_RED; ch <= GX_CH_BLUE; ch++)
		GXPlanarCopyChannel(dst->obj[ch], dst->rect, src->rect, ch);

	dst->dirty = true; src->dirty = false;
}

void GXPlanarApplyBlend(gx_surface_t *dst, gx_surface_t *src)
{
	uint8_t alpha[3] = {
		state.filter_weight[0] * 255. + .5,
		state.filter_weight[1] * 255. + .5,
		state.filter_weight[2] * 255. + .5
	};

	Mtx44 projection;
	guOrtho(projection, 0., 1024., 0., 1024., 0., 1.);

	GX_SetBlendMode(GX_BM_NONE, GX_BL_ZERO, GX_BL_ZERO, GX_LO_CLEAR);
	GX_SetAlphaCompare(GX_GREATER, 0, GX_AOP_AND, GX_GREATER, 0);
	GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
	GX_SetZCompLoc(GX_FALSE);

	GX_SetNumChans(0);
	GX_SetNumTexGens(1);
	GX_SetNumIndStages(0);
	GX_SetNumTevStages(2);

	GX_SetTevSwapModeTable(GX_TEV_SWAP0, GX_CH_BLUE, GX_CH_GREEN, GX_CH_RED, GX_CH_ALPHA);

	GX_SetTevKColor(GX_KCOLOR0, (GXColor){alpha[0], alpha[1], alpha[2]});

	GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLORNULL);
	GX_SetTevColorIn(GX_TEVSTAGE0, GX_CC_TEXC, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO);
	GX_SetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
	GX_SetTevKAlphaSel(GX_TEVSTAGE0, GX_TEV_KASEL_1);
	GX_SetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_KONST, GX_CA_ZERO);
	GX_SetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_COMP_BGR24_GT, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
	GX_SetTevDirect(GX_TEVSTAGE0);

	GX_SetTevOrder(GX_TEVSTAGE1, GX_TEXCOORD0, GX_TEXMAP1, GX_COLORNULL);
	GX_SetTevKColorSel(GX_TEVSTAGE1, GX_TEV_KCSEL_K0);
	GX_SetTevColorIn(GX_TEVSTAGE1, GX_CC_TEXC, GX_CC_CPREV, GX_CC_KONST, GX_CC_ZERO);
	GX_SetTevColorOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
	GX_SetTevKAlphaSel(GX_TEVSTAGE1, GX_TEV_KASEL_1);
	GX_SetTevAlphaIn(GX_TEVSTAGE1, GX_CA_ZERO, GX_CA_ZERO, GX_CA_KONST, GX_CA_APREV);
	GX_SetTevAlphaOp(GX_TEVSTAGE1, GX_TEV_COMP_BGR24_GT, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
	GX_SetTevDirect(GX_TEVSTAGE1);

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

	GX_SetPixelFmt(GX_PF_RGB8_Z24, GX_ZC_LINEAR);
	GX_SetCopyFilter(GX_FALSE, NULL, GX_FALSE, NULL);

	if (src->dirty) {
		src->shadow = (src->shadow - 1 + src->shadows) % src->shadows;
		GX_PreloadEntireTexture(&src->obj[0], &src->region[src->shadow]);
	}
	GX_LoadTexObjPreloaded(&src->obj[0], &src->region[(src->shadow + 0) % src->shadows], GX_TEXMAP0);
	GX_LoadTexObjPreloaded(&src->obj[0], &src->region[(src->shadow + 1) % src->shadows], GX_TEXMAP1);

	for (int ch = GX_CH_RED; ch <= GX_CH_BLUE; ch++)
		GXPlanarCopyChannel(dst->obj[ch], dst->rect, src->rect, ch);

	dst->dirty = true; src->dirty = false;
}

void GXPlanarApplyDeflicker(gx_surface_t *dst, gx_surface_t *src)
{
	uint8_t alpha[3] = {
		state.filter_weight[0] * 255. + .5,
		state.filter_weight[1] * 255. + .5,
		state.filter_weight[2] * 255. + .5
	};

	Mtx44 projection;
	guOrtho(projection, 0., 1024., 0., 1024., 0., 1.);

	GX_SetBlendMode(GX_BM_NONE, GX_BL_ZERO, GX_BL_ZERO, GX_LO_CLEAR);
	GX_SetAlphaCompare(GX_GREATER, 0, GX_AOP_AND, GX_GREATER, 0);
	GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
	GX_SetZCompLoc(GX_FALSE);

	GX_SetNumChans(0);
	GX_SetNumTexGens(1);
	GX_SetNumIndStages(0);
	GX_SetNumTevStages(3);

	GX_SetTevSwapModeTable(GX_TEV_SWAP0, GX_CH_BLUE, GX_CH_GREEN, GX_CH_RED, GX_CH_ALPHA);

	GX_SetTevKColor(GX_KCOLOR0, (GXColor){alpha[0] + 1, alpha[1] + 1, alpha[2] + 1});

	GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLORNULL);
	GX_SetTevColorIn(GX_TEVSTAGE0, GX_CC_TEXC, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO);
	GX_SetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
	GX_SetTevKAlphaSel(GX_TEVSTAGE0, GX_TEV_KASEL_1);
	GX_SetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_KONST, GX_CA_ZERO);
	GX_SetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_COMP_BGR24_GT, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
	GX_SetTevDirect(GX_TEVSTAGE0);

	GX_SetTevOrder(GX_TEVSTAGE1, GX_TEXCOORD0, GX_TEXMAP2, GX_COLORNULL);
	GX_SetTevKColorSel(GX_TEVSTAGE1, GX_TEV_KCSEL_K0);
	GX_SetTevColorIn(GX_TEVSTAGE1, GX_CC_TEXC, GX_CC_CPREV, GX_CC_KONST, GX_CC_ONE);
	GX_SetTevColorOp(GX_TEVSTAGE1, GX_TEV_COMP_BGR24_EQ, GX_TB_ZERO, GX_CS_SCALE_1, GX_FALSE, GX_TEVREG0);
	GX_SetTevAlphaIn(GX_TEVSTAGE1, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_APREV);
	GX_SetTevAlphaOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
	GX_SetTevDirect(GX_TEVSTAGE1);

	GX_SetTevOrder(GX_TEVSTAGE2, GX_TEXCOORD0, GX_TEXMAP1, GX_COLORNULL);
	GX_SetTevColorIn(GX_TEVSTAGE2, GX_CC_TEXC, GX_CC_CPREV, GX_CC_C0, GX_CC_ZERO);
	GX_SetTevColorOp(GX_TEVSTAGE2, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
	GX_SetTevKAlphaSel(GX_TEVSTAGE2, GX_TEV_KASEL_1);
	GX_SetTevAlphaIn(GX_TEVSTAGE2, GX_CA_ZERO, GX_CA_ZERO, GX_CA_KONST, GX_CA_APREV);
	GX_SetTevAlphaOp(GX_TEVSTAGE2, GX_TEV_COMP_BGR24_GT, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
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

	GX_SetPixelFmt(GX_PF_RGB8_Z24, GX_ZC_LINEAR);
	GX_SetCopyFilter(GX_FALSE, NULL, GX_FALSE, NULL);

	if (src->dirty) {
		src->shadow = (src->shadow - 1 + src->shadows) % src->shadows;
		GX_PreloadEntireTexture(&src->obj[0], &src->region[src->shadow]);
	}
	GX_LoadTexObjPreloaded(&src->obj[0], &src->region[(src->shadow + 0) % src->shadows], GX_TEXMAP0);
	GX_LoadTexObjPreloaded(&src->obj[0], &src->region[(src->shadow + 1) % src->shadows], GX_TEXMAP1);
	GX_LoadTexObjPreloaded(&src->obj[0], &src->region[(src->shadow + 2) % src->shadows], GX_TEXMAP2);

	for (int ch = GX_CH_RED; ch <= GX_CH_BLUE; ch++)
		GXPlanarCopyChannel(dst->obj[ch], dst->rect, src->rect, ch);

	dst->dirty = true; src->dirty = false;
}

void GXPlanarApplyScale2xEx(gx_surface_t *dst, gx_surface_t *src, gx_surface_t *yuv)
{
	Mtx44 projection;
	guOrtho(projection, 0., 1024., 0., 1024., 0., 1.);

	GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
	GX_SetAlphaCompare(GX_GREATER, 0, GX_AOP_AND, GX_GREATER, 0);
	GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
	GX_SetZCompLoc(GX_FALSE);

	GX_SetNumChans(0);
	GX_SetNumTexGens(6);
	GX_SetNumIndStages(1);
	GX_SetNumTevStages(12);

	GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
	GX_SetTexCoordGen2(GX_TEXCOORD1, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY, GX_FALSE, GX_DTTMTX2);
	GX_SetTexCoordGen2(GX_TEXCOORD2, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY, GX_FALSE, GX_DTTMTX8);
	GX_SetTexCoordGen2(GX_TEXCOORD3, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY, GX_FALSE, GX_DTTMTX4);
	GX_SetTexCoordGen2(GX_TEXCOORD4, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY, GX_FALSE, GX_DTTMTX6);
	GX_SetTexCoordGen(GX_TEXCOORD5, GX_TG_MTX2x4, GX_TG_TEX0, GX_TEXMTX0);

	GX_SetIndTexOrder(GX_INDTEXSTAGE0, GX_TEXCOORD5, GX_TEXMAP5);
	GX_SetIndTexMatrix(GX_ITM_1, indtexmtx[1], 0);
	GX_SetIndTexMatrix(GX_ITM_2, indtexmtx[2], 0);

	GX_SetTevSwapModeTable(GX_TEV_SWAP0, GX_CH_BLUE, GX_CH_GREEN, GX_CH_RED, GX_CH_ALPHA);

	GX_SetTevKColor(GX_KCOLOR0, (GXColor){13, 13, 23});
	GX_SetTevKColor(GX_KCOLOR1, (GXColor){26, 26, 46});

	GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD1, GX_TEXMAP1, GX_COLORNULL);
	GX_SetTevKColorSel(GX_TEVSTAGE0, GX_TEV_KCSEL_K0);
	GX_SetTevColorIn(GX_TEVSTAGE0, GX_CC_TEXC, GX_CC_ZERO, GX_CC_ZERO, GX_CC_KONST);
	GX_SetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_FALSE, GX_TEVPREV);
	GX_SetTevDirect(GX_TEVSTAGE0);

	GX_SetTevOrder(GX_TEVSTAGE1, GX_TEXCOORD2, GX_TEXMAP1, GX_COLORNULL);
	GX_SetTevColorIn(GX_TEVSTAGE1, GX_CC_TEXC, GX_CC_ZERO, GX_CC_ZERO, GX_CC_CPREV);
	GX_SetTevColorOp(GX_TEVSTAGE1, GX_TEV_SUB, GX_TB_ZERO, GX_CS_SCALE_1, GX_FALSE, GX_TEVPREV);
	GX_SetTevDirect(GX_TEVSTAGE1);

	GX_SetTevOrder(GX_TEVSTAGE2, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLORNULL);
	GX_SetTevKColorSel(GX_TEVSTAGE2, GX_TEV_KCSEL_K1);
	GX_SetTevColorIn(GX_TEVSTAGE2, GX_CC_CPREV, GX_CC_KONST, GX_CC_ONE, GX_CC_ZERO);
	GX_SetTevColorOp(GX_TEVSTAGE2, GX_TEV_COMP_RGB8_GT, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVREG0);
	GX_SetTevDirect(GX_TEVSTAGE2);

	GX_SetTevOrder(GX_TEVSTAGE3, GX_TEXCOORD3, GX_TEXMAP1, GX_COLORNULL);
	GX_SetTevKColorSel(GX_TEVSTAGE3, GX_TEV_KCSEL_K0);
	GX_SetTevColorIn(GX_TEVSTAGE3, GX_CC_TEXC, GX_CC_ZERO, GX_CC_ZERO, GX_CC_KONST);
	GX_SetTevColorOp(GX_TEVSTAGE3, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_FALSE, GX_TEVPREV);
	GX_SetTevDirect(GX_TEVSTAGE3);

	GX_SetTevOrder(GX_TEVSTAGE4, GX_TEXCOORD4, GX_TEXMAP1, GX_COLORNULL);
	GX_SetTevColorIn(GX_TEVSTAGE4, GX_CC_TEXC, GX_CC_ZERO, GX_CC_ZERO, GX_CC_CPREV);
	GX_SetTevColorOp(GX_TEVSTAGE4, GX_TEV_SUB, GX_TB_ZERO, GX_CS_SCALE_1, GX_FALSE, GX_TEVPREV);
	GX_SetTevDirect(GX_TEVSTAGE4);

	GX_SetTevOrder(GX_TEVSTAGE5, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLORNULL);
	GX_SetTevKColorSel(GX_TEVSTAGE5, GX_TEV_KCSEL_K1);
	GX_SetTevColorIn(GX_TEVSTAGE5, GX_CC_CPREV, GX_CC_KONST, GX_CC_C0, GX_CC_ZERO);
	GX_SetTevColorOp(GX_TEVSTAGE5, GX_TEV_COMP_RGB8_GT, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVREG0);
	GX_SetTevDirect(GX_TEVSTAGE5);

	GX_SetTevOrder(GX_TEVSTAGE6, GX_TEXCOORD0, GX_TEXMAP1, GX_COLORNULL);
	GX_SetTevKColorSel(GX_TEVSTAGE6, GX_TEV_KCSEL_K0);
	GX_SetTevColorIn(GX_TEVSTAGE6, GX_CC_TEXC, GX_CC_ZERO, GX_CC_ZERO, GX_CC_KONST);
	GX_SetTevColorOp(GX_TEVSTAGE6, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_FALSE, GX_TEVPREV);
	GX_SetTevIndirect(GX_TEVSTAGE6, GX_INDTEXSTAGE0, GX_ITF_8, GX_ITB_STU, GX_ITM_1, GX_ITW_OFF, GX_ITW_OFF, GX_FALSE, GX_FALSE, GX_ITBA_OFF);

	GX_SetTevOrder(GX_TEVSTAGE7, GX_TEXCOORD0, GX_TEXMAP1, GX_COLORNULL);
	GX_SetTevColorIn(GX_TEVSTAGE7, GX_CC_TEXC, GX_CC_ZERO, GX_CC_ZERO, GX_CC_CPREV);
	GX_SetTevColorOp(GX_TEVSTAGE7, GX_TEV_SUB, GX_TB_ZERO, GX_CS_SCALE_1, GX_FALSE, GX_TEVPREV);
	GX_SetTevIndirect(GX_TEVSTAGE7, GX_INDTEXSTAGE0, GX_ITF_8, GX_ITB_STU, GX_ITM_2, GX_ITW_OFF, GX_ITW_OFF, GX_FALSE, GX_FALSE, GX_ITBA_OFF);

	GX_SetTevOrder(GX_TEVSTAGE8, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLORNULL);
	GX_SetTevKColorSel(GX_TEVSTAGE8, GX_TEV_KCSEL_K1);
	GX_SetTevColorIn(GX_TEVSTAGE8, GX_CC_CPREV, GX_CC_KONST, GX_CC_ONE, GX_CC_ZERO);
	GX_SetTevColorOp(GX_TEVSTAGE8, GX_TEV_COMP_RGB8_GT, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
	GX_SetTevDirect(GX_TEVSTAGE8);

	GX_SetTevOrder(GX_TEVSTAGE9, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLORNULL);
	GX_SetTevColorIn(GX_TEVSTAGE9, GX_CC_CPREV, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO);
	GX_SetTevColorOp(GX_TEVSTAGE9, GX_TEV_COMP_BGR24_EQ, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
	GX_SetTevKAlphaSel(GX_TEVSTAGE9, GX_TEV_KASEL_1);
	GX_SetTevAlphaIn(GX_TEVSTAGE9, GX_CA_ZERO, GX_CA_ZERO, GX_CA_KONST, GX_CA_ZERO);
	GX_SetTevAlphaOp(GX_TEVSTAGE9, GX_TEV_COMP_BGR24_EQ, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
	GX_SetTevDirect(GX_TEVSTAGE9);

	GX_SetTevOrder(GX_TEVSTAGE10, GX_TEXCOORD0, GX_TEXMAP0, GX_COLORNULL);
	GX_SetTevColorIn(GX_TEVSTAGE10, GX_CC_C0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_TEXC);
	GX_SetTevColorOp(GX_TEVSTAGE10, GX_TEV_COMP_BGR24_GT, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
	GX_SetTevAlphaIn(GX_TEVSTAGE10, GX_CA_ZERO, GX_CA_ZERO, GX_CA_APREV, GX_CA_ZERO);
	GX_SetTevAlphaOp(GX_TEVSTAGE10, GX_TEV_COMP_BGR24_GT, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
	GX_SetTevIndirect(GX_TEVSTAGE10, GX_INDTEXSTAGE0, GX_ITF_8, GX_ITB_STU, GX_ITM_1, GX_ITW_OFF, GX_ITW_OFF, GX_FALSE, GX_FALSE, GX_ITBA_OFF);

	GX_SetTevOrder(GX_TEVSTAGE11, GX_TEXCOORD0, GX_TEXMAP0, GX_COLORNULL);
	GX_SetTevColorIn(GX_TEVSTAGE11, GX_CC_TEXC, GX_CC_ZERO, GX_CC_ZERO, GX_CC_CPREV);
	GX_SetTevColorOp(GX_TEVSTAGE11, GX_TEV_ADD, GX_TB_ZERO, GX_CS_DIVIDE_2, GX_TRUE, GX_TEVPREV);
	GX_SetTevAlphaIn(GX_TEVSTAGE11, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_APREV);
	GX_SetTevAlphaOp(GX_TEVSTAGE11, GX_TEV_ADD, GX_TB_ZERO, GX_CS_DIVIDE_2, GX_TRUE, GX_TEVPREV);
	GX_SetTevIndirect(GX_TEVSTAGE11, GX_INDTEXSTAGE0, GX_ITF_8, GX_ITB_STU, GX_ITM_2, GX_ITW_OFF, GX_ITW_OFF, GX_FALSE, GX_FALSE, GX_ITBA_OFF);

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

	GX_SetPixelFmt(GX_PF_RGB8_Z24, GX_ZC_LINEAR);
	GX_SetCopyFilter(GX_FALSE, NULL, GX_FALSE, NULL);

	if (src->dirty) GX_PreloadEntireTexture(&src->obj[0], &src->region[0]);
	if (yuv->dirty) GX_PreloadEntireTexture(&yuv->obj[0], &yuv->region[0]);
	GX_LoadTexObjPreloaded(&src->obj[0], &src->region[0], GX_TEXMAP0);
	GX_LoadTexObjPreloaded(&yuv->obj[0], &yuv->region[0], GX_TEXMAP1);

	GX_LoadTexObj(&indtexobj[0], GX_TEXMAP5);

	for (int ch = GX_CH_RED; ch <= GX_CH_BLUE; ch++)
		GXPlanarCopyChannel(dst->obj[ch], dst->rect, src->rect, ch);

	dst->dirty = true; src->dirty = false; yuv->dirty = false;
}

void GXPlanarApplyScale2x(gx_surface_t *dst, gx_surface_t *src, bool blend)
{
	Mtx44 projection;
	guOrtho(projection, 0., 1024., 0., 1024., 0., 1.);

	GX_SetBlendMode(GX_BM_NONE, GX_BL_ZERO, GX_BL_ZERO, GX_LO_CLEAR);
	GX_SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
	GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
	GX_SetZCompLoc(GX_FALSE);

	GX_SetNumChans(0);
	GX_SetNumTexGens(6);
	GX_SetNumIndStages(1);
	GX_SetNumTevStages(7);

	GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
	GX_SetTexCoordGen2(GX_TEXCOORD1, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY, GX_FALSE, GX_DTTMTX2);
	GX_SetTexCoordGen2(GX_TEXCOORD2, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY, GX_FALSE, GX_DTTMTX8);
	GX_SetTexCoordGen2(GX_TEXCOORD3, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY, GX_FALSE, GX_DTTMTX4);
	GX_SetTexCoordGen2(GX_TEXCOORD4, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY, GX_FALSE, GX_DTTMTX6);
	GX_SetTexCoordGen(GX_TEXCOORD5, GX_TG_MTX2x4, GX_TG_TEX0, GX_TEXMTX0);

	GX_SetIndTexOrder(GX_INDTEXSTAGE0, GX_TEXCOORD5, GX_TEXMAP5);
	GX_SetIndTexMatrix(GX_ITM_1, indtexmtx[1], 0);
	GX_SetIndTexMatrix(GX_ITM_2, indtexmtx[2], 0);

	GX_SetTevSwapModeTable(GX_TEV_SWAP0, GX_CH_BLUE, GX_CH_GREEN, GX_CH_RED, GX_CH_ALPHA);

	GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD1, GX_TEXMAP0, GX_COLORNULL);
	GX_SetTevColorIn(GX_TEVSTAGE0, GX_CC_TEXC, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO);
	GX_SetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
	GX_SetTevDirect(GX_TEVSTAGE0);

	GX_SetTevOrder(GX_TEVSTAGE1, GX_TEXCOORD2, GX_TEXMAP0, GX_COLORNULL);
	GX_SetTevColorIn(GX_TEVSTAGE1, GX_CC_CPREV, GX_CC_TEXC, GX_CC_ZERO, GX_CC_ZERO);
	GX_SetTevColorOp(GX_TEVSTAGE1, GX_TEV_COMP_BGR24_EQ, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
	GX_SetTevKAlphaSel(GX_TEVSTAGE1, GX_TEV_KASEL_1);
	GX_SetTevAlphaIn(GX_TEVSTAGE1, GX_CA_ZERO, GX_CA_ZERO, GX_CA_KONST, GX_CA_ZERO);
	GX_SetTevAlphaOp(GX_TEVSTAGE1, GX_TEV_COMP_BGR24_EQ, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
	GX_SetTevDirect(GX_TEVSTAGE1);

	GX_SetTevOrder(GX_TEVSTAGE2, GX_TEXCOORD3, GX_TEXMAP0, GX_COLORNULL);
	GX_SetTevColorIn(GX_TEVSTAGE2, GX_CC_TEXC, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO);
	GX_SetTevColorOp(GX_TEVSTAGE2, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
	GX_SetTevDirect(GX_TEVSTAGE2);

	GX_SetTevOrder(GX_TEVSTAGE3, GX_TEXCOORD4, GX_TEXMAP0, GX_COLORNULL);
	GX_SetTevColorIn(GX_TEVSTAGE3, GX_CC_CPREV, GX_CC_TEXC, GX_CC_ZERO, GX_CC_ZERO);
	GX_SetTevColorOp(GX_TEVSTAGE3, GX_TEV_COMP_BGR24_EQ, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
	GX_SetTevKAlphaSel(GX_TEVSTAGE3, GX_TEV_KASEL_1);
	GX_SetTevAlphaIn(GX_TEVSTAGE3, GX_CA_ZERO, GX_CA_ZERO, GX_CA_KONST, GX_CA_APREV);
	GX_SetTevAlphaOp(GX_TEVSTAGE3, GX_TEV_COMP_BGR24_EQ, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
	GX_SetTevDirect(GX_TEVSTAGE3);

	GX_SetTevOrder(GX_TEVSTAGE4, GX_TEXCOORD0, GX_TEXMAP0, GX_COLORNULL);
	GX_SetTevColorIn(GX_TEVSTAGE4, GX_CC_TEXC, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO);
	GX_SetTevColorOp(GX_TEVSTAGE4, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
	GX_SetTevAlphaIn(GX_TEVSTAGE4, GX_CA_APREV, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO);
	GX_SetTevAlphaOp(GX_TEVSTAGE4, GX_TEV_SUB, GX_TB_ZERO, GX_CS_SCALE_1, GX_FALSE, GX_TEVPREV);
	GX_SetTevIndirect(GX_TEVSTAGE4, GX_INDTEXSTAGE0, GX_ITF_8, GX_ITB_STU, GX_ITM_1, GX_ITW_OFF, GX_ITW_OFF, GX_FALSE, GX_FALSE, GX_ITBA_OFF);

	GX_SetTevOrder(GX_TEVSTAGE5, GX_TEXCOORD0, GX_TEXMAP0, GX_COLORNULL);
	GX_SetTevColorIn(GX_TEVSTAGE5, GX_CC_CPREV, GX_CC_TEXC, GX_CC_CPREV, GX_CC_ZERO);
	GX_SetTevColorOp(GX_TEVSTAGE5, GX_TEV_COMP_BGR24_EQ, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
	GX_SetTevKAlphaSel(GX_TEVSTAGE5, GX_TEV_KASEL_1);
	GX_SetTevAlphaIn(GX_TEVSTAGE5, GX_CA_ZERO, GX_CA_ZERO, GX_CA_KONST, GX_CA_APREV);
	GX_SetTevAlphaOp(GX_TEVSTAGE5, GX_TEV_COMP_BGR24_EQ, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
	GX_SetTevIndirect(GX_TEVSTAGE5, GX_INDTEXSTAGE0, GX_ITF_8, GX_ITB_STU, GX_ITM_2, GX_ITW_OFF, GX_ITW_OFF, GX_FALSE, GX_FALSE, GX_ITBA_OFF);

	if (blend) {
		GX_SetTevOrder(GX_TEVSTAGE6, GX_TEXCOORD0, GX_TEXMAP0, GX_COLORNULL);
		GX_SetTevColorIn(GX_TEVSTAGE6, GX_CC_TEXC, GX_CC_CPREV, GX_CC_APREV, GX_CC_TEXC);
		GX_SetTevColorOp(GX_TEVSTAGE6, GX_TEV_ADD, GX_TB_ZERO, GX_CS_DIVIDE_2, GX_TRUE, GX_TEVPREV);
		GX_SetTevDirect(GX_TEVSTAGE6);
	} else {
		GX_SetTevOrder(GX_TEVSTAGE6, GX_TEXCOORD0, GX_TEXMAP0, GX_COLORNULL);
		GX_SetTevColorIn(GX_TEVSTAGE6, GX_CC_TEXC, GX_CC_CPREV, GX_CC_APREV, GX_CC_ZERO);
		GX_SetTevColorOp(GX_TEVSTAGE6, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
		GX_SetTevDirect(GX_TEVSTAGE6);
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

	GX_SetPixelFmt(GX_PF_RGB8_Z24, GX_ZC_LINEAR);
	GX_SetCopyFilter(GX_FALSE, NULL, GX_FALSE, NULL);

	if (src->dirty) GX_PreloadEntireTexture(&src->obj[0], &src->region[0]);
	GX_LoadTexObjPreloaded(&src->obj[0], &src->region[0], GX_TEXMAP0);

	GX_LoadTexObj(&indtexobj[0], GX_TEXMAP5);

	for (int ch = GX_CH_RED; ch <= GX_CH_BLUE; ch++)
		GXPlanarCopyChannel(dst->obj[ch], dst->rect, src->rect, ch);

	dst->dirty = true; src->dirty = false;
}

void GXPlanarApplyEagle2x(gx_surface_t *dst, gx_surface_t *src)
{
	Mtx44 projection;
	guOrtho(projection, 0., 1024., 0., 1024., 0., 1.);

	GX_SetBlendMode(GX_BM_NONE, GX_BL_ZERO, GX_BL_ZERO, GX_LO_CLEAR);
	GX_SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
	GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
	GX_SetZCompLoc(GX_FALSE);

	GX_SetNumChans(0);
	GX_SetNumTexGens(2);
	GX_SetNumIndStages(3);
	GX_SetNumTevStages(4);

	GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
	GX_SetTexCoordGen(GX_TEXCOORD1, GX_TG_MTX2x4, GX_TG_TEX0, GX_TEXMTX0);

	GX_SetIndTexOrder(GX_INDTEXSTAGE0, GX_TEXCOORD1, GX_TEXMAP5);
	GX_SetIndTexOrder(GX_INDTEXSTAGE1, GX_TEXCOORD1, GX_TEXMAP6);
	GX_SetIndTexOrder(GX_INDTEXSTAGE2, GX_TEXCOORD1, GX_TEXMAP7);
	GX_SetIndTexMatrix(GX_ITM_0, indtexmtx[0], 0);

	GX_SetTevSwapModeTable(GX_TEV_SWAP0, GX_CH_BLUE, GX_CH_GREEN, GX_CH_RED, GX_CH_ALPHA);

	GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLORNULL);
	GX_SetTevColorIn(GX_TEVSTAGE0, GX_CC_TEXC, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO);
	GX_SetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
	GX_SetTevIndirect(GX_TEVSTAGE0, GX_INDTEXSTAGE1, GX_ITF_8, GX_ITB_STU, GX_ITM_0, GX_ITW_OFF, GX_ITW_OFF, GX_FALSE, GX_FALSE, GX_ITBA_OFF);

	GX_SetTevOrder(GX_TEVSTAGE1, GX_TEXCOORD0, GX_TEXMAP0, GX_COLORNULL);
	GX_SetTevColorIn(GX_TEVSTAGE1, GX_CC_CPREV, GX_CC_TEXC, GX_CC_CPREV, GX_CC_ZERO);
	GX_SetTevColorOp(GX_TEVSTAGE1, GX_TEV_COMP_BGR24_EQ, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
	GX_SetTevKAlphaSel(GX_TEVSTAGE1, GX_TEV_KASEL_1);
	GX_SetTevAlphaIn(GX_TEVSTAGE1, GX_CA_ZERO, GX_CA_ZERO, GX_CA_KONST, GX_CA_ZERO);
	GX_SetTevAlphaOp(GX_TEVSTAGE1, GX_TEV_COMP_BGR24_EQ, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
	GX_SetTevIndirect(GX_TEVSTAGE1, GX_INDTEXSTAGE2, GX_ITF_8, GX_ITB_STU, GX_ITM_0, GX_ITW_OFF, GX_ITW_OFF, GX_FALSE, GX_FALSE, GX_ITBA_OFF);

	GX_SetTevOrder(GX_TEVSTAGE2, GX_TEXCOORD0, GX_TEXMAP0, GX_COLORNULL);
	GX_SetTevColorIn(GX_TEVSTAGE2, GX_CC_CPREV, GX_CC_TEXC, GX_CC_CPREV, GX_CC_ZERO);
	GX_SetTevColorOp(GX_TEVSTAGE2, GX_TEV_COMP_BGR24_EQ, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
	GX_SetTevAlphaIn(GX_TEVSTAGE2, GX_CA_ZERO, GX_CA_ZERO, GX_CA_APREV, GX_CA_ZERO);
	GX_SetTevAlphaOp(GX_TEVSTAGE2, GX_TEV_COMP_BGR24_EQ, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
	GX_SetTevIndirect(GX_TEVSTAGE2, GX_INDTEXSTAGE0, GX_ITF_8, GX_ITB_STU, GX_ITM_0, GX_ITW_OFF, GX_ITW_OFF, GX_FALSE, GX_FALSE, GX_ITBA_OFF);

	GX_SetTevOrder(GX_TEVSTAGE3, GX_TEXCOORD0, GX_TEXMAP0, GX_COLORNULL);
	GX_SetTevColorIn(GX_TEVSTAGE3, GX_CC_TEXC, GX_CC_CPREV, GX_CC_APREV, GX_CC_ZERO);
	GX_SetTevColorOp(GX_TEVSTAGE3, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
	GX_SetTevDirect(GX_TEVSTAGE3);

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

	GX_SetPixelFmt(GX_PF_RGB8_Z24, GX_ZC_LINEAR);
	GX_SetCopyFilter(GX_FALSE, NULL, GX_FALSE, NULL);

	if (src->dirty) GX_PreloadEntireTexture(&src->obj[0], &src->region[0]);
	GX_LoadTexObjPreloaded(&src->obj[0], &src->region[0], GX_TEXMAP0);

	GX_LoadTexObj(&indtexobj[0], GX_TEXMAP5);
	GX_LoadTexObj(&indtexobj[1], GX_TEXMAP6);
	GX_LoadTexObj(&indtexobj[2], GX_TEXMAP7);

	for (int ch = GX_CH_RED; ch <= GX_CH_BLUE; ch++)
		GXPlanarCopyChannel(dst->obj[ch], dst->rect, src->rect, ch);

	dst->dirty = true; src->dirty = false;
}

void GXPlanarApplyScan2x(gx_surface_t *dst, gx_surface_t *src, bool field)
{
	Mtx44 projection;
	guOrtho(projection, 0., 1024., 0., 1024., 0., 1.);

	GX_SetBlendMode(GX_BM_NONE, GX_BL_ZERO, GX_BL_ZERO, GX_LO_CLEAR);
	GX_SetAlphaCompare(GX_GREATER, 0, GX_AOP_AND, GX_GREATER, 0);
	GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
	GX_SetZCompLoc(GX_FALSE);

	GX_SetNumChans(0);
	GX_SetNumTexGens(1);
	GX_SetNumIndStages(0);
	GX_SetNumTevStages(1);

	GX_SetTevSwapModeTable(GX_TEV_SWAP0, GX_CH_BLUE, GX_CH_GREEN, GX_CH_RED, GX_CH_ALPHA);

	GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLORNULL);
	GX_SetTevColorIn(GX_TEVSTAGE0, GX_CC_TEXC, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO);
	GX_SetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
	GX_SetTevKAlphaSel(GX_TEVSTAGE0, GX_TEV_KASEL_1);
	GX_SetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_KONST, GX_CA_ZERO);
	GX_SetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_COMP_BGR24_GT, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
	GX_SetTevDirect(GX_TEVSTAGE0);

	GX_ClearVtxDesc();
	GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XY, GX_S16, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_S16, 0);

	GX_LoadProjectionMtx(projection, GX_ORTHOGRAPHIC);
	GX_SetCurrentMtx(GX_PNMTX0);
	GX_SetViewport(0., 0., 1024., 1024., 0., 1.);

	GX_SetFieldMask(field, !field);
	GX_SetFieldMode(GX_FALSE, GX_FALSE);

	GX_SetPixelFmt(GX_PF_RGB8_Z24, GX_ZC_LINEAR);
	GX_SetCopyFilter(GX_FALSE, NULL, GX_FALSE, NULL);

	if (src->dirty) GX_PreloadEntireTexture(&src->obj[0], &src->region[0]);
	GX_LoadTexObjPreloaded(&src->obj[0], &src->region[0], GX_TEXMAP0);

	for (int ch = GX_CH_RED; ch <= GX_CH_BLUE; ch++)
		GXPlanarCopyChannel(dst->obj[ch], dst->rect, src->rect, ch);

	dst->dirty = true; src->dirty = false;
}

void GXPlanarAllocState(void)
{
	GX_InitTexObj(&indtexobj[0], indtexdata[0], 2, 2, GX_TF_IA8, GX_REPEAT, GX_REPEAT, GX_FALSE);
	GX_InitTexObj(&indtexobj[1], indtexdata[1], 2, 2, GX_TF_IA8, GX_REPEAT, GX_REPEAT, GX_FALSE);
	GX_InitTexObj(&indtexobj[2], indtexdata[2], 2, 2, GX_TF_IA8, GX_REPEAT, GX_REPEAT, GX_FALSE);
	GX_InitTexObjFilterMode(&indtexobj[0], GX_NEAR, GX_NEAR);
	GX_InitTexObjFilterMode(&indtexobj[1], GX_NEAR, GX_NEAR);
	GX_InitTexObjFilterMode(&indtexobj[2], GX_NEAR, GX_NEAR);
}
