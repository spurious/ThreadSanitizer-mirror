/*
  This file is part of ThreadSanitizer, a dynamic data race detector.

  Copyright (C) 2008-2009 Google Inc
     opensource@google.com

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License as
  published by the Free Software Foundation; either version 2 of the
  License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
  02111-1307, USA.

  The GNU General Public License is contained in the file COPYING.
*/

// Author: Konstantin Serebryany.
//
// Some libc functions are implemented in a way unfriendly to race detectors
// and memcheck-like tools.
// E.g. strlen() may read up to 7 bytes past the allocated buffer.
// To avoid false positives in these functions, the tool needs to replace these
// funcions with simpler implementation.
//
// The includer must define these macros:
// REPORT_WRITE_RANGE, REPORT_READ_RANGE, EXTRA_REPLACE_PARAMS, NOINLINE
// See ts_valgrind_intercepts.c and ts_pin.cc.

#ifndef TS_REPLACE_H_
#define TS_REPLACE_H_

static NOINLINE char *Replace_memchr(EXTRA_REPLACE_PARAMS const char *s,
                                     int c, size_t n) {
  size_t i;
  char *ret = 0;
  for (i = 0; i < n; i++) {
    if (s[i] == c) {
      ret = (char*)(&s[i]);
      break;
    }
  }
  REPORT_READ_RANGE(s, ret ? i + 1 : n);
  return ret;
}

static NOINLINE char *Replace_strchr(EXTRA_REPLACE_PARAMS const char *s,
                                     int c) {
  size_t i;
  char *ret = 0;
  for (i = 0; s[i]; i++) {
    if (s[i] == c) {
      ret = (char*)(&s[i]);
      break;
    }
  }
  REPORT_READ_RANGE(s, ret ? i + 1 : i);
  return ret;
}

static NOINLINE char *Replace_strrchr(EXTRA_REPLACE_PARAMS const char *s,
                                      int c) {
  char* ret = 0;
  size_t i;
  for (i = 0; s[i]; i++) {
    if (s[i] == c) {
      ret = (char*)&s[i];
    }
  }
  REPORT_READ_RANGE(s, i);
  return ret;
}

static NOINLINE size_t Replace_strlen(EXTRA_REPLACE_PARAMS const char *s) {
  size_t i = 0;
  for (i = 0; s[i]; i++) {
  }
  REPORT_READ_RANGE(s, i);
  return i;
}

static NOINLINE char *Replace_memcpy(EXTRA_REPLACE_PARAMS char *dst,
                                     const char *src, size_t len) {
  size_t i;
  for (i = 0; i < len; i++) {
    dst[i] = src[i];
  }
  REPORT_READ_RANGE(src, i);
  REPORT_WRITE_RANGE(dst, i);
  return dst;
}

static NOINLINE char *Replace_strcpy(EXTRA_REPLACE_PARAMS char *dst,
                                     const char *src) {
  size_t i;
  for (i = 0; src[i]; i++) {
    dst[i] = src[i];
  }
  dst[i] = 0;
  REPORT_READ_RANGE(src, i + 1);
  REPORT_WRITE_RANGE(dst, i + 1);
  return dst;
}

static NOINLINE int Replace_strcmp(EXTRA_REPLACE_PARAMS const char *s1,
                                   const char *s2) {
  unsigned char c1;
  unsigned char c2;
  size_t i;
  for (i = 0; ; i++) {
    c1 = (unsigned char)s1[i];
    c2 = (unsigned char)s2[i];
    if (c1 != c2) break;
    if (c1 == 0) break;
  }
  REPORT_READ_RANGE(s1, i+1);
  REPORT_READ_RANGE(s2, i+1);
  if (c1 < c2) return -1;
  if (c1 > c2) return 1;
  return 0;
}
#endif  // TS_REPLACE_H_
