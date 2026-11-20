#ifndef EREADER_UI_H
#define EREADER_UI_H

#include <graph/graph.h>
#include <font/font.h>

namespace er {

/* y to hand to graph_draw_text_font() so the glyph ink (not the line box)
 * sits on the vertical middle of [boxY, boxY+boxH) */
int textTop(font_t* font, int size, int boxY, int boxH);
int textWidth(font_t* font, const char* text, int size);

/* label centred both ways inside a rect */
void drawLabel(graph_t* g, font_t* font, const char* text, int size,
		const grect_t& box, uint32_t ink);

/* capsule button: width = label + 2*padX, label centred. border 0 = none */
int     pillWidth(font_t* font, const char* text, int size, int padX);
grect_t drawPill(graph_t* g, font_t* font, const char* text, int size,
		int x, int y, int h, int padX, uint32_t bg, uint32_t ink, uint32_t border);

}

#endif
