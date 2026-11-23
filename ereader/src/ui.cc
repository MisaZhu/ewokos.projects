#include "ui.h"

namespace er {

int textWidth(font_t* font, const char* text, int size) {
	uint32_t w = 0, h = 0;
	font_text_size(text, font, (uint32_t)size, &w, &h);
	return (int)w;
}

/* centring rides libfont's FONT_ALIGN_CENTER: it measures the union of
 * the label's real glyph ink (renderer-identical metrics, cached inside
 * libfont) instead of the asymmetric CJK line box — the same measurement
 * this file used to do by hand with its own ink/ascender caches. */
void drawLabel(graph_t* g, font_t* font, const char* text, int size,
		const grect_t& box, uint32_t ink) {
	graph_draw_text_font_align(g, box.x, box.y, box.w, box.h,
			text, font, (uint32_t)size, ink, FONT_ALIGN_CENTER);
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