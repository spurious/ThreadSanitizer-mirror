/* Relite: GCC instrumentation plugin for ThreadSanitizer
 * Copyright (c) 2011, Google Inc. All rights reserved.
 * Author: Dmitry Vyukov (dvyukov)
 *
 * Relite is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3, or (at your option) any later
 * version. See http://www.gnu.org/licenses/
 */

#ifndef RELITE_IGNORE_H_INCLUDED
#define RELITE_IGNORE_H_INCLUDED
#ifdef __cplusplus
extern "C" {
#endif


typedef enum relite_ignore_e {
  relite_ignore_none    = 1 << 0,
  relite_ignore_mop     = 1 << 1,
  relite_ignore_rec     = 1 << 2,
  relite_ignore_hist    = 1 << 3,
} relite_ignore_e;


void                    relite_ignore_init  (char const* file_name);
int                     relite_ignore_file  (char const* file);
relite_ignore_e         relite_ignore_func  (char const* func);


#ifdef __cplusplus
}
#endif
#endif

