/* Compiler instrumentation test suite (CITS)
 * Copyright (c) 2011, Google Inc. All rights reserved.
 * Author: Dmitry Vyukov (dvyukov)
 *
 * CITS is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3, or (at your option) any later
 * version. See http://www.gnu.org/licenses/
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <dlfcn.h>

#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <map>

#include "frontend.h"


struct test_mop_desc_t {
  bool                  is_hit;
  bool                  is_multi_allowed;
  bool                  is_optional;
  bool                  is_store;
  bool                  is_sblock_defined;
  bool                  is_sblock;
  bool                  is_msize_defined;
  int                   msize;
  int                   source_line;
};


struct test_desc_t {
  typedef std::vector<test_mop_desc_t>      mop_list_t;
  std::string           name;
  mop_list_t            mops;
};


static std::vector<mop_desc_t> rt_mops;
typedef std::vector<test_desc_t> test_list_t;


void                    read                (...) {}
void                    addr                (void const volatile*) {}
int                     init                () {return 0;}


void                    frontend_mop_cb     (mop_desc_t const& mop) {
  rt_mops.push_back(mop);
}


static std::ostream&    operator <<         (std::ostream&      s,
                                             test_mop_desc_t const& mop) {
  s << (mop.is_store ? "ST" : "LD");
  if (mop.is_sblock_defined)
    s << (mop.is_sblock ? "+" : "-");
  if (mop.is_msize_defined)
    s << "(" << mop.msize << ")";
  if (mop.is_multi_allowed)
    s << "*";
  if (mop.is_optional)
    s << "?";
  s << "@" << mop.source_line;
  return s;
}


static std::ostream&    operator <<         (std::ostream&      s,
                                             mop_desc_t const& mop) {
  s << (mop.is_store ? "ST" : "LD");
  s << (mop.is_sblock ? "+" : "-");
  s << "(" << mop.msize << ")";
  s << "@" << mop.source_line;
  return s;
}


void                    parse_source        (char const*        file_name,
                                             test_list_t&       test_list) {
  FILE* f = fopen(file_name, "rb");
  if (f == 0)
    printf("failed to open file '%s'\n", file_name), exit(1);
  test_desc_t* test = 0;
  int line_no = 0;
  char line [4096];
  for (;;) {
    line_no += 1;
    if (0 == fgets(line, sizeof(line), f))
      break;
    char const* test_prefix = "namespace "; 
    char const* test_postfix = " {\n"; 
    if (strncmp(line, test_prefix, strlen(test_prefix)) == 0
        && strstr(line, test_postfix) != 0) {
      test_desc_t test_desc;
      test_desc.name.assign(line + strlen(test_prefix),
          strlen(line) - strlen(test_prefix) - strlen(test_postfix));
      test_list.push_back(test_desc);
      test = &test_list.back();
      continue;
    }
    if (test == 0)
      continue;
    char const* pos = line;
    for (;;) {
      pos = strstr(pos, "//");
      if (pos == 0)
        break;
      pos += 2;
      test_mop_desc_t mop = {};
      mop.source_line = line_no;
      if (pos[0] == 'L' && pos[1] == 'D')
        mop.is_store = false;
      else if (pos[0] == 'S' && pos[1] == 'T')
        mop.is_store = true;
      else
        continue;
      pos += 2;
      if (pos[0] == '+' || pos[0] == '-') {
        mop.is_sblock_defined = true;
        mop.is_sblock = (pos[0] == '+');
        pos += 1;
      }
      if (pos[0] == '(') {
        pos += 1;
        mop.is_msize_defined = true;
        if (strncmp(pos, "ptr", 3) == 0)
          mop.msize = sizeof(void*);
        else
          mop.msize = atoi(pos);
        pos = strchr(pos, ')');
        if (pos == 0) {
          printf("no ')' after size in annotation on line %u\n", line_no);
          exit(1);
        }
      }
      if (pos[0] == '*') {
        mop.is_multi_allowed = true;
        pos += 1;
      }
      if (pos[0] == '?') {
        mop.is_optional = true;
        pos += 1;
      }
      test->mops.push_back(mop);
    }
  }  
  fclose(f);
}


int main() {
  frontend_init();

  test_list_t test_list;
  parse_source("test_source.cc", test_list);
  
  int failed_count = 0;
  for (size_t test_idx = 0; test_idx != test_list.size(); test_idx += 1) {
    test_desc_t& test = test_list[test_idx];
    int fill_len = 40 - test.name.length();
    if (fill_len < 0)
      fill_len = 0;
    std::cout << test.name << std::string(fill_len, ' ') << "...";
    int ec = 0;

    char mangled [1024];
    snprintf(mangled, sizeof(mangled), "_ZN%u%s4testEv",
             test.name.length(), test.name.c_str());
    void(*func)() = (void(*)())dlsym(RTLD_DEFAULT, mangled);
    if (func == 0) {
      std::cout << "CAN'T LOCATE" << std::endl;
      continue;
    }
    
    frontend_test_begin();
    func();
    std::string error_desc;
    if (false == frontend_test_end(&error_desc)) {
      std::cout << error_desc << std::endl;
      continue;
    }
    
    for (size_t i = 0; i != rt_mops.size(); i += 1) {
      mop_desc_t const& rt_mop = rt_mops[i];
      bool is_matched = false;
      bool is_doubled = false;
      for (size_t j = 0; j != test.mops.size(); j += 1) {
        test_mop_desc_t& test_mop = test.mops[j];
        if (rt_mop.source_line == test_mop.source_line
            && rt_mop.is_store == test_mop.is_store)
        {
          if (test_mop.is_hit == false || test_mop.is_multi_allowed) {
            test_mop.is_hit = true;
            is_matched = true;
            break;          
          } else {
            is_doubled = true;
          }
        }
      }
      if (is_matched == false) {
        if (ec++ == 0)
          std::cout << "FAILED" << std::endl;
        std::cout << (is_doubled ? "DOUBLED      " : "EXCESSIVE    ")
            << rt_mop << std::endl;
      }
    }
    rt_mops.clear();
    
    for (size_t i = 0; i != test.mops.size(); i += 1) {
      test_mop_desc_t const& mop = test.mops[i];
      if (mop.is_hit != mop.is_optional)
        continue;
      if (ec++ == 0)
        std::cout << "FAILED" << std::endl;
      std::cout << (mop.is_hit ? "OPTIONAL     " : "MISSED       ")
          << mop << std::endl;
    }
    
    if (ec == 0)
      std::cout << "OK";
    else 
      failed_count += 1;
    std::cout << std::endl;
  }
 
  std::cout << "failed/total " << failed_count << "/" << test_list.size() << std::endl;

  return 0;
}





