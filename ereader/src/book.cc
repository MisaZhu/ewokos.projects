#include "book.h"
#include <stdlib.h>
#include <unistd.h>

namespace er {

/* ---- PageCache ---------------------------------------------------------- */
PageCache::PageCache(size_t cap) {
	pthread_mutex_init(&_lock, NULL);
	_clock = 0;
	_cap = cap;
}

PageCache::~PageCache() {
	clear();
}

void PageCache::evict() {
	while(_map.size() > _cap) {
		map<PageKey, PageEntry>::iterator victim = _map.end();
		uint64_t oldest = (uint64_t)-1;
		for(map<PageKey, PageEntry>::iterator it = _map.begin(); it != _map.end(); ++it) {
			if(it->second.refs == 0 && it->second.used < oldest) {
				oldest = it->second.used;
				victim = it;
			}
		}
		if(victim == _map.end())
			return; /* everything borrowed */
		graph_free(victim->second.g);
		_map.erase(victim);
	}
}

graph_t* PageCache::acquire(int page, int w, int h, uint32_t ink) {
	PageKey k = {page, w, h, ink};
	pthread_mutex_lock(&_lock);
	map<PageKey, PageEntry>::iterator it = _map.find(k);
	graph_t* g = NULL;
	if(it != _map.end() && !it->second.dead) {
		it->second.refs++;
		it->second.used = ++_clock;
		g = it->second.g;
	}
	pthread_mutex_unlock(&_lock);
	return g;
}

void PageCache::release(graph_t* g) {
	if(g == NULL)
		return;
	pthread_mutex_lock(&_lock);
	for(map<PageKey, PageEntry>::iterator it = _map.begin(); it != _map.end(); ++it) {
		if(it->second.g == g) {
			if(it->second.refs > 0)
				it->second.refs--;
			if(it->second.dead && it->second.refs == 0) {
				graph_free(it->second.g);
				_map.erase(it);
			}
			break;
		}
	}
	pthread_mutex_unlock(&_lock);
}

void PageCache::put(int page, int w, int h, uint32_t ink, graph_t* g) {
	if(g == NULL)
		return;
	PageKey k = {page, w, h, ink};
	pthread_mutex_lock(&_lock);
	map<PageKey, PageEntry>::iterator it = _map.find(k);
	if(it != _map.end()) {
		if(it->second.refs > 0) {
			/* still borrowed (dead entry awaiting release): drop the new
			   render; the next request cycle retries once it is gone */
			pthread_mutex_unlock(&_lock);
			graph_free(g);
			return;
		}
		graph_free(it->second.g);
		it->second.g = g;
		it->second.used = ++_clock;
		it->second.dead = false;
	}
	else {
		PageEntry e = {g, ++_clock, 0, false};
		_map[k] = e;
		evict();
	}
	pthread_mutex_unlock(&_lock);
}

bool PageCache::has(int page, int w, int h, uint32_t ink) {
	PageKey k = {page, w, h, ink};
	pthread_mutex_lock(&_lock);
	map<PageKey, PageEntry>::iterator it = _map.find(k);
	bool r = it != _map.end() && !it->second.dead;
	pthread_mutex_unlock(&_lock);
	return r;
}

void PageCache::clear() {
	pthread_mutex_lock(&_lock);
	map<PageKey, PageEntry>::iterator it = _map.begin();
	while(it != _map.end()) {
		if(it->second.refs == 0) {
			graph_free(it->second.g);
			_map.erase(it++);
		}
		else {
			/* UI is mid-blit: mark dead, release() frees it */
			it->second.dead = true;
			++it;
		}
	}
	pthread_mutex_unlock(&_lock);
}

/* ---- Preloader ----------------------------------------------------------- */
Preloader::Preloader() {
	pthread_mutex_init(&_mtx, NULL);
	pthread_cond_init(&_cond, NULL);
	_running = false;
	_started = false;
	_book = NULL;
	_pending = NULL;
	_closeReq = false;
	_opened = false;
	_gen = 0;
	_cacheGen = 0;
}

Preloader::~Preloader() {
	stop();
}

void Preloader::start() {
	if(_started)
		return;
	_running = true;
	_started = true;
	pthread_create(&_tid, NULL, trampoline, this);
}

void Preloader::stop() {
	if(!_started)
		return;
	pthread_mutex_lock(&_mtx);
	_running = false;
	pthread_cond_signal(&_cond);
	pthread_mutex_unlock(&_mtx);
	pthread_join(_tid, NULL);
	_started = false;
	if(_book != NULL) {
		delete _book;
		_book = NULL;
	}
	if(_pending != NULL) {
		delete _pending;
		_pending = NULL;
	}
}

void* Preloader::trampoline(void* p) {
	((Preloader*)p)->loop();
	return NULL;
}

void Preloader::loop() {
	pthread_mutex_lock(&_mtx);
	while(_running) {
		/* close request: drop the current book entirely */
		if(_closeReq) {
			if(_book != NULL)
				delete _book;
			_book = NULL;
			_opened = false;
			_closeReq = false;
			_cache.clear();
			_cacheGen++;
		}
		/* book hand-off */
		if(_pending != NULL || (_book != NULL && !_opened)) {
			if(_pending != NULL) {
				if(_book != NULL)
					delete _book;
				_book = _pending;
				_pending = NULL;
				_opened = false;
				_cache.clear();
				_cacheGen++;
			}
			if(_book != NULL && !_opened) {
				Book* b = _book;
				pthread_mutex_unlock(&_mtx);
				bool ok = b->open();
				pthread_mutex_lock(&_mtx);
				if(_book == b) {
					_opened = true;
					if(!ok) { /* keep the shell; pageCount stays 0 */ }
					_cacheGen++;
				}
				continue;
			}
		}

		/* pick the highest-priority missing page of the current generation */
		PageReq job;
		bool haveJob = false;
		uint64_t jobGen = _gen;
		if(_book != NULL && _opened) {
			for(size_t i = 0; i < _want.size(); i++) {
				const PageReq& r = _want[i];
				if(r.page < 0)
					continue;
				if(!_cache.has(r.page, r.w, r.h, r.ink)) {
					job = r;
					haveJob = true;
					break;
				}
			}
		}

		if(!haveJob) {
			pthread_cond_wait(&_cond, &_mtx);
			continue;
		}

		Book* b = _book;
		pthread_mutex_unlock(&_mtx);
		graph_t* g = b->renderPage(job.page, job.w, job.h, job.ink);
		pthread_mutex_lock(&_mtx);
		if(_book == b && _gen == jobGen && g != NULL) {
			_cache.put(job.page, job.w, job.h, job.ink, g);
			_cacheGen++;
			g = NULL;
		}
		if(g != NULL)
			graph_free(g); /* stale result */
	}
	pthread_mutex_unlock(&_mtx);
}

void Preloader::setBook(Book* b) {
	pthread_mutex_lock(&_mtx);
	_pending = b;
	if(b == NULL)
		_closeReq = true;
	_gen++;
	_want.clear();
	pthread_cond_signal(&_cond);
	/* wait until the worker took the hand-off (it may be mid-render) */
	while(_running && (_pending != NULL || _closeReq)) {
		pthread_mutex_unlock(&_mtx);
		usleep(2000);
		pthread_mutex_lock(&_mtx);
	}
	pthread_mutex_unlock(&_mtx);
}

bool Preloader::bookReady() {
	pthread_mutex_lock(&_mtx);
	bool r = _book != NULL && _opened;
	pthread_mutex_unlock(&_mtx);
	return r;
}

int Preloader::pageCount() {
	pthread_mutex_lock(&_mtx);
	int n = (_book != NULL && _opened) ? _book->pageCount() : 0;
	pthread_mutex_unlock(&_mtx);
	return n;
}

void Preloader::request(const vector<PageReq>& reqs) {
	pthread_mutex_lock(&_mtx);
	_want = reqs;
	_gen++;
	pthread_cond_signal(&_cond);
	pthread_mutex_unlock(&_mtx);
}

void Preloader::clear() {
	pthread_mutex_lock(&_mtx);
	_want.clear();
	_gen++;
	_cache.clear();
	_cacheGen++;
	pthread_mutex_unlock(&_mtx);
}

graph_t* Preloader::acquire(int page, int w, int h, uint32_t ink) {
	return _cache.acquire(page, w, h, ink);
}

void Preloader::release(graph_t* g) {
	_cache.release(g);
}

uint64_t Preloader::cacheGen() {
	pthread_mutex_lock(&_mtx);
	uint64_t g = _cacheGen;
	pthread_mutex_unlock(&_mtx);
	return g;
}

}