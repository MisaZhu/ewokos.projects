#ifndef EREADER_SHELF_H
#define EREADER_SHELF_H

#include "common.h"
#include <Widget/Widget.h>
#include <unordered_map>

using namespace Ewok;

namespace er {

struct BookItem {
	string fname;      /* full path                    */
	string title;      /* file name without extension  */
	bool pdf;
	uint32_t hash;
	int page;          /* saved progress, -1 none      */
	int pct;
};

class ShelfWidget : public Widget {
public:
	typedef void (*ActionFunc)(const string& fname, void* arg);
		typedef void (*ExitFunc)(void* arg);

private:
	Store* _store;
	vector<BookItem> _items;
	int _scroll, _maxScroll;
	int _hover, _pressed;
	bool _dragging;
	int _dragFromY, _dragScroll;
	bool _moved;
	unordered_map<uint32_t, int> _wcache;
	int _wcacheSize;

	/* hit rects rebuilt every paint */
	grect_t _importBtn, _refreshBtn, _themeBtn, _exitBtn, _emptyBtn;
	vector<grect_t> _covers;

	int  measure(font_t* font, int size, uint32_t cp);
	void wrapTitle(font_t* font, int size, const string& title,
			int maxW, int maxLines, vector<string>& out);
	void drawCover(graph_t* g, XTheme* theme, const BookItem& it,
			const grect_t& r, bool hover);
	void layout(const grect_t& r);
	void clampScroll();
	int  itemAt(int x, int y);

protected:
	void onRepaint(graph_t* g, XTheme* theme, const grect_t& r);
	bool onMouse(xevent_t* ev);
	bool onIM(xevent_t* ev);

public:
	ActionFunc openFunc;
	ActionFunc importFunc;
	ExitFunc exitFunc;
	void* funcArg;

	ShelfWidget(Store* store);
	void refresh();
};

}

#endif