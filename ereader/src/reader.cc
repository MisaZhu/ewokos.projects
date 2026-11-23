#include "reader.h"
#include "txtbook.h"
#include "pdf.h"
#include "ui.h"
#include <x++/X.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ewoksys/keydef.h>

namespace er {

enum { ANIM_STEPS = 6 };   /* ~180ms slide at 30fps */

static inline int topBarH() { return X::hidpiSize(46); }
static inline int botBarH() { return X::hidpiSize(54); }

/* shrink a title to fit maxW, ellipsizing on a codepoint boundary */
static string elide(font_t* font, int size, const string& s, int maxW) {
	uint32_t w = 0, h = 0;
	font_text_size(s.c_str(), font, size, &w, &h);
	if((int)w <= maxW)
		return s;
	vector<uint32_t> cps = decodeUtf8((const uint8_t*)s.data(), (int)s.size());
	while(!cps.empty()) {
		cps.pop_back();
		string t;
		char buf[4];
		for(size_t i = 0; i < cps.size(); i++) {
			int n = encodeUtf8(cps[i], buf);
			t.append(buf, n);
		}
		t += "\xE2\x80\xA6";
		font_text_size(t.c_str(), font, size, &w, &h);
		if((int)w <= maxW)
			return t;
	}
	return "\xE2\x80\xA6";
}

/* toolbar capsules share one padding; centring lives in ui.cc */
static int pillW(font_t* font, const char* text, int size) {
	return pillWidth(font, text, size, X::hidpiSize(11));
}

static grect_t pill(graph_t* g, font_t* font, const char* text, int size,
		int x, int y, int h, uint32_t bg, uint32_t ink, uint32_t border) {
	return drawPill(g, font, text, size, x, y, h, X::hidpiSize(11), bg, ink, border);
}

ReaderWidget::ReaderWidget(Store* store, Preloader* pre) {
	_store = store;
	_pre = pre;
	_pdf = false;
	_open = false;
	_pendingPage = 0;
	_spread = 0;
	_two = false;
	_pageW = 0;
	_pageH = 0;
	_lastCount = -2;
	_lastGen = 0;
	_shownEpoch = 0;
	_newEpoch = 0;
	_holdEpoch = 0;
	_oldCount = 0;
	_keepPage = 0;
	_hold[0] = NULL;
	_hold[1] = NULL;
	_holdPage[0] = -1;
	_holdPage[1] = -1;
	_toolbar = true;
	_anim = false;
	_animFrom = 0;
	_animDir = 1;
	_animStep = 0;
	_down = false;
	_moved = false;
	_seekDrag = false;
	_downX = 0;
	_downY = 0;
	_seekX0 = 0;
	_seekX1 = 0;
	_seekY = 0;
	backFunc = NULL;
	funcArg = NULL;
	memset(&_backBtn, 0, sizeof(grect_t));
	memset(&_smallBtn, 0, sizeof(grect_t));
	memset(&_bigBtn, 0, sizeof(grect_t));
	memset(&_themeBtn, 0, sizeof(grect_t));
	memset(&_seekHit, 0, sizeof(grect_t));
}

/* ---- spread arithmetic --------------------------------------------------- */
int ReaderWidget::maxSpread(int count) const {
	if(count <= 0)
		return 0;
	return _two ? count/2 : count - 1;
}

int ReaderWidget::firstPage(int spread) const {
	if(_two)
		return spread <= 0 ? 0 : 2*spread - 1;
	return spread;
}

int ReaderWidget::spreadOf(int page) const {
	if(page < 0)
		return 0;
	return _two ? (page + 1)/2 : page;
}

void ReaderWidget::spreadPages(int spread, int& left, int& right) const {
	if(_two) {
		if(spread <= 0) { left = -1; right = 0; }
		else { left = 2*spread - 1; right = 2*spread; }
	}
	else {
		left = spread;
		right = -1;
	}
}

int ReaderWidget::curPercent(int count) const {
	if(count <= 0)
		return 0;
	int last = _two ? (_spread <= 0 ? 0 : 2*_spread) : _spread;
	if(last > count - 1)
		last = count - 1;
	return (last + 1) * 100 / count;
}

void ReaderWidget::saveProgress() {
	if(!_open || _fname.empty())
		return;
	int pct = _lastCount > 0 ? curPercent(_lastCount) : -1;
	_store->setProgress(_fname, firstPage(_spread), pct);
	_store->save();
}

/* ---- book lifecycle ------------------------------------------------------- */
void ReaderWidget::openBook(const string& fname) {
	if(_open)
		saveProgress();
	_fname = fname;
	_title = stripExt(baseName(fname));
	_pdf = endsWith(fname, ".pdf");
	_pendingPage = _store->progress(fname);
	if(_pendingPage < 0)
		_pendingPage = 0;
	_spread = 0;
	_pageW = 0; /* force geometry re-apply on first paint */
	_pageH = 0;
	_lastCount = -2;
	_lastGen = 0;
	_toolbar = true;
	_anim = false;
	_open = true;
	abortTransition(false);

	Book* b;
	if(_pdf) {
		b = new PdfBook(fname);
	}
	else {
		TxtBook* t = new TxtBook(fname);
		t->setFontSize(X::hidpiSize(_store->settings.fontSize));
		t->setLineGap(_store->settings.lineGap);
		b = t;
	}
	_pre->setBook(b);
	_shownEpoch = _pre->epoch();
	update();
}

void ReaderWidget::closeBook() {
	if(!_open)
		return;
	abortTransition(false);
	saveProgress();
	_open = false;
	_pre->setBook(NULL);
	_pre->clear();
	if(backFunc != NULL)
		backFunc(funcArg);
}

/* font size change: rebuild the txt book on the worker (the UI never
   touches a live Book pointer), keeping the reading position. The old
   pages stay on screen completely unchanged (held against cache
   eviction) until the new font's spread is fully rendered, then the
   whole view swaps in a single repaint: no blank flash, no dissolve. */
void ReaderWidget::rebuildBook() {
	int keep = firstPage(_spread);
	TxtBook* t = new TxtBook(_fname);
	t->setFontSize(X::hidpiSize(_store->settings.fontSize));
	t->setLineGap(_store->settings.lineGap);
	_pre->setBook(t);
	uint64_t ne = _pre->epoch();
	if(_newEpoch != 0) {
		/* still waiting: same held base, retarget the wait */
		_newEpoch = ne;
		_keepPage = keep;
	}
	else {
		startTransition(ne, keep);
	}
	_anim = false;
	requestPages();
	update();
}

void ReaderWidget::releaseHolds() {
	for(int i = 0; i < 2; i++) {
		if(_hold[i] != NULL)
			_pre->release(_hold[i]);
		_hold[i] = NULL;
		_holdPage[i] = -1;
	}
	_holdEpoch = 0;
}

void ReaderWidget::startTransition(uint64_t newEpoch, int keepPage) {
	_keepPage = keepPage;
	_oldCount = _lastCount;
	_holdEpoch = _shownEpoch;
	uint32_t ink = palette(_store->settings.theme).ink;
	int l, r;
	spreadPages(_spread, l, r);
	int pages[2] = {l, r};
	for(int i = 0; i < 2; i++) {
		_holdPage[i] = pages[i];
		_hold[i] = NULL;
		if(pages[i] >= 0 && (_oldCount <= 0 || pages[i] < _oldCount))
			_hold[i] = _pre->acquire(pages[i], _pageW, _pageH, ink, _holdEpoch);
	}
	_newEpoch = newEpoch;
}

void ReaderWidget::abortTransition(bool commit) {
	releaseHolds();
	if(commit && _newEpoch != 0)
		_shownEpoch = _newEpoch;
	_newEpoch = 0;
}

bool ReaderWidget::spreadReady(uint64_t epoch, int count, int spread) {
	int l, r;
	spreadPages(spread, l, r);
	int pages[2] = {l, r};
	uint32_t ink = palette(_store->settings.theme).ink;
	for(int i = 0; i < 2; i++) {
		int pg = pages[i];
		if(pg < 0 || pg >= count)
			continue;
		if(!_pre->hasPage(pg, _pageW, _pageH, ink, epoch))
			return false;
	}
	return true;
}

void ReaderWidget::changeFontSize(int delta) {
	if(_pdf || !_open)
		return;
	int v = _store->settings.fontSize + delta;
	if(v < 12) v = 12;
	if(v > 40) v = 40;
	if(v == _store->settings.fontSize)
		return;
	_store->settings.fontSize = v;
	_store->save();
	rebuildBook();
}

void ReaderWidget::cycleTheme() {
	abortTransition(false);
	_store->settings.theme = (_store->settings.theme + 1) % THEME_COUNT;
	_store->save();
	_pre->clear();
	requestPages();
	update();
}

/* ---- geometry + preload orchestration ------------------------------------ */
void ReaderWidget::applyGeometry(const grect_t& r) {
	int contentH = r.h - topBarH() - botBarH();
	int contentW = r.w;
	int margin = X::hidpiSize(20);
	int gutter = X::hidpiSize(16);
	bool two = (contentW >= (int)(contentH * 1.15)) &&
		contentW >= X::hidpiSize(620);
	int ph = contentH - margin*2;
	int pw = two ? (contentW - margin*2 - gutter)/2 : contentW - margin*2;
	if(ph < 64) ph = 64;
	if(pw < 64) pw = 64;
	if(pw == _pageW && ph == _pageH && two == _two && _pageW > 0)
		return;
	abortTransition(true);
	int keep = (_pageW > 0) ? firstPage(_spread) : _pendingPage;
	_two = two;
	_pageW = pw;
	_pageH = ph;
	_spread = spreadOf(keep);
	if(_lastCount > 0 && _spread > maxSpread(_lastCount))
		_spread = maxSpread(_lastCount);
	_pendingPage = -1;
	_anim = false;
	requestPages();
}

void ReaderWidget::requestPages() {
	if(!_open || _pageW <= 0)
		return;
	int count = _lastCount > 0 ? _lastCount : 0;
	uint32_t ink = palette(_store->settings.theme).ink;
	uint64_t eq = _newEpoch != 0 ? _newEpoch : _shownEpoch;
	vector<PageReq> reqs;
	/* current spread first, then the one ahead, then the one behind */
	int order[3] = {_spread, _spread + 1, _spread - 1};
	for(int k = 0; k < 3; k++) {
		int s = order[k];
		if(s < 0)
			continue;
		if(count > 0 && s > maxSpread(count))
			continue;
		int l, rr;
		spreadPages(s, l, rr);
		int pages[2] = {l, rr};
		for(int j = 0; j < 2; j++) {
			int pg = pages[j];
			if(pg < 0 || (count > 0 && pg >= count))
				continue;
			bool dup = false;
			for(size_t i = 0; i < reqs.size(); i++)
				if(reqs[i].page == pg) { dup = true; break; }
			if(dup)
				continue;
			PageReq q = {pg, _pageW, _pageH, ink, eq};
			reqs.push_back(q);
		}
	}
	_pre->request(reqs);
}

/* ---- navigation ----------------------------------------------------------- */
void ReaderWidget::gotoSpread(int s, bool animate) {
	if(_lastCount > 0) {
		if(s < 0) s = 0;
		if(s > maxSpread(_lastCount)) s = maxSpread(_lastCount);
	}
	if(s < 0)
		s = 0;
	if(s == _spread)
		return;
	if(_newEpoch != 0) {
		/* follow the reader: re-anchor the pending swap */
		_keepPage = firstPage(s);
	}
	if(animate) {
		_animFrom = _spread;
		_animDir = (s > _spread) ? 1 : -1;
		_animStep = 0;
		_anim = true;
	}
	else {
		_anim = false;
	}
	_spread = s;
	saveProgress();
	requestPages();
	update();
}

void ReaderWidget::turn(int dir) {
	if(!_open)
		return;
	if(dir < 0 && _spread <= 0)
		return;
	if(dir > 0 && _lastCount > 0 && _spread >= maxSpread(_lastCount))
		return;
	gotoSpread(_spread + dir, true);
}

void ReaderWidget::seekTo(int x) {
	if(_lastCount <= 0 || _seekX1 <= _seekX0)
		return;
	if(x < _seekX0) x = _seekX0;
	if(x > _seekX1) x = _seekX1;
	int pct = (x - _seekX0) * 100 / (_seekX1 - _seekX0);
	int page = pct * _lastCount / 100;
	if(page > _lastCount - 1)
		page = _lastCount - 1;
	int s = spreadOf(page);
	if(s != _spread) {
		if(_newEpoch != 0)
			_keepPage = firstPage(s);
		_spread = s;
		_anim = false;
		requestPages();
		update();
	}
}

/* ---- painting -------------------------------------------------------------- */
void ReaderWidget::drawPaper(graph_t* g, const Palette& p, int x, int y) {
	uint32_t shadow = (_store->settings.theme == THEME_NIGHT) ?
		0x66000000 : 0x2a3a3226;
	graphShadowRound(g, x, y, _pageW, _pageH, X::hidpiSize(3), p.page, shadow);
}

void ReaderWidget::drawSlot(graph_t* g, font_t* font, const Palette& p,
		int page, int x, int y, uint64_t epoch) {
	drawPaper(g, p, x, y);
	if(page < 0 || (_lastCount > 0 && page >= _lastCount)) {
		/* blank leaf facing the first page: quiet bookplate */
		int fx = x + X::hidpiSize(18), fy = y + X::hidpiSize(18);
		graph_round(g, fx, fy, _pageW - X::hidpiSize(36), _pageH - X::hidpiSize(36),
				2, 1, p.line);
		return;
	}
	/* held pages are already borrowed: blit without re-acquiring */
	graph_t* pg = NULL;
	bool held = false;
	if(epoch == _holdEpoch) {
		for(int i = 0; i < 2; i++) {
			if(_hold[i] != NULL && _holdPage[i] == page) {
				pg = _hold[i];
				held = true;
				break;
			}
		}
	}
	if(pg == NULL)
		pg = _pre->acquire(page, _pageW, _pageH, p.ink, epoch);
	if(pg != NULL) {
		if(_pdf)
			graph_blt(pg, 0, 0, _pageW, _pageH, g, x, y, _pageW, _pageH);
		else
			graph_blt_alpha(pg, 0, 0, _pageW, _pageH, g, x, y, _pageW, _pageH, 255);
		if(!held)
			_pre->release(pg);
	}
	else {
		if(font != NULL) {
			const char* msg = (_lastCount < 0) ? "排版中…" : "渲染中…";
			int fs = X::hidpiSize(12);
			graph_draw_text_font_align(g, x, y, _pageW, _pageH,
					msg, font, fs, p.faint, FONT_ALIGN_CENTER);
		}
	}
}

void ReaderWidget::drawSpread(graph_t* g, font_t* font, const Palette& p,
		const grect_t& content, int spread, int offsetX, uint64_t epoch) {
	int margin = X::hidpiSize(20);
	int gutter = X::hidpiSize(16);
	int y0 = content.y + margin;
	int l, rr;
	spreadPages(spread, l, rr);
	if(_two) {
		int total = _pageW*2 + gutter;
		int x0 = content.x + (content.w - total)/2 + offsetX;
		drawSlot(g, font, p, l, x0, y0, epoch);
		drawSlot(g, font, p, rr, x0 + _pageW + gutter, y0, epoch);
		/* spine shadow between the two leaves */
		graph_fill_rect(g, x0 + _pageW, y0, gutter, _pageH, 0x10000000);
		graph_fill_rect(g, x0 + _pageW + gutter/2, y0, 1, _pageH, 0x22000000);
	}
	else {
		int x0 = content.x + (content.w - _pageW)/2 + offsetX;
		drawSlot(g, font, p, l, x0, y0, epoch);
	}
}

void ReaderWidget::drawBars(graph_t* g, XTheme* theme, const grect_t& r,
		const Palette& p) {
	font_t* font = theme->getFont();
	int bs = X::hidpiSize(12);
	int topH = topBarH();
	int botH = botBarH();

	/* ---- top bar ---- */
	graph_fill_rect(g, r.x, r.y, r.w, topH, p.bar);
	graph_fill_rect(g, r.x, r.y + topH - 1, r.w, 1, p.line);
	int by = r.y + (topH - X::hidpiSize(30))/2;
	int bh = X::hidpiSize(30);
	grect_t back = pill(g, font, "‹ 书架", bs, r.x + X::hidpiSize(14), by, bh,
			p.bg, p.barInk, p.line);
	_backBtn = back; _backBtn.x -= r.x; _backBtn.y -= r.y;

	int rightX = r.x + r.w - X::hidpiSize(14);
	string tl = fmt("主题·%s", themeName(_store->settings.theme));
	int tw = pillW(font, tl.c_str(), bs);
	grect_t th = pill(g, font, tl.c_str(), bs, rightX - tw, by, bh,
			p.bg, p.barInk, p.line);
	_themeBtn = th; _themeBtn.x -= r.x; _themeBtn.y -= r.y;
	int leftmost = th.x;
	memset(&_smallBtn, 0, sizeof(grect_t));
	memset(&_bigBtn, 0, sizeof(grect_t));
	if(!_pdf) {
		int wplus = pillW(font, "A+", bs);
		grect_t bp = pill(g, font, "A+", bs, th.x - X::hidpiSize(8) - wplus,
				by, bh, p.bg, p.barInk, p.line);
		_bigBtn = bp; _bigBtn.x -= r.x; _bigBtn.y -= r.y;
		int wminus = pillW(font, "A-", bs);
		grect_t bm = pill(g, font, "A-", bs, bp.x - X::hidpiSize(8) - wminus,
				by, bh, p.bg, p.barInk, p.line);
		_smallBtn = bm; _smallBtn.x -= r.x; _smallBtn.y -= r.y;
		leftmost = bm.x;
	}
	int ts = X::hidpiSize(14);
	int tx0 = back.x + back.w + X::hidpiSize(16);
	int tx1 = leftmost - X::hidpiSize(16);
	if(tx1 > tx0) {
		string title = elide(font, ts, _title, tx1 - tx0);
		graph_draw_text_font_align(g, tx0, r.y, tx1 - tx0, topH,
				title.c_str(), font, ts, p.barInk, FONT_ALIGN_CENTER);
	}

	/* ---- bottom bar: progress rail ---- */
	int by0 = r.y + r.h - botH;
	graph_fill_rect(g, r.x, by0, r.w, botH, p.bar);
	graph_fill_rect(g, r.x, by0, r.w, 1, p.line);
	int fs = X::hidpiSize(11);
	/* while waiting for the new font's renders the bars keep the old
	   pagination so nothing on screen jumps before the swap */
	int count = (_newEpoch != 0) ? _oldCount : _lastCount;
	string left, right;
	if(count > 0) {
		if(_two) {
			int last = 2*_spread;
			if(last > count - 1) last = count - 1;
			if(_spread <= 0)
				left = fmt("第 1 页 / 共 %d 页", count);
			else
				left = fmt("第 %d–%d 页 / 共 %d 页",
						firstPage(_spread) + 1, last + 1, count);
		}
		else {
			left = fmt("第 %d 页 / 共 %d 页", _spread + 1, count);
		}
		right = fmt("%d%%", curPercent(count));
	}
	else {
		left = fmt("第 %d 页 · 排版中…", firstPage(_spread) + 1);
		right = "…";
	}
	uint32_t lw = 0, lh = 0, rw = 0, rh = 0;
	font_text_size(left.c_str(), font, fs, &lw, &lh);
	font_text_size(right.c_str(), font, fs, &rw, &rh);
	graph_draw_text_font_align(g, r.x + X::hidpiSize(16), by0, (int)lw, botH,
			left.c_str(), font, fs, p.barInk, FONT_ALIGN_LEFT|FONT_ALIGN_V_CENTER);
	graph_draw_text_font_align(g, r.x + r.w - X::hidpiSize(16) - (int)rw, by0, (int)rw, botH,
			right.c_str(), font, fs, p.barInk, FONT_ALIGN_LEFT|FONT_ALIGN_V_CENTER);

	_seekX0 = X::hidpiSize(16) + (int)lw + X::hidpiSize(18);
	_seekX1 = r.w - X::hidpiSize(16) - (int)rw - X::hidpiSize(18);
	_seekY = (by0 - r.y) + botH/2;
	if(_seekX1 > _seekX0 + X::hidpiSize(40)) {
		int trackW = _seekX1 - _seekX0;
		graph_fill_round(g, r.x + _seekX0, r.y + _seekY - X::hidpiSize(2),
				trackW, X::hidpiSize(4), X::hidpiSize(2), p.line);
		int thumbX = _seekX0;
		if(count > 0) {
			int fx = trackW * curPercent(count) / 100;
			if(fx > X::hidpiSize(4))
				graph_fill_round(g, r.x + _seekX0, r.y + _seekY - X::hidpiSize(2),
						fx, X::hidpiSize(4), X::hidpiSize(2), p.accent);
			thumbX = _seekX0 + fx;
		}
		graph_fill_circle(g, r.x + thumbX, r.y + _seekY, X::hidpiSize(7), p.accent);
		graph_fill_circle(g, r.x + thumbX, r.y + _seekY, X::hidpiSize(3), p.bar);
		_seekHit.x = _seekX0 - X::hidpiSize(10);
		_seekHit.y = _seekY - X::hidpiSize(16);
		_seekHit.w = trackW + X::hidpiSize(20);
		_seekHit.h = X::hidpiSize(32);
	}
	else {
		memset(&_seekHit, 0, sizeof(grect_t));
	}
}

void ReaderWidget::onRepaint(graph_t* g, XTheme* theme, const grect_t& r) {
	const Palette& p = palette(_store->settings.theme);
	graph_fill_rect(g, r.x, r.y, r.w, r.h, p.bg);
	if(!_open)
		return;

	applyGeometry(r);
	grect_t content = {r.x, r.y + topBarH(), r.w, r.h - topBarH() - botBarH()};
	font_t* font = theme->getFont();

	if(!_pre->bookReady() && _newEpoch == 0) {
		const char* msg = "正在打开书籍…";
		int fs = X::hidpiSize(15);
		graph_draw_text_font_align(g, r.x, r.y, r.w, r.h,
				msg, font, fs, p.faint, FONT_ALIGN_CENTER);
		drawBars(g, theme, r, p);
		graph_unset_clip(g);
		return;
	}
	if(_lastCount == 0) {
		const char* m1 = "无法打开这本书";
		const char* m2 = "文件已损坏，或是不支持的格式";
		int f1 = X::hidpiSize(17), f2 = X::hidpiSize(12);
		uint32_t w1 = 0, h1 = 0, w2 = 0, h2 = 0;
		font_text_size(m1, font, f1, &w1, &h1);
		font_text_size(m2, font, f2, &w2, &h2);
		int cy = r.y + r.h/2;
		graph_draw_text_font(g, r.x + (r.w - (int)w1)/2, cy - (int)h1 - X::hidpiSize(4), m1,
				font, f1, p.ink);
		graph_draw_text_font(g, r.x + (r.w - (int)w2)/2, cy + X::hidpiSize(4),
				m2, font, f2, p.faint);
		drawBars(g, theme, r, p);
		graph_unset_clip(g);
		return;
	}

	graph_set_clip(g, content.x, content.y, content.w, content.h);
	/* during a font-size swap the held old pages stay on screen
	   unchanged; the new epoch appears in one full repaint at commit */
	if(_anim) {
		double t = (double)_animStep / (double)ANIM_STEPS;
		double inv = 1.0 - t;
		double e = 1.0 - inv*inv*inv; /* ease-out cubic */
		int shift = (int)(e * content.w);
		drawSpread(g, font, p, content, _animFrom, -_animDir*shift, _shownEpoch);
		drawSpread(g, font, p, content, _spread, _animDir*(content.w - shift), _shownEpoch);
	}
	else {
		drawSpread(g, font, p, content, _spread, 0, _shownEpoch);
	}
	graph_unset_clip(g);

	if(_toolbar)
		drawBars(g, theme, r, p);
}

void ReaderWidget::onTimer(uint32_t timerFPS, uint32_t timerSteps) {
	(void)timerFPS;
	(void)timerSteps;
	if(!isVisible() || !_open)
		return;
	bool changed = false;
	if(_anim) {
		_animStep++;
		if(_animStep >= ANIM_STEPS)
			_anim = false;
		changed = true;
	}
	if(_newEpoch != 0) {
		/* waiting: screen stays frozen on the held old pages */
		if(_pre->bookReady()) {
			int nc = _pre->pageCount();
			if(nc == 0) {
				/* new book failed to open: drop to the error screen */
				abortTransition(false);
				_lastCount = 0;
				changed = true;
			}
			else {
				/* nc may still be -1 (whole-book layout running in
				   background): swap as soon as the visible spread
				   is rendered, never wait for the total count */
				int s = spreadOf(_keepPage);
				if(nc > 0 && s > maxSpread(nc))
					s = maxSpread(nc);
				int readyCount = nc > 0 ? nc : 0x7fffffff;
				if(spreadReady(_newEpoch, readyCount, s)) {
					/* commit: one full repaint at the new font size */
					_spread = s;
					_lastCount = nc;
					_shownEpoch = _newEpoch;
					releaseHolds();
					_newEpoch = 0;
					_anim = false;
					requestPages();
					saveProgress();
					changed = true;
				}
			}
		}
		if(changed)
			update();
		return;
	}
	uint64_t gen = _pre->cacheGen();
	if(gen != _lastGen) {
		_lastGen = gen;
		changed = true;
	}
	int count = _pre->pageCount();
	if(count != _lastCount) {
		_lastCount = count;
		if(count > 0 && _spread > maxSpread(count))
			_spread = maxSpread(count);
		requestPages();
		changed = true;
	}
	if(changed)
		update();
}

/* ---- input ---------------------------------------------------------------- */
bool ReaderWidget::onMouse(xevent_t* ev) {
	if(!_open)
		return false;
	gpos_t pos = getInsidePos(ev->value.mouse.x, ev->value.mouse.y);
	switch(ev->state) {
	case MOUSE_STATE_DOWN:
		_down = true;
		_moved = false;
		_downX = ev->value.mouse.x;
		_downY = ev->value.mouse.y;
		if(_toolbar && _lastCount > 0 &&
				check_in_rect(pos.x, pos.y, &_seekHit)) {
			_seekDrag = true;
			seekTo(pos.x);
		}
		return true;
	case MOUSE_STATE_DRAG:
		if(_seekDrag) {
			seekTo(pos.x);
			return true;
		}
		if(_down) {
			int dx = ev->value.mouse.x - _downX;
			int dy = ev->value.mouse.y - _downY;
			if(abs(dx) > X::hidpiSize(6) || abs(dy) > X::hidpiSize(6))
				_moved = true;
		}
		return true;
	case MOUSE_STATE_UP:
	case MOUSE_STATE_CLICK: {
		if(!_down)
			return false;
		_down = false;
		if(_seekDrag) {
			_seekDrag = false;
			saveProgress();
			return true;
		}
		if(_moved) {
			/* horizontal swipe turns the page */
			int dx = ev->value.mouse.x - _downX;
			int dy = ev->value.mouse.y - _downY;
			if(abs(dx) > X::hidpiSize(60) && abs(dx) > abs(dy)*2)
				turn(dx < 0 ? 1 : -1);
			return true;
		}
		if(_toolbar) {
			if(check_in_rect(pos.x, pos.y, &_backBtn)) {
				closeBook();
				return true;
			}
			if(check_in_rect(pos.x, pos.y, &_themeBtn)) {
				cycleTheme();
				return true;
			}
			if(!_pdf && check_in_rect(pos.x, pos.y, &_smallBtn)) {
				changeFontSize(-2);
				return true;
			}
			if(!_pdf && check_in_rect(pos.x, pos.y, &_bigBtn)) {
				changeFontSize(2);
				return true;
			}
		}
		/* tap zones: left third back, right third forward, middle chrome */
		int w = getRootArea().w;
		if(pos.x < w/3)
			turn(-1);
		else if(pos.x > w*2/3)
			turn(1);
		else {
			_toolbar = !_toolbar;
			update();
		}
		return true;
	}
	default:
		return false;
	}
}

bool ReaderWidget::onIM(xevent_t* ev) {
	if(!_open || ev->state != XIM_STATE_PRESS)
		return false;
	int v = ev->value.im.value;
	if(v == KEY_LEFT || v == KEY_UP || v == JOYSTICK_LEFT) {
		turn(-1);
		return true;
	}
	if(v == KEY_RIGHT || v == KEY_DOWN || v == ' ' || v == JOYSTICK_RIGHT) {
		turn(1);
		return true;
	}
	if(v == KEY_HOME) {
		gotoSpread(0, true);
		return true;
	}
	if(v == KEY_END) {
		if(_lastCount > 0)
			gotoSpread(maxSpread(_lastCount), true);
		return true;
	}
	if(v == KEY_ESC) {
		if(!_toolbar) {
			_toolbar = true;
			update();
		}
		else {
			closeBook();
		}
		return true;
	}
	if(v == '+' || v == '=') {
		changeFontSize(2);
		return true;
	}
	if(v == '-') {
		changeFontSize(-2);
		return true;
	}
	if(v == 't' || v == 'T') {
		cycleTheme();
		return true;
	}
	return false;
}

}