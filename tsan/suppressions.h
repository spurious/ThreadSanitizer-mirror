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

// Author: Evgeniy Stepanov.

// This file contains the parser and matcher for valgrind-compatible
// suppressions. It supports extended suppression syntax, see details at
// http://code.google.com/p/data-race-test/wiki/ThreadSanitizerSuppressions

#ifndef TSAN_SUPPRESSIONS_H_
#define TSAN_SUPPRESSIONS_H_

#include "ts_util.h"

class Suppressions {
 public:
  Suppressions();
  ~Suppressions();

  // Read suppressions file from string. May be called several times.
  // Return the number of parsed suppressions or -1 if an error occured.
  int ReadFromString(const string &str);

  // Returns the string describing the last error. Undefined if there was no
  // error.
  string GetErrorString();

  // Returns the line number of the last error. Undefined if there was no error.
  int GetErrorLineNo();

  // Checks if a given stack trace is suppressed.
  bool StackTraceSuppressed(string tool_name, string warning_name,
      const vector<string>& function_names_mangled,
      const vector<string>& function_names_demangled,
      const vector<string>& object_names,
      string *name_of_suppression);

 private:
  struct SuppressionsRep;
  SuppressionsRep* rep_;
};

#endif  // TSAN_SUPPRESSIONS_H_
