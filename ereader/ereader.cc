/* ereader: an iBooks-style reading app for EwokOS.
   Shelf of txt/pdf books under /data/books, immersive reader with
   two-page landscape spreads and background page preloading. */
#include <Widget/WidgetWin.h>
#include <Widget/WidgetX.h>
#include <Widget/RootWidget.h>
#include <WidgetEx/FileDialog.h>
#include <x++/X.h>
#include <string.h>
#include "common.h"
#include "book.h"
#include "shelf.h"
#include "reader.h"

using namespace Ewok;
using namespace er;

class ReaderWin: public WidgetWin {
	FileDialog fdialog;
protected:
	void onDialoged(XWin* from, int res, void* arg) {
		(void)arg;
		if(res != Dialog::RES_OK || from != &fdialog)
			return;
		string fname = fdialog.getResult();
		if(fname.empty())
			return;
		if(!endsWith(fname, ".txt") && !endsWith(fname, ".pdf"))
			return;
		/* import: copy into the shelf unless it already lives there */
		string dir = store.settings.shelfDir;
		string target = dir + "/" + baseName(fname);
		if(fname != target && !fileExists(target))
			copyFile(fname, target);
		shelf->refresh();
		if(fileExists(target))
			openReader(target);
	}
public:
	Store store;
	Preloader pre;
	ShelfWidget* shelf;
	ReaderWidget* reader;

	ReaderWin() {
		shelf = NULL;
		reader = NULL;
	}

	void openReader(const string& fname) {
		shelf->hide();
		reader->show();
		reader->openBook(fname);
		getRoot()->focus(reader);
	}

	void showShelf() {
		reader->hide();
		shelf->show();
		shelf->refresh();
		getRoot()->focus(shelf);
	}

	void importBooks() {
		fdialog.setInitPath("/");
		fdialog.popup(this, 0, 0, "导入书籍", XWIN_STYLE_NORMAL);
		/* dialog runs its own WidgetWin theme: give it the CJK face too */
		fdialog.getTheme()->loadSystem();
		fdialog.getTheme()->setFont("system-cn", fdialog.getTheme()->basic.fontSize);
	}
};

static void onShelfOpen(const string& fname, void* arg) {
	((ReaderWin*)arg)->openReader(fname);
}

static void onShelfImport(const string& fname, void* arg) {
	(void)fname;
	((ReaderWin*)arg)->importBooks();
}

static void onReaderBack(void* arg) {
	((ReaderWin*)arg)->showShelf();
}

int main(int argc, char** argv) {
	X x;
	ReaderWin win;

	ensureDir(SHELF_DEFAULT);
	win.store.setShelf(SHELF_DEFAULT);
	win.store.load();
	win.pre.start();

	RootWidget* root = new RootWidget();
	win.setRoot(root);
	root->setType(Container::VERTICAL);

	win.shelf = new ShelfWidget(&win.store);
	win.shelf->openFunc = onShelfOpen;
	win.shelf->importFunc = onShelfImport;
	win.shelf->funcArg = &win;
	root->add(win.shelf);

	win.reader = new ReaderWidget(&win.store, &win.pre);
	win.reader->backFunc = onReaderBack;
	win.reader->funcArg = &win;
	root->add(win.reader);
	win.reader->hide();

	win.shelf->refresh();
	root->focus(win.shelf);

	win.open(&x, -1, -1, -1, 0, 0, "ereader", XWIN_STYLE_NORMAL);
	/* CJK-capable face for all widget text (titles, dialogs, bars) */
	win.getTheme()->loadSystem();
	win.getTheme()->setFont("system-cn", win.getTheme()->basic.fontSize);
	win.setTimer(30);

	if(argc >= 2) {
		/* the device shell splits on spaces without quoting: rejoin argv so a
		 * book path containing spaces can be passed on the command line */
		string path = argv[1];
		for(int i = 2; i < argc; i++) { path += " "; path += argv[i]; }
		win.openReader(path.c_str());
	}

	widgetXRun(&x, &win);
	win.pre.stop();
	return 0;
}