/* 
 * Copyright (c) 2015-2024, Extrems' Corner.org
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

void GXSolidDrawRect(rect_t rect, uint32_t color[4])
{
	GX_Begin(GX_QUADS, GX_VTXFMT0, 4);

	GX_Position2s16(rect.x, rect.y);
	GX_Color1u32(color[0]);

	GX_Position2s16(rect.x + rect.w, rect.y);
	GX_Color1u32(color[1]);

	GX_Position2s16(rect.x + rect.w, rect.y + rect.h);
	GX_Color1u32(color[2]);

	GX_Position2s16(rect.x, rect.y + rect.h);
	GX_Color1u32(color[3]);
}

void GXSolidAllocState(void)
{
	Mtx44 projection;
	guOrtho(projection,
		-screen.y, screen.y + screen.h,
		-screen.x, screen.x + screen.w, 0., 1.);

	displist = GXAllocBuffer(GX_FIFO_MINSIZE);
	GX_BeginDispList(displist, GX_FIFO_MINSIZE);

	GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
	GX_SetAlphaCompare(GX_GREATER, 0, GX_AOP_AND, GX_GREATER, 0);
	GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
	GX_SetZCompLoc(GX_FALSE);

	GX_SetNumChans(1);
	GX_SetNumTexGens(0);
	GX_SetNumIndStages(0);
	GX_SetNumTevStages(1);

	GX_SetTevSwapModeTable(GX_TEV_SWAP0, GX_CH_RED, GX_CH_GREEN, GX_CH_BLUE, GX_CH_ALPHA);

	GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0);
	GX_SetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_RASC);
	GX_SetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
	GX_SetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_RASA);
	GX_SetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
	GX_SetTevDirect(GX_TEVSTAGE0);

	GX_ClearVtxDesc();
	GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XY, GX_S16, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);

	GX_LoadProjectionMtx(projection, GX_ORTHOGRAPHIC);
	GX_SetCurrentMtx(GX_PNMTX0);

	dispsize = GX_EndDispList();
	displist = realloc_in_place(displist, dispsize);
}

void GXSolidSetState(void)
{
	if (state.current != STATE_SOLID) {
		state.current  = STATE_SOLID;
		GX_CallDispList(displist, dispsize);
	}
}
