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
static GXTexObj texobj[5];

void GXCursorDrawPoint(uint32_t index, float x, float y, float angle)
{
	uint16_t width, height;

	if (TPL_GetTextureInfo(&tdf, index, NULL, &width, &height) == 0) {
		Mtx viewmodel;
		guMtxRotDeg(viewmodel, 'z', angle);
		guMtxTransApply(viewmodel, viewmodel, x, y, 0);
		GX_LoadPosMtxImm(viewmodel, GX_PNMTX1);

		GX_LoadTexObj(&texobj[index], GX_TEXMAP0);

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

void GXCursorAllocState(void)
{
	Mtx44 projection;
	guOrtho(projection,
		-screen.y, screen.y + screen.h,
		-screen.x, screen.x + screen.w, 0., 1.);

	TPL_OpenTPLFromHandle(&tdf, GXOpenFile(state.cursor));
	TPL_GetTexture(&tdf, 0, &texobj[0]);
	TPL_GetTexture(&tdf, 1, &texobj[1]);
	TPL_GetTexture(&tdf, 2, &texobj[2]);
	TPL_GetTexture(&tdf, 3, &texobj[3]);
	TPL_GetTexture(&tdf, 4, &texobj[4]);

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

	GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLORNULL);
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

void GXCursorSetState(void)
{
	if (state.current != STATE_CURSOR) {
		state.current  = STATE_CURSOR;
		GX_CallDispList(displist, dispsize);
	}
}
