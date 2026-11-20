#ifndef EREADER_COMMON_H
#define EREADER_COMMON_H

#include <stdint.h>
#include <string>
#include <vector>
#include <graph/graph.h>

using namespace std;

namespace er {

/* ---- reading themes: warm paper / sepia / night ---------------------- */
enum {
	THEME_PAPER = 0,
	THEME_SEPIA,
	THEME_NIGHT,
	THEME_COUNT
};

struct Palette {
	uint32_t bg;      /* desk around the page          */
	uint32_t page;    /* paper                         */
	uint32_t ink;     /* body text                     */
	uint32_t faint;   /* secondary text                */
	uint32_t accent;  /* amber highlight / progress    */
	uint32_t bar;     /* toolbar surface               */
	uint32_t barInk;  /* toolbar text                  */
	uint32_t line;    /* hairline                      */
	uint32_t cover0;  /* cover gradient top            */
	uint32_t cover1;  /* cover gradient bottom         */
};

const Palette& palette(int theme);
const char* themeName(int theme);

/* ---- book cover hues (derived from title hash) ------------------------ */
struct CoverHue {
	uint32_t base;
	uint32_t dark;
	uint32_t light;
};
const CoverHue& coverHue(uint32_t hash);
uint32_t strHash(const string& s);

/* ---- settings + per-book progress, persisted as JSON ------------------ */
struct Settings {
	int fontSize;   /* logical px, 12..40 (hidpi-scaled when rendering) */
	int theme;      /* THEME_*             */
	int lineGap;    /* line height percent */
	string shelfDir;

	Settings();
};

class Store {
	string fname;
	struct Prog { string name; int page; int pct; };
	vector<Prog> _progress;
public:
	Settings settings;

	void setShelf(const string& dir);
	void load();
	void save();

	int  progress(const string& bookFname);          /* saved page index, -1 none */
	int  progressPct(const string& bookFname);       /* saved percent, -1 none */
	void setProgress(const string& bookFname, int page, int pct);
};

/* ---- small helpers ----------------------------------------------------- */
string fmt(const char* f, ...);
string baseName(const string& path);
string stripExt(const string& name);
bool   endsWith(const string& s, const char* suffix); /* case-insensitive */
bool   ensureDir(const string& path);
bool   copyFile(const string& from, const string& to);
bool   fileExists(const string& path);

/* decode a whole buffer as UTF-8 (invalid bytes become U+FFFD) */
vector<uint32_t> decodeUtf8(const uint8_t* data, int size);
/* encode one codepoint, returns bytes written (buf >= 4) */
int encodeUtf8(uint32_t cp, char* buf);

bool isCjk(uint32_t cp);
/* characters that must not start / end a line (CJK typesetting) */
bool noLineStart(uint32_t cp);
bool noLineEnd(uint32_t cp);

/* vertical gradient fill */
void graphVGradient(graph_t* g, int x, int y, int w, int h,
		uint32_t top, uint32_t bottom);
/* rounded rect with soft drop shadow (shadow drawn first, offset down) */
void graphShadowRound(graph_t* g, int x, int y, int w, int h,
		int radius, uint32_t color, uint32_t shadow);

extern const char* SHELF_DEFAULT;   /* "/data/books" */
extern const char* STATE_FNAME;     /* ".ereader.json" inside shelf dir */

}

#endif