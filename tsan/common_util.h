#ifndef TSAN_COMMON_UTIL__
#define TSAN_COMMON_UTIL__

#include "ts_util.h"

bool StringMatch(const string& wildcard, const string& text);
string ConvertToPlatformIndependentPath(const string &s);

#endif
