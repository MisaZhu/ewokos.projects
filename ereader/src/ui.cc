#include "ui.h"

namespace er {

/* font_get_face() is an IPC round trip to fontd: remember the few
 * (font, size) pairs the UI actually uses */
enum { ASC_SLOTS = 8 };
static struct { font_t* font; int size; int asc; } _asc[ASC_SLOTS];
static int _ascNext = 0;

/* ascender in pixels, 0 if the face cannot be queried */
static int ascender(font_t* font, int size) {
	for(int i = 0; i < ASC_SLOTS; i++) {
		if(_asc[i].font == font && _asc[i].size == size)
			return _asc[i].asc;
	}
	face_info_t face;
	int asc = 0;
	if(font_get_face(font, (uint32_t)size, &face) == 0 && face.ascender > 0)
		asc = face.ascender / 64; /* 26.6 fixed point */
	_asc[_ascNext].font = font;
	_asc[_ascNext].size = size;
	_asc[_ascNext].asc = asc;
	_ascNext = (_ascNext + 1) % ASC_SLOTS;
	return asc;
}

int textTop(font_t* font, int size, int boxY, int boxH) {
	int asc = ascender(font, size);
	if(asc <= 0) {
		int lh = font_get_height(font, (uint32_t)size);
		if(lh <= 0) lh = size;
		return boxY + (boxH - lh)/2;
	}
	/* graph_draw_text_font() puts the baseline at y + ascender. The line box
	 * (ascender + descender) is taller than the ink and not symmetric around
	 * it, so centring the line box leaves labels visibly low. Ideographs,
	 * capitals and digits all have their ink centre ~0.37em above the
	 * baseline: put that on the middle of the box instead. */
	int baseline = boxY + boxH/2 + (size * 37 + 50) / 100;
	return baseline - asc;
}

int textWidth(font_t* font, const char* text, int size) {
	uint32_t w = 0, h = 0;
	font_text_size(text, font, (uint32_t)size, &w, &h);
	return (int)w;
}

void drawLabel(graph_t* g, font_t* font, const char* text, int size,
		const grect_t& box, uint32_t ink) {
	graph_draw_text_font(g, box.x + (box.w - textWidth(font, text, size))/2,
			textTop(font, size, box.y, box.h), text, font, (uint32_t)size, ink);
}

int pillWidth(font_t* font, const char* text, int size, int padX) {
	return textWidth(font, text, size) + padX*2;
}

grect_t drawPill(graph_t* g, font_t* font, const char* text, int size,
		int x, int y, int h, int padX, uint32_t bg, uint32_t ink, uint32_t border) {
	grect_t r = {x, y, pillWidth(font, text, size, padX), h};
	graph_fill_round(g, r.x, r.y, r.w, r.h, h/2, bg);
	if(border != 0)
		graph_round(g, r.x, r.y, r.w, r.h, h/2, 1, border);
	drawLabel(g, font, text, size, r, ink);
	return r;
}

}
