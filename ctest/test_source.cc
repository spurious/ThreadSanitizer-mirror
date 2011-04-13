/* Compiler instrumentation test suite (CITS)
 * Copyright (c) 2011, Google Inc. All rights reserved.
 * Author: Dmitry Vyukov (dvyukov)
 *
 * CITS is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3, or (at your option) any later
 * version. See http://www.gnu.org/licenses/
 */


/*
Annotation syntax is as follows:
"//" ("LD"/"ST") ["+"/"-"] ["(" (integer/"ptr") ")"] ["*"] ["?"]
"LD"/"ST" is load/store
"+"/"-" is "superblock start"/"no superblock start"
"(" (integer/"ptr") ")" is memory access size
                              ("ptr" stands for "sizeof(void*)")
"*" is "the memory access can be repeated several times"
"?" is "the memory access is optional"

So the sortest form is just //LD or //ST
However it can be //LD+(8)*? as well, which means
"load of size 8 which starts a new superblock,
which can be repeated several times but optional"
*/ 


// Some helper functions for tests:
void read(...); // as-if reads a variable
void addr(void const volatile*); // makes a var addressable


namespace local_non_addressable {
void test() {
  int var1;
  var1 = 0;
  read(var1);
  int var2 = 0;
  read(var2);
}
}


namespace local_addressable {
void test() {
  int var1;
  addr(&var1);
  var1 = 0;         //ST+(4)
  read(var1);       //LD-(4)
  int var2 = 0;     //ST+(4)
  addr(&var2);
  read(var2);       //LD+(4)
}
}


namespace vptr_load {
void test() {
  struct X {
    virtual void foo() {}
    virtual void bar() {}
  };

  X* x = new X;     //ST+(ptr)
  x->foo();         //LD+(ptr)
  x->bar();         //LD+(ptr)
}
}


namespace simple_aliasing {
void test() {
  int var;
  addr(&var);
  var = 1;          //ST
  read(var);        //LD?
}
}


namespace bitfield_access {
struct X {
  unsigned m0 : 3;
  unsigned m1 : 5;
  unsigned m2 : 12;
  unsigned m3 : 1;
  short    m4 : 1;
  short    m5 : 2;
};

void test() {
  unsigned volatile tmp;
  X* x = new X;
  x->m0 = 0;        //ST(4)
  addr(x);
  x->m1 = 0;        //ST(4)
  addr(x);
  tmp = x->m2;      //LD(4)
  addr(x);
  x->m3 = 0;        //ST(4)
  addr(x);
  x->m4 = 0;        //ST(2)
  addr(x);
  tmp = x->m5;      //LD(2)
  addr(x);
  (void)tmp;
}
}




