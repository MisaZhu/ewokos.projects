#include "txtbook.h"
#include <stdlib.h>
#include <string.h>
#include <ewoksys/vfs.h>
#include <x++/X.h>

using namespace Ewok;

namespace er {

TxtBook::TxtBook(const string& fname) {
	_fname = fname;
	_title = stripExt(baseName(fname));
	pthread_mutex_init(&_mtx, NULL);
	_font = NULL;
	_fontSize = X::hidpiSize(16);
	_lineGap = 150;
	_layoutW = 0;
	_layoutH = 0;
	_pubCount = -1;
	_laidTo = 0;
	_eof = false;
	_paraStart = true;
	_pendingGap = false;
	_widthFont = 0;
	_spaceW = 0;
	_lineH = 0;
	_pad = 0;
	_abort = false;
}

TxtBook::~TxtBook() {
	if(_font != NULL)
		font_free(_font);
}

bool TxtBook::open() {
	int sz = 0;
	uint8_t* data = vfs_readfile(_fname.c_str(), &sz);
	if(data == NULL)
		return false;
	_cps = decodeUtf8(data, sz);
	free(data);
	if(!_cps.empty() && _cps[0] == 0xFEFF)
		_cps.erase(_cps.begin());

	_font = font_new("system-cn", true);
	if(_font == NULL)
		return false;
	return true;
}

/* Full-width glyphs (CJK ideographs, kana, hangul, fullwidth forms...)
   advance exactly one em in system-cn. Measuring them through fontd costs
   one IPC round trip per character on real hardware, which made whole-book
   pagination take minutes; the width is known without asking. */
static bool isWideCp(uint32_t cp) {
	return (cp >= 0x1100 && cp <= 0x115F) ||
		(cp >= 0x2E80 && cp <= 0xA4CF) ||
		(cp >= 0xAC00 && cp <= 0xD7A3) ||
		(cp >= 0xF900 && cp <= 0xFAFF) ||
		(cp >= 0xFE30 && cp <= 0xFE4F) ||
		(cp >= 0xFF01 && cp <= 0xFF60) ||
		(cp >= 0xFFE0 && cp <= 0xFFE6) ||
		(cp >= 0x20000 && cp <= 0x3FFFD);
}

int TxtBook::width(uint32_t cp) {
	if(_widthFont != _fontSize) {
		_widths.clear();
		_widthFont = _fontSize;
		uint32_t w = 0, h = 0;
		font_char_size(' ', _font, _fontSize, &w, &h);
		_spaceW = (w > 0) ? (int)w : _fontSize/2;
	}
	if(isWideCp(cp))
		return _fontSize;
	unordered_map<uint32_t,int>::iterator it = _widths.find(cp);
	if(it != _widths.end())
		return it->second;
	uint32_t w = 0, h = 0;
	font_char_size(cp, _font, _fontSize, &w, &h);
	int iw = (w > 0) ? (int)w : _fontSize/2;
	_widths[cp] = iw;
	return iw;
}

void TxtBook::resetLayout() {
	_pages.clear();
	_pubCount = -1;
	_laidTo = 0;
	_eof = false;
	_paraStart = true;
	_pendingGap = false;
}

void TxtBook::setFontSize(int px) {
	/* px is physical: callers pass hidpi-scaled logical sizes, so the
	   upper bound scales with the display too */
	if(px < 12) px = 12;
	int maxPx = X::hidpiSize(40);
	if(px > maxPx) px = maxPx;
	pthread_mutex_lock(&_mtx);
	if(px != _fontSize) {
		_fontSize = px;
		resetLayout();
	}
	pthread_mutex_unlock(&_mtx);
}

void TxtBook::setLineGap(int pct) {
	if(pct < 120) pct = 120;
	if(pct > 220) pct = 220;
	pthread_mutex_lock(&_mtx);
	if(pct != _lineGap) {
		_lineGap = pct;
		resetLayout();
	}
	pthread_mutex_unlock(&_mtx);
}

int TxtBook::pageCount() {
	/* lock-free: the UI polls this every tick and must never wait on
	   the render mutex; _pubCount is published by layoutUpTo */
	return _pubCount;
}

/* Lay out one more page from _laidTo. Caller holds _mtx and has set
   _pad/_lineH for the current geometry. */
static inline bool isSpaceCp(uint32_t cp) {
	return cp == ' ' || cp == '\t' || cp == 0x3000;
}

int TxtBook::layoutUpTo(int idx, int w, int h) {
	if(w != _layoutW || h != _layoutH) {
		_layoutW = w;
		_layoutH = h;
		resetLayout();
	}
	_pad = _fontSize;
	_lineH = _fontSize * _lineGap / 100;
	if(_lineH < _fontSize + 2)
		_lineH = _fontSize + 2;
	int cw = w - _pad*2;
	int ch = h - _pad*2;
	if(cw < _fontSize*2 || ch < _lineH) {
		_pubCount = _eof ? (int)_pages.size() : -1;
		return (int)_pages.size();
	}

	while(!_abort && !_eof && (int)_pages.size() <= idx) {
		Page page;
		int y = 0;
		bool lineOpen = false;
		Line cur;
		cur.start = _laidTo;
		cur.count = 0;
		cur.indent = false;
		cur.gap = false;
		int lineW = 0;
		int indentW = 0;

		while(true) {
			if(_abort)
				return (int)_pages.size();
			if(_laidTo >= _cps.size()) {
				_eof = true;
				if(lineOpen && cur.count > 0)
					page.lines.push_back(cur);
				break;
			}
			uint32_t cp = _cps[_laidTo];

			if(cp == '\r') {
				_laidTo++;
				continue;
			}
			if(cp == '\n') {
				_laidTo++;
				if(lineOpen && cur.count > 0) {
					page.lines.push_back(cur);
					y += _lineH;
				}
				else {
					/* blank source line: half-line breathing room */
					_pendingGap = true;
				}
				lineOpen = false;
				_paraStart = true;
				cur.count = 0;
				cur.indent = false;
				cur.gap = false;
				lineW = 0;
				indentW = 0;
				if(y >= ch)
					break;
				continue;
			}

			/* open a new visual line if needed */
			if(!lineOpen) {
				cur.gap = false;
				if(_pendingGap) {
					y += _lineH/2;
					cur.gap = true;
					_pendingGap = false;
					if(y >= ch)
						break;
				}
				if(y + _lineH > ch)
					break; /* page full */
				cur.start = _laidTo;
				cur.count = 0;
				cur.indent = false;
				lineW = 0;
				indentW = 0;
				/* skip leading whitespace, then indent CJK paragraphs */
				while(_laidTo < _cps.size() && isSpaceCp(_cps[_laidTo]) &&
						_cps[_laidTo] != '\n')
					_laidTo++;
				if(_laidTo >= _cps.size())
					continue;
				if(_cps[_laidTo] == '\n')
					continue;
				if(_paraStart && isCjk(_cps[_laidTo])) {
					cur.indent = true;
					indentW = _fontSize*2;
					lineW = indentW;
				}
				_paraStart = false;
				lineOpen = true;
				cur.start = _laidTo;
			}

			int adv;
			if(cp == '\t')
				adv = _spaceW*4;
			else if(cp == 0x3000)
				adv = _fontSize;
			else
				adv = width(cp);

			if(lineW + adv > cw && cur.count > 0) {
				/* wrap; honour CJK line-head/tail rules */
				uint32_t moved = 0;
				uint32_t movedCount = 0;
				if(noLineStart(cp) && cur.count > 1) {
					moved = _cps[cur.start + cur.count - 1];
					movedCount = 1;
					if(cur.count > 2 && noLineEnd(moved)) {
						moved = _cps[cur.start + cur.count - 2];
						movedCount = 2;
					}
				}
				else if(cur.count > 1 && noLineEnd(_cps[cur.start + cur.count - 1])) {
					moved = _cps[cur.start + cur.count - 1];
					movedCount = 1;
				}
				cur.count -= movedCount;
				page.lines.push_back(cur);
				y += _lineH;
				lineOpen = false;
				/* reprocess current cp on the fresh line (with the moved
				   characters pushed back into the stream) */
				_laidTo -= movedCount;
				if(y >= ch)
					break;
				continue;
			}

			if(cur.count == 0)
				cur.start = _laidTo;
			cur.count++;
			lineW += adv;
			_laidTo++;
		}

		if(!page.lines.empty())
			_pages.push_back(page);
		if(_eof) {
			if(_pages.empty())
				_pages.push_back(page); /* empty file: one blank page */
			break;
		}
	}
	_pubCount = _eof ? (int)_pages.size() : -1;
	return (int)_pages.size();
}

void TxtBook::drawLine(graph_t* g, const Line& ln, int x, int y, uint32_t ink) {
	int cx = x + (ln.indent ? _fontSize*2 : 0);
	for(uint32_t i = 0; i < ln.count; i++) {
		if(_abort)
			return;
		uint32_t cp = _cps[ln.start + i];
		uint32_t w = 0;
		graph_draw_unicode_font(g, cx, y, cp, _font, _fontSize, ink, &w, NULL);
		if(cp == '\t')
			cx += _spaceW*4;
		else if(cp == 0x3000)
			cx += _fontSize;
		else
			cx += width(cp);
	}
}

graph_t* TxtBook::renderPage(int idx, int w, int h, uint32_t ink) {
	pthread_mutex_lock(&_mtx);
	if(_font == NULL || w <= 0 || h <= 0) {
		pthread_mutex_unlock(&_mtx);
		return NULL;
	}
	layoutUpTo(idx, w, h);
	/* keep paginating ahead so the total count settles in background */
	if(!_eof)
		layoutUpTo(idx + 24, w, h);
	if(_abort || idx < 0 || idx >= (int)_pages.size()) {
		pthread_mutex_unlock(&_mtx);
		return NULL;
	}

	graph_t* g = graph_new(NULL, w, h);
	if(g == NULL) {
		pthread_mutex_unlock(&_mtx);
		return NULL;
	}
	graph_clear(g, 0x00000000); /* transparent: reader supplies the paper */

	const Page& page = _pages[idx];
	int y = _pad;
	for(size_t i = 0; i < page.lines.size(); i++) {
		const Line& ln = page.lines[i];
		if(ln.gap)
			y += _lineH/2;
		drawLine(g, ln, _pad, y, ink);
		y += _lineH;
	}
	pthread_mutex_unlock(&_mtx);
	return g;
}

void TxtBook::abort() {
	_abort = true;
}

/* paginate a chunk towards EOF while the worker has no page job, so the
   total page count settles without the user having to turn every page */
bool TxtBook::layoutIdle() {
	pthread_mutex_lock(&_mtx);
	if(_abort || _eof || _font == NULL || _layoutW <= 0) {
		pthread_mutex_unlock(&_mtx);
		return false;
	}
	size_t n0 = _pages.size();
	layoutUpTo((int)_pages.size() + 8, _layoutW, _layoutH);
	bool more = !_eof && _pages.size() > n0;
	pthread_mutex_unlock(&_mtx);
	return more;
}

}