#include "shelf.h"
#include "ui.h"
#include <x++/X.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <algorithm>
#include <ewoksys/keydef.h>

namespace er {

static bool itemLess(const BookItem& a, const BookItem& b) {
	return a.title < b.title;
}

ShelfWidget::ShelfWidget(Store* store) {
	_store = store;
	_scroll = 0;
	_maxScroll = 0;
	_hover = -1;
	_pressed = -1;
	_dragging = false;
	_dragFromY = 0;
	_dragScroll = 0;
	_moved = false;
	_wcacheSize = 0;
	openFunc = NULL;
	importFunc = NULL;
	funcArg = NULL;
	memset(&_importBtn, 0, sizeof(grect_t));
	memset(&_refreshBtn, 0, sizeof(grect_t));
	memset(&_themeBtn, 0, sizeof(grect_t));
	memset(&_emptyBtn, 0, sizeof(grect_t));
}

void ShelfWidget::refresh() {
	_items.clear();
	const string& dir = _store->settings.shelfDir;
	DIR* d = opendir(dir.c_str());
	if(d != NULL) {
		struct dirent* e;
		while((e = readdir(d)) != NULL) {
			string name = e->d_name;
			if(name.empty() || name[0] == '.')
				continue;
			bool pdf = endsWith(name, ".pdf");
			bool txt = endsWith(name, ".txt");
			if(!pdf && !txt)
				continue;
			string full = dir + "/" + name;
			struct stat st;
			if(stat(full.c_str(), &st) != 0 || !S_ISREG(st.st_mode) || st.st_size == 0)
				continue;
			BookItem it;
			it.fname = full;
			it.title = stripExt(name);
			it.pdf = pdf;
			it.hash = strHash(it.title);
			it.page = _store->progress(full);
			it.pct = _store->progressPct(full);
			_items.push_back(it);
		}
		closedir(d);
	}
	sort(_items.begin(), _items.end(), itemLess);
	_hover = -1;
	clampScroll();
	update();
}

int ShelfWidget::measure(font_t* font, int size, uint32_t cp) {
	if(_wcacheSize != size) {
		_wcache.clear();
		_wcacheSize = size;
	}
	unordered_map<uint32_t,int>::iterator it = _wcache.find(cp);
	if(it != _wcache.end())
		return it->second;
	uint32_t w = 0, h = 0;
	font_char_size(cp, font, size, &w, &h);
	int iw = (w > 0) ? (int)w : size/2;
	_wcache[cp] = iw;
	return iw;
}

void ShelfWidget::wrapTitle(font_t* font, int size, const string& title,
		int maxW, int maxLines, vector<string>& out) {
	out.clear();
	vector<uint32_t> cps = decodeUtf8((const uint8_t*)title.data(), (int)title.size());
	string line;
	int lw = 0;
	for(size_t i = 0; i < cps.size(); i++) {
		int cw = measure(font, size, cps[i]);
		if(lw + cw > maxW && !line.empty()) {
			out.push_back(line);
			if((int)out.size() >= maxLines) {
				/* ellipsize the last line */
				string& last = out.back();
				while(!last.empty()) {
					vector<uint32_t> lc = decodeUtf8((const uint8_t*)last.data(), (int)last.size());
					int w = 0;
					for(size_t k = 0; k < lc.size(); k++)
						w += measure(font, size, lc[k]);
					if(w + measure(font, size, 0x2026) <= maxW)
						break;
					/* drop last codepoint */
					int back = 1;
					while(back < (int)last.size() &&
							((uint8_t)last[last.size()-back] & 0xC0) == 0x80)
						back++;
					last.erase(last.size()-back);
				}
				last += "\xE2\x80\xA6";
				return;
			}
			line.clear();
			lw = 0;
		}
		char buf[4];
		int n = encodeUtf8(cps[i], buf);
		line.append(buf, n);
		lw += cw;
	}
	if(!line.empty() && (int)out.size() < maxLines)
		out.push_back(line);
}

void ShelfWidget::drawCover(graph_t* g, XTheme* theme, const BookItem& it,
		const grect_t& r, bool hover) {
	const CoverHue& hue = coverHue(it.hash);
	int rad = X::hidpiSize(5);
	uint32_t shadow = (palette(_store->settings.theme).bg == 0xff121419) ?
		0x66000000 : 0x303a3226;
	int lift = hover ? X::hidpiSize(3) : 0;
	grect_t c = {r.x, r.y - lift, r.w, r.h};

	graphShadowRound(g, c.x, c.y, c.w, c.h, rad, hue.base, shadow);
	graphVGradient(g, c.x+rad, c.y, c.w-rad*2, c.h, hue.base, hue.dark);
	/* spine */
	graph_fill_round(g, c.x, c.y, X::hidpiSize(12), c.h, rad, hue.dark);
	graph_fill_rect(g, c.x + X::hidpiSize(6), c.y, X::hidpiSize(6), c.h, hue.dark);
	graph_fill_rect(g, c.x + X::hidpiSize(12), c.y + rad, 1, c.h - rad*2, hue.light);
	/* inner frame */
	int fx = c.x + X::hidpiSize(20), fy = c.y + X::hidpiSize(12);
	int fw = c.w - X::hidpiSize(30), fh = c.h - X::hidpiSize(24);
	graph_round(g, fx, fy, fw, fh, 2, 1, hue.light);

	/* title */
	font_t* font = theme->getFont();
	int ts = X::hidpiSize(13);
	vector<string> lines;
	wrapTitle(font, ts, it.title, fw - X::hidpiSize(12), 4, lines);
	int ty = fy + X::hidpiSize(14);
	for(size_t i = 0; i < lines.size(); i++) {
		uint32_t tw = 0, th = 0;
		font_text_size(lines[i].c_str(), font, ts, &tw, &th);
		graph_draw_text_font(g, fx + (fw - (int)tw)/2, ty, lines[i].c_str(),
				font, ts, 0xfff6f1e6);
		ty += ts + X::hidpiSize(4);
	}
	/* ornament rule */
	graph_fill_rect(g, fx + fw/4, c.y + c.h - X::hidpiSize(34), fw/2, 1, hue.light);
	/* format badge */
	const char* badge = it.pdf ? "PDF" : "TXT";
	int bs = X::hidpiSize(9);
	uint32_t bw = 0, bh = 0;
	font_text_size(badge, font, bs, &bw, &bh);
	int bx = fx + (fw - (int)bw - X::hidpiSize(10))/2;
	int by = c.y + c.h - X::hidpiSize(28);
	grect_t br = {bx, by, (int)bw + X::hidpiSize(10), bs + X::hidpiSize(6)};
	graph_fill_round(g, br.x, br.y, br.w, br.h, X::hidpiSize(3), hue.dark);
	drawLabel(g, font, badge, bs, br, hue.light);

	if(hover)
		graph_fill_round(g, c.x, c.y, c.w, c.h, rad, 0x1affffff);
}

void ShelfWidget::layout(const grect_t& r) {
	int headerH = X::hidpiSize(86);
	int cw = X::hidpiSize(112), ch = X::hidpiSize(152);
	int gapX = X::hidpiSize(30), rowH = ch + X::hidpiSize(44);
	int margin = X::hidpiSize(28);
	int cols = (r.w - margin*2 + gapX) / (cw + gapX);
	if(cols < 1) cols = 1;
	int rows = ((int)_items.size() + cols - 1) / cols;
	int contentH = rows * rowH + X::hidpiSize(20);
	_maxScroll = contentH - (r.h - headerH);
	if(_maxScroll < 0) _maxScroll = 0;
}

void ShelfWidget::clampScroll() {
	if(_scroll < 0) _scroll = 0;
	if(_scroll > _maxScroll) _scroll = _maxScroll;
}

int ShelfWidget::itemAt(int x, int y) {
	for(size_t i = 0; i < _covers.size(); i++) {
		if(check_in_rect(x, y, &_covers[i]))
			return (int)i;
	}
	return -1;
}

void ShelfWidget::onRepaint(graph_t* g, XTheme* theme, const grect_t& r) {
	const Palette& p = palette(_store->settings.theme);
	font_t* font = theme->getFont();
	int margin = X::hidpiSize(28);
	int headerH = X::hidpiSize(86);

	/* desk */
	graphVGradient(g, r.x, r.y, r.w, r.h, p.cover0, p.cover1);
	/* faint plank seams for the library feel */
	uint32_t seam = (_store->settings.theme == THEME_NIGHT) ? 0x14000000 : 0x0d3a3226;
	for(int y = r.y + headerH + X::hidpiSize(196) - (_scroll % X::hidpiSize(196));
			y < r.y + r.h; y += X::hidpiSize(196))
		graph_fill_rect(g, r.x, y, r.w, 1, seam);

	/* header */
	int ts = X::hidpiSize(26);
	graph_draw_text_font(g, r.x + margin, r.y + X::hidpiSize(18), "书架",
			font, ts, p.ink);
	string sub = fmt("%s  ·  %d 本  ·  点击封面开始阅读",
			_store->settings.shelfDir.c_str(), (int)_items.size());
	graph_draw_text_font(g, r.x + margin, r.y + X::hidpiSize(54), sub.c_str(),
			font, X::hidpiSize(12), p.faint);

	/* header buttons (right aligned) */
	int bh = X::hidpiSize(30), by = r.y + X::hidpiSize(20);
	int bs = X::hidpiSize(12);
	int bx = r.x + r.w - margin;
	int pad = X::hidpiSize(11), gap = X::hidpiSize(10);
	const char* tname = themeName(_store->settings.theme);
	string tl = fmt("主题·%s", tname);
	_themeBtn = drawPill(g, font, tl.c_str(), bs,
			bx - pillWidth(font, tl.c_str(), bs, pad), by, bh, pad, p.bar, p.barInk, p.line);
	bx = _themeBtn.x - gap;

	_refreshBtn = drawPill(g, font, "刷新", bs,
			bx - pillWidth(font, "刷新", bs, pad), by, bh, pad, p.bar, p.barInk, p.line);
	bx = _refreshBtn.x - gap;

	_importBtn = drawPill(g, font, "导入书籍", bs,
			bx - pillWidth(font, "导入书籍", bs, pad), by, bh, pad, p.accent, 0xff2a2118, 0);

	graph_fill_rect(g, r.x, r.y + headerH - 1, r.w, 1, p.line);

	layout(r);
	clampScroll();
	_covers.clear();

	if(_items.empty()) {
		/* empty state: drawn open book + guidance */
		int cx = r.x + r.w/2, cy = r.y + headerH + (r.h - headerH)/2 - X::hidpiSize(30);
		int bw = X::hidpiSize(96), bh2 = X::hidpiSize(64);
		graph_fill_round(g, cx - bw, cy - bh2/2, bw, bh2, X::hidpiSize(6), p.bar);
		graph_fill_round(g, cx, cy - bh2/2, bw, bh2, X::hidpiSize(6), p.bar);
		graph_round(g, cx - bw, cy - bh2/2, bw, bh2, X::hidpiSize(6), 2, p.faint);
		graph_round(g, cx, cy - bh2/2, bw, bh2, X::hidpiSize(6), 2, p.faint);
		graph_fill_rect(g, cx - 1, cy - bh2/2, 2, bh2, p.faint);
		for(int i = 1; i <= 3; i++) {
			graph_fill_rect(g, cx - bw + X::hidpiSize(14), cy - bh2/2 + i*X::hidpiSize(14),
					bw - X::hidpiSize(28), 1, p.line);
			graph_fill_rect(g, cx + X::hidpiSize(14), cy - bh2/2 + i*X::hidpiSize(14),
					bw - X::hidpiSize(28), 1, p.line);
		}
		const char* t1 = "书架还是空的";
		uint32_t w1 = 0, h1 = 0;
		font_text_size(t1, font, X::hidpiSize(18), &w1, &h1);
		graph_draw_text_font(g, cx - (int)w1/2, cy + bh2/2 + X::hidpiSize(22), t1,
				font, X::hidpiSize(18), p.ink);
		const char* t2 = "把 txt / pdf 文件放进 /data/books，或点击下方按钮导入";
		uint32_t w2 = 0, h2 = 0;
		font_text_size(t2, font, X::hidpiSize(12), &w2, &h2);
		graph_draw_text_font(g, cx - (int)w2/2, cy + bh2/2 + X::hidpiSize(52), t2,
				font, X::hidpiSize(12), p.faint);
		const char* t3 = "导入书籍";
		int epad = X::hidpiSize(18);
		_emptyBtn = drawPill(g, font, t3, bs,
				cx - pillWidth(font, t3, bs, epad)/2, cy + bh2/2 + X::hidpiSize(80),
				X::hidpiSize(36), epad, p.accent, 0xff2a2118, 0);
		graph_unset_clip(g);
		return;
	}

	/* covers grid, clipped below the header */
	graph_set_clip(g, r.x, r.y + headerH, r.w, r.h - headerH);
	int cw = X::hidpiSize(112), ch = X::hidpiSize(152);
	int gapX = X::hidpiSize(30), rowH = ch + X::hidpiSize(44);
	int cols = (r.w - margin*2 + gapX) / (cw + gapX);
	if(cols < 1) cols = 1;
	int gridW = cols*cw + (cols-1)*gapX;
	int x0 = r.x + (r.w - gridW)/2;
	int y0 = r.y + headerH + X::hidpiSize(18) - _scroll;

	for(size_t i = 0; i < _items.size(); i++) {
		int col = (int)i % cols, row = (int)i / cols;
		grect_t c = {x0 + col*(cw+gapX), y0 + row*rowH, cw, ch};
		grect_t local = {c.x - r.x, c.y - r.y, c.w, c.h};
		_covers.push_back(local);
		if(c.y + ch < r.y + headerH || c.y > r.y + r.h)
			continue;
		drawCover(g, theme, _items[i], c, (int)i == _hover);

		/* progress line under the cover */
		int py = c.y + ch + X::hidpiSize(12);
		int pct = _items[i].pct;
		if(pct >= 0) {
			graph_fill_round(g, c.x, py, cw, X::hidpiSize(4), X::hidpiSize(2), p.line);
			int fw = cw * pct / 100;
			if(fw > X::hidpiSize(4))
				graph_fill_round(g, c.x, py, fw, X::hidpiSize(4), X::hidpiSize(2), p.accent);
			string ps = fmt("%d%%", pct);
			graph_draw_text_font(g, c.x, py + X::hidpiSize(8), ps.c_str(),
					font, X::hidpiSize(10), p.faint);
		}
		else if(_items[i].page > 0) {
			string ps = fmt("读到第 %d 页", _items[i].page + 1);
			graph_draw_text_font(g, c.x, py, ps.c_str(),
					font, X::hidpiSize(10), p.faint);
		}
		else {
			graph_draw_text_font(g, c.x, py, "未开始",
					font, X::hidpiSize(10), p.faint);
		}
	}
	graph_unset_clip(g);

	/* scrollbar in the shelf's own language */
	if(_maxScroll > 0) {
		int trackH = r.h - headerH - X::hidpiSize(16);
		int tx = r.x + r.w - X::hidpiSize(8);
		int ty = r.y + headerH + X::hidpiSize(8);
		graph_fill_round(g, tx, ty, X::hidpiSize(4), trackH, X::hidpiSize(2), p.line);
		int thumbH = trackH * (r.h - headerH) / (r.h - headerH + _maxScroll);
		if(thumbH < X::hidpiSize(24)) thumbH = X::hidpiSize(24);
		int thumbY = ty + (trackH - thumbH) * _scroll / _maxScroll;
		graph_fill_round(g, tx, thumbY, X::hidpiSize(4), thumbH, X::hidpiSize(2), p.accent);
	}
}

bool ShelfWidget::onMouse(xevent_t* ev) {
	gpos_t pos = getInsidePos(ev->value.mouse.x, ev->value.mouse.y);
	switch(ev->state) {
	case MOUSE_STATE_MOVE: {
		int h = itemAt(pos.x, pos.y);
		if(h != _hover) {
			_hover = h;
			update();
		}
		return true;
	}
	case MOUSE_STATE_DOWN:
		_dragging = true;
		_moved = false;
		_dragFromY = ev->value.mouse.y;
		_dragScroll = _scroll;
		_pressed = itemAt(pos.x, pos.y);
		return true;
	case MOUSE_STATE_DRAG:
		if(_dragging) {
			int dy = ev->value.mouse.y - _dragFromY;
			if(abs(dy) > X::hidpiSize(6))
				_moved = true;
			if(_moved) {
				_scroll = _dragScroll - dy;
				clampScroll();
				update();
			}
		}
		return true;
	case MOUSE_STATE_UP:
	case MOUSE_STATE_CLICK: {
		if(!_dragging)
			return false;
		_dragging = false;
		if(_moved)
			return true;
		if(check_in_rect(pos.x, pos.y, &_importBtn) ||
				(_items.empty() && check_in_rect(pos.x, pos.y, &_emptyBtn))) {
			if(importFunc != NULL)
				importFunc("", funcArg);
			return true;
		}
		if(check_in_rect(pos.x, pos.y, &_refreshBtn)) {
			refresh();
			return true;
		}
		if(check_in_rect(pos.x, pos.y, &_themeBtn)) {
			_store->settings.theme = (_store->settings.theme + 1) % THEME_COUNT;
			_store->save();
			update();
			return true;
		}
		int it = itemAt(pos.x, pos.y);
		if(it >= 0 && it == _pressed && openFunc != NULL)
			openFunc(_items[it].fname, funcArg);
		return true;
	}
	default:
		return false;
	}
}

bool ShelfWidget::onIM(xevent_t* ev) {
	if(ev->state != XIM_STATE_PRESS)
		return false;
	int step = X::hidpiSize(196);
	int v = ev->value.im.value;
	if(v == KEY_DOWN) { _scroll += step/2; clampScroll(); update(); return true; }
	if(v == KEY_UP) { _scroll -= step/2; clampScroll(); update(); return true; }
	if(v == KEY_RIGHT) { _scroll += step; clampScroll(); update(); return true; }
	if(v == KEY_LEFT) { _scroll -= step; clampScroll(); update(); return true; }
	if(v == KEY_HOME) { _scroll = 0; update(); return true; }
	if(v == KEY_END) { _scroll = _maxScroll; update(); return true; }
	if(v == KEY_ENTER || v == JOYSTICK_PRESS) {
		if(_hover >= 0 && _hover < (int)_items.size() && openFunc != NULL)
			openFunc(_items[_hover].fname, funcArg);
		return true;
	}
	return false;
}

}