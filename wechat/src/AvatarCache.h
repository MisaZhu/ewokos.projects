#ifndef WECHAT_AVATAR_CACHE_HH
#define WECHAT_AVATAR_CACHE_HH

#include "Backend.h"
#include <graph/graph.h>
#include <font/font.h>
#include <map>

using namespace std;

/* Loads res avatars once and draws them (or a colored initial fallback)
 * at any size. Shared by session list, contact list and chat bubbles. */
class AvatarCache {
	map<string, graph_t*> cache;
public:
	~AvatarCache();
	graph_t* get(const WContact* c);
	void draw(graph_t* g, const WContact* c, int32_t x, int32_t y,
			int32_t size, font_t* font, uint32_t fontSize);
};

#endif