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
// Author: Timur Iskhodzhanov.

// This file contains utility classes and functions used by ThreadSanitizer.
// TODO(kcc): move more utilities from thread_sanitizer.cc to this file.

#ifndef __TS_UTIL_H__
#define __TS_UTIL_H__


class ThreadSanitizerReport;

// Returns true if the error has been recorded.
bool RecordErrorIfNotSuppressed(ThreadSanitizerReport *report);




#endif  // __TS_UTIL_H__
