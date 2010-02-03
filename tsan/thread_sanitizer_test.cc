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

// This file contains tests for various parts of ThreadSanitizer.

#include <gtest/gtest.h>

#include "ts_heap_info.h"
#include "ts_simple_cache.h"

// Testing the HeapMap.
struct TestHeapInfo {
  uintptr_t ptr;
  uintptr_t size;
  int       val;
  TestHeapInfo() : ptr(0), size(0), val(0) { }
  TestHeapInfo(uintptr_t p, uintptr_t s, uintptr_t v) :
    ptr(p), size(s), val(v) { }
};

TEST(ThreadSanitizer, HeapInfoTest) {
  HeapMap<TestHeapInfo> map;
  TestHeapInfo info;
  EXPECT_EQ(0U, map.size());
  EXPECT_FALSE(map.GetInfo(12345, &info));
  EXPECT_FALSE(map.IsHeapMem(12345, &info));

  // Insert range [1000, 1000+100) with value 1.
  map.InsertInfo(1000, TestHeapInfo(1000, 100, 1));
  EXPECT_EQ(1U, map.size());
  EXPECT_TRUE(map.GetInfo(1000, &info));
  EXPECT_EQ(1000U, info.ptr);
  EXPECT_EQ(100U, info.size);
  EXPECT_EQ(1, info.val);

  EXPECT_TRUE(map.IsHeapMem(1000, &info));
  EXPECT_EQ(1, info.val);
  EXPECT_TRUE(map.IsHeapMem(1050, &info));
  EXPECT_EQ(1, info.val);
  EXPECT_TRUE(map.IsHeapMem(1099, &info));
  EXPECT_EQ(1, info.val);
  EXPECT_FALSE(map.IsHeapMem(1100, &info));
  EXPECT_FALSE(map.IsHeapMem(2000, &info));

  EXPECT_FALSE(map.GetInfo(2000, &info));
  EXPECT_FALSE(map.GetInfo(3000, &info));

  // Insert range [2000, 2000+200) with value 2.
  map.InsertInfo(2000, TestHeapInfo(2000, 200, 2));
  EXPECT_EQ(2U, map.size());

  EXPECT_TRUE(map.GetInfo(1000, &info));
  EXPECT_EQ(1, info.val);

  EXPECT_TRUE(map.GetInfo(2000, &info));
  EXPECT_EQ(2, info.val);

  EXPECT_TRUE(map.IsHeapMem(1000, &info));
  EXPECT_EQ(1, info.val);
  EXPECT_TRUE(map.IsHeapMem(1050, &info));
  EXPECT_EQ(1, info.val);
  EXPECT_TRUE(map.IsHeapMem(1099, &info));
  EXPECT_EQ(1, info.val);
  EXPECT_FALSE(map.IsHeapMem(1100, &info));

  EXPECT_TRUE(map.IsHeapMem(2000, &info));
  EXPECT_EQ(2, info.val);
  EXPECT_TRUE(map.IsHeapMem(2199, &info));
  EXPECT_EQ(2, info.val);

  EXPECT_FALSE(map.GetInfo(2200, &info));
  EXPECT_FALSE(map.GetInfo(3000, &info));

  // Insert range [3000, 3000+300) with value 3.
  map.InsertInfo(3000, TestHeapInfo(3000, 300, 3));
  EXPECT_EQ(3U, map.size());

  EXPECT_TRUE(map.GetInfo(1000, &info));
  EXPECT_EQ(1, info.val);

  EXPECT_TRUE(map.GetInfo(2000, &info));
  EXPECT_EQ(2, info.val);

  EXPECT_TRUE(map.GetInfo(3000, &info));
  EXPECT_EQ(3, info.val);

  EXPECT_TRUE(map.IsHeapMem(1050, &info));
  EXPECT_EQ(1, info.val);

  EXPECT_TRUE(map.IsHeapMem(2100, &info));
  EXPECT_EQ(2, info.val);

  EXPECT_TRUE(map.IsHeapMem(3200, &info));
  EXPECT_EQ(3, info.val);

  // Remove range [2000,2000+200)
  map.EraseInfo(2000);
  EXPECT_EQ(2U, map.size());

  EXPECT_TRUE(map.IsHeapMem(1050, &info));
  EXPECT_EQ(1, info.val);

  EXPECT_FALSE(map.IsHeapMem(2100, &info));

  EXPECT_TRUE(map.IsHeapMem(3200, &info));
  EXPECT_EQ(3, info.val);

}

TEST(ThreadSanitizer, PtrToBoolCacheTest) {
  PtrToBoolCache<256> c;
  bool val;
  EXPECT_FALSE(c.Lookup(123, &val));

  c.Insert(0, false);
  c.Insert(1, true);
  c.Insert(2, false);
  c.Insert(3, true);

  EXPECT_TRUE(c.Lookup(0, &val));
  EXPECT_EQ(false, val);
  EXPECT_TRUE(c.Lookup(1, &val));
  EXPECT_EQ(true, val);
  EXPECT_TRUE(c.Lookup(2, &val));
  EXPECT_EQ(false, val);
  EXPECT_TRUE(c.Lookup(3, &val));
  EXPECT_EQ(true, val);

  EXPECT_FALSE(c.Lookup(256, &val));
  EXPECT_FALSE(c.Lookup(257, &val));
  EXPECT_FALSE(c.Lookup(258, &val));
  EXPECT_FALSE(c.Lookup(259, &val));

  c.Insert(0, true);
  c.Insert(1, false);

  EXPECT_TRUE(c.Lookup(0, &val));
  EXPECT_EQ(true, val);
  EXPECT_TRUE(c.Lookup(1, &val));
  EXPECT_EQ(false, val);
  EXPECT_TRUE(c.Lookup(2, &val));
  EXPECT_EQ(false, val);
  EXPECT_TRUE(c.Lookup(3, &val));
  EXPECT_EQ(true, val);

  c.Insert(256, false);
  c.Insert(257, false);
  EXPECT_FALSE(c.Lookup(0, &val));
  EXPECT_FALSE(c.Lookup(1, &val));
  EXPECT_TRUE(c.Lookup(2, &val));
  EXPECT_EQ(false, val);
  EXPECT_TRUE(c.Lookup(3, &val));
  EXPECT_EQ(true, val);
  EXPECT_TRUE(c.Lookup(256, &val));
  EXPECT_EQ(false, val);
  EXPECT_TRUE(c.Lookup(257, &val));
  EXPECT_EQ(false, val);
}

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
