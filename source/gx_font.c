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
#include "video.h"

#include <mgba-util/gui/font.h>

static void *displist;
static uint32_t dispsize;

static sys_fontheader *fontdata;
static GXTexObj texobj;
static GXTexRegion texregion;

static void GXFontDrawCell(int16_t x1, int16_t y1, uint32_t c, int16_t s1, int16_t t1)
{
	int16_t x2 = x1 + fontdata->cell_width;
	int16_t y2 = y1 - fontdata->cell_height;

	int16_t s2 = s1 + fontdata->cell_width;
	int16_t t2 = t1 + fontdata->cell_height;

	GX_Begin(GX_QUADS, GX_VTXFMT0, 4);

	GX_Position2s16(x1, y2);
	GX_Color1u32(c);
	GX_TexCoord2s16(s1, t1);

	GX_Position2s16(x2, y2);
	GX_Color1u32(c);
	GX_TexCoord2s16(s2, t1);

	GX_Position2s16(x2, y1);
	GX_Color1u32(c);
	GX_TexCoord2s16(s2, t2);

	GX_Position2s16(x1, y1);
	GX_Color1u32(c);
	GX_TexCoord2s16(s1, t2);
}

void GXFontAllocState(void)
{
	Mtx44 projection;
	guOrtho(projection,
		-screen.y - screen.h / 2, screen.y + screen.h / 2,
		-screen.x - screen.w / 2, screen.x + screen.w / 2, 0., 1.);

	if (SYS_SetFontEncoding(SYS_FONTENC_ANSI) == SYS_FONTENC_ANSI)
		fontdata = SYS_AllocArenaMemHi(SYS_FONTSIZE_ANSI, 32);
	else
		fontdata = SYS_AllocArenaMemHi(SYS_FONTSIZE_SJIS, 32);

	SYS_InitFont(fontdata);
	fontdata->sheet_image = (fontdata->sheet_image + 31) & ~31;

	GX_InitTexObj(&texobj, (void *)fontdata + fontdata->sheet_image,
		fontdata->sheet_width, fontdata->sheet_height,
		fontdata->sheet_format, GX_CLAMP, GX_CLAMP, GX_FALSE);
	GX_InitTexObjLOD(&texobj, GX_LINEAR, GX_LINEAR, 0., 0., 0., GX_TRUE, GX_TRUE, GX_ANISO_4);
	GX_InitTexCacheRegion(&texregion, GX_FALSE, 0xC0000, GX_TEXCACHE_128K, 0x00000, GX_TEXCACHE_NONE);

	displist = GXAllocBuffer(GX_FIFO_MINSIZE);
	GX_BeginDispList(displist, GX_FIFO_MINSIZE);

	GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
	GX_SetAlphaCompare(GX_GREATER, 0, GX_AOP_AND, GX_GREATER, 0);
	GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
	GX_SetZCompLoc(GX_FALSE);

	GX_SetNumChans(1);
	GX_SetNumTexGens(1);
	GX_SetNumIndStages(0);
	GX_SetNumTevStages(1);

	GX_SetTevSwapModeTable(GX_TEV_SWAP0, GX_CH_ALPHA, GX_CH_BLUE, GX_CH_GREEN, GX_CH_RED);

	GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
	GX_SetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_TEXC, GX_CC_RASC, GX_CC_ZERO);
	GX_SetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
	GX_SetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_TEXA, GX_CA_RASA, GX_CA_ZERO);
	GX_SetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
	GX_SetTevDirect(GX_TEVSTAGE0);

	GX_ClearVtxDesc();
	GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XY, GX_S16, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_S16, 0);

	GX_LoadProjectionMtx(projection, GX_ORTHOGRAPHIC);
	GX_SetCurrentMtx(GX_PNMTX1);

	dispsize = GX_EndDispList();
	displist = realloc_in_place(displist, dispsize);
}

void GXFontSetState(void)
{
	if (state.current != STATE_FONT) {
		state.current  = STATE_FONT;
		GX_CallDispList(displist, dispsize);
	}
}

unsigned GUIFontHeight(const struct GUIFont *font)
{
	return fontdata->cell_height;
}

unsigned GUIFontGlyphWidth(const struct GUIFont *font, uint32_t glyph)
{
	uint8_t *table = (void *)fontdata + fontdata->width_table;

	if (glyph < fontdata->first_char || glyph > fontdata->last_char)
		return table[fontdata->inval_char];
	else
		return table[glyph - fontdata->first_char];
}

void GUIFontIconMetrics(const struct GUIFont *font, enum GUIIcon icon, unsigned *w, unsigned *h)
{
	if (w) *w = 0;
	if (h) *h = 0;
}

void GUIFontDrawGlyph(struct GUIFont *font, int x, int y, uint32_t color, uint32_t glyph)
{
	void *image;
	int32_t xpos, ypos, width;

	SYS_GetFontTexture(glyph, &image, &xpos, &ypos, &width);
	GX_InitTexObjData(&texobj, image);
	GX_LoadTexObjPreloaded(&texobj, &texregion, GX_TEXMAP0);

	GXFontDrawCell(x, y, color, xpos, ypos);
}

void GUIFontDrawIcon(struct GUIFont *font, int x, int y, enum GUIAlignment align, enum GUIOrientation orient, uint32_t color, enum GUIIcon icon)
{
}

void GUIFontDrawIconSize(struct GUIFont *font, int x, int y, int w, int h, uint32_t color, enum GUIIcon icon)
{
}
