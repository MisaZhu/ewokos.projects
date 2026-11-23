#ifndef EREADER_TXTBOOK_H
#define EREADER_TXTBOOK_H

#include "book.h"
#include <font/font.h>
#include <unordered_map>

namespace er {

/* Plain-text book: whole file read into memory once (single big VFS read,
   no per-char IPC), decoded to codepoints, paginated lazily on the worker
   thread. Per-codepoint advance widths are measured once per font size
   and cached, so pagination and drawing are pure CPU afterwards. */
class TxtBook : public Book {
public:
	struct Line {
		uint32_t start;   /* index into _cps          */
		uint32_t count;
		bool indent;      /* paragraph first line     */
		bool gap;         /* blank line before        */
	};
	struct Page {
		vector<Line> lines;
	};

private:
	string _fname;
	string _title;
	vector<uint32_t> _cps;

	pthread_mutex_t _mtx;
	font_t* _font;                       /* worker-owned font handle   */
	int _fontSize;
	int _lineGap;
	int _layoutW, _layoutH;              /* layout the pages were built for */
	vector<Page> _pages;
	int _pubCount;                       /* lock-free published count (-1 laying out) */
	uint32_t _laidTo;                    /* codepoints consumed so far */
	bool _eof;
	bool _paraStart;
	bool _pendingGap;
	unordered_map<uint32_t, int> _widths; /* codepoint -> advance px    */
	int _widthFont;                      /* font size _widths is for   */
	int _spaceW;
	int _lineH;
	int _pad;
	volatile bool _abort;              /* set when the book is replaced */

	int  width(uint32_t cp);
	void resetLayout();
	/* grow _pages until page idx exists or EOF; returns pages available */
	int  layoutUpTo(int idx, int w, int h);
	void drawLine(graph_t* g, const Line& ln, int x, int y, uint32_t ink);

public:
	TxtBook(const string& fname);
	~TxtBook();

	bool open();
	int  pageCount();
	graph_t* renderPage(int idx, int w, int h, uint32_t ink);
	string title() { return _title; }
	bool isTxt() { return true; }
	void setFontSize(int px);
	void setLineGap(int pct);
	void abort();
	bool layoutIdle();
};

}

#endif