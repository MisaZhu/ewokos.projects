#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <tinyjson/tinyjson.h>
#include <ewoksys/vfs.h>

namespace er {

const char* SHELF_DEFAULT = "/data/books";
const char* STATE_FNAME   = ".ereader.json";

/* ---- palettes --------------------------------------------------------- */
static const Palette _palettes[THEME_COUNT] = {
	{ /* paper: warm parchment desk, cream page, walnut ink, amber accent */
		0xffe9e0cf, 0xfffbf7ee, 0xff3a3226, 0xff9a8f7c, 0xffd98e32,
		0xfff3ecdd, 0xff5a5040, 0x223a3226, 0xfff6efdf, 0xffe4d8c0 },
	{ /* sepia: deeper amber desk */
		0xffd9c6a5, 0xfff4e9d4, 0xff4a3b28, 0xff97866b, 0xffc47a2c,
		0xffe6d7ba, 0xff5c4c34, 0x224a3b28, 0xffefe2c6, 0xffdcc9a4 },
	{ /* night: charcoal desk, slate page, moonlight ink */
		0xff121419, 0xff1e2126, 0xffb8bcc2, 0xff6a7078, 0xffe8a33d,
		0xff1a1d22, 0xffaeb4bc, 0x22000000, 0xff23262c, 0xff191c21 },
};

const Palette& palette(int theme) {
	if(theme < 0 || theme >= THEME_COUNT)
		theme = THEME_PAPER;
	return _palettes[theme];
}

const char* themeName(int theme) {
	switch(theme) {
	case THEME_SEPIA: return "护眼";
	case THEME_NIGHT: return "夜间";
	default: return "纸张";
	}
}

/* ---- cover hues -------------------------------------------------------- */
static const CoverHue _hues[] = {
	{ 0xffb5484d, 0xff7e2f34, 0xffd98a8e }, /* crimson  */
	{ 0xffd9963b, 0xff9c6520, 0xffecc489 }, /* amber    */
	{ 0xff7a8450, 0xff525a33, 0xffaeb68b }, /* olive    */
	{ 0xff3e7c74, 0xff28544e, 0xff82b3ac }, /* teal     */
	{ 0xff4a6fa5, 0xff304b73, 0xff8fa9cd }, /* slate    */
	{ 0xff7d5ba6, 0xff543b73, 0xffb09ccb }, /* plum     */
	{ 0xffb06a3b, 0xff7a4623, 0xffd3a37e }, /* rust     */
	{ 0xff4f7a52, 0xff335236, 0xff92b494 }, /* forest   */
};

const CoverHue& coverHue(uint32_t hash) {
	return _hues[hash % (sizeof(_hues)/sizeof(_hues[0]))];
}

uint32_t strHash(const string& s) {
	uint32_t h = 2166136261u;
	for(size_t i = 0; i < s.size(); i++) {
		h ^= (uint8_t)s[i];
		h *= 16777619u;
	}
	return h;
}

/* ---- settings / store --------------------------------------------------- */
Settings::Settings() {
	fontSize = 16;
	theme = THEME_PAPER;
	lineGap = 150;
	shelfDir = SHELF_DEFAULT;
}

void Store::setShelf(const string& dir) {
	settings.shelfDir = dir;
	fname = dir + "/" + STATE_FNAME;
}

void Store::load() {
	if(fname.empty())
		setShelf(SHELF_DEFAULT);
	json_var_t* v = json_parse_file(fname.c_str());
	if(v == NULL)
		return;
	settings.fontSize = json_get_int_def(v, "fontSize", 16);
	settings.theme    = json_get_int_def(v, "theme", THEME_PAPER);
	settings.lineGap  = json_get_int_def(v, "lineGap", 150);
	if(settings.fontSize < 12) settings.fontSize = 12;
	if(settings.fontSize > 40) settings.fontSize = 40;
	if(settings.theme < 0 || settings.theme >= THEME_COUNT)
		settings.theme = THEME_PAPER;
	if(settings.lineGap < 120) settings.lineGap = 120;
	if(settings.lineGap > 220) settings.lineGap = 220;
	json_var_unref(v);
}

void Store::save() {
	if(fname.empty())
		return;
	json_var_t* v = json_var_new_obj(NULL, NULL);
	json_var_add(v, "fontSize", json_var_new_int(settings.fontSize));
	json_var_add(v, "theme", json_var_new_int(settings.theme));
	json_var_add(v, "lineGap", json_var_new_int(settings.lineGap));

	json_var_t* prog = json_var_new_obj(NULL, NULL);
	for(size_t i = 0; i < _progress.size(); i++) {
		json_var_t* e = json_var_new_obj(NULL, NULL);
		json_var_add(e, "page", json_var_new_int(_progress[i].page));
		json_var_add(e, "pct", json_var_new_int(_progress[i].pct));
		json_var_add(prog, _progress[i].name.c_str(), e);
	}
	json_var_add(v, "progress", prog);

	char* s = json_var_to_cstr(v);
	if(s != NULL) {
		FILE* f = fopen(fname.c_str(), "w");
		if(f != NULL) {
			fwrite(s, 1, strlen(s), f);
			fclose(f);
		}
		free(s);
	}
	json_var_unref(v);
}

int Store::progress(const string& bookFname) {
	for(size_t i = 0; i < _progress.size(); i++)
		if(_progress[i].name == bookFname)
			return _progress[i].page;
	/* not in memory: try the file (first call after load) */
	json_var_t* v = json_parse_file(fname.c_str());
	if(v != NULL) {
		json_var_t* prog = json_get_obj(v, "progress");
		if(prog != NULL) {
			json_var_t* p = json_get_obj_member(prog, bookFname.c_str());
			if(p != NULL) {
				Prog e;
				e.name = bookFname;
				e.page = json_get_int_def(p, "page", -1);
				e.pct = json_get_int_def(p, "pct", -1);
				_progress.push_back(e);
				json_var_unref(v);
				return e.page;
			}
		}
		json_var_unref(v);
	}
	return -1;
}

int Store::progressPct(const string& bookFname) {
	for(size_t i = 0; i < _progress.size(); i++)
		if(_progress[i].name == bookFname)
			return _progress[i].pct;
	progress(bookFname); /* loads from file if present */
	for(size_t i = 0; i < _progress.size(); i++)
		if(_progress[i].name == bookFname)
			return _progress[i].pct;
	return -1;
}

void Store::setProgress(const string& bookFname, int page, int pct) {
	for(size_t i = 0; i < _progress.size(); i++) {
		if(_progress[i].name == bookFname) {
			_progress[i].page = page;
			if(pct >= 0)
				_progress[i].pct = pct;
			return;
		}
	}
	Prog e;
	e.name = bookFname;
	e.page = page;
	e.pct = pct;
	_progress.push_back(e);
}

/* ---- helpers ------------------------------------------------------------ */
string fmt(const char* f, ...) {
	char buf[512];
	va_list ap;
	va_start(ap, f);
	vsnprintf(buf, sizeof(buf), f, ap);
	va_end(ap);
	return string(buf);
}

string baseName(const string& path) {
	size_t p = path.rfind('/');
	if(p == string::npos)
		return path;
	return path.substr(p+1);
}

string stripExt(const string& name) {
	size_t p = name.rfind('.');
	if(p == string::npos || p == 0)
		return name;
	return name.substr(0, p);
}

bool endsWith(const string& s, const char* suffix) {
	size_t n = strlen(suffix);
	if(s.size() < n)
		return false;
	for(size_t i = 0; i < n; i++) {
		char a = s[s.size()-n+i];
		char b = suffix[i];
		if(a >= 'A' && a <= 'Z') a += 32;
		if(b >= 'A' && b <= 'Z') b += 32;
		if(a != b)
			return false;
	}
	return true;
}

bool ensureDir(const string& path) {
	struct stat st;
	if(stat(path.c_str(), &st) == 0)
		return S_ISDIR(st.st_mode);
	/* create parents one by one */
	string cur;
	size_t i = 0;
	if(path[0] == '/') { cur = "/"; i = 1; }
	while(i <= path.size()) {
		if(i == path.size() || path[i] == '/') {
			if(!cur.empty() && cur != "/")
				mkdir(cur.c_str(), 0755);
			if(i < path.size())
				cur += "/";
		}
		else
			cur += path[i];
		i++;
	}
	return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool fileExists(const string& path) {
	struct stat st;
	return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

bool copyFile(const string& from, const string& to) {
	int sz = 0;
	uint8_t* data = vfs_readfile(from.c_str(), &sz);
	if(data == NULL)
		return false;
	FILE* f = fopen(to.c_str(), "w");
	if(f == NULL) {
		free(data);
		return false;
	}
	int wr = fwrite(data, 1, sz, f);
	fclose(f);
	free(data);
	return wr == sz;
}

vector<uint32_t> decodeUtf8(const uint8_t* data, int size) {
	vector<uint32_t> out;
	out.reserve(size);
	int i = 0;
	while(i < size) {
		uint8_t b = data[i];
		uint32_t cp = 0xFFFD;
		int need = 0;
		if(b < 0x80) { cp = b; need = 0; }
		else if((b & 0xE0) == 0xC0) { cp = b & 0x1F; need = 1; }
		else if((b & 0xF0) == 0xE0) { cp = b & 0x0F; need = 2; }
		else if((b & 0xF8) == 0xF0) { cp = b & 0x07; need = 3; }
		if(i + need < size) {
			bool ok = true;
			for(int k = 1; k <= need; k++) {
				if((data[i+k] & 0xC0) != 0x80) { ok = false; break; }
				cp = (cp << 6) | (data[i+k] & 0x3F);
			}
			if(ok) {
				if(need == 1 && cp < 0x80) ok = false;
				if(need == 2 && cp < 0x800) ok = false;
				if(need == 3 && cp < 0x10000) ok = false;
				if(cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) ok = false;
			}
			if(ok) {
				out.push_back(cp);
				i += need + 1;
				continue;
			}
		}
		out.push_back(0xFFFD);
		i++;
	}
	return out;
}

int encodeUtf8(uint32_t cp, char* buf) {
	if(cp < 0x80) { buf[0] = (char)cp; return 1; }
	if(cp < 0x800) {
		buf[0] = (char)(0xC0 | (cp >> 6));
		buf[1] = (char)(0x80 | (cp & 0x3F));
		return 2;
	}
	if(cp < 0x10000) {
		buf[0] = (char)(0xE0 | (cp >> 12));
		buf[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
		buf[2] = (char)(0x80 | (cp & 0x3F));
		return 3;
	}
	buf[0] = (char)(0xF0 | (cp >> 18));
	buf[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
	buf[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
	buf[3] = (char)(0x80 | (cp & 0x3F));
	return 4;
}

bool isCjk(uint32_t cp) {
	return (cp >= 0x4E00 && cp <= 0x9FFF) ||
		(cp >= 0x3400 && cp <= 0x4DBF) ||
		(cp >= 0x3000 && cp <= 0x303F) ||
		(cp >= 0xFF00 && cp <= 0xFFEF) ||
		(cp >= 0x3040 && cp <= 0x30FF);
}

bool noLineStart(uint32_t cp) {
	static const uint32_t tbl[] = {
		0x3002,0xFF0C,0x3001,0xFF0E,0xFF1A,0xFF1B,0xFF1F,0xFF01,
		0xFF09,0xFF3D,0xFF5D,0x3009,0x300B,0x300D,0x300F,0x3011,
		0x2026,0x2014,0xFF5E,0x00B7,
		',','.',':',';','?','!',')',']','}','%',
		0x2019,0x201D,0x3015,0x3017,0x3019,0x301B,
	};
	for(size_t i = 0; i < sizeof(tbl)/sizeof(tbl[0]); i++)
		if(tbl[i] == cp) return true;
	return false;
}

bool noLineEnd(uint32_t cp) {
	static const uint32_t tbl[] = {
		0xFF08,0xFF3B,0xFF5B,0x3008,0x300A,0x300C,0x300E,0x3010,
		'(','[','{',0x2018,0x201C,0x3014,0x3016,0x3018,0x301A,
	};
	for(size_t i = 0; i < sizeof(tbl)/sizeof(tbl[0]); i++)
		if(tbl[i] == cp) return true;
	return false;
}

/* ---- drawing helpers ------------------------------------------------------ */
void graphVGradient(graph_t* g, int x, int y, int w, int h,
		uint32_t top, uint32_t bottom) {
	if(w <= 0 || h <= 0) return;
	int tr = color_r(top), tg = color_g(top), tb = color_b(top);
	int dr = color_r(bottom) - tr, dg = color_g(bottom) - tg, db = color_b(bottom) - tb;
	for(int j = 0; j < h; j++) {
		uint32_t c = 0xff000000 |
			((uint32_t)(tr + dr*j/h) << 16) |
			((uint32_t)(tg + dg*j/h) << 8) |
			(uint32_t)(tb + db*j/h);
		graph_fill_rect(g, x, y+j, w, 1, c);
	}
}

void graphShadowRound(graph_t* g, int x, int y, int w, int h,
		int radius, uint32_t color, uint32_t shadow) {
	/* two soft-ish shadow layers, then the card itself */
	graph_fill_round(g, x+2, y+4, w, h, radius, shadow);
	graph_fill_round(g, x+1, y+2, w, h, radius, shadow);
	graph_fill_round(g, x, y, w, h, radius, color);
}

}