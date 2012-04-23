//===-- tsan_suppressions_test.cc -------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of ThreadSanitizer (TSan), a race detector.
//
//===----------------------------------------------------------------------===//
#include "tsan_suppressions.h"
#include "tsan_rtl.h"
#include "gtest/gtest.h"

#include <string.h>

namespace __tsan {

TEST(Suppressions, Parse) {
  ScopedInRtl in_rtl;
  Suppression *supp = SuppressionParse(
    "foo\n"
    " 	bar\n"  // NOLINT
    "baz	 \n"  // NOLINT
    "# a comment\n"
    "quz\n"
  );  // NOLINT
  EXPECT_EQ(0, strcmp(supp->func, "quz"));
  supp = supp->next;
  EXPECT_EQ(0, strcmp(supp->func, "baz"));
  supp = supp->next;
  EXPECT_EQ(0, strcmp(supp->func, "bar"));
  supp = supp->next;
  EXPECT_EQ(0, strcmp(supp->func, "foo"));
  supp = supp->next;
  EXPECT_EQ((Suppression*)0, supp);
}

TEST(Suppressions, Parse2) {
  ScopedInRtl in_rtl;
  Suppression *supp = SuppressionParse(
    "  	# first line comment\n"  // NOLINT
    " 	bar 	\n"  // NOLINT
    "baz* *baz\n"
    "# a comment\n"
    "# last line comment\n"
  );  // NOLINT
  EXPECT_EQ(0, strcmp(supp->func, "baz* *baz"));
  supp = supp->next;
  EXPECT_EQ(0, strcmp(supp->func, "bar"));
  supp = supp->next;
  EXPECT_EQ((Suppression*)0, supp);
}

static bool MyMatch(const char *templ, const char *func) {
  char tmp[1024];
  strcpy(tmp, templ);  // NOLINT
  return SuppressionMatch(tmp, func);
}

TEST(Suppressions, Match) {
  EXPECT_TRUE(MyMatch("foobar", "foobar"));
  EXPECT_TRUE(MyMatch("foobar", "prefix_foobar_postfix"));
  EXPECT_TRUE(MyMatch("*foobar*", "prefix_foobar_postfix"));
  EXPECT_TRUE(MyMatch("foo*bar", "foo_middle_bar"));
  EXPECT_TRUE(MyMatch("foo*bar", "foobar"));
  EXPECT_TRUE(MyMatch("foo*bar*baz", "foo_middle_bar_another_baz"));
  EXPECT_TRUE(MyMatch("foo*bar*baz", "foo_middle_barbaz"));

  EXPECT_FALSE(MyMatch("foo", "baz"));
  EXPECT_FALSE(MyMatch("foobarbaz", "foobar"));
  EXPECT_FALSE(MyMatch("foobarbaz", "barbaz"));
  EXPECT_FALSE(MyMatch("foo*bar", "foobaz"));
  EXPECT_FALSE(MyMatch("foo*bar", "foo_baz"));
}

}  // namespace __tsan
