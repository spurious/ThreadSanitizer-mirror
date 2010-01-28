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

#ifndef TS_REPLACE_H_
#define TS_REPLACE_H_


static NOINLINE void *Replace_memchr(const char *s, int c, size_t n) {
  size_t i;
  for (i = 0; i < n; i++)
    if (s[i] == c) return (void*)(&s[i]);
  return 0;
}

static NOINLINE size_t Replace_strlen(const char *s) {
  size_t res = 0;
  while (s[res])
    res++;
  return res;
}

static NOINLINE void *Replace_strchr(const char *s, int c) {
  while (*s) {
    if (*s == c)
      return (char*)s;
    s++;
  }
  return 0;
}

static NOINLINE char *Replace_strrchr(const char *s, int c) {
  char* ret = 0;
  size_t i = 0;
  while (s[i] != 0) {
    if (s[i] == c)
      ret = (char*)&s[i];
    i++;
  }
  return ret;
}

static NOINLINE char *Replace_memcpy(char *dst, const char *src, size_t len) {
  size_t i;
  for (i = 0; i < len; i++) {
    dst[i] = src[i];
  }
  return dst;
}

static NOINLINE char *Replace_strcpy(char *dst, const char *src) {
  size_t i;
  for (i = 0; src[i]; i++) {
    dst[i] = src[i];
  }
  dst[i] = 0;
  return dst;
}

static NOINLINE int Replace_strcmp(const char *s1, const char *s2) {
  // TODO(kcc): do we need to handle locales, etc?
  unsigned char c1;
  unsigned char c2;
  while (1) {
    c1 = *(unsigned char *)s1;
    c2 = *(unsigned char *)s2;
    if (c1 != c2) break;
    if (c1 == 0) break;
    s1++; s2++;
  }
  if (c1 < c2) return -1;
  if (c1 > c2) return 1;
  return 0;
}
#endif  // TS_REPLACE_H_
