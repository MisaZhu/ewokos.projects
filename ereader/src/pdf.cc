#include "pdf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <zlib.h>
#include <font/font.h>
#include <graph/graph_jpeg.h>
#include <ewoksys/vfs.h>
#include <unordered_map>
#include <algorithm>

namespace er {

/* ============================ object model ============================ */
enum OType {
	O_NULL = 0, O_BOOL, O_INT, O_REAL, O_STR, O_NAME,
	O_ARR, O_DICT, O_STREAM, O_REF
};

struct Obj {
	OType t;
	int64_t i;
	double r;
	string s;                 /* O_STR bytes / O_NAME text        */
	vector<Obj> a;
	/* EWOK_STL map() default-constructs a nil node holding pair<K,V>,
	   so a map<string,Obj> member would recurse Obj() -> map() -> Obj()
	   forever.  PDF dicts are tiny; a flat pair vector is recursion-free
	   (vector's ctor only allocates, it never constructs elements). */
	vector<pair<string, Obj> > d;
	const uint8_t* raw;       /* O_STREAM: unfiltered bytes       */
	int rawLen;
	int onum, ogen;           /* owning indirect object (crypto key) */

	Obj() : t(O_NULL), i(0), r(0), raw(NULL), rawLen(0), onum(0), ogen(0) { }

	bool isNum() const { return t == O_INT || t == O_REAL; }
	double num() const {
		if(t == O_INT) return (double)i;
		if(t == O_REAL) return r;
		return 0;
	}
	const Obj* get(const char* k) const {
		if(t != O_DICT && t != O_STREAM) return NULL;
		for(size_t i = 0; i < d.size(); i++)
			if(d[i].first == k) return &d[i].second;
		return NULL;
	}
	double getNum(const char* k, double def) const {
		const Obj* o = get(k);
		return (o != NULL && o->isNum()) ? o->num() : def;
	}
	string getName(const char* k) const {
		const Obj* o = get(k);
		return (o != NULL && o->t == O_NAME) ? o->s : string();
	}
	string getStr(const char* k) const {
		const Obj* o = get(k);
		return (o != NULL && o->t == O_STR) ? o->s : string();
	}
};

/* map-like insert-or-assign on the flat dict vector */
static Obj& dset(Obj& o, const string& k) {
	for(size_t i = 0; i < o.d.size(); i++)
		if(o.d[i].first == k) return o.d[i].second;
	o.d.push_back(pair<string, Obj>(k, Obj()));
	return o.d.back().second;
}

/* ============================== lexer ================================= */
static inline bool ws(uint8_t c) {
	return c == 0 || c == 9 || c == 10 || c == 12 || c == 13 || c == 32;
}
static inline bool delim(uint8_t c) {
	return c=='(' || c==')' || c=='<' || c=='>' || c=='[' || c==']' ||
		c=='{' || c=='}' || c=='/' || c=='%';
}

struct Lex {
	const uint8_t* b;
	int n, p;
	Lex(const uint8_t* b_, int n_, int p_) : b(b_), n(n_), p(p_) { }

	void skip() {
		while(p < n) {
			if(ws(b[p])) { p++; continue; }
			if(b[p] == '%') {
				while(p < n && b[p] != 10 && b[p] != 13) p++;
				continue;
			}
			break;
		}
	}
	bool kw(const char* k) {
		int len = (int)strlen(k);
		if(p + len > n) return false;
		if(memcmp(b + p, k, len) != 0) return false;
		if(p + len < n && !ws(b[p+len]) && !delim(b[p+len])) return false;
		p += len;
		return true;
	}
	string name() {
		string s;
		p++; /* '/' */
		while(p < n && !ws(b[p]) && !delim(b[p])) {
			if(b[p] == '#' && p + 2 < n) {
				int hi = hexVal(b[p+1]), lo = hexVal(b[p+2]);
				if(hi >= 0 && lo >= 0) {
					s += (char)((hi << 4) | lo);
					p += 3;
					continue;
				}
			}
			s += (char)b[p++];
		}
		return s;
	}
	static int hexVal(uint8_t c) {
		if(c >= '0' && c <= '9') return c - '0';
		if(c >= 'a' && c <= 'f') return c - 'a' + 10;
		if(c >= 'A' && c <= 'F') return c - 'A' + 10;
		return -1;
	}
	string litString() {
		string s;
		p++; /* '(' */
		int depth = 1;
		while(p < n && depth > 0) {
			uint8_t c = b[p++];
			if(c == '(') { depth++; s += '('; }
			else if(c == ')') { depth--; if(depth > 0) s += ')'; }
			else if(c == '\\') {
				if(p >= n) break;
				uint8_t e = b[p++];
				switch(e) {
				case 'n': s += '\n'; break;
				case 'r': s += '\r'; break;
				case 't': s += '\t'; break;
				case 'b': s += '\b'; break;
				case 'f': s += '\f'; break;
				case '(': s += '('; break;
				case ')': s += ')'; break;
				case '\\': s += '\\'; break;
				case 10: break;
				case 13: if(p < n && b[p] == 10) p++; break;
				default:
					if(e >= '0' && e <= '7') {
						int v = e - '0';
						for(int k = 0; k < 2 && p < n && b[p] >= '0' && b[p] <= '7'; k++)
							v = v*8 + (b[p++] - '0');
						s += (char)v;
					}
					else s += (char)e;
				}
			}
			else s += (char)c;
		}
		return s;
	}
	string hexString() {
		string s;
		p++; /* '<' */
		int hi = -1;
		while(p < n && b[p] != '>') {
			int v = hexVal(b[p]);
			if(v >= 0) {
				if(hi < 0) hi = v;
				else { s += (char)((hi << 4) | v); hi = -1; }
			}
			p++;
		}
		if(hi >= 0) s += (char)(hi << 4);
		if(p < n) p++;
		return s;
	}
	Obj number() {
		int start = p;
		if(p < n && (b[p] == '+' || b[p] == '-')) p++;
		bool real = false;
		while(p < n) {
			uint8_t c = b[p];
			if(c >= '0' && c <= '9') { p++; }
			else if(c == '.') { real = true; p++; }
			else break;
		}
		string s((const char*)b + start, p - start);
		Obj o;
		if(real) { o.t = O_REAL; o.r = atof(s.c_str()); }
		else { o.t = O_INT; o.i = strtoll(s.c_str(), NULL, 10); }
		return o;
	}
	Obj parse(int depth);
};

Obj Lex::parse(int depth) {
	Obj o;
	if(depth > 48) return o;
	skip();
	if(p >= n) return o;
	uint8_t c = b[p];
	if(c == '/') { o.t = O_NAME; o.s = name(); return o; }
	if(c == '(') { o.t = O_STR; o.s = litString(); return o; }
	if(c == '<') {
		if(p + 1 < n && b[p+1] == '<') {
			p += 2;
			o.t = O_DICT;
			while(true) {
				skip();
				if(p >= n) break;
				if(b[p] == '>' && p + 1 < n && b[p+1] == '>') { p += 2; break; }
				if(b[p] != '/') { p++; continue; }
				string key = name();
				Obj v = parse(depth + 1);
				dset(o, key) = v;
			}
			return o;
		}
		o.t = O_STR; o.s = hexString(); return o;
	}
	if(c == '[') {
		p++;
		o.t = O_ARR;
		while(true) {
			skip();
			if(p >= n) break;
			if(b[p] == ']') { p++; break; }
			int before = p;
			Obj v = parse(depth + 1);
			if(p == before) { p++; continue; }
			o.a.push_back(v);
		}
		return o;
	}
	if(c == ']' || c == '>' || c == ')') { p++; return o; }
	if((c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.') {
		o = number();
		/* "N G R" reference */
		int save = p;
		skip();
		if(p < n && b[p] >= '0' && b[p] <= '9') {
			Obj g = number();
			skip();
			if(p < n && b[p] == 'R' &&
					(p + 1 >= n || ws(b[p+1]) || delim(b[p+1]))) {
				p++;
				Obj ref;
				ref.t = O_REF;
				ref.i = o.i;
				ref.r = g.i;
				return ref;
			}
		}
		p = save;
		return o;
	}
	if(kw("true"))  { o.t = O_BOOL; o.i = 1; return o; }
	if(kw("false")) { o.t = O_BOOL; o.i = 0; return o; }
	if(kw("null"))  { return o; }
	/* bare keyword (obj / endobj / stream / R / garbage): consume token */
	while(p < n && !ws(b[p]) && !delim(b[p])) p++;
	return o;
}

/* ============================= filters ================================ */
static bool flateDecode(const uint8_t* in, int n, vector<uint8_t>& out) {
	if(n <= 0) return false;
	uLongf cap = (uLongf)n * 4 + 1024;
	for(int tries = 0; tries < 12; tries++) {
		out.resize(cap);
		uLongf dl = cap;
		int res = uncompress(out.data(), &dl, in, (uLong)n);
		if(res == Z_OK) { out.resize(dl); return true; }
		if(res != Z_BUF_ERROR) return false;
		cap *= 2;
		if(cap > 64u*1024*1024) return false;
	}
	return false;
}

static void pngUnfilter(vector<uint8_t>& data, int colors, int bpc, int columns) {
	int bpp = (colors * bpc + 7) / 8;
	if(bpp < 1) bpp = 1;
	int rowLen = (columns * colors * bpc + 7) / 8;
	int stride = rowLen + 1;
	if((int)data.size() < stride) return;
	vector<uint8_t> out(rowLen * ((data.size() - 1) / stride));
	size_t op = 0;
	for(size_t i = 0; i + stride <= data.size(); i += stride) {
		int ft = data[i];
		const uint8_t* row = &data[i+1];
		uint8_t* cur = &out[op];
		const uint8_t* prev = (op >= (size_t)rowLen) ? &out[op - rowLen] : NULL;
		for(int x = 0; x < rowLen; x++) {
			int a = (x >= bpp) ? cur[x - bpp] : 0;
			int b = prev ? prev[x] : 0;
			int c = (prev && x >= bpp) ? prev[x - bpp] : 0;
			int v = row[x];
			switch(ft) {
			case 0: break;
			case 1: v += a; break;
			case 2: v += b; break;
			case 3: v += (a + b) / 2; break;
			case 4: {
				int pp = a + b - c;
				int pa = abs(pp - a), pb = abs(pp - b), pc = abs(pp - c);
				v += (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
				break;
			}
			default: break;
			}
			cur[x] = (uint8_t)v;
		}
		op += rowLen;
	}
	out.resize(op);
	data.swap(out);
}

static void tiffUnfilter(vector<uint8_t>& data, int colors, int columns) {
	if(colors <= 0) return;
	int rowLen = columns * colors;
	for(size_t i = 0; i + rowLen <= data.size(); i += rowLen)
		for(int x = colors; x < rowLen; x++)
			data[i+x] = (uint8_t)(data[i+x] + data[i+x-colors]);
}

static bool asciiHexDecode(const uint8_t* in, int n, vector<uint8_t>& out) {
	out.clear();
	int hi = -1;
	for(int i = 0; i < n; i++) {
		if(in[i] == '>') break;
		int v = Lex::hexVal(in[i]);
		if(v < 0) continue;
		if(hi < 0) hi = v;
		else { out.push_back((uint8_t)((hi << 4) | v)); hi = -1; }
	}
	if(hi >= 0) out.push_back((uint8_t)(hi << 4));
	return true;
}

static bool ascii85Decode(const uint8_t* in, int n, vector<uint8_t>& out) {
	out.clear();
	uint32_t acc = 0;
	int cnt = 0;
	for(int i = 0; i < n; i++) {
		uint8_t c = in[i];
		if(c == '~') break;
		if(ws(c)) continue;
		if(c == 'z' && cnt == 0) {
			out.push_back(0); out.push_back(0); out.push_back(0); out.push_back(0);
			continue;
		}
		if(c < '!' || c > 'u') continue;
		acc = acc * 85 + (c - '!');
		cnt++;
		if(cnt == 5) {
			for(int k = 3; k >= 0; k--) out.push_back((uint8_t)(acc >> (k*8)));
			acc = 0; cnt = 0;
		}
	}
	if(cnt > 0) {
		for(int k = cnt; k < 5; k++) acc = acc * 85 + 84;
		for(int k = 3; k >= 5 - cnt; k--) out.push_back((uint8_t)(acc >> (k*8)));
	}
	return true;
}

static bool runLengthDecode(const uint8_t* in, int n, vector<uint8_t>& out) {
	out.clear();
	int i = 0;
	while(i < n) {
		int len = in[i++];
		if(len == 128) break;
		if(len < 128) {
			for(int k = 0; k <= len && i < n; k++) out.push_back(in[i++]);
		}
		else {
			if(i >= n) break;
			uint8_t v = in[i++];
			for(int k = 0; k < 257 - len; k++) out.push_back(v);
		}
	}
	return true;
}

/* ====================== standard security handler =====================
   Empty-user-password documents (the common "restricted" PDFs): derive
   the file key from the trailer, verify U, then RC4/AESV2-decrypt every
   string and stream with its per-object key. Self-contained MD5/RC4/AES
   so the reader needs no crypto service on the device. */
struct Md5 {
	uint32_t h[4];
	uint64_t len;
	uint8_t blk[64];
	int fill;
	Md5() { reset(); }
	void reset() {
		h[0]=0x67452301; h[1]=0xefcdab89; h[2]=0x98badcfe; h[3]=0x10325476;
		len = 0; fill = 0;
	}
	static uint32_t rol(uint32_t x, int c) { return (x << c) | (x >> (32-c)); }
	void block(const uint8_t* p) {
		static const uint32_t K[64] = {
			0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,
			0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,0x6b901122,0xfd987193,0xa679438e,0x49b40821,
			0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,
			0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,
			0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,
			0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,
			0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,
			0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391 };
		static const int S[64] = {
			7,12,17,22,7,12,17,22,7,12,17,22,7,12,17,22,
			5,9,14,20,5,9,14,20,5,9,14,20,5,9,14,20,
			4,11,16,23,4,11,16,23,4,11,16,23,4,11,16,23,
			6,10,15,21,6,10,15,21,6,10,15,21,6,10,15,21 };
		uint32_t m[16];
		for(int i = 0; i < 16; i++)
			m[i] = (uint32_t)p[i*4] | ((uint32_t)p[i*4+1]<<8) |
				((uint32_t)p[i*4+2]<<16) | ((uint32_t)p[i*4+3]<<24);
		uint32_t a=h[0], b=h[1], c=h[2], d=h[3];
		for(int i = 0; i < 64; i++) {
			uint32_t f; int g;
			if(i < 16)      { f = (b & c) | (~b & d); g = i; }
			else if(i < 32) { f = (d & b) | (~d & c); g = (5*i+1) & 15; }
			else if(i < 48) { f = b ^ c ^ d;          g = (3*i+5) & 15; }
			else            { f = c ^ (b | ~d);       g = (7*i) & 15; }
			uint32_t tmp = d; d = c; c = b;
			b = b + rol(a + f + K[i] + m[g], S[i]);
			a = tmp;
		}
		h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d;
	}
	void update(const uint8_t* p, int n) {
		len += (uint64_t)n;
		while(n > 0) {
			int take = 64 - fill;
			if(take > n) take = n;
			memcpy(blk + fill, p, take);
			fill += take; p += take; n -= take;
			if(fill == 64) { block(blk); fill = 0; }
		}
	}
	void final(uint8_t out[16]) {
		uint64_t bits = len * 8;
		uint8_t pad = 0x80;
		update(&pad, 1);
		pad = 0;
		while(fill != 56) update(&pad, 1);
		uint8_t lb[8];
		for(int i = 0; i < 8; i++) lb[i] = (uint8_t)(bits >> (8*i));
		update(lb, 8);
		for(int i = 0; i < 4; i++)
			for(int k = 0; k < 4; k++)
				out[i*4+k] = (uint8_t)(h[i] >> (8*k));
	}
};

static void md5Buf(const uint8_t* p, int n, uint8_t out[16]) {
	Md5 m; m.update(p, n); m.final(out);
}

static void rc4Crypt(const uint8_t* key, int kn, uint8_t* data, int n) {
	uint8_t s[256];
	for(int i = 0; i < 256; i++) s[i] = (uint8_t)i;
	int j = 0;
	for(int i = 0; i < 256; i++) {
		j = (j + s[i] + key[i % kn]) & 255;
		uint8_t t = s[i]; s[i] = s[j]; s[j] = t;
	}
	int i = 0; j = 0;
	for(int k = 0; k < n; k++) {
		i = (i + 1) & 255;
		j = (j + s[i]) & 255;
		uint8_t t = s[i]; s[i] = s[j]; s[j] = t;
		data[k] ^= s[(s[i] + s[j]) & 255];
	}
}

/* AES-128 (encrypt for the U check, decrypt for streams) */
static const uint8_t AES_SBOX[256] = {
	0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
	0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
	0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
	0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
	0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
	0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
	0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
	0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
	0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
	0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
	0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
	0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
	0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
	0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
	0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
	0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16 };
static uint8_t AES_RSBOX[256];
static bool aesTablesInit = false;
static void aesInit() {
	if(aesTablesInit) return;
	for(int i = 0; i < 256; i++) AES_RSBOX[AES_SBOX[i]] = (uint8_t)i;
	aesTablesInit = true;
}
static uint8_t xtime(uint8_t x) { return (uint8_t)((x << 1) ^ ((x & 0x80) ? 0x1b : 0)); }
static uint8_t gmul(uint8_t a, uint8_t b) {
	uint8_t r = 0;
	for(int i = 0; i < 8; i++) {
		if(b & 1) r ^= a;
		b >>= 1;
		a = xtime(a);
	}
	return r;
}
static void aesExpand(const uint8_t key[16], uint8_t rk[176]) {
	memcpy(rk, key, 16);
	uint8_t rcon = 1;
	for(int i = 16; i < 176; i += 4) {
		uint8_t t[4];
		memcpy(t, rk + i - 4, 4);
		if(i % 16 == 0) {
			uint8_t tmp = t[0];
			t[0] = AES_SBOX[t[1]] ^ rcon; t[1] = AES_SBOX[t[2]];
			t[2] = AES_SBOX[t[3]]; t[3] = AES_SBOX[tmp];
			rcon = xtime(rcon);
		}
		for(int k = 0; k < 4; k++) rk[i+k] = rk[i-16+k] ^ t[k];
	}
}
static void aesEncBlock(const uint8_t rk[176], uint8_t b[16]) {
	for(int k = 0; k < 16; k++) b[k] ^= rk[k];
	for(int round = 1; round <= 10; round++) {
		for(int k = 0; k < 16; k++) b[k] = AES_SBOX[b[k]];
		uint8_t t;
		t=b[1]; b[1]=b[5]; b[5]=b[9]; b[9]=b[13]; b[13]=t;
		t=b[2]; b[2]=b[10]; b[10]=t; t=b[6]; b[6]=b[14]; b[14]=t;
		t=b[15]; b[15]=b[11]; b[11]=b[7]; b[7]=b[3]; b[3]=t;
		if(round < 10) {
			for(int c = 0; c < 4; c++) {
				uint8_t* p = b + c*4;
				uint8_t a0=p[0], a1=p[1], a2=p[2], a3=p[3];
				p[0] = xtime(a0) ^ (xtime(a1)^a1) ^ a2 ^ a3;
				p[1] = a0 ^ xtime(a1) ^ (xtime(a2)^a2) ^ a3;
				p[2] = a0 ^ a1 ^ xtime(a2) ^ (xtime(a3)^a3);
				p[3] = (xtime(a0)^a0) ^ a1 ^ a2 ^ xtime(a3);
			}
		}
		for(int k = 0; k < 16; k++) b[k] ^= rk[round*16+k];
	}
}
static void aesDecBlock(const uint8_t rk[176], uint8_t b[16]) {
	for(int k = 0; k < 16; k++) b[k] ^= rk[160+k];
	for(int round = 9; round >= 0; round--) {
		uint8_t t;
		t=b[13]; b[13]=b[9]; b[9]=b[5]; b[5]=b[1]; b[1]=t;
		t=b[10]; b[10]=b[2]; b[2]=t; t=b[14]; b[14]=b[6]; b[6]=t;
		t=b[3]; b[3]=b[7]; b[7]=b[11]; b[11]=b[15]; b[15]=t;
		for(int k = 0; k < 16; k++) b[k] = AES_RSBOX[b[k]];
		for(int k = 0; k < 16; k++) b[k] ^= rk[round*16+k];
		if(round > 0) {
			for(int c = 0; c < 4; c++) {
				uint8_t* p = b + c*4;
				uint8_t a0=p[0], a1=p[1], a2=p[2], a3=p[3];
				p[0] = gmul(a0,14) ^ gmul(a1,11) ^ gmul(a2,13) ^ gmul(a3,9);
				p[1] = gmul(a0,9) ^ gmul(a1,14) ^ gmul(a2,11) ^ gmul(a3,13);
				p[2] = gmul(a0,13) ^ gmul(a1,9) ^ gmul(a2,14) ^ gmul(a3,11);
				p[3] = gmul(a0,11) ^ gmul(a1,13) ^ gmul(a2,9) ^ gmul(a3,14);
			}
		}
	}
}
/* AES-128-CBC decrypt; first 16 bytes of in are the IV, PKCS#7 stripped */
static bool aesCbcDecrypt(const uint8_t key[16], const uint8_t* in, int n,
		vector<uint8_t>& out) {
	if(n < 32 || (n & 15) != 0) return false;
	aesInit();
	uint8_t rk[176];
	aesExpand(key, rk);
	out.resize(n - 16);
	uint8_t prev[16];
	memcpy(prev, in, 16);
	for(int off = 16; off < n; off += 16) {
		uint8_t b[16];
		memcpy(b, in + off, 16);
		uint8_t ct[16];
		memcpy(ct, b, 16);
		aesDecBlock(rk, b);
		for(int k = 0; k < 16; k++) b[k] ^= prev[k];
		memcpy(out.data() + off - 16, b, 16);
		memcpy(prev, ct, 16);
	}
	int pad = out.empty() ? 0 : out.back();
	if(pad >= 1 && pad <= 16 && pad <= (int)out.size())
		out.resize(out.size() - pad);
	return true;
}

static const uint8_t PDF_PAD[32] = {
	0x28,0xBF,0x4E,0x5E,0x4E,0x75,0x8A,0x41,0x64,0x00,0x4E,0x56,0xFF,0xFA,0x01,0x08,
	0x2E,0x2E,0x00,0xB6,0xD0,0x68,0x3E,0x80,0x2F,0x0C,0xA9,0xFE,0x64,0x53,0x69,0x7A };

/* ===================== CCITT Group 4 (fax) decoder =====================
   Scanned books store pages as 1-bit G4 images. Self-contained decoder
   producing packed MSB-first rows (0 = white, 1 = black). */
struct G4Bits {
	const uint8_t* p;
	int n, pos;
	G4Bits(const uint8_t* p_, int n_) : p(p_), n(n_), pos(0) { }
	int bit() {
		if(pos >= n * 8) return -1;
		int b = (p[pos >> 3] >> (7 - (pos & 7))) & 1;
		pos++;
		return b;
	}
	int peek(int k) {
		if(pos + k > n * 8) return -1;
		int v = 0;
		for(int i = 0; i < k; i++) v = (v << 1) | ((p[(pos+i) >> 3] >> (7 - ((pos+i) & 7))) & 1);
		return v;
	}
};
struct G4Code { uint16_t code; uint8_t len; uint16_t val; };
/* vertical mode: value = delta + 3; codes V0=1, VL1=010, VR1=011,
   VL2=000010, VR2=000011, VL3=0000010, VR3=0000011 */
static const G4Code G4_V[7] = {
	{0x1,1,3},{0x2,3,2},{0x3,3,4},{0x2,6,1},{0x3,6,5},{0x2,7,0},{0x3,7,6}
};
static const G4Code G4_WT[64] = {
	{0x35,8,0},{0x7,6,1},{0x7,4,2},{0x8,4,3},{0xB,4,4},{0xC,4,5},{0xE,4,6},{0xF,4,7},
	{0x13,5,8},{0x14,5,9},{0x7,5,10},{0x8,5,11},{0x8,6,12},{0x3,6,13},{0x34,6,14},{0x35,6,15},
	{0x2A,6,16},{0x2B,6,17},{0x27,7,18},{0xC,7,19},{0x8,7,20},{0x17,7,21},{0x3,7,22},{0x4,7,23},
	{0x28,7,24},{0x2B,7,25},{0x13,7,26},{0x24,7,27},{0x18,7,28},{0x2,8,29},{0x3,8,30},{0x1A,8,31},
	{0x1B,8,32},{0x12,8,33},{0x13,8,34},{0x14,8,35},{0x15,8,36},{0x16,8,37},{0x17,8,38},{0x28,8,39},
	{0x29,8,40},{0x2A,8,41},{0x2B,8,42},{0x2C,8,43},{0x2D,8,44},{0x4,8,45},{0x5,8,46},{0xA,8,47},
	{0xB,8,48},{0x52,8,49},{0x53,8,50},{0x54,8,51},{0x55,8,52},{0x24,8,53},{0x25,8,54},{0x58,8,55},
	{0x59,8,56},{0x5A,8,57},{0x5B,8,58},{0x4A,8,59},{0x4B,8,60},{0x32,8,61},{0x33,8,62},{0x34,8,63}
};
static const G4Code G4_BT[64] = {
	{0x37,10,0},{0x2,3,1},{0x3,2,2},{0x2,2,3},{0x3,3,4},{0x3,4,5},{0x2,4,6},{0x3,5,7},
	{0x5,6,8},{0x4,6,9},{0x4,7,10},{0x5,7,11},{0x7,7,12},{0x4,8,13},{0x7,8,14},{0x18,9,15},
	{0x17,10,16},{0x18,10,17},{0x8,10,18},{0x67,11,19},{0x68,11,20},{0x6C,11,21},{0x37,11,22},{0x28,11,23},
	{0x17,11,24},{0x18,11,25},{0xCA,12,26},{0xCB,12,27},{0xCC,12,28},{0xCD,12,29},{0x68,12,30},{0x69,12,31},
	{0x6A,12,32},{0x6B,12,33},{0xD2,12,34},{0xD3,12,35},{0xD4,12,36},{0xD5,12,37},{0xD6,12,38},{0xD7,12,39},
	{0x6C,12,40},{0x6D,12,41},{0xDA,12,42},{0xDB,12,43},{0x54,12,44},{0x55,12,45},{0x56,12,46},{0x57,12,47},
	{0x64,12,48},{0x65,12,49},{0x52,12,50},{0x53,12,51},{0x24,12,52},{0x37,12,53},{0x38,12,54},{0x27,12,55},
	{0x28,12,56},{0x58,12,57},{0x59,12,58},{0x2B,12,59},{0x2C,12,60},{0x5A,12,61},{0x66,12,62},{0x67,12,63}
};
static const G4Code G4_WM[40] = {
	{0x1B,5,64},{0x12,5,128},{0x17,6,192},{0x37,7,256},{0x36,8,320},{0x37,8,384},{0x64,8,448},{0x65,8,512},
	{0x68,8,576},{0x67,8,640},{0xCC,9,704},{0xCD,9,768},{0xD2,9,832},{0xD3,9,896},{0xD4,9,960},{0xD5,9,1024},
	{0xD6,9,1088},{0xD7,9,1152},{0xD8,9,1216},{0xD9,9,1280},{0xDA,9,1344},{0xDB,9,1408},{0x98,9,1472},{0x99,9,1536},
	{0x9A,9,1600},{0x18,6,1664},{0x9B,9,1728},{0x8,11,1792},{0xC,11,1856},{0xD,11,1920},{0x12,12,1984},{0x13,12,2048},
	{0x14,12,2112},{0x15,12,2176},{0x16,12,2240},{0x17,12,2304},{0x1C,12,2368},{0x1D,12,2432},{0x1E,12,2496},{0x1F,12,2560}
};
static const G4Code G4_BM[40] = {
	{0xF,10,64},{0xC8,12,128},{0xC9,12,192},{0x5B,12,256},{0x33,12,320},{0x34,12,384},{0x35,12,448},{0x6C,13,512},
	{0x6D,13,576},{0x4A,13,640},{0x4B,13,704},{0x4C,13,768},{0x4D,13,832},{0x72,13,896},{0x73,13,960},{0x74,13,1024},
	{0x75,13,1088},{0x76,13,1152},{0x77,13,1216},{0x52,13,1280},{0x53,13,1344},{0x54,13,1408},{0x55,13,1472},{0x5A,13,1536},
	{0x5B,13,1600},{0x64,13,1664},{0x65,13,1728},{0x8,11,1792},{0xC,11,1856},{0xD,11,1920},{0x12,12,1984},{0x13,12,2048},
	{0x14,12,2112},{0x15,12,2176},{0x16,12,2240},{0x17,12,2304},{0x1C,12,2368},{0x1D,12,2432},{0x1E,12,2496},{0x1F,12,2560}
};
static int g4Run(G4Bits& br, const G4Code* term, const G4Code* makeup) {
	int total = 0;
	for(;;) {
		int run = -1;
		for(int k = 2; k <= 13 && run < 0; k++) {
			int v = br.peek(k);
			if(v < 0) return -1;
			for(int i = 0; i < 64; i++)
				if(term[i].len == k && term[i].code == v) { run = term[i].val; br.pos += k; break; }
			if(run < 0)
				for(int i = 0; i < 40; i++)
					if(makeup[i].len == k && makeup[i].code == v) { run = makeup[i].val; br.pos += k; break; }
		}
		if(run < 0) return -1;
		total += run;
		if(run < 64) return total;
		if(total > 100000) return -1;
	}
}
static bool ccittG4Decode(const uint8_t* in, int n, int w, int h,
		bool blackIs1, vector<uint8_t>& out) {
#ifdef G4_DEBUG
#define G4FAIL(msg) do { fprintf(stderr, "G4 fail y=%d a0=%d color=%d bitpos=%d: %s\n", y, a0, color, br.pos, msg); return false; } while(0)
#else
#define G4FAIL(msg) return false
#endif
	if(w <= 0 || h <= 0 || w > 8192 || h > 8192) return false;
	int stride = (w + 7) / 8;
	out.assign((size_t)stride * h, 0);
	G4Bits br(in, n);
	vector<int> ref, cur;
	/* imaginary b->w change at w; real changes alternate w->b first, so
	   the trailing sentinel must keep odd-index = b->w parity */
	ref.push_back(w);
	for(int y = 0; y < h; y++) {
		cur.clear();
		int a0 = -1, color = 0;
		uint8_t* row = out.data() + (size_t)y * stride;
		for(int guard = 0; guard < w * 4 + 64; guard++) {
			/* b1: first ref change of the current colour right of a0;
			   b2: the next one after b1 */
			size_t bi = 0;
			while(bi < ref.size() && ref[bi] <= a0) bi++;
			/* changes alternate w->b (even index), b->w (odd index);
			   b1 must be a change to the colour opposite to a0's */
			if(color == 0 && bi < ref.size() && (bi & 1) == 1) bi++;
			if(color == 1 && bi < ref.size() && (bi & 1) == 0) bi++;
			int b1 = (bi < ref.size()) ? ref[bi] : w;
			int b2 = (bi + 1 < ref.size()) ? ref[bi+1] : w;
			if(br.peek(1) < 0) G4FAIL("eof");
			/* mode codes: V0=1 VR1=011 VL1=010 H=001 P=0001,
			   V2/V3 are 6/7 bits */
			int d = 0;
			bool vert = false;
			for(int i = 0; i < 7 && !vert; i++)
				if(br.peek(G4_V[i].len) == G4_V[i].code) {
					d = G4_V[i].val - 3;
					br.pos += G4_V[i].len;
					vert = true;
				}
			if(vert) {
				int a1 = b1 + d;
				if(a1 > w) a1 = w;
				if(a1 < 0 || a1 < a0) G4FAIL("vrange");
				if(color == 1)
					for(int x = (a0 < 0 ? 0 : a0); x < a1; x++)
						row[x >> 3] |= (uint8_t)(0x80 >> (x & 7));
				if(a1 < w) cur.push_back(a1);
				a0 = a1;
				color ^= 1;
			}
			else {
				if(br.peek(4) == 1) { /* pass */
					br.pos += 4;
					if(color == 1)
						for(int x = (a0 < 0 ? 0 : a0); x < b2; x++)
							row[x >> 3] |= (uint8_t)(0x80 >> (x & 7));
					a0 = b2;
				}
				else if(br.peek(3) == 1) { /* horizontal */
					br.pos += 3;
					int r1 = g4Run(br, color ? G4_BT : G4_WT, color ? G4_BM : G4_WM);
					int r2 = g4Run(br, color ? G4_WT : G4_BT, color ? G4_WM : G4_BM);
					if(r1 < 0 || r2 < 0) G4FAIL("hrun");
					int a1 = (a0 < 0 ? 0 : a0) + r1;
					int a2 = a1 + r2;
					if(a1 > w || a2 > w) G4FAIL("hrange");
					{
						/* the black run is the first one only when a0 is black */
						int x0 = color ? (a0 < 0 ? 0 : a0) : a1;
						int x1 = color ? a1 : a2;
						for(int x = x0; x < x1; x++)
							row[x >> 3] |= (uint8_t)(0x80 >> (x & 7));
					}
					if(a1 < w) cur.push_back(a1);
					if(a2 < w) cur.push_back(a2);
					a0 = a2;
				}
				else G4FAIL("mode");
			}
			if(a0 >= w) break;
		}
		if(a0 < w && color == 1)
			for(int x = (a0 < 0 ? 0 : a0); x < w; x++)
				row[x >> 3] |= (uint8_t)(0x80 >> (x & 7));
		if(blackIs1)
			for(int k = 0; k < stride; k++) row[k] = (uint8_t)~row[k];
		ref.swap(cur);
		ref.push_back(w);
	}
#undef G4FAIL
	return true;
}

/* ============================== document ============================== */
struct XE {
	int type;   /* 0 free, 1 plain, 2 in objstm */
	int off;
	int gen;
	int stm;
	int idx;
	XE() : type(0), off(0), gen(0), stm(0), idx(0) { }
};

struct PdfDoc {
	const uint8_t* buf;
	int size;
	map<int, XE> xref;
	map<int, Obj> objs;
	Obj trailer;
	vector<Obj> pages;
	bool encrypted;
	/* standard security handler state (empty user password) */
	bool decOn;
	bool decAes;      /* AESV2 vs RC4 */
	int keyLen;
	uint8_t key[16];
	int encNum;       /* Encrypt dict object: its strings stay plaintext */
	int saltMode;     /* object-key variant: -1 unknown, 0 standard, 1 "sAlT" */

	PdfDoc() : buf(NULL), size(0), encrypted(false), decOn(false),
		decAes(false), keyLen(0), encNum(-1), saltMode(-1) { trailer.t = O_DICT; }

	Obj resolve(const Obj& o, int depth = 0) {
		if(o.t != O_REF || depth > 32) return o;
		return resolve(getObj((int)o.i), depth + 1);
	}

	Obj getObj(int num) {
		map<int,Obj>::iterator it = objs.find(num);
		if(it != objs.end()) return it->second;
		Obj o;
		map<int,XE>::iterator xe = xref.find(num);
		if(xe == xref.end()) { objs[num] = o; return o; }
		if(xe->second.type == 1) {
			Lex lx(buf, size, xe->second.off);
			lx.skip();
			lx.number(); lx.skip(); lx.number(); lx.skip();
			if(!lx.kw("obj")) { objs[num] = o; return o; }
			o = lx.parse(0);
			o.onum = num; o.ogen = xe->second.gen;
			if(decOn && num != encNum) decryptStrings(o, num, xe->second.gen);
			lx.skip();
			if(lx.kw("stream")) {
				if(lx.p < size && buf[lx.p] == 13) lx.p++;
				if(lx.p < size && buf[lx.p] == 10) lx.p++;
				o.t = O_STREAM;
				const Obj* len = o.get("Length");
				int64_t l = 0;
				if(len != NULL) {
					Obj rl = resolve(*len);
					l = rl.isNum() ? (int64_t)rl.num() : 0;
				}
				if(l <= 0 || lx.p + l > size) {
					/* broken length: scan for endstream */
					int q = lx.p;
					while(q + 9 < size) {
						if(memcmp(buf + q, "endstream", 9) == 0) break;
						q++;
					}
					l = q - lx.p;
					while(l > 0 && (buf[lx.p + l - 1] == 10 || buf[lx.p + l - 1] == 13)) l--;
				}
				o.raw = buf + lx.p;
				o.rawLen = (int)l;
			}
		}
		else if(xe->second.type == 2) {
			Obj stm = getObj(xe->second.stm);
			if(stm.t == O_STREAM) {
				vector<uint8_t> data;
				if(decode(stm, data)) {
					int N = (int)stm.getNum("N", 0);
					int First = (int)stm.getNum("First", 0);
					Lex hd(data.data(), (int)data.size(), 0);
					int objNum = 0, objOff = 0;
					bool found = false;
					for(int k = 0; k < N; k++) {
						hd.skip(); Obj a = hd.number();
						hd.skip(); Obj b = hd.number();
						if(a.i == num) { objNum = (int)a.i; objOff = (int)b.i; found = true; break; }
					}
					if(found && First + objOff < (int)data.size()) {
						Lex ob(data.data(), (int)data.size(), First + objOff);
						o = ob.parse(0);
						(void)objNum;
					}
				}
			}
		}
		objs[num] = o;
		return o;
	}

	bool applyFilters(const Obj& stm, vector<uint8_t>& out) {
		const Obj* filter = stm.get("Filter");
		const Obj* parms = stm.get("DecodeParms");
		if(parms == NULL) parms = stm.get("DP");
		if(filter == NULL) return true;
		vector<Obj> filters, parmsList;
		if(filter->t == O_NAME) filters.push_back(*filter);
		else if(filter->t == O_ARR) filters = filter->a;
		if(parms != NULL) {
			if(parms->t == O_ARR) parmsList = parms->a;
			else parmsList.push_back(*parms);
		}
		for(size_t i = 0; i < filters.size(); i++) {
			Obj f = resolve(filters[i]);
			string fn = (f.t == O_NAME) ? f.s : "";
			Obj pm;
			if(i < parmsList.size()) pm = resolve(parmsList[i]);
			vector<uint8_t> next;
			bool ok = false;
			if(fn == "FlateDecode" || fn == "Fl") {
				ok = flateDecode(out.data(), (int)out.size(), next);
				if(ok) {
					int pred = (int)pm.getNum("Predictor", 1);
					int cols = (int)pm.getNum("Columns", 1);
					int colors = (int)pm.getNum("Colors", 1);
					int bpc = (int)pm.getNum("BitsPerComponent", 8);
					if(pred >= 10) pngUnfilter(next, colors, bpc, cols);
					else if(pred == 2) tiffUnfilter(next, colors, cols);
				}
			}
			else if(fn == "ASCIIHexDecode" || fn == "AHx")
				ok = asciiHexDecode(out.data(), (int)out.size(), next);
			else if(fn == "ASCII85Decode" || fn == "A85")
				ok = ascii85Decode(out.data(), (int)out.size(), next);
			else if(fn == "RunLengthDecode" || fn == "RL")
				ok = runLengthDecode(out.data(), (int)out.size(), next);
			else if(fn == "DCTDecode" || fn == "DCT" || fn == "JPXDecode") {
				/* keep compressed bytes; image path decodes them */
				return true;
			}
			else if(fn == "CCITTFaxDecode" || fn == "CCF") {
				int k = (int)pm.getNum("K", 0);
				if(k >= 0) return false; /* G3 1-D/2-D: rare, unsupported */
				int cols = (int)pm.getNum("Columns", 1728);
				int rows = (int)pm.getNum("Rows", 0);
				bool blackIs1 = pm.getNum("BlackIs1", 0) != 0;
				if(rows <= 0) rows = (int)stm.getNum("Height", 0);
				ok = ccittG4Decode(out.data(), (int)out.size(), cols, rows,
						blackIs1, next);
			}
			else return false; /* unsupported filter */
			if(!ok) return false;
			out.swap(next);
		}
		return true;
	}

	/* true when the stream's first filter can tell plaintext from garbage
	   (image data such as DCT "decodes" from anything) */
	bool filtersVerify(const Obj& stm) {
		const Obj* f = stm.get("Filter");
		if(f == NULL) return false;
		Obj r = resolve(*f);
		if(r.t == O_ARR && !r.a.empty()) r = resolve(r.a[0]);
		return r.t == O_NAME && (r.s == "FlateDecode" || r.s == "Fl");
	}

	bool decode(const Obj& stm, vector<uint8_t>& out) {
		if(stm.t != O_STREAM || stm.raw == NULL) return false;
		out.assign((uint8_t*)stm.raw, (uint8_t*)stm.raw + stm.rawLen);
		if(decOn && stm.onum != encNum) {
			/* some tools declare AESV2 but encrypt with the AESV3 "sAlT"
			   object-key derivation; retry the salted key like qpdf does.
			   The variant is document-wide: learn it from the first stream
			   that verifies, then use it first for everything else */
			bool verify = filtersVerify(stm);
			for(int pass = 0; pass < 2; pass++) {
				bool salt = (saltMode == 1) != (pass == 1);
				if(salt && !decAes) continue;
				vector<uint8_t> plain;
				if(!decryptData(stm.onum, stm.ogen, out, plain, salt)) continue;
				if(applyFilters(stm, plain)) {
					if(verify) saltMode = salt ? 1 : 0;
					out.swap(plain);
					return true;
				}
			}
			/* some re-saved PDFs mix encrypted and plaintext objects;
			   fall back to the raw bytes when decryption yields garbage */
			out.assign((uint8_t*)stm.raw, (uint8_t*)stm.raw + stm.rawLen);
		}
		return applyFilters(stm, out);
	}

	int64_t readBE(const uint8_t* p, int w) {
		int64_t v = 0;
		for(int k = 0; k < w; k++) v = (v << 8) | p[k];
		return v;
	}

	/* ---- standard security handler, empty user password ---- */
	void objKey(int num, int gen, uint8_t ok[16], int& okn, bool salt) {
		uint8_t tmp[36];
		memcpy(tmp, key, keyLen);
		tmp[keyLen]   = (uint8_t)(num & 0xff);
		tmp[keyLen+1] = (uint8_t)((num >> 8) & 0xff);
		tmp[keyLen+2] = (uint8_t)((num >> 16) & 0xff);
		tmp[keyLen+3] = (uint8_t)(gen & 0xff);
		tmp[keyLen+4] = (uint8_t)((gen >> 8) & 0xff);
		int n = keyLen + 5;
		if(salt) {
			memcpy(tmp + n, "sAlT", 4);
			n += 4;
		}
		uint8_t h[16];
		md5Buf(tmp, n, h);
		okn = keyLen + 5;
		if(okn > 16) okn = 16;
		if(decAes) okn = 16;
		memcpy(ok, h, okn);
	}

	bool decryptData(int num, int gen, const vector<uint8_t>& in,
			vector<uint8_t>& out, bool salt = false) {
		uint8_t ok[16]; int okn;
		objKey(num, gen, ok, okn, salt);
		if(decAes)
			return aesCbcDecrypt(ok, in.data(), (int)in.size(), out);
		out = in;
		if(!out.empty())
			rc4Crypt(ok, okn, out.data(), (int)out.size());
		return true;
	}

	void decryptStrings(Obj& o, int num, int gen) {
		if(o.t == O_STR) {
			vector<uint8_t> in(o.s.begin(), o.s.end()), out;
			if(decryptData(num, gen, in, out)) {
				string ns(out.empty() ? "" : (const char*)out.data(), out.size());
				o.s = ns;
			}
		}
		for(size_t i = 0; i < o.a.size(); i++) decryptStrings(o.a[i], num, gen);
		for(size_t i = 0; i < o.d.size(); i++) decryptStrings(o.d[i].second, num, gen);
	}

	/* locate "num gen obj" by linear scan: needed when the xref itself is
	   an encrypted stream, so pass 1 has no entries to resolve through */
	Obj scanObj(int num) {
		Obj o;
		char pfx[24];
		int pn = snprintf(pfx, sizeof(pfx), "%d ", num);
		for(int i = 0; i + pn + 8 < size; i++) {
			if(buf[i] != (uint8_t)pfx[0]) continue;
			if(memcmp(buf + i, pfx, pn) != 0) continue;
			if(i > 0 && !ws(buf[i-1]) && buf[i-1] != '>') continue;
			Lex lx(buf, size, i);
			lx.number(); lx.skip();
			Obj g = lx.number(); lx.skip();
			if(!lx.kw("obj")) continue;
			o = lx.parse(0);
			o.onum = num; o.ogen = (int)g.i;
			return o;
		}
		return o;
	}

	bool setupKey(const Obj& enc, const string& id0) {
		if(enc.getName("Filter") != "Standard") return false;
		int R = (int)enc.getNum("R", 0);
		int V = (int)enc.getNum("V", 0);
		if(R < 2 || R > 4 || (V != 1 && V != 2 && V != 4)) return false;
		string O = enc.getStr("O"), U = enc.getStr("U");
		if(O.size() < 32 || U.size() < 32) return false;
		int32_t P = (int32_t)enc.getNum("P", 0);
		keyLen = (R == 2) ? 5 : (int)enc.getNum("Length", 40)/8;
		if(keyLen < 5 || keyLen > 16) return false;
		decAes = false;
		if(V == 4) {
			const Obj* cf = enc.get("CF");
			const Obj* sf = enc.get("StmF");
			string cfn = (sf != NULL && sf->t == O_NAME) ? sf->s : "";
			if(cf != NULL && cf->t == O_DICT) {
				const Obj* e = cf->get(cfn.c_str());
				if(e != NULL && e->t == O_DICT) {
					string cfm = e->getName("CFM");
					if(cfm == "AESV2") decAes = true;
					else if(cfm != "V2" && cfm != "None") return false;
				}
			}
		}
		/* algorithm 2: file key from the empty user password */
		uint8_t pad[128];
		int pn = 0;
		memcpy(pad + pn, PDF_PAD, 32); pn += 32;
		memcpy(pad + pn, O.data(), 32); pn += 32;
		pad[pn++] = (uint8_t)(P & 0xff);
		pad[pn++] = (uint8_t)((P >> 8) & 0xff);
		pad[pn++] = (uint8_t)((P >> 16) & 0xff);
		pad[pn++] = (uint8_t)((P >> 24) & 0xff);
		memcpy(pad + pn, id0.data(), id0.size()); pn += (int)id0.size();
		uint8_t h[16];
		md5Buf(pad, pn, h);
		if(R >= 3) {
			for(int i = 0; i < 50; i++) md5Buf(h, keyLen, h);
		}
		memcpy(key, h, keyLen);
		/* algorithm 5 (R>=3) / 4 (R2): verify the empty password against U */
		Md5 m;
		m.update(PDF_PAD, 32);
		m.update((const uint8_t*)id0.data(), (int)id0.size());
		uint8_t uh[16];
		m.final(uh);
		uint8_t u[32];
		if(R == 2) {
			memcpy(u, uh, 16);
			rc4Crypt(key, keyLen, u, 16);
			return memcmp(u, U.data(), 16) == 0;
		}
		rc4Crypt(key, keyLen, uh, 16);
		for(int i = 1; i <= 19; i++) {
			uint8_t k2[16];
			for(int k = 0; k < keyLen; k++) k2[k] = key[k] ^ (uint8_t)i;
			rc4Crypt(k2, keyLen, uh, 16);
		}
		memcpy(u, uh, 16);
		memset(u + 16, 0, 16);
		return memcmp(u, U.data(), 16) == 0;
	}

	void loadXrefStream(int off, Obj& local) {
		Lex lx(buf, size, off);
		lx.skip();
		Obj no = lx.number(); lx.skip(); lx.number(); lx.skip();
		if(!lx.kw("obj")) return;
		Obj o = lx.parse(0);
		o.onum = (int)no.i; o.ogen = 0;
		lx.skip();
		if(!lx.kw("stream")) return;
		if(lx.p < size && buf[lx.p] == 13) lx.p++;
		if(lx.p < size && buf[lx.p] == 10) lx.p++;
		o.t = O_STREAM;
		int64_t l = (int64_t)o.getNum("Length", 0);
		if(l <= 0 || lx.p + l > size) return;
		o.raw = buf + lx.p;
		o.rawLen = (int)l;
		for(size_t i = 0; i < o.d.size(); i++)
			if(o.d[i].first != "XRefStm")
				dset(local, o.d[i].first) = o.d[i].second;
		vector<uint8_t> data;
		if(!decode(o, data)) return;
		const Obj* w = o.get("W");
		if(w == NULL || w->t != O_ARR || w->a.size() < 3) return;
		int w0 = (int)w->a[0].num(), w1 = (int)w->a[1].num(), w2 = (int)w->a[2].num();
		int row = w0 + w1 + w2;
		if(row <= 0) return;
		vector<pair<int,int> > ranges;
		const Obj* index = o.get("Index");
		if(index != NULL && index->t == O_ARR && index->a.size() >= 2) {
			for(size_t i = 0; i + 1 < index->a.size(); i += 2)
				ranges.push_back(pair<int,int>((int)index->a[i].num(), (int)index->a[i+1].num()));
		}
		else
			ranges.push_back(pair<int,int>(0, (int)o.getNum("Size", 0)));
		size_t pos = 0;
		for(size_t r = 0; r < ranges.size(); r++) {
			for(int k = 0; k < ranges[r].second; k++) {
				if(pos + row > data.size()) return;
				const uint8_t* e = &data[pos];
				pos += row;
				int type = w0 ? (int)readBE(e, w0) : 1;
				int64_t f1 = readBE(e + w0, w1);
				int64_t f2 = w2 ? readBE(e + w0 + w1, w2) : 0;
				int num = ranges[r].first + k;
				if(xref.find(num) != xref.end()) continue; /* newer wins */
				XE xe;
				xe.type = type;
				if(type == 1) { xe.off = (int)f1; xe.gen = (int)f2; }
				else if(type == 2) { xe.stm = (int)f1; xe.idx = (int)f2; }
				xref[num] = xe;
			}
		}
	}

	void loadClassicXref(int off, Obj& local) {
		Lex lx(buf, size, off);
		lx.skip();
		if(!lx.kw("xref")) return;
		while(true) {
			lx.skip();
			if(lx.p >= size) break;
			if(!(buf[lx.p] >= '0' && buf[lx.p] <= '9')) break;
			Obj first = lx.number();
			lx.skip();
			Obj count = lx.number();
			for(int64_t k = 0; k < count.i; k++) {
				lx.skip();
				if(lx.p >= size) break;
				if(!(buf[lx.p] >= '0' && buf[lx.p] <= '9')) break;
				Obj eoff = lx.number();
				lx.skip();
				Obj egen = lx.number();
				lx.skip();
				if(lx.p >= size) break;
				char type = (char)buf[lx.p++];
				int num = (int)(first.i + k);
				if(xref.find(num) == xref.end() && type == 'n') {
					XE xe;
					xe.type = 1;
					xe.off = (int)eoff.i;
					xe.gen = (int)egen.i;
					xref[num] = xe;
				}
			}
		}
		lx.skip();
		if(lx.kw("trailer")) {
			Obj t = lx.parse(0);
			for(size_t i = 0; i < t.d.size(); i++)
				if(t.d[i].first != "XRefStm")
					dset(local, t.d[i].first) = t.d[i].second;
		}
	}

	bool loadXrefChain(int off, int depth) {
		if(depth > 32 || off <= 0 || off >= size) return false;
		Lex lx(buf, size, off);
		lx.skip();
		bool isStream = !(lx.p + 4 <= size && memcmp(buf + lx.p, "xref", 4) == 0);
		Obj local;
		local.t = O_DICT; /* merged trailer keys; get() requires a dict type */
		if(isStream)
			loadXrefStream(off, local);
		else
			loadClassicXref(off, local);
		const Obj* p = local.get("Prev");
		if(p != NULL && p->isNum())
			loadXrefChain((int)p->num(), depth + 1);
		/* older trailers merged first, newer entries win */
		for(size_t i = 0; i < local.d.size(); i++)
			if(local.d[i].first != "Prev")
				dset(trailer, local.d[i].first) = local.d[i].second;
		return true;
	}

	void collectPages(const Obj& node, const Obj& mbox, const Obj& cbox,
			const Obj& res, const Obj& rot, int depth) {
		if(depth > 64 || pages.size() > 20000) return;
		Obj n = resolve(node);
		if(n.t != O_DICT && n.t != O_STREAM) return;
		const Obj* mb = n.get("MediaBox");
		const Obj* cb = n.get("CropBox");
		const Obj* rs = n.get("Resources");
		const Obj* rt = n.get("Rotate");
		Obj useMb = mb ? *mb : mbox;
		Obj useCb = cb ? *cb : cbox;
		Obj useRs = rs ? *rs : res;
		Obj useRt = rt ? *rt : rot;
		const Obj* kids = n.get("Kids");
		string type = n.getName("Type");
		if(kids != NULL && kids->t == O_ARR && type != "Page") {
			for(size_t i = 0; i < kids->a.size(); i++)
				collectPages(kids->a[i], useMb, useCb, useRs, useRt, depth + 1);
			return;
		}
		/* leaf */
		Obj page = n;
		if(mb == NULL && useMb.t == O_ARR) dset(page, "MediaBox") = useMb;
		if(cb == NULL && useCb.t == O_ARR) dset(page, "CropBox") = useCb;
		if(rs == NULL && useRs.t != O_NULL) dset(page, "Resources") = useRs;
		if(rt == NULL && useRt.isNum()) dset(page, "Rotate") = useRt;
		pages.push_back(page);
	}

	bool load(const uint8_t* b, int n) {
		buf = b; size = n;
		if(n < 32 || memcmp(b, "%PDF", 4) != 0) return false;
		/* find last startxref */
		int tail = n > 2048 ? n - 2048 : 0;
		int off = -1;
		for(int i = n - 10; i >= tail; i--) {
			if(memcmp(b + i, "startxref", 9) == 0) {
				Lex lx(b, n, i + 9);
				lx.skip();
				Obj v = lx.number();
				off = (int)v.i;
				break;
			}
		}
		if(off < 0) return false;
		loadXrefChain(off, 0);
		const Obj* encRef = trailer.get("Encrypt");
		if(encRef != NULL) {
			encrypted = true;
			/* pass 1 ran without crypto (the xref stream itself may be
			   encrypted): derive the key from the plaintext trailer, then
			   re-read the whole xref chain with decryption enabled */
			Obj enc = resolve(*encRef);
			if(enc.t != O_DICT && encRef->t == O_REF)
				enc = scanObj((int)encRef->i); /* encrypted xref: no entries yet */
			const Obj* id = trailer.get("ID");
			string id0;
			if(id != NULL && id->t == O_ARR && !id->a.empty() && id->a[0].t == O_STR)
				id0 = id->a[0].s;
			if(enc.t != O_DICT || !setupKey(enc, id0)) return false;
			if(encRef->t == O_REF) encNum = (int)encRef->i;
			xref.clear();
			objs.clear();
			trailer.d.clear();
			decOn = true;
			loadXrefChain(off, 0);
		}
		const Obj* rootRef = trailer.get("Root");
		if(rootRef == NULL) return false;
		Obj root = resolve(*rootRef);
		const Obj* pagesRef = root.get("Pages");
		if(pagesRef == NULL) return false;
		Obj nullObj;
		collectPages(*pagesRef, nullObj, nullObj, nullObj, nullObj, 0);
		return !pages.empty();
	}
};

/* ======================= standard font metrics ======================== */
/* Helvetica advance widths, chars 32..126 (units/1000) */
static const short HELV_W[95] = {
	278,278,355,556,556,889,667,191,333,333,389,584,278,333,278,278,
	556,556,556,556,556,556,556,556,556,556,278,278,584,584,584,556,
	1015,667,667,722,722,667,611,778,722,278,500,667,556,833,722,778,
	667,778,722,667,611,722,667,944,667,667,611,278,278,278,469,556,
	333,556,556,500,556,556,278,556,556,222,222,500,222,833,556,556,
	556,556,333,500,278,556,500,722,500,500,500,334,260,334,584
};
static const short HELVB_W[95] = {
	278,333,474,556,556,889,722,238,333,333,389,584,278,333,278,278,
	556,556,556,556,556,556,556,556,556,556,333,333,584,584,584,611,
	975,722,722,722,722,667,611,778,722,278,556,722,611,833,722,778,
	667,778,722,667,611,722,667,944,667,667,611,333,278,333,584,556,
	333,556,611,556,611,556,333,611,611,278,278,556,278,889,611,611,
	611,611,389,556,333,611,556,778,556,556,500,389,280,389,584
};
static const short TIMES_W[95] = {
	250,333,408,500,500,833,778,180,333,333,500,564,250,333,250,278,
	500,500,500,500,500,500,500,500,500,500,278,278,564,564,564,444,
	921,722,667,667,722,611,556,722,722,333,389,722,611,889,722,722,
	556,722,667,556,611,722,722,944,722,722,611,333,278,333,469,500,
	333,444,500,444,500,444,333,500,500,278,278,500,278,778,500,500,
	500,500,333,389,278,500,500,722,500,500,444,480,200,480,541
};

/* cp1252 (WinAnsi) 0x80..0xFF -> unicode */
static const uint16_t WINANSI_HI[128] = {
	0x20AC,0x0081,0x201A,0x0192,0x201E,0x2026,0x2020,0x2021,
	0x02C6,0x2030,0x0160,0x2039,0x0152,0x008D,0x017D,0x008F,
	0x0090,0x2018,0x2019,0x201C,0x201D,0x2022,0x2013,0x2014,
	0x02DC,0x2122,0x0161,0x203A,0x0153,0x009D,0x017E,0x0178,
	0x00A0,0x00A1,0x00A2,0x00A3,0x00A4,0x00A5,0x00A6,0x00A7,
	0x00A8,0x00A9,0x00AA,0x00AB,0x00AC,0x00AD,0x00AE,0x00AF,
	0x00B0,0x00B1,0x00B2,0x00B3,0x00B4,0x00B5,0x00B6,0x00B7,
	0x00B8,0x00B9,0x00BA,0x00BB,0x00BC,0x00BD,0x00BE,0x00BF,
	0x00C0,0x00C1,0x00C2,0x00C3,0x00C4,0x00C5,0x00C6,0x00C7,
	0x00C8,0x00C9,0x00CA,0x00CB,0x00CC,0x00CD,0x00CE,0x00CF,
	0x00D0,0x00D1,0x00D2,0x00D3,0x00D4,0x00D5,0x00D6,0x00D7,
	0x00D8,0x00D9,0x00DA,0x00DB,0x00DC,0x00DD,0x00DE,0x00DF,
	0x00E0,0x00E1,0x00E2,0x00E3,0x00E4,0x00E5,0x00E6,0x00E7,
	0x00E8,0x00E9,0x00EA,0x00EB,0x00EC,0x00ED,0x00EE,0x00EF,
	0x00F0,0x00F1,0x00F2,0x00F3,0x00F4,0x00F5,0x00F6,0x00F7,
	0x00F8,0x00F9,0x00FA,0x00FB,0x00FC,0x00FD,0x00FE,0x00FF
};

static const uint16_t MACROMAN_HI[128] = {
	0x00C4,0x00C5,0x00C7,0x00C9,0x00D1,0x00D6,0x00DC,0x00E1,
	0x00E0,0x00E2,0x00E4,0x00E3,0x00E5,0x00E7,0x00E9,0x00E8,
	0x00EA,0x00EB,0x00ED,0x00EC,0x00EE,0x00EF,0x00F1,0x00F3,
	0x00F2,0x00F4,0x00F6,0x00F5,0x00FA,0x00F9,0x00FB,0x00FC,
	0x2020,0x00B0,0x00A2,0x00A3,0x00A7,0x2022,0x00B6,0x00DF,
	0x00AE,0x00A9,0x2122,0x00B4,0x00A8,0x2260,0x00C6,0x00D8,
	0x221E,0x00B1,0x2264,0x2265,0x00A5,0x00B5,0x2202,0x2211,
	0x220F,0x03C0,0x222B,0x00AA,0x00BA,0x03A9,0x00E6,0x00F8,
	0x00BF,0x00A1,0x00AC,0x221A,0x0192,0x2248,0x2206,0x00AB,
	0x00BB,0x2026,0x00A0,0x00C0,0x00C3,0x00D5,0x0152,0x0153,
	0x2013,0x2014,0x201C,0x201D,0x2018,0x2019,0x00F7,0x25CA,
	0x00FF,0x0178,0x2044,0x20AC,0x2039,0x203A,0xFB01,0xFB02,
	0x2021,0x00B7,0x201A,0x201E,0x2030,0x00C2,0x00CA,0x00C1,
	0x00CB,0x00C8,0x00CD,0x00CE,0x00CF,0x00CC,0x00D3,0x00D4,
	0xF8FF,0x00D2,0x00DA,0x00DB,0x00D9,0x0131,0x02C6,0x02DC,
	0x00AF,0x02D8,0x02D9,0x02DA,0x00B8,0x02DD,0x02DB,0x02C7
};

struct GlyphName { const char* name; uint16_t uni; };
static const GlyphName GLYPH_NAMES[] = {
	{"space",32},{"exclam",33},{"quotedbl",34},{"numbersign",35},
	{"dollar",36},{"percent",37},{"ampersand",38},{"quotesingle",39},
	{"parenleft",40},{"parenright",41},{"asterisk",42},{"plus",43},
	{"comma",44},{"hyphen",45},{"period",46},{"slash",47},
	{"zero",48},{"one",49},{"two",50},{"three",51},{"four",52},
	{"five",53},{"six",54},{"seven",55},{"eight",56},{"nine",57},
	{"colon",58},{"semicolon",59},{"less",60},{"equal",61},
	{"greater",62},{"question",63},{"at",64},
	{"bracketleft",91},{"backslash",92},{"bracketright",93},
	{"asciicircum",94},{"underscore",95},{"grave",96},
	{"braceleft",123},{"bar",124},{"braceright",125},{"asciitilde",126},
	{"bullet",0x2022},{"dagger",0x2020},{"daggerdbl",0x2021},
	{"ellipsis",0x2026},{"emdash",0x2014},{"endash",0x2013},
	{"florin",0x0192},{"fraction",0x2044},{"guilsinglleft",0x2039},
	{"guilsinglright",0x203A},{"minus",0x2212},{"perthousand",0x2030},
	{"quotedblbase",0x201E},{"quotedblleft",0x201C},{"quotedblright",0x201D},
	{"quoteleft",0x2018},{"quoteright",0x2019},{"quotesinglbase",0x201A},
	{"trademark",0x2122},{"fi",0xFB01},{"fl",0xFB02},
	{"Lslash",0x0141},{"lslash",0x0142},{"Scaron",0x0160},{"scaron",0x0161},
	{"Zcaron",0x017D},{"zcaron",0x017E},{"breve",0x02D8},{"caron",0x02C7},
	{"circumflex",0x02C6},{"dotaccent",0x02D9},{"hungarumlaut",0x02DD},
	{"ogonek",0x02DB},{"ring",0x02DA},{"tilde",0x02DC},
	{"exclamdown",0xA1},{"cent",0xA2},{"sterling",0xA3},{"currency",0xA4},
	{"yen",0xA5},{"brokenbar",0xA6},{"section",0xA7},{"dieresis",0xA8},
	{"copyright",0xA9},{"ordfeminine",0xAA},{"guillemotleft",0xAB},
	{"logicalnot",0xAC},{"registered",0xAE},{"macron",0xAF},
	{"degree",0xB0},{"plusminus",0xB1},{"twosuperior",0xB2},
	{"threesuperior",0xB3},{"acute",0xB4},{"mu",0xB5},{"paragraph",0xB6},
	{"periodcentered",0xB7},{"cedilla",0xB8},{"onesuperior",0xB9},
	{"ordmasculine",0xBA},{"guillemotright",0xBB},{"onequarter",0xBC},
	{"onehalf",0xBD},{"threequarters",0xBE},{"questiondown",0xBF},
	{"Agrave",0xC0},{"Aacute",0xC1},{"Acircumflex",0xC2},{"Atilde",0xC3},
	{"Adieresis",0xC4},{"Aring",0xC5},{"AE",0xC6},{"Ccedilla",0xC7},
	{"Egrave",0xC8},{"Eacute",0xC9},{"Ecircumflex",0xCA},{"Edieresis",0xCB},
	{"Igrave",0xCC},{"Iacute",0xCD},{"Icircumflex",0xCE},{"Idieresis",0xCF},
	{"Eth",0xD0},{"Ntilde",0xD1},{"Ograve",0xD2},{"Oacute",0xD3},
	{"Ocircumflex",0xD4},{"Otilde",0xD5},{"Odieresis",0xD6},{"multiply",0xD7},
	{"Oslash",0xD8},{"Ugrave",0xD9},{"Uacute",0xDA},{"Ucircumflex",0xDB},
	{"Udieresis",0xDC},{"Yacute",0xDD},{"Thorn",0xDE},{"germandbls",0xDF},
	{"agrave",0xE0},{"aacute",0xE1},{"acircumflex",0xE2},{"atilde",0xE3},
	{"adieresis",0xE4},{"aring",0xE5},{"ae",0xE6},{"ccedilla",0xE7},
	{"egrave",0xE8},{"eacute",0xE9},{"ecircumflex",0xEA},{"edieresis",0xEB},
	{"igrave",0xEC},{"iacute",0xED},{"icircumflex",0xEE},{"idieresis",0xEF},
	{"eth",0xF0},{"ntilde",0xF1},{"ograve",0xF2},{"oacute",0xF3},
	{"ocircumflex",0xF4},{"otilde",0xF5},{"odieresis",0xF6},{"divide",0xF7},
	{"oslash",0xF8},{"ugrave",0xF9},{"uacute",0xFA},{"ucircumflex",0xFB},
	{"udieresis",0xFC},{"yacute",0xFD},{"thorn",0xFE},{"ydieresis",0xFF},
	{"Euro",0x20AC},{"nbspace",0xA0},{"sfthyphen",0xAD},
	{"periodsuperior",0xB7},{"middot",0xB7},
};

static uint32_t glyphNameToUni(const string& name) {
	if(name.size() == 1)
		return (uint8_t)name[0];
	if(name.size() == 7 && name.compare(0, 3, "uni") == 0) {
		return (uint32_t)strtol(name.c_str() + 3, NULL, 16);
	}
	if(name.size() == 6 && name[0] == 'u') {
		return (uint32_t)strtol(name.c_str() + 1, NULL, 16);
	}
	for(size_t i = 0; i < sizeof(GLYPH_NAMES)/sizeof(GLYPH_NAMES[0]); i++)
		if(name == GLYPH_NAMES[i].name)
			return GLYPH_NAMES[i].uni;
	/* strip suffix like "a.sc" */
	size_t dot = name.find('.');
	if(dot != string::npos && dot > 0)
		return glyphNameToUni(name.substr(0, dot));
	return 0;
}

}/* ============================ fonts =================================== */
namespace er {

struct PdfFont {
	font_t* ft;
	bool twoByte;
	double w[256];
	bool hw[256];
	map<int, double> cw;      /* Type0 CID widths          */
	double dw;
	map<uint32_t, uint32_t> toUni;
	map<int, uint32_t> enc;   /* /Differences              */
	const short* stdW;        /* standard-14 table or NULL */
	bool courier;
	bool pua;                 /* synthesized cmap carries PUA->gid */
	bool macRoman;            /* /Encoding /MacRomanEncoding       */
	map<int, int> asc;
	uint64_t tick;             /* FontCache LRU stamp */

	PdfFont() : ft(NULL), twoByte(false), dw(1000), stdW(NULL), courier(false),
			pua(false), macRoman(false), tick(0) {
		for(int i = 0; i < 256; i++) { w[i] = 500; hw[i] = false; }
	}
};

static uint32_t hashBytes(const uint8_t* p, int n) {
	uint32_t h = 2166136261u;
	for(int i = 0; i < n; i++) { h ^= p[i]; h *= 16777619u; }
	return h;
}

static int utf16be(const string& s, size_t& i) {
	if(i + 1 >= s.size()) return -1;
	int hi = (uint8_t)s[i], lo = (uint8_t)s[i+1];
	i += 2;
	int v = (hi << 8) | lo;
	if(v >= 0xD800 && v <= 0xDBFF && i + 1 < s.size()) {
		int h2 = (uint8_t)s[i], l2 = (uint8_t)s[i+1];
		int lo16 = (h2 << 8) | l2;
		if(lo16 >= 0xDC00 && lo16 <= 0xDFFF) {
			i += 2;
			return 0x10000 + ((v - 0xD800) << 10) + (lo16 - 0xDC00);
		}
	}
	return v;
}

/* "<0041>" -> raw bytes from hex pairs (the token text is NOT the payload) */
static string hexBytes(const string& tok) {
	string out;
	for(size_t i = 1; i + 1 < tok.size(); i += 2) {
		int hi = Lex::hexVal((uint8_t)tok[i]);
		int lo = Lex::hexVal((uint8_t)tok[i+1]);
		if(hi < 0 || lo < 0) break;
		out += (char)((hi << 4) | lo);
	}
	return out;
}
/* CMap source code: big-endian integer over 1..2 bytes */
static int cmapSrc(const string& tok) {
	string b = hexBytes(tok);
	if(b.empty()) return -1;
	int v = 0;
	for(size_t i = 0; i < b.size(); i++) v = (v << 8) | (uint8_t)b[i];
	return v;
}
/* CMap destination: UTF-16BE units, surrogate pairs folded */
static int cmapDst(const string& tok) {
	string b = hexBytes(tok);
	size_t i = 0;
	return utf16be(b, i);
}

static void parseToUnicode(const string& cm, PdfFont* f) {
	size_t pos = 0;
	while(pos < cm.size()) {
		size_t bc = cm.find("beginbfchar", pos);
		size_t br = cm.find("beginbfrange", pos);
		if(bc == string::npos && br == string::npos) break;
		bool range = (br != string::npos && (bc == string::npos || br < bc));
		size_t start = range ? br + 12 : bc + 11;
		size_t end = cm.find(range ? "endbfrange" : "endbfchar", start);
		if(end == string::npos) break;
		string body = cm.substr(start, end - start);
		/* tokenize <hex> and [ ... ] */
		vector<string> toks;
		size_t i = 0;
		while(i < body.size()) {
			if(body[i] == '<') {
				size_t j = body.find('>', i);
				if(j == string::npos) break;
				toks.push_back(body.substr(i, j - i + 1));
				i = j + 1;
			}
			else if(body[i] == '[') {
				size_t j = body.find(']', i);
				if(j == string::npos) break;
				toks.push_back(body.substr(i, j - i + 1));
				i = j + 1;
			}
			else i++;
		}
		if(!range) {
			for(size_t k = 0; k + 1 < toks.size(); k += 2) {
				string a = toks[k], b = toks[k+1];
				if(a.size() < 3 || b.size() < 3) continue;
				int src = cmapSrc(a);
				int dst = cmapDst(b);
				if(src >= 0 && dst >= 0) f->toUni[(uint32_t)src] = (uint32_t)dst;
			}
		}
		else {
			for(size_t k = 0; k + 2 < toks.size(); k += 3) {
				string a = toks[k], b = toks[k+1], c = toks[k+2];
				if(a.size() < 3 || b.size() < 3) continue;
				int lo = cmapSrc(a);
				int hi = cmapSrc(b);
				if(lo < 0 || hi < 0 || hi < lo || hi - lo > 65535) continue;
				if(c[0] == '<') {
					int dst = cmapDst(c);
					for(int v = lo; v <= hi; v++) f->toUni[(uint32_t)v] = (uint32_t)(dst + v - lo);
				}
				else if(c[0] == '[') {
					size_t j = 1;
					for(int v = lo; v <= hi && j < c.size(); v++) {
						while(j < c.size() && c[j] != '<') j++;
						if(j >= c.size()) break;
						size_t e = c.find('>', j);
						if(e == string::npos) break;
						string h = c.substr(j, e - j + 1);
						int dst = cmapDst(h);
						if(dst >= 0) f->toUni[(uint32_t)v] = (uint32_t)dst;
						j = e + 1;
					}
				}
			}
		}
		pos = end + (range ? 10 : 9);
	}
}

/* Subset CID fonts (Identity-H) usually ship WITHOUT a usable cmap: glyphs
   are addressed by CID == glyph id, and post 3.0 subsets carry no names for
   freetype to synthesize one from. fontd draws by Unicode, so synthesize a
   format-12 cmap: ToUnicode unicodes -> gid, plus a plane-15 PUA range
   (0xF0000+gid) so CIDs with no ToUnicode entry still render by glyph id.
   Returns empty when the font already has a cmap or nothing to map. */
static vector<uint8_t> synthesizeCmap(const vector<uint8_t>& ttf,
		const map<uint32_t,uint32_t>& toUni, const vector<uint8_t>& c2g) {
	vector<uint8_t> out;
	if(ttf.size() < 12) return out;
	int num = (ttf[4] << 8) | ttf[5];
	if(num <= 0 || 12 + 16*num > (int)ttf.size()) return out;
	uint32_t numGlyphs = 0;
	for(int i = 0; i < num; i++) {
		size_t e = 12 + 16*i;
		if(ttf[e]=='c' && ttf[e+1]=='m' && ttf[e+2]=='a' && ttf[e+3]=='p')
			return out; /* real cmap present: trust it */
		if(ttf[e]=='m' && ttf[e+1]=='a' && ttf[e+2]=='x' && ttf[e+3]=='p') {
			uint32_t mo = ((uint32_t)ttf[e+8]<<24)|((uint32_t)ttf[e+9]<<16)|
					((uint32_t)ttf[e+10]<<8)|ttf[e+11];
			if(mo + 6 <= ttf.size())
				numGlyphs = ((uint32_t)ttf[mo+4] << 8) | ttf[mo+5];
		}
	}
	vector<pair<uint32_t,uint32_t> > ent; /* unicode -> gid */
	for(map<uint32_t,uint32_t>::const_iterator it = toUni.begin();
			it != toUni.end(); ++it) {
		uint32_t cid = it->first, u = it->second, gid = cid;
		if(!c2g.empty()) {
			size_t p = (size_t)cid * 2;
			if(p + 1 >= c2g.size()) continue;
			gid = ((uint32_t)c2g[p] << 8) | c2g[p+1];
		}
		if(u == 0 || u >= 0xF0000 || gid > 0xFFFF) continue;
		ent.push_back(pair<uint32_t,uint32_t>(u, gid));
	}
	if(ent.empty() && numGlyphs == 0) return out;
	sort(ent.begin(), ent.end());
	size_t w = 0;
	for(size_t i = 0; i < ent.size(); i++)
		if(i == 0 || ent[i].first != ent[i-1].first) ent[w++] = ent[i];
	ent.resize(w);

	/* format 12 groups: merge consecutive (uni,gid) runs */
	struct Grp { uint32_t u0, u1, g0; };
	vector<Grp> gr;
	for(size_t i = 0; i < ent.size(); i++) {
		if(!gr.empty() && ent[i].first == gr.back().u1 + 1 &&
				ent[i].second == gr.back().g0 + (gr.back().u1 - gr.back().u0) + 1) {
			gr.back().u1 = ent[i].first;
		}
		else {
			Grp g; g.u0 = g.u1 = ent[i].first; g.g0 = ent[i].second;
			gr.push_back(g);
		}
	}
	if(numGlyphs > 0 && numGlyphs <= 0xFFFF) {
		Grp g; g.u0 = 0xF0000; g.u1 = 0xF0000 + numGlyphs - 1; g.g0 = 0;
		gr.push_back(g);
	}
	if(gr.empty()) return out;

	int subLen = 16 + 12 * (int)gr.size();
	int cmapLen = 20 + subLen; /* 2 encoding records, shared subtable */
	vector<uint8_t> cm(cmapLen, 0);
	cm[2] = 0; cm[3] = 2;            /* numTables */
	cm[4] = 0; cm[5] = 0;            /* platform 0 (unicode) */
	cm[6] = 0; cm[7] = 4;            /* encoding 4 (full repertoire) */
	cm[8] = 0; cm[9] = 0; cm[10] = 0; cm[11] = 20;
	cm[12] = 0; cm[13] = 3;          /* platform 3 (windows) */
	cm[14] = 0; cm[15] = 10;         /* encoding 10 (full repertoire) */
	cm[16] = 0; cm[17] = 0; cm[18] = 0; cm[19] = 20;
	size_t s = 20;
	cm[s] = 0; cm[s+1] = 12;         /* format */
	cm[s+4] = (uint8_t)(subLen >> 24); cm[s+5] = (uint8_t)(subLen >> 16);
	cm[s+6] = (uint8_t)(subLen >> 8); cm[s+7] = (uint8_t)subLen;
	cm[s+12] = (uint8_t)(gr.size() >> 24); cm[s+13] = (uint8_t)(gr.size() >> 16);
	cm[s+14] = (uint8_t)(gr.size() >> 8); cm[s+15] = (uint8_t)gr.size();
	for(size_t i = 0; i < gr.size(); i++) {
		size_t g = s + 16 + 12*i;
		uint32_t v[3] = { gr[i].u0, gr[i].u1, gr[i].g0 };
		for(int k = 0; k < 3; k++) {
			cm[g+4*k] = (uint8_t)(v[k] >> 24); cm[g+4*k+1] = (uint8_t)(v[k] >> 16);
			cm[g+4*k+2] = (uint8_t)(v[k] >> 8); cm[g+4*k+3] = (uint8_t)v[k];
		}
	}

	/* rebuild sfnt: sorted directory with one extra entry, tables rebased */
	struct Ent { char tag[4]; uint32_t off, len; };
	vector<Ent> dirs;
	for(int i = 0; i < num; i++) {
		size_t e = 12 + 16*i;
		Ent en;
		memcpy(en.tag, &ttf[e], 4);
		en.off = ((uint32_t)ttf[e+8]<<24)|((uint32_t)ttf[e+9]<<16)|
				((uint32_t)ttf[e+10]<<8)|ttf[e+11];
		en.len = ((uint32_t)ttf[e+12]<<24)|((uint32_t)ttf[e+13]<<16)|
				((uint32_t)ttf[e+14]<<8)|ttf[e+15];
		if(en.off + en.len > ttf.size()) return out;
		dirs.push_back(en);
	}
	Ent ce; memcpy(ce.tag, "cmap", 4); ce.len = cmapLen;
	dirs.push_back(ce);
	for(size_t i = 1; i < dirs.size(); i++)
		for(size_t j = i; j > 0 && memcmp(dirs[j].tag, dirs[j-1].tag, 4) < 0; j--) {
			Ent t = dirs[j]; dirs[j] = dirs[j-1]; dirs[j-1] = t;
		}
	int n2 = (int)dirs.size();
	int sel2 = 0; while((2 << (sel2+1)) <= n2*16) sel2++;
	int sr2 = 16 << sel2;
	out.resize(12 + 16*n2, 0);
	memcpy(&out[0], &ttf[0], 4);
	out[4] = (uint8_t)(n2 >> 8); out[5] = (uint8_t)n2;
	out[6] = (uint8_t)(sr2 >> 8); out[7] = (uint8_t)sr2;
	out[8] = 0; out[9] = (uint8_t)sel2;
	int rs2 = n2*16 - sr2;
	out[10] = (uint8_t)(rs2 >> 8); out[11] = (uint8_t)rs2;
	size_t pos = 12 + 16*n2;
	for(int i = 0; i < n2; i++) {
		size_t e = 12 + 16*i;
		memcpy(&out[e], dirs[i].tag, 4);
		bool isCmap = memcmp(dirs[i].tag, "cmap", 4) == 0;
		uint32_t off = isCmap ? 0xFFFFFFFFu : (uint32_t)pos;
		out[e+8] = (uint8_t)(off >> 24); out[e+9] = (uint8_t)(off >> 16);
		out[e+10] = (uint8_t)(off >> 8); out[e+11] = (uint8_t)off;
		out[e+12] = (uint8_t)(dirs[i].len >> 24); out[e+13] = (uint8_t)(dirs[i].len >> 16);
		out[e+14] = (uint8_t)(dirs[i].len >> 8); out[e+15] = (uint8_t)dirs[i].len;
		if(!isCmap) {
			for(uint32_t k = 0; k < dirs[i].len; k++)
				out.push_back(ttf[dirs[i].off + k]);
			while(out.size() & 3) out.push_back(0);
			pos = out.size();
		}
	}
	uint32_t cmapOff = (uint32_t)out.size();
	for(size_t k = 0; k < cm.size(); k++) out.push_back(cm[k]);
	for(int i = 0; i < n2; i++) {
		size_t e = 12 + 16*i;
		if(out[e+8] == 0xFF && out[e+9] == 0xFF && out[e+10] == 0xFF && out[e+11] == 0xFF) {
			out[e+8] = (uint8_t)(cmapOff >> 24); out[e+9] = (uint8_t)(cmapOff >> 16);
			out[e+10] = (uint8_t)(cmapOff >> 8); out[e+11] = (uint8_t)cmapOff;
		}
	}
	return out;
}

/* embedded font program: TrueType (FontFile2), CFF/OpenType (FontFile3)
   or Type1 (FontFile); FreeType sniffs the actual format from the bytes */
static const Obj* embeddedProgram(const Obj& fds) {
	const Obj* e = fds.get("FontFile2");
	if(e == NULL) e = fds.get("FontFile3");
	if(e == NULL) e = fds.get("FontFile");
	return e;
}

/* extract an embedded TrueType font program and register it with fontd */
static font_t* loadEmbedded(const uint8_t* data, int n, font_t* fallback) {
	if(data == NULL || n < 12) return fallback;
	string name = fmt("erpdf%08x", hashBytes(data, n));
	string path = "/tmp/" + name + ".ttf";
	FILE* f = fopen(path.c_str(), "w");
	if(f != NULL) {
		fwrite(data, 1, n, f);
		fclose(f);
	}
	if(font_load(name.c_str(), path.c_str()) >= 0) {
		font_t* ft = font_new(name.c_str(), true);
		if(ft != NULL) return ft;
	}
	return fallback;
}

struct FontCache {
	map<long, PdfFont*> byKey;
	font_t* sysFont;
	uint64_t clock;
	FontCache() : sysFont(NULL), clock(0) { }
	~FontCache() {
		for(map<long,PdfFont*>::iterator it = byKey.begin(); it != byKey.end(); ++it) {
			if(it->second->ft != NULL && it->second->ft != sysFont)
				font_free(it->second->ft);
			delete it->second;
		}
		if(sysFont != NULL) font_free(sysFont);
	}
};

/* per-page subset fonts accumulate without bound; cap the live set and
   release the least recently used ones so fontd slots stay available */
#define PDF_FONT_CACHE_MAX 64
static void fcInsert(FontCache* fc, long key, PdfFont* f) {
	f->tick = ++fc->clock;
	fc->byKey[key] = f;
	while(fc->byKey.size() > PDF_FONT_CACHE_MAX) {
		map<long, PdfFont*>::iterator victim = fc->byKey.end();
		uint64_t best = ~0ull;
		for(map<long, PdfFont*>::iterator it = fc->byKey.begin(); it != fc->byKey.end(); ++it) {
			if(it->second->ft == fc->sysFont) continue;
			if(it->second->tick < best) { best = it->second->tick; victim = it; }
		}
		if(victim == fc->byKey.end()) break;
		if(victim->second->ft != NULL && victim->second->ft != fc->sysFont)
			font_free(victim->second->ft);
		delete victim->second;
		fc->byKey.erase(victim);
	}
}

static PdfFont* loadFont(PdfDoc* doc, FontCache* fc, const Obj& ref, long key) {
	map<long,PdfFont*>::iterator it = fc->byKey.find(key);
	if(it != fc->byKey.end()) {
		it->second->tick = ++fc->clock;
		return it->second;
	}

	PdfFont* f = new PdfFont();
	f->ft = fc->sysFont;
	vector<uint8_t> c2g;   /* explicit CIDToGIDMap stream, if any */
	Obj fd = doc->resolve(ref);
	if(fd.t != O_DICT && fd.t != O_STREAM) {
		fcInsert(fc, key, f);
		return f;
	}
	string subtype = fd.getName("Subtype");
	string base = fd.getName("BaseFont");
	size_t plus = base.find('+');
	if(plus == 8) base = base.substr(plus + 1);

	const Obj* embedded = NULL;
	if(subtype == "Type0") {
		f->twoByte = true;
		const Obj* desc = fd.get("DescendantFonts");
		Obj cid;
		if(desc != NULL) {
			Obj da = doc->resolve(*desc);
			if(da.t == O_ARR && !da.a.empty()) cid = doc->resolve(da.a[0]);
		}
		f->dw = cid.getNum("DW", 1000);
		const Obj* warr = cid.get("W");
		if(warr != NULL && warr->t == O_ARR) {
			size_t i = 0;
			while(i < warr->a.size()) {
				int c0 = (int)doc->resolve(warr->a[i]).num();
				if(i + 1 >= warr->a.size()) break;
				Obj nxt = doc->resolve(warr->a[i+1]);
				if(nxt.t == O_ARR) {
					for(size_t k = 0; k < nxt.a.size(); k++)
						f->cw[c0 + (int)k] = doc->resolve(nxt.a[k]).num();
					i += 2;
				}
				else if(i + 2 < warr->a.size()) {
					int c1 = (int)nxt.num();
					double wv = doc->resolve(warr->a[i+2]).num();
					if(c1 >= c0 && c1 - c0 < 65536)
						for(int c = c0; c <= c1; c++) f->cw[c] = wv;
					i += 3;
				}
				else break;
			}
		}
		const Obj* tu = fd.get("ToUnicode");
		if(tu != NULL) {
			Obj tus = doc->resolve(*tu);
			vector<uint8_t> cm;
			if(doc->decode(tus, cm))
				parseToUnicode(string((const char*)cm.data(), cm.size()), f);
		}
		const Obj* c2gm = cid.get("CIDToGIDMap");
		if(c2gm != NULL) {
			Obj c2go = doc->resolve(*c2gm);
			if(c2go.t == O_STREAM) doc->decode(c2go, c2g);
		}
		const Obj* fdesc = cid.get("FontDescriptor");
		if(fdesc != NULL) {
			Obj fds = doc->resolve(*fdesc);
			embedded = embeddedProgram(fds);
		}
	}
	else {
		/* simple font: /Widths + /FirstChar, /Encoding /Differences */
		const Obj* widths = fd.get("Widths");
		int first = (int)fd.getNum("FirstChar", 0);
		if(widths != NULL && widths->t == O_ARR) {
			for(size_t k = 0; k < widths->a.size(); k++) {
				int c = first + (int)k;
				if(c >= 0 && c < 256) {
					f->w[c] = doc->resolve(widths->a[k]).num();
					f->hw[c] = true;
				}
			}
		}
		const Obj* enc = fd.get("Encoding");
		if(enc != NULL) {
			Obj encd = doc->resolve(*enc);
			if(encd.t == O_NAME && encd.s == "MacRomanEncoding")
				f->macRoman = true;
			const Obj* diffs = encd.get("Differences");
			if(diffs != NULL && diffs->t == O_ARR) {
				int code = 0;
				for(size_t k = 0; k < diffs->a.size(); k++) {
					Obj e = doc->resolve(diffs->a[k]);
					if(e.isNum()) code = (int)e.num();
					else if(e.t == O_NAME) {
						uint32_t u = glyphNameToUni(e.s);
						if(u > 0 && code >= 0 && code < 256) f->enc[code] = u;
						code++;
					}
				}
			}
		}
		if(base.find("Helvetica") == 0)
			f->stdW = (base.find("Bold") != string::npos) ? HELVB_W : HELV_W;
		else if(base.find("Times") == 0)
			f->stdW = TIMES_W;
		else if(base.find("Courier") == 0)
			f->courier = true;
		else if(base.find("Arial") != string::npos)
			f->stdW = (base.find("Bold") != string::npos) ? HELVB_W : HELV_W;
		const Obj* fdesc = fd.get("FontDescriptor");
		if(fdesc != NULL) {
			Obj fds = doc->resolve(*fdesc);
			embedded = embeddedProgram(fds);
		}
	}

	if(embedded != NULL) {
		Obj es = doc->resolve(*embedded);
		vector<uint8_t> prog;
		if(doc->decode(es, prog)) {
			if(f->twoByte) {
				vector<uint8_t> patched = synthesizeCmap(prog, f->toUni, c2g);
				if(!patched.empty()) {
					prog.swap(patched);
					f->pua = true;
				}
			}
			f->ft = loadEmbedded(prog.data(), (int)prog.size(), fc->sysFont);
		}
	}
	fcInsert(fc, key, f);
	return f;
}

/* ============================ renderer ================================ */
struct Pt { double x, y; };
struct SubPath { vector<Pt> pts; bool closed; };

struct ColorSpace {
	int kind;                 /* 0 gray 1 rgb 2 cmyk 3 indexed 4 pattern */
	vector<uint8_t> pal;
	int base;
	int hi;
	ColorSpace() : kind(0), base(0), hi(255) { }
};

struct GS {
	double ctm[6];
	grect_t clip;
	uint32_t fill, stroke;
	double fa, sa;
	double lw;
	double tm[6], tlm[6];
	PdfFont* font;
	double fs, tc, tw, tz, tl, ts;
	int tr;
	ColorSpace fcs, scs;
	GS() : fa(1), sa(1), lw(1), font(NULL), fs(12), tc(0), tw(0),
		tz(100), tl(0), ts(0), tr(0) {
		for(int i = 0; i < 6; i++) { ctm[i] = 0; tm[i] = 0; tlm[i] = 0; }
		ctm[0] = ctm[3] = 1;
		tm[0] = tm[3] = 1;
		tlm[0] = tlm[3] = 1;
		fill = stroke = 0xff000000;
		clip.x = clip.y = 0; clip.w = clip.h = 0;
	}
};

static void mmul(const double a[6], const double b[6], double o[6]) {
	double r[6];
	r[0] = a[0]*b[0] + a[1]*b[2];
	r[1] = a[0]*b[1] + a[1]*b[3];
	r[2] = a[2]*b[0] + a[3]*b[2];
	r[3] = a[2]*b[1] + a[3]*b[3];
	r[4] = a[4]*b[0] + a[5]*b[2] + b[4];
	r[5] = a[4]*b[1] + a[5]*b[3] + b[5];
	memcpy(o, r, sizeof(r));
}
static void mpoint(const double m[6], double x, double y, double* ox, double* oy) {
	*ox = m[0]*x + m[2]*y + m[4];
	*oy = m[1]*x + m[3]*y + m[5];
}
static double mdet(const double m[6]) { return m[0]*m[3] - m[1]*m[2]; }

struct Render {
	PdfDoc* doc;
	FontCache* fc;
	graph_t* g;
	vector<GS> stack;
	vector<SubPath> path;
	Pt cur;
	bool hasCur;
	bool pendingClip, clipEO;
	Obj res;
	int depth;

	Render(PdfDoc* d, FontCache* f, graph_t* gg, const Obj& r)
		: doc(d), fc(f), g(gg), hasCur(false),
		  pendingClip(false), clipEO(false), res(r), depth(0) {
		GS base;
		base.clip = {0, 0, gg->w, gg->h};
		stack.push_back(base);
	}
	GS& gs() { return stack.back(); }

	void setClipRect(const grect_t& r) {
		gs().clip = r;
		/* graph_set_clip() intersects with the canvas and an empty result
		 * leaves clip.w/h == 0, which every graph primitive reads as "no clip"
		 * (everything draws). An empty clip is tracked by hidden() instead. */
		if(r.w <= 0 || r.h <= 0)
			graph_set_clip(g, 0, 0, g->w, g->h);
		else
			graph_set_clip(g, r.x, r.y, r.w, r.h);
	}

	/* current clip region is empty: nothing may be painted */
	bool hidden() {
		const grect_t& c = gs().clip;
		if(c.w <= 0 || c.h <= 0) return true;
		return c.x >= g->w || c.y >= g->h || c.x + c.w <= 0 || c.y + c.h <= 0;
	}

	uint32_t blend(uint32_t c, double a) {
		if(a >= 1) return c | 0xff000000;
		if(a <= 0) return 0x00000000;
		return ((uint32_t)(a*255) << 24) | (c & 0xffffff);
	}

	void colorFrom(ColorSpace& cs, vector<double>& v, uint32_t& out) {
		double r = 0, gg = 0, b = 0;
		if(cs.kind == 3 && !v.empty()) { /* indexed */
			int idx = (int)v[0];
			if(idx < 0) idx = 0;
			if(idx > cs.hi) idx = cs.hi;
			int ncomp = (cs.base == 1) ? 3 : (cs.base == 2 ? 4 : 1);
			int off = idx * ncomp;
			if(off + ncomp <= (int)cs.pal.size()) {
				if(cs.base == 1) {
					r = cs.pal[off]/255.0; gg = cs.pal[off+1]/255.0; b = cs.pal[off+2]/255.0;
				}
				else if(cs.base == 2) {
					double c = cs.pal[off]/255.0, m = cs.pal[off+1]/255.0,
						y = cs.pal[off+2]/255.0, k = cs.pal[off+3]/255.0;
					r = (1-c)*(1-k); gg = (1-m)*(1-k); b = (1-y)*(1-k);
				}
				else r = gg = b = cs.pal[off]/255.0;
			}
		}
		else if(cs.kind == 1 && v.size() >= 3) { r = v[0]; gg = v[1]; b = v[2]; }
		else if(cs.kind == 2 && v.size() >= 4) {
			r = (1-v[0])*(1-v[3]); gg = (1-v[1])*(1-v[3]); b = (1-v[2])*(1-v[3]);
		}
		else if(cs.kind == 4) { r = gg = b = 0.5; }
		else if(!v.empty()) { r = gg = b = v[0]; }
		if(r < 0) r = 0; if(r > 1) r = 1;
		if(gg < 0) gg = 0; if(gg > 1) gg = 1;
		if(b < 0) b = 0; if(b > 1) b = 1;
		out = 0xff000000 | ((uint32_t)(r*255) << 16) | ((uint32_t)(gg*255) << 8) | (uint32_t)(b*255);
	}

	void setColorSpace(ColorSpace& cs, const Obj& spec) {
		Obj s = doc->resolve(spec);
		if(s.t == O_NAME) {
			if(s.s == "DeviceRGB" || s.s == "RGB") cs.kind = 1;
			else if(s.s == "DeviceCMYK" || s.s == "CMYK") cs.kind = 2;
			else if(s.s == "Pattern") cs.kind = 4;
			else cs.kind = 0;
			/* resource color space? */
			const Obj* rcs = res.get("ColorSpace");
			if(rcs != NULL) {
				Obj rd = doc->resolve(*rcs);
				const Obj* e = rd.get(s.s.c_str());
				if(e != NULL) { setColorSpace(cs, *e); return; }
			}
			return;
		}
		if(s.t == O_ARR && !s.a.empty()) {
			Obj fam = doc->resolve(s.a[0]);
			string fn = fam.t == O_NAME ? fam.s : "";
			if(fn == "Indexed" && s.a.size() >= 4) {
				cs.kind = 3;
				ColorSpace base;
				setColorSpace(base, s.a[1]);
				cs.base = base.kind;
				cs.hi = (int)doc->resolve(s.a[2]).num();
				Obj look = doc->resolve(s.a[3]);
				if(look.t == O_STR)
					cs.pal.assign((uint8_t*)look.s.data(),
							(uint8_t*)look.s.data() + look.s.size());
				else if(look.t == O_STREAM) {
					vector<uint8_t> d;
					if(doc->decode(look, d)) cs.pal = d;
				}
			}
			else if(fn == "ICCBased" && s.a.size() >= 2) {
				Obj st = doc->resolve(s.a[1]);
				int n = (int)st.getNum("N", 3);
				cs.kind = (n == 1) ? 0 : (n == 4 ? 2 : 1);
			}
			else if(fn == "Separation" || fn == "DeviceN") {
				cs.kind = (s.a.size() >= 3) ? 1 : 0;
				if(s.a.size() >= 3) {
					ColorSpace alt;
					setColorSpace(alt, s.a[2]);
					cs.kind = alt.kind == 2 ? 2 : (alt.kind == 0 ? 0 : 1);
				}
			}
			else if(fn == "CalRGB" || fn == "Lab") cs.kind = 1;
			else if(fn == "CalGray") cs.kind = 0;
			else cs.kind = 0;
		}
	}

	/* ---- paths ---- */
	void movePt(double x, double y) {
		SubPath sp;
		sp.closed = false;
		cur = {x, y};
		hasCur = true;
		sp.pts.push_back(cur);
		path.push_back(sp);
	}
	void linePt(double x, double y) {
		if(!hasCur) movePt(x, y);
		cur = {x, y};
		path.back().pts.push_back(cur);
	}
	void curvePt(double x1, double y1, double x2, double y2, double x3, double y3) {
		if(!hasCur) movePt(x1, y1);
		Pt p0 = cur;
		const int N = 12;
		for(int i = 1; i <= N; i++) {
			double t = (double)i / N, u = 1 - t;
			double x = u*u*u*p0.x + 3*u*u*t*x1 + 3*u*t*t*x2 + t*t*t*x3;
			double y = u*u*u*p0.y + 3*u*u*t*y1 + 3*u*t*t*y2 + t*t*t*y3;
			path.back().pts.push_back(Pt{x, y});
		}
		cur = {x3, y3};
	}
	void rectPt(double x, double y, double w, double h) {
		movePt(x, y);
		linePt(x+w, y);
		linePt(x+w, y+h);
		linePt(x, y+h);
		path.back().closed = true;
		hasCur = false;
	}
	void closePath() {
		if(!path.empty()) path.back().closed = true;
		if(!path.empty() && !path.back().pts.empty())
			cur = path.back().pts[0];
	}

	void pathBBoxDevice(double m[6], grect_t& out) {
		double minx = 1e9, miny = 1e9, maxx = -1e9, maxy = -1e9;
		bool any = false;
		for(size_t i = 0; i < path.size(); i++) {
			for(size_t k = 0; k < path[i].pts.size(); k++) {
				double x, y;
				mpoint(m, path[i].pts[k].x, path[i].pts[k].y, &x, &y);
				if(x < minx) minx = x;
				if(x > maxx) maxx = x;
				if(y < miny) miny = y;
				if(y > maxy) maxy = y;
				any = true;
			}
		}
		if(!any) { out = {0, 0, 0, 0}; return; }
		out.x = (int)floor(minx); out.y = (int)floor(miny);
		out.w = (int)ceil(maxx) - out.x + 1;
		out.h = (int)ceil(maxy) - out.y + 1;
	}

	void applyPendingClip() {
		if(!pendingClip) return;
		pendingClip = false;
		grect_t bb;
		pathBBoxDevice(gs().ctm, bb);
		grect_t c = gs().clip;
		grect_t n;
		n.x = bb.x > c.x ? bb.x : c.x;
		n.y = bb.y > c.y ? bb.y : c.y;
		int rx = (bb.x + bb.w) < (c.x + c.w) ? (bb.x + bb.w) : (c.x + c.w);
		int by = (bb.y + bb.h) < (c.y + c.h) ? (bb.y + bb.h) : (c.y + c.h);
		n.w = rx - n.x;
		n.h = by - n.y;
		setClipRect(n);
	}

	struct Cross { double x; int dir; };

	void fillPath(bool eo) {
		if(path.empty() || hidden()) return;
		double m[6];
		memcpy(m, gs().ctm, sizeof(m));
		grect_t bb;
		pathBBoxDevice(m, bb);
		int y0 = bb.y < 0 ? 0 : bb.y;
		int y1 = (bb.y + bb.h) > g->h ? g->h : (bb.y + bb.h);
		uint32_t col = blend(gs().fill, gs().fa);
		uint8_t ca = col >> 24;
		vector<Cross> xs;
		for(int y = y0; y < y1; y++) {
			double yc = y + 0.5;
			xs.clear();
			for(size_t i = 0; i < path.size(); i++) {
				vector<Pt>& pts = path[i].pts;
				if(pts.size() < 2) continue;
				size_t n = pts.size();
				for(size_t k = 0; k < n; k++) {
					size_t k2 = (k + 1) % n;
					if(k2 == 0 && !path[i].closed && n > 2) {
						/* open subpath: fill implicitly closes */
					}
					double x1, y1d, x2, y2d;
					mpoint(m, pts[k].x, pts[k].y, &x1, &y1d);
					mpoint(m, pts[k2].x, pts[k2].y, &x2, &y2d);
					if((y1d <= yc && y2d > yc) || (y2d <= yc && y1d > yc)) {
						double x = x1 + (yc - y1d) * (x2 - x1) / (y2d - y1d);
						xs.push_back(Cross{x, y2d > y1d ? 1 : -1});
					}
				}
			}
			if(xs.size() < 2) continue;
			/* insertion sort (few crossings per row) */
			for(size_t i = 1; i < xs.size(); i++) {
				Cross c = xs[i];
				size_t j = i;
				while(j > 0 && xs[j-1].x > c.x) { xs[j] = xs[j-1]; j--; }
				xs[j] = c;
			}
			if(eo) {
				for(size_t i = 0; i + 1 < xs.size(); i += 2)
					span(col, ca, (int)ceil(xs[i].x - 0.5), (int)floor(xs[i+1].x - 0.5) + 1, y);
			}
			else {
				int wind = 0;
				int start = 0;
				for(size_t i = 0; i < xs.size(); i++) {
					int prev = wind;
					wind += xs[i].dir;
					if(prev == 0 && wind != 0) start = (int)ceil(xs[i].x - 0.5);
					else if(prev != 0 && wind == 0)
						span(col, ca, start, (int)floor(xs[i].x - 0.5) + 1, y);
				}
			}
		}
	}

	void span(uint32_t col, uint8_t ca, int x0, int x1, int y) {
		if(x1 <= x0) return;
		if(x0 < 0) x0 = 0;
		if(x1 > g->w) x1 = g->w;
		if(x1 <= x0) return;
		if(ca == 255) {
			graph_fill_rect(g, x0, y, x1 - x0, 1, col);
			return;
		}
		uint8_t r = color_r(col), gg = color_g(col), b = color_b(col);
		for(int x = x0; x < x1; x++)
			graph_pixel_argb(g, x, y, ca, r, gg, b);
	}

	void strokePath() {
		if(path.empty() || hidden()) return;
		double m[6];
		memcpy(m, gs().ctm, sizeof(m));
		double scale = sqrt(fabs(mdet(m)));
		int lw = (int)(gs().lw * scale + 0.5);
		if(lw < 1) lw = 1;
		if(lw > 64) lw = 64;
		uint32_t col = gs().stroke;
		if(gs().sa < 1) {
			/* approximate alpha stroke over white paper */
			double a = gs().sa;
			uint32_t r = (uint32_t)(color_r(col)*a + 255*(1-a));
			uint32_t gg = (uint32_t)(color_g(col)*a + 255*(1-a));
			uint32_t b = (uint32_t)(color_b(col)*a + 255*(1-a));
			col = 0xff000000 | (r << 16) | (gg << 8) | b;
		}
		for(size_t i = 0; i < path.size(); i++) {
			vector<Pt>& pts = path[i].pts;
			if(pts.size() < 2) continue;
			size_t n = pts.size();
			size_t lim = path[i].closed ? n : n - 1;
			for(size_t k = 0; k < lim; k++) {
				size_t k2 = (k + 1) % n;
				double x1, y1, x2, y2;
				mpoint(m, pts[k].x, pts[k].y, &x1, &y1);
				mpoint(m, pts[k2].x, pts[k2].y, &x2, &y2);
				graph_wline(g, (int)x1, (int)y1, (int)x2, (int)y2, lw, col);
			}
		}
	}

	void paint(const string& op) {
		bool doFill = false, doStroke = false, eo = false;
		if(op == "f" || op == "F") doFill = true;
		else if(op == "f*") { doFill = true; eo = true; }
		else if(op == "S") doStroke = true;
		else if(op == "s") { closePath(); doStroke = true; }
		else if(op == "B") { doFill = true; doStroke = true; }
		else if(op == "B*") { doFill = true; doStroke = true; eo = true; }
		else if(op == "b") { closePath(); doFill = true; doStroke = true; }
		else if(op == "b*") { closePath(); doFill = true; doStroke = true; eo = true; }
		applyPendingClip();
		if(doFill) fillPath(eo);
		if(doStroke) strokePath();
		path.clear();
		hasCur = false;
	}

	/* ---- text ---- */
	int ascent(PdfFont* f, int px) {
		map<int,int>::iterator it = f->asc.find(px);
		if(it != f->asc.end()) return it->second;
		face_info_t face;
		int a = px;
		if(font_get_face(f->ft, px, &face) == 0 && face.ascender > 0)
			a = face.ascender / 64;
		f->asc[px] = a;
		return a;
	}

	void showString(const string& s) {
		GS& t = gs();
		PdfFont* f = t.font;
		if(f == NULL || f->ft == NULL) return;
		double th = t.tz / 100.0;
		bool hide = hidden();
		size_t i = 0;
		while(i < s.size()) {
			uint32_t code;
			if(f->twoByte) {
				if(i + 1 >= s.size()) break;
				code = ((uint8_t)s[i] << 8) | (uint8_t)s[i+1];
				i += 2;
			}
			else {
				code = (uint8_t)s[i];
				i++;
			}
			double w0 = 500;
			uint32_t uni = code;
			if(f->twoByte) {
				map<int,double>::iterator wit = f->cw.find((int)code);
				w0 = (wit != f->cw.end()) ? wit->second : f->dw;
				map<uint32_t,uint32_t>::iterator uit = f->toUni.find(code);
				uni = (uit != f->toUni.end()) ? uit->second :
						(f->pua ? 0xF0000u + code : code);
			}
			else {
				if(f->hw[code]) w0 = f->w[code];
				else if(f->courier) w0 = 600;
				else if(f->stdW && code >= 32 && code <= 126) w0 = f->stdW[code-32];
				map<int,uint32_t>::iterator eit = f->enc.find((int)code);
				if(eit != f->enc.end()) uni = eit->second;
				else if(code >= 0x80 && code <= 0xFF)
					uni = f->macRoman ? MACROMAN_HI[code-0x80] : WINANSI_HI[code-0x80];
				else if(code < 32) uni = 0;
			}
			double tx = (w0 * 0.001 * t.fs + t.tc + ((code == 32 && !f->twoByte) ? t.tw : 0)) * th;
			if(t.tr != 3 && uni >= 32 && !hide) {
				double trm[6] = {t.fs*th, 0, 0, t.fs, 0, t.ts};
				double m1[6], m2[6];
				mmul(trm, t.tm, m1);
				mmul(m1, t.ctm, m2);
				double px = sqrt(fabs(mdet(m2)));
				if(px >= 3 && px <= 256) {
					double x, y;
					mpoint(m2, 0, 0, &x, &y);
					int ipx = (int)(px + 0.5);
					uint32_t col = blend(t.fill, t.fa);
					graph_draw_unicode_font(g, (int)(x+0.5),
							(int)(y - ascent(f, ipx) + 0.5), uni, f->ft, ipx, col, NULL, NULL);
				}
			}
			double tr[6] = {1, 0, 0, 1, tx, 0};
			mmul(tr, t.tm, t.tm);
		}
	}

	/* ---- images & forms ---- */
	void drawImage(const Obj& dict);
	void doXObject(const string& name);

	/* ---- content stream interpreter ---- */
	void run(const uint8_t* data, int n);
};

}namespace er {

static bool isImageFilter(const Obj& dict) {
	const Obj* f = dict.get("Filter");
	if(f == NULL) return false;
	/* CCITT is decoded to raw 1-bit samples by applyFilters, so it takes
	   the generic sample path, not the jpeg path */
	if(f->t == O_NAME) return f->s == "DCTDecode" || f->s == "JPXDecode";
	if(f->t == O_ARR) {
		for(size_t i = 0; i < f->a.size(); i++)
			if(f->a[i].t == O_NAME && (f->a[i].s == "DCTDecode" || f->a[i].s == "JPXDecode"))
				return true;
	}
	return false;
}

void Render::drawImage(const Obj& dict) {
	int iw = (int)dict.getNum("Width", 0);
	int ih = (int)dict.getNum("Height", 0);
	if(iw <= 0 || ih <= 0 || iw > 8192 || ih > 8192) return;
	if(hidden()) return;
	int bpc = (int)dict.getNum("BitsPerComponent", 8);
	bool mask = dict.getNum("ImageMask", 0) != 0;
	if(mask) bpc = 1;

	vector<uint8_t> samples;
	graph_t* img = NULL;
	/* img pixels are stored straight into img->buffer: graph_pixel() blends
	 * (and drops alpha-0 writes entirely), so it cannot build an alpha plane */
	bool alpha = false;
	bool dct = isImageFilter(dict);
	if(dct) {
		if(dict.t != O_STREAM) return;
		vector<uint8_t> jd;
		if(!doc->decode(dict, jd)) return;
		img = jpeg_image_new_from_data(jd.data(), (uint32_t)jd.size());
		if(img == NULL) return; /* JPX or broken jpeg: skip gracefully */
	}
	else {
		if(!doc->decode(dict, samples)) return;
		/* color space */
		ColorSpace cs;
		const Obj* cspec = dict.get("ColorSpace");
		if(cspec != NULL) setColorSpace(cs, *cspec);
		if(mask) cs.kind = 0;
		int ncomp = (cs.kind == 1) ? 3 : (cs.kind == 2 ? 4 : 1);
		if(cs.kind == 3) ncomp = 1;
		img = graph_new(NULL, iw, ih);
		if(img == NULL) return;
		graph_clear(img, 0xffffffff);
		uint32_t* px = img->buffer;
		bool maskInv = false;
		if(mask) {
			alpha = true;
			const Obj* dec = dict.get("Decode");
			maskInv = dec != NULL && dec->t == O_ARR && dec->a.size() >= 2 &&
				doc->resolve(dec->a[0]).num() > 0.5;
		}
		int stride = (iw * ncomp * bpc + 7) / 8;
		for(int y = 0; y < ih; y++) {
			const uint8_t* row = samples.data() + (size_t)y * stride;
			if((size_t)(y+1) * stride > samples.size()) break;
			for(int x = 0; x < iw; x++) {
				double v[4] = {0, 0, 0, 0};
				if(bpc == 8) {
					for(int c = 0; c < ncomp; c++) {
						size_t off = (size_t)x * ncomp + c;
						v[c] = (off < (size_t)stride) ? row[off] / 255.0 : 0;
					}
				}
				else if(bpc == 1) {
					int bit = (row[x >> 3] >> (7 - (x & 7))) & 1;
					v[0] = bit;
				}
				else if(bpc == 4) {
					uint8_t byte = row[(x * ncomp) >> 1];
					for(int c = 0; c < ncomp; c++) {
						int nib = ((x * ncomp + c) & 1) ? (byte & 15) : (byte >> 4);
						v[c] = nib / 15.0;
					}
				}
				else {
					for(int c = 0; c < ncomp; c++) v[c] = 0.5;
				}
				if(mask) {
					/* stencil: sample 0 paints with the fill colour */
					bool on = (v[0] == 0) != maskInv;
					px[(size_t)y * iw + x] = on ?
						(0xff000000 | (gs().fill & 0xffffff)) : 0x00000000;
					continue;
				}
				if(cs.kind == 3) {
					int idx = (int)(v[0] * ((bpc == 1) ? 1 : (bpc == 4 ? 15 : 255)));
					vector<double> iv;
					iv.push_back(idx);
					uint32_t c;
					colorFrom(cs, iv, c);
					px[(size_t)y * iw + x] = c | 0xff000000;
					continue;
				}
				vector<double> vv(v, v + ncomp);
				uint32_t c;
				colorFrom(cs, vv, c);
				px[(size_t)y * iw + x] = c | 0xff000000;
			}
		}
		/* soft mask */
		const Obj* sm = dict.get("SMask");
		if(sm != NULL) {
			Obj smd = doc->resolve(*sm);
			vector<uint8_t> sd;
			if(smd.t == O_STREAM && doc->decode(smd, sd)) {
				int sw = (int)smd.getNum("Width", iw);
				int sh = (int)smd.getNum("Height", ih);
				int sbpc = (int)smd.getNum("BitsPerComponent", 8);
				int sstride = (sw * sbpc + 7) / 8;
				if(sw > 0 && sh > 0) {
					alpha = true;
					for(int y = 0; y < ih; y++) {
						int sy = y * sh / ih;
						for(int x = 0; x < iw; x++) {
							int sx = x * sw / iw;
							size_t off = (size_t)sy * sstride + (sx * sbpc / 8);
							if(off >= sd.size()) continue;
							uint8_t a = (sbpc == 8) ? sd[off] :
								((sd[off] >> (7 - (sx & 7))) & 1) * 255;
							uint32_t& p = px[(size_t)y * iw + x];
							p = (p & 0xffffff) | ((uint32_t)a << 24);
						}
					}
				}
			}
		}
	}

	/* place the unit square through the CTM */
	double m[6];
	memcpy(m, gs().ctm, sizeof(m));
	double x00, y00, x10, y10, x01, y01;
	mpoint(m, 0, 0, &x00, &y00);
	mpoint(m, 1, 0, &x10, &y10);
	mpoint(m, 0, 1, &x01, &y01);
	if(fabs(m[1]) < 1e-6 && fabs(m[2]) < 1e-6 && m[0] > 0 && m[3] < 0) {
		/* upright, axis-aligned (the common case): scale the whole image onto
		 * its device rect. graph_blt() never scales - it copies the top-left
		 * min(src,dst) corner 1:1 - so the fit variants are required here. */
		int dx = (int)floor(x00 + 0.5);
		int dy = (int)floor(y01 + 0.5);
		int dw = (int)floor(x10 + 0.5) - dx;
		int dh = (int)floor(y00 + 0.5) - dy;
		if(dw < 1) dw = 1;
		if(dh < 1) dh = 1;
		if(dw <= 16384 && dh <= 16384) {
			if(alpha)
				graph_blt_fit_alpha(img, 0, 0, img->w, img->h, g, dx, dy, dw, dh, 255);
			else
				graph_blt_fit(img, 0, 0, img->w, img->h, g, dx, dy, dw, dh);
		}
	}
	else {
		double det = mdet(m);
		if(fabs(det) > 1e-9) {
			double inv[6];
			inv[0] = m[3]/det; inv[1] = -m[1]/det;
			inv[2] = -m[2]/det; inv[3] = m[0]/det;
			inv[4] = (m[2]*m[5] - m[3]*m[4])/det;
			inv[5] = (m[1]*m[4] - m[0]*m[5])/det;
			int bx = (int)floor(x00 < x10 ? (x00 < x01 ? x00 : x01) : (x10 < x01 ? x10 : x01));
			int by = (int)floor(y00 < y10 ? (y00 < y01 ? y00 : y01) : (y10 < y01 ? y10 : y01));
			double x11 = x10 + x01 - x00, y11 = y10 + y01 - y00;
			int bw = (int)ceil(x00 > x10 ? (x00 > x11 ? x00 : x11) : (x10 > x11 ? x10 : x11)) - bx;
			int bh = (int)ceil(y00 > y10 ? (y00 > y11 ? y00 : y11) : (y10 > y11 ? y10 : y11)) - by;
			if(bw > 0 && bh > 0 && bw < 8192 && bh < 8192) {
				for(int y = by; y < by + bh; y++) {
					if(y < 0 || y >= g->h) continue;
					for(int x = bx; x < bx + bw; x++) {
						if(x < 0 || x >= g->w) continue;
						double u, v;
						mpoint(inv, x + 0.5, y + 0.5, &u, &v);
						if(u < 0 || u >= 1 || v < 0 || v >= 1) continue;
						int sx = (int)(u * img->w), sy = (int)((1 - v) * img->h);
						if(sx >= img->w) sx = img->w - 1;
						if(sy >= img->h) sy = img->h - 1;
						uint32_t p = graph_get_pixel(img, sx, sy);
						uint8_t a = p >> 24;
						if(a == 255) graph_pixel(g, x, y, p);
						else if(a > 0)
							graph_pixel_argb(g, x, y, a, color_r(p), color_g(p), color_b(p));
					}
				}
			}
		}
	}
	graph_free(img);
}

void Render::doXObject(const string& name) {
	const Obj* xres = res.get("XObject");
	if(xres == NULL) return;
	Obj xd = doc->resolve(*xres);
	const Obj* entry = xd.get(name.c_str());
	if(entry == NULL) return;
	Obj xo = doc->resolve(*entry);
	if(xo.t != O_STREAM) return;
	string sub = xo.getName("Subtype");
	if(sub == "Image") {
		drawImage(xo);
		return;
	}
	if(sub != "Form" || depth >= 8) return;
	vector<uint8_t> content;
	if(!doc->decode(xo, content)) return;

	stack.push_back(gs());
	GS& g2 = gs();
	const Obj* matrix = xo.get("Matrix");
	if(matrix != NULL && matrix->t == O_ARR && matrix->a.size() >= 6) {
		double fm[6];
		for(int i = 0; i < 6; i++) fm[i] = doc->resolve(matrix->a[i]).num();
		mmul(fm, g2.ctm, g2.ctm);
	}
	const Obj* bbox = xo.get("BBox");
	if(bbox != NULL && bbox->t == O_ARR && bbox->a.size() >= 4) {
		double bx[4];
		for(int i = 0; i < 4; i++) bx[i] = doc->resolve(bbox->a[i]).num();
		double x1, y1, x2, y2;
		mpoint(g2.ctm, bx[0], bx[1], &x1, &y1);
		mpoint(g2.ctm, bx[2], bx[3], &x2, &y2);
		grect_t bb = {(int)floor(x1 < x2 ? x1 : x2), (int)floor(y1 < y2 ? y1 : y2),
			(int)ceil(fabs(x2 - x1)) + 1, (int)ceil(fabs(y2 - y1)) + 1};
		grect_t c = g2.clip, n;
		n.x = bb.x > c.x ? bb.x : c.x;
		n.y = bb.y > c.y ? bb.y : c.y;
		int rx = (bb.x + bb.w) < (c.x + c.w) ? (bb.x + bb.w) : (c.x + c.w);
		int by2 = (bb.y + bb.h) < (c.y + c.h) ? (bb.y + bb.h) : (c.y + c.h);
		n.w = rx - n.x; n.h = by2 - n.y;
		setClipRect(n);
	}
	Obj savedRes = res;
	const Obj* fr = xo.get("Resources");
	if(fr != NULL) res = doc->resolve(*fr);
	depth++;
	path.clear();
	hasCur = false;
	run(content.data(), (int)content.size());
	depth--;
	res = savedRes;
	path.clear();
	hasCur = false;
	stack.pop_back();
	setClipRect(gs().clip);
}

void Render::run(const uint8_t* data, int n) {
	Lex lx(data, n, 0);
	vector<Obj> st;
	st.reserve(32);
	while(true) {
		lx.skip();
		if(lx.p >= lx.n) break;
		uint8_t c = lx.b[lx.p];
		bool isObjStart = (c >= '0' && c <= '9') || c == '+' || c == '-' ||
			c == '.' || c == '/' || c == '(' || c == '<' || c == '[';
		if(isObjStart) {
			int before = lx.p;
			Obj o = lx.parse(0);
			if(lx.p == before) { lx.p++; continue; }
			st.push_back(o);
			if(st.size() > 512) st.erase(st.begin(), st.begin() + 256);
			continue;
		}
		/* keyword operator */
		int start = lx.p;
		while(lx.p < lx.n && !ws(lx.b[lx.p]) && !delim(lx.b[lx.p])) lx.p++;
		string op((const char*)data + start, lx.p - start);
		if(op.empty()) { lx.p++; continue; }

		if(op == "BI") {
			/* inline image: skip to EI */
			while(lx.p < lx.n) {
				if(lx.b[lx.p] == 'E' && lx.p + 2 < lx.n && lx.b[lx.p+1] == 'I' &&
						(lx.p + 2 >= lx.n || ws(lx.b[lx.p+2]))) {
					lx.p += 2;
					break;
				}
				lx.p++;
			}
			st.clear();
			continue;
		}

		/* q/Q change the stack vector: never hold a GS& across them */
		if(op == "q") { stack.push_back(gs()); st.clear(); continue; }
		if(op == "Q") {
			if(stack.size() > 1) stack.pop_back();
			setClipRect(gs().clip);
			path.clear(); hasCur = false;
			st.clear();
			continue;
		}

		GS& g2 = gs();
		if(op == "cm" && st.size() >= 6) {
			double m[6];
			for(int i = 0; i < 6; i++) m[i] = st[st.size()-6+i].num();
			mmul(m, g2.ctm, g2.ctm);
		}
		else if(op == "w" && !st.empty()) g2.lw = st.back().num();
		else if(op == "gs" && !st.empty() && st.back().t == O_NAME) {
			const Obj* er = res.get("ExtGState");
			if(er != NULL) {
				Obj ed = doc->resolve(*er);
				const Obj* e = ed.get(st.back().s.c_str());
				if(e != NULL) {
					Obj es = doc->resolve(*e);
					const Obj* ca = es.get("CA"); if(ca && ca->isNum()) g2.sa = ca->num();
					const Obj* fa = es.get("ca"); if(fa && fa->isNum()) g2.fa = fa->num();
					const Obj* lw = es.get("LW"); if(lw && lw->isNum()) g2.lw = lw->num();
				}
			}
		}
		/* path construction */
		else if(op == "m" && st.size() >= 2) {
			movePt(st[st.size()-2].num(), st[st.size()-1].num());
		}
		else if(op == "l" && st.size() >= 2) {
			linePt(st[st.size()-2].num(), st[st.size()-1].num());
		}
		else if(op == "c" && st.size() >= 6) {
			size_t b = st.size()-6;
			curvePt(st[b].num(), st[b+1].num(), st[b+2].num(), st[b+3].num(),
					st[b+4].num(), st[b+5].num());
		}
		else if(op == "v" && st.size() >= 4) {
			size_t b = st.size()-4;
			curvePt(cur.x, cur.y, st[b].num(), st[b+1].num(), st[b+2].num(), st[b+3].num());
		}
		else if(op == "y" && st.size() >= 4) {
			size_t b = st.size()-4;
			curvePt(st[b].num(), st[b+1].num(), st[b+2].num(), st[b+3].num(),
					st[b+2].num(), st[b+3].num());
		}
		else if(op == "h") closePath();
		else if(op == "re" && st.size() >= 4) {
			size_t b = st.size()-4;
			rectPt(st[b].num(), st[b+1].num(), st[b+2].num(), st[b+3].num());
		}
		else if(op == "W") { pendingClip = true; clipEO = false; }
		else if(op == "W*") { pendingClip = true; clipEO = true; }
		else if(op == "n") { applyPendingClip(); path.clear(); hasCur = false; }
		else if(op == "f" || op == "F" || op == "f*" || op == "S" || op == "s" ||
				op == "B" || op == "B*" || op == "b" || op == "b*") {
			paint(op);
		}
		/* color */
		else if(op == "g" && !st.empty()) {
			g2.fcs.kind = 0;
			vector<double> v; v.push_back(st.back().num());
			colorFrom(g2.fcs, v, g2.fill);
		}
		else if(op == "G" && !st.empty()) {
			g2.scs.kind = 0;
			vector<double> v; v.push_back(st.back().num());
			colorFrom(g2.scs, v, g2.stroke);
		}
		else if((op == "rg" || op == "RG") && st.size() >= 3) {
			vector<double> v;
			for(int i = 0; i < 3; i++) v.push_back(st[st.size()-3+i].num());
			if(op == "rg") { g2.fcs.kind = 1; colorFrom(g2.fcs, v, g2.fill); }
			else { g2.scs.kind = 1; colorFrom(g2.scs, v, g2.stroke); }
		}
		else if((op == "k" || op == "K") && st.size() >= 4) {
			vector<double> v;
			for(int i = 0; i < 4; i++) v.push_back(st[st.size()-4+i].num());
			if(op == "k") { g2.fcs.kind = 2; colorFrom(g2.fcs, v, g2.fill); }
			else { g2.scs.kind = 2; colorFrom(g2.scs, v, g2.stroke); }
		}
		else if((op == "cs" || op == "CS") && !st.empty() && st.back().t == O_NAME) {
			Obj spec;
			spec.t = O_NAME; spec.s = st.back().s;
			if(op == "cs") setColorSpace(g2.fcs, spec);
			else setColorSpace(g2.scs, spec);
		}
		else if(op == "sc" || op == "scn" || op == "SC" || op == "SCN") {
			bool fill = (op == "sc" || op == "scn");
			ColorSpace& cs = fill ? g2.fcs : g2.scs;
			if(!st.empty() && st.back().t == O_NAME) {
				/* pattern: flat fallback */
				cs.kind = 4;
				uint32_t c;
				vector<double> v;
				colorFrom(cs, v, c);
				if(fill) g2.fill = c; else g2.stroke = c;
			}
			else {
				vector<double> v;
				for(size_t i = 0; i < st.size(); i++)
					if(st[i].isNum()) v.push_back(st[i].num());
				uint32_t c;
				colorFrom(cs, v, c);
				if(fill) g2.fill = c; else g2.stroke = c;
			}
		}
		/* text state */
		else if(op == "BT") {
			for(int i = 0; i < 6; i++) { g2.tm[i] = 0; g2.tlm[i] = 0; }
			g2.tm[0] = g2.tm[3] = g2.tlm[0] = g2.tlm[3] = 1;
		}
		else if(op == "ET") { }
		else if(op == "Tf" && st.size() >= 2 && st[st.size()-2].t == O_NAME) {
			const Obj* fres = res.get("Font");
			if(fres != NULL) {
				Obj frd = doc->resolve(*fres);
				const Obj* entry = frd.get(st[st.size()-2].s.c_str());
				if(entry != NULL) {
					long key = (entry->t == O_REF) ? (long)entry->i :
						(long)(strHash(st[st.size()-2].s) & 0x7fffffff) + 1000000000L;
					g2.font = loadFont(doc, fc, *entry, key);
				}
			}
			g2.fs = st.back().num();
		}
		else if(op == "Tc" && !st.empty()) g2.tc = st.back().num();
		else if(op == "Tw" && !st.empty()) g2.tw = st.back().num();
		else if(op == "Tz" && !st.empty()) g2.tz = st.back().num();
		else if(op == "TL" && !st.empty()) g2.tl = st.back().num();
		else if(op == "Ts" && !st.empty()) g2.ts = st.back().num();
		else if(op == "Tr" && !st.empty()) g2.tr = (int)st.back().num();
		else if(op == "Tm" && st.size() >= 6) {
			for(int i = 0; i < 6; i++) {
				g2.tm[i] = st[st.size()-6+i].num();
				g2.tlm[i] = g2.tm[i];
			}
		}
		else if((op == "Td" || op == "TD") && st.size() >= 2) {
			double tx = st[st.size()-2].num(), ty = st[st.size()-1].num();
			double tr[6] = {1, 0, 0, 1, tx, ty};
			mmul(tr, g2.tlm, g2.tlm);
			memcpy(g2.tm, g2.tlm, sizeof(tr));
			if(op == "TD") g2.tl = -ty;
		}
		else if(op == "T*") {
			double tr[6] = {1, 0, 0, 1, 0, -g2.tl};
			mmul(tr, g2.tlm, g2.tlm);
			memcpy(g2.tm, g2.tlm, sizeof(tr));
		}
		else if(op == "Tj" && !st.empty() && st.back().t == O_STR) {
			showString(st.back().s);
		}
		else if(op == "TJ" && !st.empty() && st.back().t == O_ARR) {
			double th = g2.tz / 100.0;
			for(size_t i = 0; i < st.back().a.size(); i++) {
				Obj e = doc->resolve(st.back().a[i]);
				if(e.t == O_STR) showString(e.s);
				else if(e.isNum()) {
					double tx = -e.num() / 1000.0 * g2.fs * th;
					double tr[6] = {1, 0, 0, 1, tx, 0};
					mmul(tr, g2.tm, g2.tm);
				}
			}
		}
		else if(op == "'" && !st.empty() && st.back().t == O_STR) {
			double tr[6] = {1, 0, 0, 1, 0, -g2.tl};
			mmul(tr, g2.tlm, g2.tlm);
			memcpy(g2.tm, g2.tlm, sizeof(tr));
			showString(st.back().s);
		}
		else if(op == "\"" && st.size() >= 3 && st.back().t == O_STR) {
			g2.tw = st[st.size()-3].num();
			g2.tc = st[st.size()-2].num();
			double tr[6] = {1, 0, 0, 1, 0, -g2.tl};
			mmul(tr, g2.tlm, g2.tlm);
			memcpy(g2.tm, g2.tlm, sizeof(tr));
			showString(st.back().s);
		}
		else if(op == "Do" && !st.empty() && st.back().t == O_NAME) {
			doXObject(st.back().s);
		}
		/* ignored: j J M d i sh BX EX BMC BDC EMC DP MP */
		st.clear();
	}
}

/* ============================ PdfBook ================================= */
struct PdfBook::Impl {
	uint8_t* data;
	int size;
	PdfDoc doc;
	FontCache fc;
	pthread_mutex_t mtx;
	bool ok;
	int count;

	Impl() : data(NULL), size(0), ok(false), count(0) {
		pthread_mutex_init(&mtx, NULL);
	}
	~Impl() {
		if(data != NULL) free(data);
	}
};

PdfBook::PdfBook(const string& fname) {
	_fname = fname;
	_title = stripExt(baseName(fname));
	_p = new Impl();
}

PdfBook::~PdfBook() {
	delete _p;
}

bool PdfBook::open() {
	_p->data = vfs_readfile(_fname.c_str(), &_p->size);
	if(_p->data == NULL) {
		printf("ereader: read failed: %s\n", _fname.c_str());
		return false;
	}
	_p->fc.sysFont = font_new("system-cn", true);
	if(_p->fc.sysFont == NULL) {
		printf("ereader: no system font\n");
		return false;
	}
	_p->ok = _p->doc.load(_p->data, _p->size);
	if(!_p->ok) {
		printf("ereader: pdf parse failed: %s\n", _fname.c_str());
		/* nothing will ever be rendered: do not sit on the whole file */
		free(_p->data);
		_p->data = NULL;
	}
	_p->count = _p->ok ? (int)_p->doc.pages.size() : 0;
	return _p->ok;
}

int PdfBook::pageCount() {
	return _p->count;
}

graph_t* PdfBook::renderPage(int idx, int w, int h, uint32_t ink) {
	(void)ink;
	pthread_mutex_lock(&_p->mtx);
	if(!_p->ok || idx < 0 || idx >= (int)_p->doc.pages.size() || w <= 4 || h <= 4) {
		pthread_mutex_unlock(&_p->mtx);
		return NULL;
	}
	Obj page = _p->doc.pages[idx];
	Obj box;
	const Obj* cb = page.get("CropBox");
	const Obj* mb = page.get("MediaBox");
	if(cb != NULL && cb->t == O_ARR && cb->a.size() >= 4) box = *cb;
	else if(mb != NULL && mb->t == O_ARR && mb->a.size() >= 4) box = *mb;
	else {
		pthread_mutex_unlock(&_p->mtx);
		return NULL;
	}
	double bx[4];
	for(int i = 0; i < 4; i++) bx[i] = _p->doc.resolve(box.a[i]).num();
	double pw = bx[2] - bx[0], ph = bx[3] - bx[1];
	if(pw <= 1 || ph <= 1) {
		pthread_mutex_unlock(&_p->mtx);
		return NULL;
	}
	int rot = 0;
	const Obj* rotO = page.get("Rotate");
	if(rotO != NULL) rot = (int)_p->doc.resolve(*rotO).num();
	rot = ((rot % 360) + 360) % 360;
	double dw = (rot == 90 || rot == 270) ? ph : pw;
	double dh = (rot == 90 || rot == 270) ? pw : ph;
	double s = (w / dw < h / dh) ? w / dw : h / dh;
	double ox = (w - dw * s) / 2.0;
	double oy = (h - dh * s) / 2.0;

	graph_t* g = graph_new(NULL, w, h);
	if(g == NULL) {
		pthread_mutex_unlock(&_p->mtx);
		return NULL;
	}
	graph_clear(g, 0xffffffff);

	Obj res;
	const Obj* pr = page.get("Resources");
	if(pr != NULL) res = _p->doc.resolve(*pr);
	Render rd(&_p->doc, &_p->fc, g, res);
	double base[6];
	if(rot == 90) {
		base[0] = 0; base[1] = s; base[2] = -s; base[3] = 0;
		base[4] = ox + bx[3]*s; base[5] = oy - bx[0]*s;
	}
	else if(rot == 180) {
		base[0] = -s; base[1] = 0; base[2] = 0; base[3] = -s;
		base[4] = ox + bx[2]*s; base[5] = oy + bx[3]*s;
	}
	else if(rot == 270) {
		base[0] = 0; base[1] = -s; base[2] = s; base[3] = 0;
		base[4] = ox - bx[1]*s; base[5] = oy + bx[2]*s;
	}
	else {
		base[0] = s; base[1] = 0; base[2] = 0; base[3] = -s;
		base[4] = ox - bx[0]*s; base[5] = oy + ph*s + bx[1]*s;
	}
	memcpy(rd.gs().ctm, base, sizeof(base));

	/* content streams */
	const Obj* contents = page.get("Contents");
	if(contents != NULL) {
		vector<uint8_t> all;
		Obj cr = _p->doc.resolve(*contents);
		if(cr.t == O_STREAM) {
			vector<uint8_t> d;
			if(_p->doc.decode(cr, d)) {
				all.insert(all.end(), d.begin(), d.end());
			}
		}
		else if(cr.t == O_ARR) {
			for(size_t i = 0; i < cr.a.size(); i++) {
				Obj cs = _p->doc.resolve(cr.a[i]);
				vector<uint8_t> d;
				if(cs.t == O_STREAM && _p->doc.decode(cs, d)) {
					all.insert(all.end(), d.begin(), d.end());
					all.push_back('\n');
				}
			}
		}
		if(!all.empty())
			rd.run(all.data(), (int)all.size());
	}
	pthread_mutex_unlock(&_p->mtx);
	return g;
}

}