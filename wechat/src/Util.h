#ifndef WECHAT_UTIL_HH
#define WECHAT_UTIL_HH

#include <string>
#include <stdint.h>

using namespace std;

/* UTF-8 aware editing helpers: EditLine's stock backspace chops one byte,
 * which corrupts multi-byte CJK characters. */
void utf8PopBack(string& s);
void utf8Append(string& s, uint32_t v);

/* "HH:MM" for a unix timestamp (local time). */
string timeHHMM(uint64_t t);

#endif