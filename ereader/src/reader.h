#ifndef EREADER_READER_H
#define EREADER_READER_H

#include "common.h"
#include "book.h"
#include <font/font.h>
#include <Widget/Widget.h>

using namespace Ewok;

namespace er {

/* Immersive reading surface: single page in portrait, two-page spread
   in landscape, slide page-turn animation, tap zones, swipe, keyboard,
   draggable progress rail. Pages are rendered ahead of time by the
   Preloader worker; painting only blits cached bitmaps. */
class ReaderWidget : public Widget {
public:
	typedef void (*BackFunc)(void* arg);

private:
	Store* _store;
	Preloader* _pre;

	string _fname;
	string _title;
	bool _pdf;
	bool _open;            /* a book has been handed to the worker */
	int _pendingPage;      /* progress to apply once geometry exists */

	int _spread;
	bool _two;             /* two-page spread mode               */
	int _pageW, _pageH;    /* render geometry of one page        */
	int _lastCount;
	uint64_t _lastGen;

	bool _toolbar;

	/* slide animation */
	bool _anim;
	int _animFrom, _animDir, _animStep;

	/* pointer gesture state */
	bool _down, _moved, _seekDrag;
	int _downX, _downY;

	/* hit rects (widget-local), rebuilt every paint */
	grect_t _backBtn, _smallBtn, _bigBtn, _themeBtn;
	grect_t _seekHit;
	int _seekX0, _seekX1, _seekY;

	int  maxSpread(int count) const;
	int  firstPage(int spread) const;
	int  spreadOf(int page) const;
	void spreadPages(int spread, int& left, int& right) const;
	int  curPercent(int count) const;
	void saveProgress();
	void applyGeometry(const grect_t& r);
	void requestPages();
	void rebuildBook();
	void gotoSpread(int s, bool animate);
	void turn(int dir);
	void seekTo(int x);
	void changeFontSize(int delta);
	void cycleTheme();
	void drawPaper(graph_t* g, const Palette& p, int x, int y);
	void drawSlot(graph_t* g, font_t* font, const Palette& p, int page, int x, int y);
	void drawSpread(graph_t* g, font_t* font, const Palette& p,
			const grect_t& content, int spread, int offsetX);
	void drawBars(graph_t* g, XTheme* theme, const grect_t& r, const Palette& p);

protected:
	void onRepaint(graph_t* g, XTheme* theme, const grect_t& r);
	void onTimer(uint32_t timerFPS, uint32_t timerSteps);
	bool onMouse(xevent_t* ev);
	bool onIM(xevent_t* ev);

public:
	BackFunc backFunc;
	void* funcArg;

	ReaderWidget(Store* store, Preloader* pre);
	void openBook(const string& fname);
	void closeBook();
	bool isOpen() const { return _open; }
};

}

#endif