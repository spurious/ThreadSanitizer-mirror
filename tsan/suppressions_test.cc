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

// This file contains tests for suppressions implementation.

#include <gtest/gtest.h>

#include "suppressions.h"

#define VEC(arr) *(new vector<string>(arr, arr + sizeof(arr) / sizeof(*arr)))

class BaseSuppressionsTest : public ::testing::Test {
 protected:
  bool IsSuppressed(string tool, string warning_type, const vector<string>& f_m,
      const vector<string>& f_d, const vector<string>& o) {
    string result;
    return supp_.StackTraceSuppressed(
        tool, warning_type, f_m, f_d, o, &result);
  }

  bool IsSuppressed(const vector<string>& f_m, const vector<string>& f_d,
      const vector<string>& o) {
    return IsSuppressed("test_tool", "test_warning_type", f_m, f_d, o);
  }

  Suppressions supp_;
};

class SuppressionsTest : public BaseSuppressionsTest {
 protected:
  virtual void SetUp() {
    const string data =
        "{\n"
        "  name\n"
        "  test_tool,tool2:test_warning_type\n"
        "  fun:function1\n"
        "  obj:object1\n"
        "  fun:function2\n"
        "}";
    supp_.ReadFromString(data);
  }
};


TEST_F(SuppressionsTest, Simple) {
  string m[] = {"aa", "bb", "cc"};
  string d[] = {"aaa", "bbb", "ccc"};
  string o[] = {"object1", "object2", "object3"};
  ASSERT_FALSE(IsSuppressed(VEC(m), VEC(d), VEC(o)));
}

TEST_F(SuppressionsTest, Simple2) {
  string m[] = {"function1", "bb", "function2"};
  string d[] = {"aaa", "bbb", "ccc"};
  string o[] = {"object2", "object1", "object3"};
  ASSERT_TRUE(IsSuppressed(VEC(m), VEC(d), VEC(o)));
}

// A long stack trace is ok.
TEST_F(SuppressionsTest, LongTrace) {
  string m[] = {"function1", "bb", "function2", "zz"};
  string d[] = {"aaa", "bbb", "ccc", "zzz"};
  string o[] = {"object2", "object1", "object3", "o4"};
  ASSERT_TRUE(IsSuppressed(VEC(m), VEC(d), VEC(o)));
}

// A stack trace template only matches at the top of the stack.
TEST_F(SuppressionsTest, OnlyMatchesAtTheTop) {
  string m[] = {"zz", "function1", "bb", "function2"};
  string d[] = {"zzz", "aaa", "bbb", "ccc"};
  string o[] = {"o0", "object2", "object1", "object3"};
  ASSERT_FALSE(IsSuppressed(VEC(m), VEC(d), VEC(o)));
}

// A short stack trace is not.
TEST_F(SuppressionsTest, ShortTrace) {
  string m[] = {"function1", "bb"};
  string d[] = {"aaa", "bbb"};
  string o[] = {"object2", "object1"};
  ASSERT_FALSE(IsSuppressed(VEC(m), VEC(d), VEC(o)));
}

class SuppressionsWithWildcardsTest : public BaseSuppressionsTest {
 protected:
  virtual void SetUp() {
    const string data =
        "{\n"
        "  name\n"
        "  test_tool,tool2:test_warning_type\n"
        "  fun:fun*1\n"
        "  obj:obj*t1\n"
        "  ...\n"
        "  fun:f?n*2\n"
        "}";
    supp_.ReadFromString(data);
  }
};

TEST_F(SuppressionsWithWildcardsTest, Wildcards1) {
  string m[] = {"function1", "bb", "function2"};
  string d[] = {"aaa", "bbb", "ccc"};
  string o[] = {"object2", "object1", "object3"};
  ASSERT_TRUE(IsSuppressed(VEC(m), VEC(d), VEC(o)));
}

TEST_F(SuppressionsWithWildcardsTest, Wildcards2) {
  string m[] = {"some_other_function1", "bb", "function2"};
  string d[] = {"aaa", "bbb", "ccc"};
  string o[] = {"object2", "object1", "object3"};
  ASSERT_FALSE(IsSuppressed(VEC(m), VEC(d), VEC(o)));
}

TEST_F(SuppressionsWithWildcardsTest, Wildcards3) {
  string m[] = {"fun1", "bb", "fanction2"};
  string d[] = {"aaa", "bbb", "ccc"};
  string o[] = {"object2", "objt1", "object3"};
  ASSERT_TRUE(IsSuppressed(VEC(m), VEC(d), VEC(o)));
}

// Tests "..." wildcard.
TEST_F(SuppressionsWithWildcardsTest, VerticalWildcards1) {
  string m[] = {"fun1", "bb", "qq", "fanction2"};
  string d[] = {"aaa", "bbb", "ddd", "ccc"};
  string o[] = {"object2", "objt1", "object3", "object4"};
  ASSERT_TRUE(IsSuppressed(VEC(m), VEC(d), VEC(o)));
}


class MultipleStackTraceTest : public BaseSuppressionsTest {
 protected:
  virtual void SetUp() {
    const string data =
        "{\n"
        "  name\n"
        "  test_tool,tool2:test_warning_type\n"
        "  {\n"
        "    fun:fun*1\n"
        "  }\n"
        "  {\n"
        "    fun:fun*2\n"
        "    fun:fun*3\n"
        "  }\n"
        "  {\n"
        "    ...\n"
        "    fun:fun*4\n"
        "    obj:obj*5\n"
        "  }\n"
        "}";
    supp_.ReadFromString(data);
  }
};

TEST_F(MultipleStackTraceTest, Simple1) {
  string m[] = {"fun1", "bb", "qq", "fun2"};
  string d[] = {"aaa", "bbb", "ddd", "ccc"};
  string o[] = {"object1", "object2", "object3", "object4"};
  ASSERT_TRUE(IsSuppressed(VEC(m), VEC(d), VEC(o)));
}

TEST_F(MultipleStackTraceTest, SecondTemplateMatches) {
  string m[] = {"fun2", "fun3", "qq", "fun2"};
  string d[] = {"aaa", "bbb", "ddd", "ccc"};
  string o[] = {"object1", "object2", "object3", "object4"};
  ASSERT_TRUE(IsSuppressed(VEC(m), VEC(d), VEC(o)));
}

TEST_F(MultipleStackTraceTest, ThirdTemplateMatches) {
  string m[] = {"fun4", "bb", "qq", "fun2"};
  string d[] = {"aaa", "bbb", "ddd", "ccc"};
  string o[] = {"object1", "object5", "object3", "object4"};
  ASSERT_TRUE(IsSuppressed(VEC(m), VEC(d), VEC(o)));
}

TEST_F(MultipleStackTraceTest, NothingMatches) {
  string m[] = {"_fun1", "bb", "qq", "fun2"};
  string d[] = {"aaa", "bbb", "ddd", "ccc"};
  string o[] = {"object1", "object2", "object3", "object4"};
  ASSERT_FALSE(IsSuppressed(VEC(m), VEC(d), VEC(o)));
}

TEST_F(MultipleStackTraceTest, TwoTemplatesMatch) {
  string m[] = {"fun1", "bb", "fun4", "fun2"};
  string d[] = {"aaa", "bbb", "ddd", "ccc"};
  string o[] = {"object1", "object2", "object3", "object5"};
  ASSERT_TRUE(IsSuppressed(VEC(m), VEC(d), VEC(o)));
}


TEST_F(BaseSuppressionsTest, StartsWithVerticalWildcard) {
  const string data =
      "{\n"
      "  name\n"
      "  test_tool:test_warning_type\n"
      "  ...\n"
      "  fun:qq\n"
      "}";
  supp_.ReadFromString(data);
  string m[] = {"fun1", "bb", "qq", "function2"};
  string d[] = {"aaa", "bbb", "ddd", "ccc"};
  string o[] = {"object2", "objt1", "object3", "object4"};
  ASSERT_TRUE(IsSuppressed(VEC(m), VEC(d), VEC(o)));
}

TEST_F(BaseSuppressionsTest, StartsWithVerticalWildcard2) {
  const string data =
      "{\n"
      "  name\n"
      "  test_tool:test_warning_type\n"
      "  ...\n"
      "  fun:fun1\n"
      "}";
  supp_.ReadFromString(data);
  string m[] = {"fun1", "bb", "qq", "function2"};
  string d[] = {"aaa", "bbb", "ddd", "ccc"};
  string o[] = {"object2", "objt1", "object3", "object4"};
  ASSERT_TRUE(IsSuppressed(VEC(m), VEC(d), VEC(o)));
}

TEST_F(BaseSuppressionsTest, EndsWithVerticalWildcard) {
  const string data =
      "{\n"
      "  name\n"
      "  test_tool:test_warning_type\n"
      "  fun:fun1\n"
      "  ...\n"
      "}";
  supp_.ReadFromString(data);
  string m[] = {"fun1", "bb", "qq", "function2"};
  string d[] = {"aaa", "bbb", "ddd", "ccc"};
  string o[] = {"object2", "objt1", "object3", "object4"};
  ASSERT_TRUE(IsSuppressed(VEC(m), VEC(d), VEC(o)));
}

TEST_F(BaseSuppressionsTest, EndsWithVerticalWildcard2) {
  const string data =
      "{\n"
      "  name\n"
      "  test_tool:test_warning_type\n"
      "  fun:qq\n"
      "  ...\n"
      "}";
  supp_.ReadFromString(data);
  string m[] = {"fun1", "bb", "qq", "function2"};
  string d[] = {"aaa", "bbb", "ddd", "ccc"};
  string o[] = {"object2", "objt1", "object3", "object4"};
  ASSERT_FALSE(IsSuppressed(VEC(m), VEC(d), VEC(o)));
}

TEST_F(BaseSuppressionsTest, Complex) {
  const string data =
      "{\n"
      "  name\n"
      "  test_tool:test_warning_type\n"
      "  fun:qq\n"
      "  ...\n"
      "  obj:obj*3\n"
      "  ...\n"
      "  fun:function?\n"
      "}";
  supp_.ReadFromString(data);
  string m[] = {"fun1", "bb", "qq", "function2"};
  string d[] = {"aaa", "bbb", "ddd", "ccc"};
  string o[] = {"object2", "objt1", "object3", "object4"};
  ASSERT_FALSE(IsSuppressed(VEC(m), VEC(d), VEC(o)));
}

TEST_F(BaseSuppressionsTest, DemangledNames) {
  const string data =
      "{\n"
      "  name\n"
      "  test_tool:test_warning_type\n"
      "  cxx:bb*w?\n"
      "}";
  supp_.ReadFromString(data);
  string m[] = {"fun1", "bb", "qq", "function2"};
  string d[] = {"bbbxxwz", "aaa", "ddd", "ccc"};
  string o[] = {"object2", "objt1", "object3", "object4"};
  ASSERT_TRUE(IsSuppressed(VEC(m), VEC(d), VEC(o)));
}

TEST_F(BaseSuppressionsTest, TrailingWhitespace) {
  const string data =
      "{\n"
      "  name\n"
      "  test_tool:test_warning_type\n"
      "  cxx:bb*w? \n"
      "}";
  supp_.ReadFromString(data);
  string m[] = {"fun1", "bb", "qq", "function2"};
  string d[] = {"bbbxxwz", "aaa", "ddd", "ccc"};
  string o[] = {"object2", "objt1", "object3", "object4"};
  ASSERT_TRUE(IsSuppressed(VEC(m), VEC(d), VEC(o)));
}

TEST(WildcardTest, Simple) {
  ASSERT_TRUE(StringMatch("abc", "abc"));
  ASSERT_FALSE(StringMatch("abcd", "abc"));
  ASSERT_FALSE(StringMatch("dabc", "abc"));
  ASSERT_FALSE(StringMatch("ab", "abc"));
  ASSERT_FALSE(StringMatch("", "abc"));
  ASSERT_FALSE(StringMatch("abc", ""));
  ASSERT_TRUE(StringMatch("", ""));
}

TEST(WildcardTest, SingleCharacterWildcard) {
  ASSERT_TRUE(StringMatch("a?c", "abc"));
  ASSERT_TRUE(StringMatch("?bc", "abc"));
  ASSERT_TRUE(StringMatch("ab?", "abc"));
  ASSERT_TRUE(StringMatch("a??", "abc"));
  ASSERT_TRUE(StringMatch("???", "abc"));
  ASSERT_TRUE(StringMatch("?", "a"));
  ASSERT_FALSE(StringMatch("?zc", "abc"));
  ASSERT_FALSE(StringMatch("?bz", "abc"));
  ASSERT_FALSE(StringMatch("b?c", "abc"));
  ASSERT_FALSE(StringMatch("az?", "abc"));
  ASSERT_FALSE(StringMatch("abc?", "abc"));
  ASSERT_FALSE(StringMatch("?abc", "abc"));
  ASSERT_FALSE(StringMatch("?", ""));
  ASSERT_FALSE(StringMatch("??", ""));
}

TEST(WildcardTest, MultiCharacterWildcard) {
  ASSERT_TRUE(StringMatch("a*d", "abcd"));
  ASSERT_TRUE(StringMatch("ab*d", "abcd"));
  ASSERT_TRUE(StringMatch("*cd", "abcd"));
  ASSERT_TRUE(StringMatch("*d", "abcd"));
  ASSERT_TRUE(StringMatch("ab*", "abcd"));
  ASSERT_TRUE(StringMatch("a*", "abcd"));
  ASSERT_TRUE(StringMatch("*", "abcd"));
  ASSERT_TRUE(StringMatch("ab*cd", "abcd"));

  ASSERT_TRUE(StringMatch("ab**", "abcd"));
  ASSERT_TRUE(StringMatch("**", "abcd"));
  ASSERT_TRUE(StringMatch("***", "abcd"));
  ASSERT_TRUE(StringMatch("**d", "abcd"));
  ASSERT_TRUE(StringMatch("*c*", "abcd"));
  ASSERT_TRUE(StringMatch("a*c*d*f", "abcdef"));
  ASSERT_TRUE(StringMatch("a*c*e*", "abcdef"));
  ASSERT_TRUE(StringMatch("*a*b*f", "abcdef"));
  ASSERT_TRUE(StringMatch("*b*d*", "abcdef"));

  ASSERT_FALSE(StringMatch("b*", "abcd"));
  ASSERT_FALSE(StringMatch("*c", "abcd"));
  ASSERT_FALSE(StringMatch("*a", "abcd"));
}

TEST(WildcardTest, WildcardCharactersInText) {
  ASSERT_TRUE(StringMatch("?", "?"));
  ASSERT_FALSE(StringMatch("a", "?"));
  ASSERT_FALSE(StringMatch("ab", "a?"));
  ASSERT_FALSE(StringMatch("ab", "?b"));
  ASSERT_TRUE(StringMatch("a?", "a?"));
  ASSERT_TRUE(StringMatch("?b", "?b"));

  ASSERT_TRUE(StringMatch("*", "*"));
  ASSERT_FALSE(StringMatch("a", "*"));
  ASSERT_FALSE(StringMatch("ab", "a*"));
  ASSERT_FALSE(StringMatch("ab", "*b"));
  ASSERT_TRUE(StringMatch("a*", "a*"));
  ASSERT_TRUE(StringMatch("*b", "*b"));
}

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}
