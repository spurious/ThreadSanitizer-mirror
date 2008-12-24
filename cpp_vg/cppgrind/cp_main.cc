// TODO: license!

extern "C" {
#include "pub_tool_basics.h"
#include "pub_tool_tooliface.h"
#include "pub_tool_libcassert.h"
#include "pub_tool_libcbase.h"
#include "pub_tool_libcprint.h"
}

#ifndef USE_STLPORT
// using coregrind/m_stl
#include "m_stl/std/vector"
#include "m_stl/std/string"
#include "m_stl/std/map"
#include "m_stl/std/set"

using namespace std;
#else
// using cppgrind/stlport
#include "stlport/vector"
#include "stlport/string"
#include "stlport/map"
#include "stlport/set"

using namespace stlport;
#endif

struct ltstr
{
  bool operator()(const char* s1, const char* s2) const
  {
    return VG_(strcmp)((const Char*)s1, (const Char*)s2) < 0;
  }
};

///////////////// Vector {{{1
inline void DoVectorTest() {
  vector<int> V;
  V.insert(V.begin(), 3);
  tl_assert(V.size() == 1 && V.capacity() >= 1 && V[0] == 3);
} // DoVectorTest()

///////////////// Map {{{1
inline void DoMapTest() {
  map<const char*, int, ltstr> months;

  months["january"] = 31;
  months["february"] = 28;
  months["march"] = 31;
  months["april"] = 30;
  months["may"] = 31;
  months["june"] = 30;
  months["july"] = 31;
  months["august"] = 31;
  months["september"] = 30;
  months["october"] = 31;
  months["november"] = 30;
  months["december"] = 31;
  
  VG_(printf)("june -> %d\n", months["june"]);
  map<const char*, int, ltstr>::iterator cur  = months.find("june");
  map<const char*, int, ltstr>::iterator prev = cur;
  map<const char*, int, ltstr>::iterator next = cur;
  ++next;
  --prev;
  VG_(printf)("Previous (in alphabetical order) is %s\n", (*prev).first);
  VG_(printf)("Next (in alphabetical order) is %s\n", (*next).first);
} // DoMapTest()

///////////////// Set {{{1
inline void DoSetTest() {
  set<int> tree;
  tree.insert(1);
  tree.insert(2);

  {
    set<int>::reverse_iterator rit(tree.rbegin());
    tl_assert( *(rit++) == 2 );
    tl_assert( *(rit++) == 1 );
    tl_assert( rit == tree.rend() );
  }

  {
    set<int> const& ctree = tree;
    set<int>::const_reverse_iterator rit(ctree.rbegin());
    tl_assert( *(rit++) == 2 );
    tl_assert( *(rit++) == 1 );
    tl_assert( rit == ctree.rend() );
  }
} // DoSetTest()

///////////////// String {{{1
inline void DoStringTest() {
#if 1
  string s(10u, ' ');           // Create a string of ten blanks.

  const char* A = "this is a test";
  s += A;
  VG_(printf)("s = %s", (s + '\n').c_str());
  VG_(printf)("As a null-terminated sequence: %s\n", s.c_str());
  VG_(printf)("The sixteenth character is %c\n", s[15]);

  //reverse(s.begin(), s.end());
  s.push_back('\n');
  VG_(printf)("%s\n", s.c_str());
#endif
} // DoStringTest()

///////////////// Valgrind Tool stuff {{{1
static void cp_post_clo_init(void)
{
}

static
IRSB* cp_instrument ( VgCallbackClosure* closure,
                      IRSB* bb,
                      VexGuestLayout* layout, 
                      VexGuestExtents* vge,
                      IRType gWordTy, IRType hWordTy )
{
    return bb;
}

static void cp_fini(Int exitcode)
{
}

static void cp_pre_clo_init(void)
{
   VG_(details_name)            ((Char*)"Cppgrind");
   VG_(details_version)         ((Char*)0);
   VG_(details_description)     ((Char*)"a binary JIT-compiler");
   VG_(details_copyright_author)((Char*)
      "Copyright (C) 2002-2008, and GNU GPL'd, by Nicholas Nethercote.");
   VG_(details_bug_reports_to)  ((Char*)VG_BUGS_TO);

   VG_(basic_tool_funcs)        (cp_post_clo_init,
                                 cp_instrument,
                                 cp_fini);

   DoVectorTest();
   DoMapTest();
   DoSetTest();
   DoStringTest();
   /* No needs, no core events to track */
}

VG_DETERMINE_INTERFACE_VERSION(cp_pre_clo_init)

/*--------------------------------------------------------------------*/
/*--- end                                                          ---*/
/*--------------------------------------------------------------------*/
