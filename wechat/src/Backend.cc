#include "Util.h"
#include <time.h>
#include <stdio.h>

void utf8PopBack(string& s) {
	if(s.empty())
		return;
	size_t i = s.length() - 1;
	/* walk back over continuation bytes (10xxxxxx) to the lead byte */
	while(i > 0 && (s[i] & 0xC0) == 0x80)
		i--;
	s.erase(i);
}

void utf8Append(string& s, uint32_t v) {
	if(v == 0)
		return;
	if(v < 0x80) {
		s += (char)v;
	}
	else if(v < 0x800) {
		s += (char)(0xC0 | (v >> 6));
		s += (char)(0x80 | (v & 0x3F));
	}
	else if(v < 0x10000) {
		s += (char)(0xE0 | (v >> 12));
		s += (char)(0x80 | ((v >> 6) & 0x3F));
		s += (char)(0x80 | (v & 0x3F));
	}
	else {
		s += (char)(0xF0 | (v >> 18));
		s += (char)(0x80 | ((v >> 12) & 0x3F));
		s += (char)(0x80 | ((v >> 6) & 0x3F));
		s += (char)(0x80 | (v & 0x3F));
	}
}

string timeHHMM(uint64_t t) {
	time_t tt = (time_t)t;
	struct tm* tm = localtime(&tt);
	char buf[16];
	if(tm == NULL)
		return "";
	snprintf(buf, sizeof(buf), "%02d:%02d", tm->tm_hour, tm->tm_min);
	return buf;
}