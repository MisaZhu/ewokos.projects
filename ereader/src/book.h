#ifndef EREADER_BOOK_H
#define EREADER_BOOK_H

#include "common.h"
#include <pthread.h>
#include <map>
#include <vector>

namespace er {

/* A book is opened and rendered exclusively on the Preloader worker
   thread; the UI thread only calls the mutex-guarded query methods. */
class Book {
public:
	virtual ~Book() { }
	virtual bool open() = 0;
	virtual int  pageCount() = 0;      /* -1 while layout still running */
	virtual graph_t* renderPage(int idx, int w, int h, uint32_t ink) = 0;
	virtual string title() = 0;
	virtual bool isTxt() = 0;
	virtual void setFontSize(int px) { (void)px; }
	virtual void setLineGap(int pct) { (void)pct; }
	/* ask a long render/layout to bail out (book is being replaced) */
	virtual void abort() { }
	/* background pagination chunk while no page job is pending;
	   true when more work may remain (false = idle/done/not applicable) */
	virtual bool layoutIdle() { return false; }
};

/* ---- rendered-page cache (LRU, refcounted while the UI blits) ---------
   Entries carry the render epoch (one per Book hand-off) so pages of the
   previous font size / book survive a swap: the UI keeps blitting them
   until the new epoch's pages are ready, then cross-fades. */
struct PageKey {
	uint64_t epoch;
	int page, w, h;
	uint32_t ink;
	bool operator<(const PageKey& o) const {
		if(epoch != o.epoch) return epoch < o.epoch;
		if(page != o.page) return page < o.page;
		if(w != o.w) return w < o.w;
		if(h != o.h) return h < o.h;
		return ink < o.ink;
	}
};

struct PageEntry {
	graph_t* g;
	uint64_t used;
	int refs;
	bool dead;   /* cleared while borrowed: free on last release */
};

class PageCache {
	pthread_mutex_t _lock;
	map<PageKey, PageEntry> _map;
	uint64_t _clock;
	size_t _cap;

	void evict();
public:
	PageCache(size_t cap = 8);
	~PageCache();

	/* UI thread: borrow a cached page (NULL if absent); release after blit */
	graph_t* acquire(int page, int w, int h, uint32_t ink, uint64_t epoch);
	void release(graph_t* g);

	/* worker thread: publish a freshly rendered page */
	void put(int page, int w, int h, uint32_t ink, uint64_t epoch, graph_t* g);
	bool has(int page, int w, int h, uint32_t ink, uint64_t epoch);
	void clear();
};

/* ---- background opener / renderer / preloader -------------------------- */
struct PageReq {
	int page, w, h;
	uint32_t ink;
	uint64_t epoch;
};

class Preloader {
	pthread_mutex_t _mtx;
	pthread_cond_t  _cond;
	pthread_t _tid;
	bool _running;
	bool _started;

	Book* _book;         /* owned; swapped/deleted by the worker */
	Book* _pending;      /* hand-off slot, guarded by _mtx */
	bool _closeReq;      /* setBook(NULL): worker drops the current book */
	bool _opened;
	bool _openOk;        /* open() result; pageCount 0 when it failed */

	uint64_t _gen;       /* bumped whenever the wanted set changes */
	vector<PageReq> _want;
	uint64_t _epoch;     /* render identity, bumped per book hand-off */

	PageCache _cache;
	uint64_t _cacheGen;  /* bumped on every publish/open (UI polls it) */

	void loop();
	static void* trampoline(void* p);
public:
	Preloader();
	~Preloader();

	void start();
	void stop();

	/* hand a new book to the worker (NULL closes the current one).
	   returns immediately: ownership transfers at once and a long render
	   on the old book is aborted so the swap never stalls the UI.
	   A non-NULL hand-off bumps the epoch but keeps the cache: the old
	   epoch's pages stay blitable (and holdable) for a flash-free swap. */
	void setBook(Book* b);
	bool bookReady();
	int  pageCount();
	uint64_t epoch();
	bool hasPage(int page, int w, int h, uint32_t ink, uint64_t epoch);

	/* replace the wanted page list (priority order); stale renders drop */
	void request(const vector<PageReq>& reqs);
	void clear();

	graph_t* acquire(int page, int w, int h, uint32_t ink, uint64_t epoch);
	void release(graph_t* g);
	uint64_t cacheGen();
};

}

#endif