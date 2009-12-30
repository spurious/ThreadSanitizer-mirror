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

// This file contains the parser for valgrind-compatible suppressions.

#include "suppressions.h"

// TODO(eugenis): convert checks to warning messages.
// TODO(eugenis): write tests for incorrect syntax.

enum LocationType {
  LT_STAR, // ...
  LT_OBJ, // obj:
  LT_FUN, // fun:
};

struct Location {
  LocationType type;
  string name;
};

struct StackTraceTemplate {
  vector<Location> locations;
};

struct Suppression {
  string name;
  set<string> tools;
  string warning_name;
  // Extra information available for some suppression types.
  // ex.: Memcheck:Param
  string extra;
  vector<StackTraceTemplate> templates;
};

class Parser {
 public:
  Parser(const string &str)
      : buffer(str), next(buffer.c_str()),
        end(buffer.c_str() + buffer.size()) {}

  bool NextSuppression(Suppression*);

 private:
  bool Eof() { return next >= end; }
  string NextLine();
  string NextLineSkipComments();
  void PutBackSkipComments(string line);
  void ParseSuppressionToolsLine(Suppression* supp, string line);
  bool IsExtraLine(string line);
  bool ParseStackTraceLine(StackTraceTemplate* trace, string line);
  bool NextStackTraceTemplate(StackTraceTemplate* trace, bool* last);

  const string& buffer;
  const char* next;
  const char* end;
  stack<string> putBackStack;
};

string Parser::NextLine() {
  const char* first = next;
  while (!Eof() && *next != '\n') {
    ++next;
  }
  string line(first, next - first);
  if (*next == '\n') {
    ++next;
  }
  return line;
}

string Parser::NextLineSkipComments() {
  string line;
  if (!putBackStack.empty()) {
    line = putBackStack.top();
    putBackStack.pop();
    return line;
  }
  while (!Eof()) {
    line = NextLine();
    // Skip empty lines.
    if (line.empty())
      continue;
    // Skip comments.
    if (line[0] == '#')
      continue;
    const char* p = line.c_str();
    const char* e = p + line.size();
    // Strip whitespace.
    while (p < e && (*p == ' ' || *p == '\t'))
      ++p;
    if (p >= e)
      continue;
    const char* last = e - 1;
    while (last > p && (*last == ' ' || *last == '\t'))
      --last;
    return string(p, last - p + 1);
  }
  return "";
}

void Parser::PutBackSkipComments(string line) {
  putBackStack.push(line);
}

void Parser::ParseSuppressionToolsLine(Suppression* supp, string line) {
  size_t idx = line.find(':');
  CHECK(idx != string::npos);
  string s1 = line.substr(0, idx);
  string s2 = line.substr(idx + 1);
  CHECK(!s1.empty());
  CHECK(!s2.empty());
  size_t idx2;
  while ((idx2 = s1.find(',')) != string::npos) {
    supp->tools.insert(s1.substr(0, idx2));
    s1.erase(0, idx2 + 1);
  }
  supp->tools.insert(s1);
  supp->warning_name = s2;
}

bool Parser::ParseStackTraceLine(StackTraceTemplate* trace, string line) {
  if (line == "...") {
    Location location = {LT_STAR, ""};
    trace->locations.push_back(location);
    return true;
  } else {
    size_t idx = line.find(':');
    CHECK(idx != string::npos);
    string s1 = line.substr(0, idx);
    string s2 = line.substr(idx + 1);
    if (s1 == "obj") {
      Location location = {LT_OBJ, s2};
      trace->locations.push_back(location);
      return true;
    } else if (s1 == "fun") {
      Location location = {LT_FUN, s2};
      trace->locations.push_back(location);
      return true;
    }
  }
  return false;
}

// Checks if this line can not be parsed by Parser::NextStackTraceTemplate
// and, therefore, is an extra information for the suppression.
bool Parser::IsExtraLine(string line) {
  if (line == "..." || line == "{" || line == "}")
    return false;
  if (line.size() < 4)
    return true;
  string prefix = line.substr(0, 4);
  return !(prefix == "obj:" || prefix == "fun:");
}

bool Parser::NextStackTraceTemplate(StackTraceTemplate* trace, bool* last_stack_trace) {
  string line = NextLineSkipComments();
  if (line == "}") { // No more stack traces in multi-trace syntax
    *last_stack_trace = true;
    return false;
  }

  if (line == "{") { // A multi-trace syntax
    line = NextLineSkipComments();
  } else {
    *last_stack_trace = true;
  }

  while (true) {
    CHECK(ParseStackTraceLine(trace, line));
    line = NextLineSkipComments();
    if (line == "}")
      break;
  }
  return true;
}

bool Parser::NextSuppression(Suppression* supp) {
  string line;
  line = NextLineSkipComments();
  if (line.empty())
    return false;
  // Opening {
  CHECK(line == "{");
  // Suppression name.
  line = NextLineSkipComments();
  CHECK(!line.empty());
  supp->name = line;
  // tool[,tool]:warning_name.
  line = NextLineSkipComments();
  CHECK(!line.empty());
  ParseSuppressionToolsLine(supp, line);
  if (0) {  // Not used currently. May still be needed later.
    // A possible extra line.
    line = NextLineSkipComments();
    if (IsExtraLine(line))
      supp->extra = line;
    else
      PutBackSkipComments(line);
  }
  // Everything else.
  bool done = false;
  while (!done) {
    StackTraceTemplate trace;
    if (NextStackTraceTemplate(&trace, &done))
      supp->templates.push_back(trace);
  }
  // TODO(eugenis): Do we need to check for empty traces?
  return true;
}

struct Suppressions::SuppressionsRep {
  vector<Suppression> suppressions;
};

Suppressions::Suppressions() : rep_(new SuppressionsRep) {}

Suppressions::~Suppressions() {
  delete rep_;
}

int Suppressions::ReadFromString(const string &str) {
  Parser parser(str);
  Suppression supp;
  while (parser.NextSuppression(&supp)) {
    rep_->suppressions.push_back(supp);
  }
  return rep_->suppressions.size();
}

struct MatcherContext {
  MatcherContext(
      const vector<string>& function_names_mangled_,
      const vector<string>& function_names_demangled_,
      const vector<string>& object_names_) :
      function_names_mangled(function_names_mangled_),
      function_names_demangled(function_names_demangled_),
      object_names(object_names_),
      tmpl(NULL)
  {}

  const vector<string>& function_names_mangled;
  const vector<string>& function_names_demangled;
  const vector<string>& object_names;
  StackTraceTemplate* tmpl;
};

static bool MatchStackTraceRecursive(MatcherContext ctx, int trace_index,
    int tmpl_index) {
  const int trace_size = ctx.function_names_mangled.size();
  const int tmpl_size = ctx.tmpl->locations.size();
  while (trace_index < trace_size && tmpl_index < tmpl_size) {
    Location& location = ctx.tmpl->locations[tmpl_index];
    if (location.type == LT_STAR) {
      ++tmpl_index;
      while (trace_index < trace_size) {
        if (MatchStackTraceRecursive(ctx, trace_index++, tmpl_index))
          return true;
      }
      return false;
    } else {
      bool match = false;
      if (location.type == LT_OBJ) {
        match = StringMatch(location.name, ctx.object_names[trace_index]);
      } else {
        CHECK(location.type == LT_FUN);
        match =
          StringMatch(location.name, ctx.function_names_mangled[trace_index]) ||
          StringMatch(location.name, ctx.function_names_demangled[trace_index]);
      }
      if (match) {
        ++trace_index;
        ++tmpl_index;
      } else {
        return false;
      }
    }
  }
  return tmpl_index == tmpl_size;
}

bool Suppressions::StackTraceSuppressed(string tool_name, string warning_name,
    const vector<string>& function_names_mangled,
    const vector<string>& function_names_demangled,
    const vector<string>& object_names,
    string *name_of_suppression) {
  MatcherContext ctx(function_names_mangled, function_names_demangled,
      object_names);
  for (vector<Suppression>::iterator it = rep_->suppressions.begin();
       it != rep_->suppressions.end(); ++it) {
    if (it->warning_name != warning_name ||
        it->tools.find(tool_name) == it->tools.end())
      continue;
    for (vector<StackTraceTemplate>::iterator it2 = it->templates.begin();
         it2 != it->templates.end(); ++it2) {
      ctx.tmpl = &*it2;
      bool result = MatchStackTraceRecursive(ctx, 0, 0);
      if (result) {
        *name_of_suppression = it->name;
        return true;
      }
    }
  }
  return false;
}
