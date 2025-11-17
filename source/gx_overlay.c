/* 
 * Copyright (c) 2015-2025, Extrems' Corner.org
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <malloc.h>
#include <gccore.h>
#include "gx.h"
#include "state.h"
#include "video.h"

static void *displist;
static uint32_t dispsize;

static TPLFile tdf;
static GXTexObj texobj;

void GXOverlayDrawRect(rect_t rect)
{
	uint16_t width, height;

	if (TPL_GetTextureInfo(&tdf, MIN(state.overlay_id, tdf.ntextures - 1), NULL, &width, &height) == 0) {
		uint16_t scale = width < height ? width / rect.w : height / rect.h;

		Mtx viewmodel;
		guMtxRotDeg(viewmodel, 'z', state.rotation);
		guMtxScaleApply(viewmodel, viewmodel,
			state.overlay_scale.x ? state.overlay_scale.x : state.zoom.x / scale,
			state.overlay_scale.y ? state.overlay_scale.y : state.zoom.y / scale, 1);
		GX_LoadPosMtxImm(viewmodel, GX_PNMTX1);

		GX_LoadTexObj(&texobj, GX_TEXMAP0);

		GX_Begin(GX_QUADS, GX_VTXFMT0, 4);

		GX_Position2s16(-width, -height);
		GX_TexCoord2s16(0, 0);

		GX_Position2s16(+width, -height);
		GX_TexCoord2s16(width, 0);

		GX_Position2s16(+width, +height);
		GX_TexCoord2s16(width, height);

		GX_Position2s16(-width, +height);
		GX_TexCoord2s16(0, height);
	}
}

void GXOverlayAllocState(void)
{
	Mtx44 projection;
	guOrtho(projection,
		-screen.y - screen.h / 2., screen.y + screen.h / 2.,
		-screen.x - screen.w / 2., screen.x + screen.w / 2., 0., 1.);

	displist = GXAllocBuffer(GX_FIFO_MINSIZE);
	GX_BeginDispList(displist, GX_FIFO_MINSIZE);

	GX_SetBlendMode(GX_BM_BLEND, GX_BL_ONE, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
	GX_SetAlphaCompare(GX_GREATER, 0, GX_AOP_AND, GX_GREATER, 0);
	GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
	GX_SetZCompLoc(GX_FALSE);

	GX_SetNumChans(0);
	GX_SetNumTexGens(1);
	GX_SetNumIndStages(0);
	GX_SetNumTevStages(1);

	GX_SetTevSwapModeTable(GX_TEV_SWAP0, GX_CH_RED, GX_CH_GREEN, GX_CH_BLUE, GX_CH_ALPHA);

	GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR_NULL);
	GX_SetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_TEXC);
	GX_SetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
	GX_SetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_TEXA);
	GX_SetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
	GX_SetTevDirect(GX_TEVSTAGE0);

	GX_ClearVtxDesc();
	GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XY, GX_S16, 1);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_S16, 0);

	GX_LoadProjectionMtx(projection, GX_ORTHOGRAPHIC);
	GX_SetCurrentMtx(GX_PNMTX1);

	dispsize = GX_EndDispList();
	displist = realloc_in_place(displist, dispsize);
}

void GXOverlaySetState(void)
{
	if (state.current != STATE_OVERLAY) {
		state.current  = STATE_OVERLAY;
		GX_CallDispList(displist, dispsize);
	}
}

void GXOverlayReadMemEx(void *buffer, int size, int index)
{
	TPL_CloseTPLFile(&tdf);
	TPL_OpenTPLFromMemory(&tdf, buffer, size);
	TPL_GetTexture(&tdf, MIN(index, tdf.ntextures - 1), &texobj);
}

void GXOverlayReadMem(void *buffer, int size, int index)
{
	TPL_CloseTPLFile(&tdf);
	TPL_OpenTPLFromHandle(&tdf, GXOpenMem(buffer, size));
	TPL_GetTexture(&tdf, MIN(index, tdf.ntextures - 1), &texobj);
}

void GXOverlayReadFile(const char *file, int index)
{
	TPL_CloseTPLFile(&tdf);
	TPL_OpenTPLFromHandle(&tdf, GXOpenFile(file));
	TPL_GetTexture(&tdf, MIN(index, tdf.ntextures - 1), &texobj);
}
