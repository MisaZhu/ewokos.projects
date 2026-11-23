#ifndef EREADER_UI_H
#define EREADER_UI_H

#include <graph/graph.h>
#include <font/font.h>

namespace er {

/* label width via font_text_size */
int textWidth(font_t* font, const char* text, int size);

/* label centred both ways inside a rect (libfont ink-measured alignment) */
void drawLabel(graph_t* g, font_t* font, const char* text, int size,
		const grect_t& box, uint32_t ink);

/* capsule button: width = label + 2*padX, label centred. border 0 = none */
int     pillWidth(font_t* font, const char* text, int size, int padX);
grect_t drawPill(graph_t* g, font_t* font, const char* text, int size,
		int x, int y, int h, int padX, uint32_t bg, uint32_t ink, uint32_t border);

}

#endif
