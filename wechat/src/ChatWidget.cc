#include "ChatWidget.h"
#include "Util.h"
#include <x++/X.h>
#include <x++/XTheme.h>
#include <x++/XWin.h>
#include <Widget/WidgetWin.h>
#include <graph/graph_image.h>
#include <mouse/mouse.h>
#include <string.h>

using namespace Ewok;

#define WX_CHAT_BG    0xFFF5F5F5
#define WX_BUBBLE_ME  0xFF95EC69
#define WX_BUBBLE_TA  0xFFFFFFFF
#define WX_TEXT       0xFF191919
#define WX_TEXT_GRAY  0xFFB2B2B2

ChatWidget::ChatWidget(Backend* b, AvatarCache* a) {
	backend = b;
	avatars = a;
	imageFunc = NULL;
	imageArg = NULL;
	contentH = 0;
	scrollY = 0;
	stickBottom = true;
	dragging = false;
	layoutDirty = true;
	alpha = false;
}

ChatWidget::~ChatWidget() {
	for(auto it = imgCache.begin(); it != imgCache.end(); ++it) {
		if(it->second != NULL)
			graph_free(it->second);
	}
}

void ChatWidget::setImageOpenFunc(ImageOpenFunc f, void* arg) {
	imageFunc = f;
	imageArg = arg;
}

graph_t* ChatWidget::getImage(const string& resFile) {
	auto it = imgCache.find(resFile);
	if(it != imgCache.end())
		return it->second;
	string fname = X::getResFullName(resFile.c_str());
	graph_t* img = graph_image_new(fname.c_str());
	imgCache[resFile] = img;
	return img;
}

void ChatWidget::setTalker(const string& id) {
	talker = id;
	scrollY = 0;
	stickBottom = true;
	layoutDirty = true;
	update();
}

int32_t ChatWidget::viewH() const {
	return area.h;
}

int32_t ChatWidget::maxScroll() const {
	int32_t m = contentH - viewH();
	return m > 0 ? m : 0;
}

/* when there are few messages they sit at the bottom of the pane,
 * like WeChat does */
int32_t ChatWidget::baseOffset() const {
	int32_t d = viewH() - contentH;
	return d > 0 ? d : 0;
}

void ChatWidget::clampScroll() {
	if(scrollY < 0)
		scrollY = 0;
	if(scrollY > maxScroll())
		scrollY = maxScroll();
	stickBottom = (scrollY >= maxScroll());
}

void ChatWidget::wrapText(const string& text, font_t* font, uint32_t fs,
		int32_t maxW, vector<string>& lines) {
	string cur;
	int32_t curW = 0;
	size_t i = 0;
	while(i < text.length()) {
		unsigned char lead = (unsigned char)text[i];
		int len = 1;
		uint32_t cp = lead;
		if(lead >= 0xF0) { len = 4; cp = lead & 0x07; }
		else if(lead >= 0xE0) { len = 3; cp = lead & 0x0F; }
		else if(lead >= 0xC0) { len = 2; cp = lead & 0x1F; }
		if(i + len > text.length())
			len = text.length() - i;
		for(int k = 1; k < len; k++)
			cp = (cp << 6) | ((unsigned char)text[i+k] & 0x3F);

		if(cp == '\n') {
			lines.push_back(cur);
			cur.clear();
			curW = 0;
			i += len;
			continue;
		}

		uint32_t cw = 0, chh = 0;
		font_char_size(cp, font, fs, &cw, &chh);
		if(curW + (int32_t)cw > maxW && !cur.empty()) {
			lines.push_back(cur);
			cur.clear();
			curW = 0;
		}
		cur.append(text, i, len);
		curW += (int32_t)cw;
		i += len;
	}
	lines.push_back(cur);
}

void ChatWidget::relayout(XTheme* theme) {
	layout.clear();
	contentH = 0;
	layoutDirty = false;
	if(theme == NULL || talker.empty() || area.w <= 0)
		return;
	WSession* s = backend->session(talker, false);
	if(s == NULL)
		return;
	font_t* font = theme->getFont();
	if(font == NULL)
		return;
	uint32_t fs = theme->basic.fontSize;

	int32_t margin = X::hidpiSize(12);
	int32_t av = X::hidpiSize(36);
	int32_t gap = X::hidpiSize(8);
	int32_t pad = X::hidpiSize(10);
	int32_t padV = X::hidpiSize(7);
	int32_t msgGap = X::hidpiSize(12);
	int32_t sepH = X::hidpiSize(28);
	int32_t lineH = font_get_height(font, fs) + X::hidpiSize(4);
	int32_t maxBubbleW = area.w - (margin + av + gap)*2 - X::hidpiSize(16);
	if(maxBubbleW > X::hidpiSize(420))
		maxBubbleW = X::hidpiSize(420);
	if(maxBubbleW < X::hidpiSize(80))
		maxBubbleW = X::hidpiSize(80);
	int32_t thumbBox = X::hidpiSize(180);

	uint64_t prevTime = 0;
	for(size_t i = 0; i < s->messages.size(); i++) {
		const WMessage& m = s->messages[i];
		ItemLayout it;
		it.y = contentH;
		it.fromMe = (m.sender == "me");
		it.isImage = (m.type == "image");
		it.sepH = 0;
		if(prevTime == 0 || m.time > prevTime + 300) {
			it.sepH = sepH;
			it.timeStr = timeHHMM(m.time);
		}
		prevTime = m.time;
		memset(&it.bubble, 0, sizeof(it.bubble));
		memset(&it.imgRect, 0, sizeof(it.imgRect));

		if(it.isImage) {
			graph_t* img = getImage(m.text);
			int32_t iw = thumbBox, ih = thumbBox;
			if(img != NULL && img->w > 0 && img->h > 0) {
				float scale = (float)thumbBox / (float)(img->w > img->h ? img->w : img->h);
				if(scale > 1.0f)
					scale = 1.0f;
				iw = (int32_t)(img->w * scale);
				ih = (int32_t)(img->h * scale);
				if(iw < 1) iw = 1;
				if(ih < 1) ih = 1;
			}
			int32_t bx = it.fromMe ?
				area.w - margin - av - gap - iw : margin + av + gap;
			it.imgRect.x = bx;
			it.imgRect.y = contentH + it.sepH;
			it.imgRect.w = iw;
			it.imgRect.h = ih;
			it.bubble = it.imgRect;
			it.h = it.sepH + ih + msgGap;
		}
		else {
			wrapText(m.text, font, fs, maxBubbleW - pad*2, it.lines);
			int32_t maxLineW = 0;
			for(size_t k = 0; k < it.lines.size(); k++) {
				uint32_t lw = 0;
				font_text_size(it.lines[k].c_str(), font, fs, &lw, NULL);
				if((int32_t)lw > maxLineW)
					maxLineW = (int32_t)lw;
			}
			int32_t bw = maxLineW + pad*2;
			int32_t bh = (int32_t)it.lines.size()*lineH + padV*2;
			int32_t bx = it.fromMe ?
				area.w - margin - av - gap - bw : margin + av + gap;
			it.bubble.x = bx;
			it.bubble.y = contentH + it.sepH;
			it.bubble.w = bw;
			it.bubble.h = bh;
			it.h = it.sepH + bh + msgGap;
		}
		contentH += it.h;
		layout.push_back(it);
	}
}

void ChatWidget::refresh(bool keepPosition) {
	if(!keepPosition)
		stickBottom = true;
	layoutDirty = true;
	if(getWin() != NULL)
		relayout(getWin()->getTheme());
	if(stickBottom)
		scrollY = maxScroll();
	else
		clampScroll();
	update();
}

void ChatWidget::onResize() {
	layoutDirty = true;
	clampScroll();
}

void ChatWidget::onRepaint(graph_t* g, XTheme* theme, const grect_t& r) {
	graph_fill_rect(g, r.x, r.y, r.w, r.h, WX_CHAT_BG);
	font_t* font = theme->getFont();
	if(font == NULL)
		return;
	uint32_t fs = theme->basic.fontSize;

	if(talker.empty()) {
		graph_draw_text_font_align(g, r.x, r.y, r.w, r.h,
				"选择一个会话开始聊天", font, fs, WX_TEXT_GRAY, FONT_ALIGN_CENTER);
		return;
	}

	if(layoutDirty)
		relayout(theme);
	if(stickBottom)
		scrollY = maxScroll();

	graph_set_clip(g, r.x, r.y, r.w, r.h);

	int32_t margin = X::hidpiSize(12);
	int32_t av = X::hidpiSize(36);
	int32_t pad = X::hidpiSize(10);
	int32_t padV = X::hidpiSize(7);
	int32_t lineH = font_get_height(font, fs) + X::hidpiSize(4);
	int32_t off = baseOffset();
	int32_t top = scrollY - off;

	WSession* s = backend->session(talker, false);
	for(size_t i = 0; i < layout.size(); i++) {
		const ItemLayout& it = layout[i];
		if(it.y + it.h < top || it.y > top + r.h)
			continue;
		int32_t sy = r.y + off - scrollY + it.y;

		if(it.sepH > 0 && !it.timeStr.empty()) {
			graph_draw_text_font_align(g, r.x, sy, r.w, it.sepH,
					it.timeStr.c_str(), font, fs - 3, WX_TEXT_GRAY, FONT_ALIGN_CENTER);
		}

		const WContact* c = NULL;
		if(s != NULL && i < s->messages.size()) {
			const WMessage& m = s->messages[i];
			c = backend->contact(it.fromMe ? "me" : m.sender);
		}
		int32_t ax = it.fromMe ? r.x + r.w - margin - av : r.x + margin;
		avatars->draw(g, c, ax, sy + it.sepH, av, font, fs - 2);

		if(it.isImage) {
			graph_t* img = NULL;
			if(s != NULL && i < s->messages.size())
				img = getImage(s->messages[i].text);
			if(img != NULL) {
				graph_blt_fit(img, 0, 0, img->w, img->h,
						g, r.x + it.imgRect.x, sy + it.sepH,
						it.imgRect.w, it.imgRect.h);
			}
			else {
				graph_fill_round(g, r.x + it.imgRect.x, sy + it.sepH,
						it.imgRect.w, it.imgRect.h, X::hidpiSize(4), 0xFFDDDDDD);
				graph_draw_text_font_align(g, r.x + it.imgRect.x, sy + it.sepH,
						it.imgRect.w, it.imgRect.h, "[图片]", font, fs - 2,
						WX_TEXT_GRAY, FONT_ALIGN_CENTER);
			}
		}
		else {
			graph_fill_round(g, r.x + it.bubble.x, sy + it.sepH,
					it.bubble.w, it.bubble.h, X::hidpiSize(6),
					it.fromMe ? WX_BUBBLE_ME : WX_BUBBLE_TA);
			for(size_t k = 0; k < it.lines.size(); k++) {
				graph_draw_text_font(g, r.x + it.bubble.x + pad,
						sy + it.sepH + padV + (int32_t)k*lineH,
						it.lines[k].c_str(), font, fs, WX_TEXT);
			}
		}
	}
}

bool ChatWidget::onMouse(xevent_t* ev) {
	gpos_t p = getInsidePos(ev->value.mouse.x, ev->value.mouse.y);

	if(ev->state == MOUSE_STATE_MOVE &&
			(ev->value.mouse.button == MOUSE_BUTTON_SCROLL_UP ||
			 ev->value.mouse.button == MOUSE_BUTTON_SCROLL_DOWN)) {
		int32_t step = X::hidpiSize(48);
		scrollY += (ev->value.mouse.button == MOUSE_BUTTON_SCROLL_DOWN) ? step : -step;
		clampScroll();
		update();
		return true;
	}
	if(ev->state == MOUSE_STATE_DOWN) {
		dragging = true;
		return true;
	}
	if(ev->state == MOUSE_STATE_DRAG) {
		if(dragging) {
			scrollY -= ev->value.mouse.ry;
			clampScroll();
			update();
		}
		return true;
	}
	if(ev->state == MOUSE_STATE_UP) {
		dragging = false;
		return true;
	}
	if(ev->state == MOUSE_STATE_CLICK && !talker.empty()) {
		int32_t cy = p.y - baseOffset() + scrollY;
		WSession* s = backend->session(talker, false);
		if(s == NULL)
			return true;
		for(size_t i = 0; i < layout.size() && i < s->messages.size(); i++) {
			const ItemLayout& it = layout[i];
			if(!it.isImage)
				continue;
			if(p.x >= it.imgRect.x && p.x < it.imgRect.x + it.imgRect.w &&
					cy >= it.imgRect.y && cy < it.imgRect.y + it.imgRect.h) {
				if(imageFunc != NULL)
					imageFunc(s->messages[i].text, imageArg);
				break;
			}
		}
		return true;
	}
	return true;
}