#include "ContactList.h"
#include <x++/X.h>
#include <x++/XTheme.h>

using namespace Ewok;

#define WX_LIST_BG     0xFFE6E5E5
#define WX_LIST_SEL    0xFFC9C7C8
#define WX_TEXT        0xFF191919
#define WX_LINE        0xFFD6D5D5

ContactList::ContactList(Backend* b, AvatarCache* a) {
	backend = b;
	avatars = a;
	openFunc = NULL;
	openArg = NULL;
	alpha = false;
	setItemSize(X::hidpiSize(56));
}

void ContactList::setOpenFunc(SessionOpenFunc f, void* arg) {
	openFunc = f;
	openArg = arg;
}

void ContactList::refresh() {
	setItemNum(backend->contacts().size());
	update();
}

void ContactList::drawItem(graph_t* g, XTheme* theme, int32_t index, const grect_t& r) {
	const vector<WContact>& contacts = backend->contacts();
	if(index < 0 || (uint32_t)index >= contacts.size())
		return;
	const WContact& c = contacts[index];

	font_t* font = theme->getFont();
	uint32_t fs = theme->basic.fontSize;
	if(font == NULL)
		return;

	bool sel = (getSelected() == index);
	graph_fill_rect(g, r.x, r.y, r.w, r.h, sel ? WX_LIST_SEL : WX_LIST_BG);
	graph_fill_rect(g, r.x + X::hidpiSize(56), r.y + r.h - 1,
			r.w - X::hidpiSize(56), 1, WX_LINE);

	int32_t av = X::hidpiSize(36);
	int32_t pad = X::hidpiSize(10);
	avatars->draw(g, &c, r.x + pad, r.y + (r.h - av)/2, av, font, fs);

	int32_t tx = r.x + pad + av + pad;
	graph_draw_text_font_align(g, tx, r.y, r.w - (tx - r.x) - pad, r.h,
			c.displayName().c_str(), font, fs, WX_TEXT, FONT_ALIGN_NONE);
}

void ContactList::onSelect(int sel) {
	const vector<WContact>& contacts = backend->contacts();
	if(sel < 0 || (uint32_t)sel >= contacts.size())
		return;
	if(openFunc != NULL)
		openFunc(contacts[sel].id, openArg);
}