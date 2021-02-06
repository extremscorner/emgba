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

static void *displist[2];
static uint32_t dispsize[2];

static float indtexmtx[][2][3] = {
	{
		{ +.5, +.0, +.0 },
		{ +.0, +.5, +.0 }
	}, {
		{ -.5, +.0, +.0 },
		{ +.0, -.5, +.0 }
	}
};

static uint16_t indtexdata[][4 * 4] ATTRIBUTE_ALIGN(32) = {
	{
		0xE0E0, 0xA0E0, 0x60E0, 0x20E0,
		0xE0A0, 0xA0A0, 0x60A0, 0x20A0,
		0xE060, 0xA060, 0x6060, 0x2060,
		0xE020, 0xA020, 0x6020, 0x2020,
	}, {
		0xC0C0, 0x40C0, 0xC0C0, 0x40C0,
		0xC040, 0x4040, 0xC040, 0x4040,
		0xC0C0, 0x40C0, 0xC0C0, 0x40C0,
		0xC040, 0x4040, 0xC040, 0x4040,
	}, {
		0x8080, 0x8080, 0x8080, 0x8080,
		0x8080, 0x8080, 0x8080, 0x8080,
		0x8080, 0x8080, 0x8080, 0x8080,
		0x8080, 0x8080, 0x8080, 0x8080,
	}
};

static GXTexObj indtexobj;

static GXColorS10 color[][3] = {
	[MATRIX_IDENTITY] = {
		{ + 0, + 0, + 0 },
		{ + 0, + 0, + 0 },
		{ + 0, + 0, + 0 }
	}, [MATRIX_GBA] = {
		{ -15, + 0, + 0 },
		{ + 0, + 0, + 0 },
		{ +15, + 0, + 0 }
	}, [MATRIX_GBI] = {
		{ -27, + 0, + 0 },
		{ + 0, + 0, + 0 },
		{ +27, + 0, + 0 }
	}, [MATRIX_NDS] = {
		{ -32, + 0, + 0 },
		{ + 0, + 0, + 0 },
		{ +32, + 0, + 0 }
	}, [MATRIX_PALM] = {
		{ -23, + 0, + 0 },
		{ + 0, + 0, + 0 },
		{ +23, + 0, + 0 }
	}, [MATRIX_PSP] = {
		{ -46, + 0, + 0 },
		{ + 0, + 0, + 0 },
		{ +46, + 0, + 0 }
	}, [MATRIX_VBA] = {
		{ + 0, + 0, + 0 },
		{ + 0, + 0, + 0 },
		{ + 0, + 0, + 0 }
	}
};

static GXColor kcolor[][4] = {
	[MATRIX_IDENTITY] = {
		{ 255,   0,   0 },
		{   0, 255,   0 },
		{   0,   0, 255 },
		{  54, 182,  18 }
	}, [MATRIX_GBA] = {
		{ 209,  32,  50 },
		{  61, 170,  19 },
		{   0,  54, 186 },
		{  71, 136,  48 }
	}, [MATRIX_GBI] = {
		{ 238,  12,   7 },
		{  45, 195,   9 },
		{   0,  48, 239 },
		{  60, 149,  46 }
	}, [MATRIX_NDS] = {
		{ 222,  26,  26 },
		{  65, 164,  43 },
		{   0,  65, 186 },
		{  67, 135,  53 }
	}, [MATRIX_PALM] = {
		{ 212,  19,  22 },
		{  66, 173,  31 },
		{   0,  64, 203 },
		{  60, 140,  55 }
	}, [MATRIX_PSP] = {
		{ 250,  10,   3 },
		{  51, 203,   3 },
		{   0,  42, 250 },
		{  61, 156,  38 }
	}, [MATRIX_VBA] = {
		{ 186,  22,  22 },
		{  69, 172,  61 },
		{   0,  61, 172 },
		{  57, 142,  56 },
	}
};

void GXPreviewDrawRect(GXTexObj texobj[3], rect_t dst_rect, rect_t src_rect)
{
	Mtx viewmodel;
	guMtxRotDeg(viewmodel, 'z', state.rotation);
	guMtxScaleApply(viewmodel, viewmodel, state.zoom.x, state.zoom.y, 1);
	GX_LoadPosMtxImm(viewmodel, GX_PNMTX1);

	GX_LoadTexObj(&texobj[0], GX_TEXMAP0);
	GX_LoadTexObj(&texobj[1], GX_TEXMAP1);
	GX_LoadTexObj(&texobj[2], GX_TEXMAP2);

	GX_LoadTexObj(&indtexobj, GX_TEXMAP3);

	GX_Begin(GX_QUADS, GX_VTXFMT0, 4);

	GX_Position2s16(-dst_rect.w, -dst_rect.h);
	GX_TexCoord2s16(src_rect.x, src_rect.y);

	GX_Position2s16(+dst_rect.w, -dst_rect.h);
	GX_TexCoord2s16(src_rect.x + src_rect.w, src_rect.y);

	GX_Position2s16(+dst_rect.w, +dst_rect.h);
	GX_TexCoord2s16(src_rect.x + src_rect.w, src_rect.y + src_rect.h);

	GX_Position2s16(-dst_rect.w, +dst_rect.h);
	GX_TexCoord2s16(src_rect.x, src_rect.y + src_rect.h);
}

void GXPreviewAllocState(void)
{
	Mtx44 projection;
	guOrtho(projection, -screen.h / 2, screen.h / 2, -screen.w / 2, screen.w / 2, 0., 1.);

	switch (state.scaler) {
		case SCALER_AREA:
			GX_InitTexObj(&indtexobj, indtexdata, 4, 4, GX_TF_IA8, GX_REPEAT, GX_REPEAT, GX_TRUE);
			GX_InitTexObjLOD(&indtexobj, GX_LIN_MIP_LIN, GX_LINEAR, 0., 2., -1./3., GX_FALSE, GX_TRUE, GX_ANISO_4);
			break;
		case SCALER_BOX:
			GX_InitTexObj(&indtexobj, indtexdata, 4, 4, GX_TF_IA8, GX_REPEAT, GX_REPEAT, GX_TRUE);
			GX_InitTexObjLOD(&indtexobj, GX_LIN_MIP_LIN, GX_LINEAR, 1., 2., 0., GX_TRUE, GX_TRUE, GX_ANISO_4);
			break;
		default:
			break;
	}

	displist[0] = GXAllocBuffer(GX_FIFO_MINSIZE);
	GX_BeginDispList(displist[0], GX_FIFO_MINSIZE);

	GX_SetBlendMode(GX_BM_NONE, GX_BL_ZERO, GX_BL_ZERO, GX_LO_CLEAR);
	GX_SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
	GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
	GX_SetZCompLoc(GX_FALSE);

	if (state.scaler > SCALER_BILINEAR) {
		GX_SetNumChans(0);
		GX_SetNumTexGens(2);
		GX_SetNumIndStages(1);
		GX_SetNumTevStages(3);

		GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
		GX_SetTexCoordGen(GX_TEXCOORD1, GX_TG_MTX2x4, GX_TG_TEX0, GX_TEXMTX1);

		GX_SetIndTexOrder(GX_INDTEXSTAGE0, GX_TEXCOORD1, GX_TEXMAP3);
		GX_SetIndTexMatrix(GX_ITM_0, indtexmtx[state.scaler == SCALER_BOX], -7);

		GX_SetTevSwapModeTable(GX_TEV_SWAP0, GX_CH_RED, GX_CH_GREEN, GX_CH_BLUE, GX_CH_ALPHA);

		GXColorS10 *colorptr = color[state.matrix];
		GXColor *kcolorptr = kcolor[state.matrix];

		GX_SetTevColorS10(GX_TEVREG0, colorptr[GX_CH_RED]);
		GX_SetTevColorS10(GX_TEVREG1, colorptr[GX_CH_GREEN]);
		GX_SetTevColorS10(GX_TEVREG2, colorptr[GX_CH_BLUE]);

		GX_SetTevKColor(GX_KCOLOR0, kcolorptr[GX_CH_RED]);
		GX_SetTevKColor(GX_KCOLOR1, kcolorptr[GX_CH_GREEN]);
		GX_SetTevKColor(GX_KCOLOR2, kcolorptr[GX_CH_BLUE]);

		GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLORNULL);
		GX_SetTevKColorSel(GX_TEVSTAGE0, GX_TEV_KCSEL_K0);
		GX_SetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_KONST, GX_CC_TEXC, GX_CC_C0);
		GX_SetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_FALSE, GX_TEVPREV);
		GX_SetTevIndirect(GX_TEVSTAGE0, GX_INDTEXSTAGE0, GX_ITF_8, GX_ITB_STU, GX_ITM_0, GX_ITW_OFF, GX_ITW_OFF, GX_FALSE, GX_FALSE, GX_ITBA_OFF);

		GX_SetTevOrder(GX_TEVSTAGE1, GX_TEXCOORD0, GX_TEXMAP1, GX_COLORNULL);
		GX_SetTevKColorSel(GX_TEVSTAGE1, GX_TEV_KCSEL_K1);
		GX_SetTevColorIn(GX_TEVSTAGE1, GX_CC_C1, GX_CC_KONST, GX_CC_TEXC, GX_CC_CPREV);
		GX_SetTevColorOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_FALSE, GX_TEVPREV);
		GX_SetTevIndRepeat(GX_TEVSTAGE1);

		GX_SetTevOrder(GX_TEVSTAGE2, GX_TEXCOORD0, GX_TEXMAP2, GX_COLORNULL);
		GX_SetTevKColorSel(GX_TEVSTAGE2, GX_TEV_KCSEL_K2);
		GX_SetTevColorIn(GX_TEVSTAGE2, GX_CC_C2, GX_CC_KONST, GX_CC_TEXC, GX_CC_CPREV);
		GX_SetTevColorOp(GX_TEVSTAGE2, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
		GX_SetTevIndRepeat(GX_TEVSTAGE2);
	} else {
		GX_SetNumChans(0);
		GX_SetNumTexGens(1);
		GX_SetNumIndStages(0);
		GX_SetNumTevStages(3);

		GX_SetTevSwapModeTable(GX_TEV_SWAP0, GX_CH_RED, GX_CH_GREEN, GX_CH_BLUE, GX_CH_ALPHA);

		GXColorS10 *colorptr = color[state.matrix];
		GXColor *kcolorptr = kcolor[state.matrix];

		GX_SetTevColorS10(GX_TEVREG0, colorptr[GX_CH_RED]);
		GX_SetTevColorS10(GX_TEVREG1, colorptr[GX_CH_GREEN]);
		GX_SetTevColorS10(GX_TEVREG2, colorptr[GX_CH_BLUE]);

		GX_SetTevKColor(GX_KCOLOR0, kcolorptr[GX_CH_RED]);
		GX_SetTevKColor(GX_KCOLOR1, kcolorptr[GX_CH_GREEN]);
		GX_SetTevKColor(GX_KCOLOR2, kcolorptr[GX_CH_BLUE]);

		GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLORNULL);
		GX_SetTevKColorSel(GX_TEVSTAGE0, GX_TEV_KCSEL_K0);
		GX_SetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_KONST, GX_CC_TEXC, GX_CC_C0);
		GX_SetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_FALSE, GX_TEVPREV);
		GX_SetTevDirect(GX_TEVSTAGE0);

		GX_SetTevOrder(GX_TEVSTAGE1, GX_TEXCOORD0, GX_TEXMAP1, GX_COLORNULL);
		GX_SetTevKColorSel(GX_TEVSTAGE1, GX_TEV_KCSEL_K1);
		GX_SetTevColorIn(GX_TEVSTAGE1, GX_CC_C1, GX_CC_KONST, GX_CC_TEXC, GX_CC_CPREV);
		GX_SetTevColorOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_FALSE, GX_TEVPREV);
		GX_SetTevDirect(GX_TEVSTAGE1);

		GX_SetTevOrder(GX_TEVSTAGE2, GX_TEXCOORD0, GX_TEXMAP2, GX_COLORNULL);
		GX_SetTevKColorSel(GX_TEVSTAGE2, GX_TEV_KCSEL_K2);
		GX_SetTevColorIn(GX_TEVSTAGE2, GX_CC_C2, GX_CC_KONST, GX_CC_TEXC, GX_CC_CPREV);
		GX_SetTevColorOp(GX_TEVSTAGE2, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
		GX_SetTevDirect(GX_TEVSTAGE2);
	}

	GX_ClearVtxDesc();
	GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XY, GX_S16, 1);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_S16, 0);

	GX_LoadProjectionMtx(projection, GX_ORTHOGRAPHIC);
	GX_SetCurrentMtx(GX_PNMTX1);

	dispsize[0] = GX_EndDispList();
	displist[0] = realloc_in_place(displist[0], dispsize[0]);

	displist[1] = GXAllocBuffer(GX_FIFO_MINSIZE);
	GX_BeginDispList(displist[1], GX_FIFO_MINSIZE);

	GX_SetBlendMode(GX_BM_NONE, GX_BL_ZERO, GX_BL_ZERO, GX_LO_CLEAR);
	GX_SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
	GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
	GX_SetZCompLoc(GX_FALSE);

	if (state.scaler > SCALER_BILINEAR) {
		GX_SetNumChans(0);
		GX_SetNumTexGens(2);
		GX_SetNumIndStages(1);
		GX_SetNumTevStages(3);

		GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
		GX_SetTexCoordGen(GX_TEXCOORD1, GX_TG_MTX2x4, GX_TG_TEX0, GX_TEXMTX1);

		GX_SetIndTexOrder(GX_INDTEXSTAGE0, GX_TEXCOORD1, GX_TEXMAP3);
		GX_SetIndTexMatrix(GX_ITM_0, indtexmtx[state.scaler == SCALER_BOX], -7);

		GX_SetTevSwapModeTable(GX_TEV_SWAP0, GX_CH_RED, GX_CH_GREEN, GX_CH_BLUE, GX_CH_ALPHA);

		GX_SetTevKColor(GX_KCOLOR3, kcolor[state.matrix][GX_CH_ALPHA]);

		GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLORNULL);
		GX_SetTevKColorSel(GX_TEVSTAGE0, GX_TEV_KCSEL_K3_R);
		GX_SetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_KONST, GX_CC_TEXC, GX_CC_ZERO);
		GX_SetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
		GX_SetTevIndirect(GX_TEVSTAGE0, GX_INDTEXSTAGE0, GX_ITF_8, GX_ITB_STU, GX_ITM_0, GX_ITW_OFF, GX_ITW_OFF, GX_FALSE, GX_FALSE, GX_ITBA_OFF);

		GX_SetTevOrder(GX_TEVSTAGE1, GX_TEXCOORD0, GX_TEXMAP1, GX_COLORNULL);
		GX_SetTevKColorSel(GX_TEVSTAGE1, GX_TEV_KCSEL_K3_G);
		GX_SetTevColorIn(GX_TEVSTAGE1, GX_CC_ZERO, GX_CC_KONST, GX_CC_TEXC, GX_CC_CPREV);
		GX_SetTevColorOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
		GX_SetTevIndRepeat(GX_TEVSTAGE1);

		GX_SetTevOrder(GX_TEVSTAGE2, GX_TEXCOORD0, GX_TEXMAP2, GX_COLORNULL);
		GX_SetTevKColorSel(GX_TEVSTAGE2, GX_TEV_KCSEL_K3_B);
		GX_SetTevColorIn(GX_TEVSTAGE2, GX_CC_ZERO, GX_CC_KONST, GX_CC_TEXC, GX_CC_CPREV);
		GX_SetTevColorOp(GX_TEVSTAGE2, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
		GX_SetTevIndRepeat(GX_TEVSTAGE2);
	} else {
		GX_SetNumChans(0);
		GX_SetNumTexGens(1);
		GX_SetNumIndStages(0);
		GX_SetNumTevStages(3);

		GX_SetTevSwapModeTable(GX_TEV_SWAP0, GX_CH_RED, GX_CH_GREEN, GX_CH_BLUE, GX_CH_ALPHA);

		GX_SetTevKColor(GX_KCOLOR3, kcolor[state.matrix][GX_CH_ALPHA]);

		GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLORNULL);
		GX_SetTevKColorSel(GX_TEVSTAGE0, GX_TEV_KCSEL_K3_R);
		GX_SetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_KONST, GX_CC_TEXC, GX_CC_ZERO);
		GX_SetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
		GX_SetTevDirect(GX_TEVSTAGE0);

		GX_SetTevOrder(GX_TEVSTAGE1, GX_TEXCOORD0, GX_TEXMAP1, GX_COLORNULL);
		GX_SetTevKColorSel(GX_TEVSTAGE1, GX_TEV_KCSEL_K3_G);
		GX_SetTevColorIn(GX_TEVSTAGE1, GX_CC_ZERO, GX_CC_KONST, GX_CC_TEXC, GX_CC_CPREV);
		GX_SetTevColorOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
		GX_SetTevDirect(GX_TEVSTAGE1);

		GX_SetTevOrder(GX_TEVSTAGE2, GX_TEXCOORD0, GX_TEXMAP2, GX_COLORNULL);
		GX_SetTevKColorSel(GX_TEVSTAGE2, GX_TEV_KCSEL_K3_B);
		GX_SetTevColorIn(GX_TEVSTAGE2, GX_CC_ZERO, GX_CC_KONST, GX_CC_TEXC, GX_CC_CPREV);
		GX_SetTevColorOp(GX_TEVSTAGE2, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
		GX_SetTevDirect(GX_TEVSTAGE2);
	}

	GX_ClearVtxDesc();
	GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XY, GX_S16, 1);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_S16, 0);

	GX_LoadProjectionMtx(projection, GX_ORTHOGRAPHIC);
	GX_SetCurrentMtx(GX_PNMTX1);

	dispsize[1] = GX_EndDispList();
	displist[1] = realloc_in_place(displist[1], dispsize[1]);
}

void GXPreviewSetState(uint32_t index)
{
	if (state.current != STATE_PREVIEW + index) {
		state.current  = STATE_PREVIEW + index;
		GX_CallDispList(displist[index], dispsize[index]);
	}
}
