/* 
 * Copyright (c) 2015-2021, Extrems' Corner.org
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <malloc.h>
#include <gccore.h>
#include "gx.h"
#include "state.h"

static void GXPackedCopyRGB(GXTexObj texobj, rect_t dst_rect, rect_t src_rect, uint8_t level)
{
	void *ptr;
	uint16_t width, height;
	uint8_t format, wrap_s, wrap_t, mipmap;

	GX_GetTexObjAll(&texobj, &ptr, &width, &height, &format, &wrap_s, &wrap_t, &mipmap);

	switch (format) {
		case GX_TF_I4:
			format = GX_CTF_R4;
			break;
		case GX_TF_I8:
			format = GX_CTF_R8;
			break;
		case GX_TF_IA4:
			format = GX_CTF_RA4;
			break;
		case GX_TF_IA8:
			format = GX_CTF_RA8;
			break;
	}

	GX_SetScissor(0, 0, width << level, height << level);
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

	GX_SetTexCopySrc(0, 0, width << level, height << level);
	GX_SetTexCopyDst(width, height, format, level);

	GX_CopyTex(ptr, GX_FALSE);
}

static void GXPackedCopyYUV(GXTexObj texobj, rect_t dst_rect, rect_t src_rect, uint8_t level)
{
	void *ptr;
	uint16_t width, height;
	uint8_t format, wrap_s, wrap_t, mipmap;

	GX_GetTexObjAll(&texobj, &ptr, &width, &height, &format, &wrap_s, &wrap_t, &mipmap);

	switch (format) {
		case GX_TF_RGBA8:
			format = GX_CTF_YUVA8;
			break;
		case GX_TF_CI4:
			format = GX_TF_I4;
			break;
		case GX_TF_CI8:
			format = GX_TF_I8;
			break;
		case GX_TF_CI14:
			format = GX_TF_IA8;
			break;
	}

	GX_SetScissor(0, 0, width << level, height << level);
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

	GX_SetTexCopySrc(0, 0, width << level, height << level);
	GX_SetTexCopyDst(width, height, format, level);

	GX_CopyTex(ptr, GX_FALSE);
}

void GXPackedApplyMix(gx_surface_t *dst, gx_surface_t *src)
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

	GX_SetTevKColor(GX_KCOLOR0, (GXColor){alpha[2], alpha[1], alpha[0]});

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

	if (src->dirty) GX_PreloadEntireTexture(&src->obj[0], &src->region[0]);
	if (dst->dirty) GX_PreloadEntireTexture(&dst->obj[0], &dst->region[0]);
	GX_LoadTexObjPreloaded(&src->obj[0], &src->region[0], GX_TEXMAP0);
	GX_LoadTexObjPreloaded(&dst->obj[0], &dst->region[0], GX_TEXMAP1);

	GXPackedCopyRGB(dst->obj[0], dst->rect, src->rect, 0);

	dst->dirty = true; src->dirty = false;
}

void GXPackedApplyYUV(gx_surface_t *dst, gx_surface_t *src)
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

	GXPackedCopyYUV(dst->obj[0], dst->rect, src->rect, 1);

	dst->dirty = true; src->dirty = false;
}
