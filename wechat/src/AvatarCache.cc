#include "AvatarCache.h"
#include <x++/X.h>
#include <graph/graph_image.h>
#include <string.h>

using namespace Ewok;

AvatarCache::~AvatarCache() {
	for(auto it = cache.begin(); it != cache.end(); ++it) {
		if(it->second != NULL)
			graph_free(it->second);
	}
}

graph_t* AvatarCache::get(const WContact* c) {
	if(c == NULL || c->avatar.empty())
		return NULL;
	auto it = cache.find(c->avatar);
	if(it != cache.end())
		return it->second;

	string fname = X::getResFullName(c->avatar.c_str());
	graph_t* img = graph_image_new(fname.c_str());
	cache[c->avatar] = img;
	return img;
}

void AvatarCache::draw(graph_t* g, const WContact* c, int32_t x, int32_t y,
		int32_t size, font_t* font, uint32_t fontSize) {
	if(size <= 0)
		return;
	graph_t* img = get(c);
	if(img != NULL) {
		graph_blt_fit(img, 0, 0, img->w, img->h, g, x, y, size, size);
		return;
	}

	/* fallback: rounded square in the contact color with the first
	 * character of the display name */
	uint32_t color = c != NULL ? c->color : 0xFF999999;
	graph_fill_round(g, x, y, size, size, size/8, color);
	if(c == NULL || font == NULL)
		return;

	string name = c->displayName();
	if(name.empty())
		return;
	/* first UTF-8 codepoint */
	char ch[8] = {0};
	int n = 1;
	unsigned char lead = (unsigned char)name[0];
	if(lead >= 0xF0) n = 4;
	else if(lead >= 0xE0) n = 3;
	else if(lead >= 0xC0) n = 2;
	if(n > (int)name.length())
		n = name.length();
	memcpy(ch, name.c_str(), n);

	uint32_t tw = 0, th = 0;
	font_text_size(ch, font, fontSize, &tw, &th);
	graph_draw_text_font(g, x + (size - (int)tw)/2,
			y + (size - (int)th)/2, ch, font, fontSize, 0xFFFFFFFF);
}