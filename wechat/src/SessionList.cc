#include "SessionList.h"
#include "Util.h"
#include <x++/X.h>
#include <x++/XTheme.h>

using namespace Ewok;

#define WX_LIST_BG     0xFFE6E5E5
#define WX_LIST_SEL    0xFFC9C7C8
#define WX_TEXT        0xFF191919
#define WX_TEXT_GRAY   0xFF999999
#define WX_BADGE       0xFFF44336

SessionList::SessionList(Backend* b, AvatarCache* a) {
	backend = b;
	avatars = a;
	openFunc = NULL;
	openArg = NULL;
	alpha = false;
	setItemSize(X::hidpiSize(64));
}

void SessionList::setOpenFunc(SessionOpenFunc f, void* arg) {
	openFunc = f;
	openArg = arg;
}

void SessionList::setCurrent(const string& talker) {
	current = talker;
	update();
}

void SessionList::refresh() {
	setItemNum(backend->sessions().size());
	update();
}

void SessionList::drawItem(graph_t* g, XTheme* theme, int32_t index, const grect_t& r) {
	vector<WSession>& sessions = backend->sessions();
	if(index < 0 || (uint32_t)index >= sessions.size())
		return;
	WSession& s = sessions[index];
	const WContact* c = backend->contact(s.talker);

	font_t* font = theme->getFont();
	uint32_t fs = theme->basic.fontSize;
	if(font == NULL)
		return;

	bool sel = (s.talker == current);
	graph_fill_rect(g, r.x, r.y, r.w, r.h, sel ? WX_LIST_SEL : WX_LIST_BG);

	int32_t av = X::hidpiSize(40);
	int32_t pad = X::hidpiSize(10);
	int32_t ay = r.y + (r.h - av)/2;
	avatars->draw(g, c, r.x + pad, ay, av, font, fs);

	int32_t tx = r.x + pad + av + pad;
	int32_t tw = r.w - (tx - r.x) - pad;

	/* time at top-right */
	string tm;
	if(!s.messages.empty())
		tm = timeHHMM(s.messages.back().time);
	uint32_t tmw = 0;
	font_text_size(tm.c_str(), font, fs - 3, &tmw, NULL);
	graph_draw_text_font(g, r.x + r.w - pad - (int32_t)tmw, r.y + pad,
			tm.c_str(), font, fs - 3, WX_TEXT_GRAY);

	/* name */
	int32_t nw = tw - (int32_t)tmw - X::hidpiSize(6);
	graph_draw_text_font_align(g, tx, r.y + pad - 2, nw, X::hidpiSize(20),
			c != NULL ? c->displayName().c_str() : s.talker.c_str(),
			font, fs, WX_TEXT, FONT_ALIGN_NONE);

	/* last message preview */
	string preview;
	if(!s.messages.empty()) {
		const WMessage& m = s.messages.back();
		if(m.type == "image")
			preview = "[图片]";
		else
			preview = m.text;
		if(m.sender == "me")
			preview = "我: " + preview;
	}
	int32_t py = r.y + r.h - pad - X::hidpiSize(16);
	int32_t pw = tw;

	/* unread badge at bottom-right */
	if(s.unread > 0) {
		char num[16];
		snprintf(num, sizeof(num), "%u", s.unread > 99 ? 99 : s.unread);
		uint32_t nw2 = 0;
		font_text_size(num, font, fs - 4, &nw2, NULL);
		int32_t bw = (int32_t)nw2 + X::hidpiSize(10);
		if(bw < X::hidpiSize(18))
			bw = X::hidpiSize(18);
		int32_t bh = X::hidpiSize(18);
		int32_t bx = r.x + r.w - pad - bw;
		int32_t by = py + (X::hidpiSize(16) - bh)/2;
		graph_fill_round(g, bx, by, bw, bh, bh/2, WX_BADGE);
		graph_draw_text_font_align(g, bx, by, bw, bh, num, font, fs - 4,
				0xFFFFFFFF, FONT_ALIGN_CENTER);
		pw -= bw + X::hidpiSize(4);
	}
	graph_draw_text_font_align(g, tx, py, pw, X::hidpiSize(16),
			preview.c_str(), font, fs - 2, WX_TEXT_GRAY, FONT_ALIGN_NONE);
}

void SessionList::onSelect(int sel) {
	vector<WSession>& sessions = backend->sessions();
	if(sel < 0 || (uint32_t)sel >= sessions.size())
		return;
	if(openFunc != NULL)
		openFunc(sessions[sel].talker, openArg);
}