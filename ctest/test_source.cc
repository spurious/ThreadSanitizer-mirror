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
int  init(); // generates some random value
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
  int volatile var1;
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
    X() {}          //ST+(ptr)
    virtual void foo() {}
    virtual void bar() {}
  };

  X* x = new X;
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


namespace tailcall {
void tailcall() __attribute__((noinline));
void tailcall() {
  volatile int var = 0;
}

void test() {
  tailcall();
}
}


namespace constructor_expr {
struct X {
  int x;
  int y;
};

void test() {
  int x = init();   //ST
  addr(&x);
  int y = init();   //ST
  addr(&y);
  X xx = {x, y};    //LD //LD
  read(xx.x, xx.y);
}
}


namespace addressable_parameter {
void foo(int p) {
  addr(&p);
  p = 1;            //ST
}

void test() {
  foo(0);
}
}


namespace mop_in_condition {
void test() {
  int var1;
  int volatile var2;
  addr(&var1);
  if (var1)         //LD
    var2 = 0;
  else 
    var2 = 1;
}
}


namespace array_ref {
void test() {
  int arr [10];
  addr(arr);
  arr[1] = 1;       //ST
  arr[2] = 2;       //ST
}
}


namespace induced_store {
void foo(int* arr, int count, int* var) {
  for (int i = 0; i != count; i += 1) {
    if (arr[i])
      *var += 1;
  }
}

void test() {
  int arr [10];
  addr(arr);
  int var;
  addr(&var);
  foo(arr, init(), &var);
}
}


namespace vptr_store_static {
struct X {
  X() {}            //ST+(ptr)
  virtual void foo() {}
  virtual void bar() {}
  virtual ~X() {}   //ST+(ptr)
};

struct Y : X {
  Y() {}            //ST+(ptr)
  virtual ~Y() {}   //LD+(ptr)
};

void test() {
  {Y y;}
}
}


namespace vptr_store_dynamic {
struct X {
  X() {}            //ST+(ptr)
  virtual void foo() {}
  virtual void bar() {}
  virtual ~X() {}   //ST+(ptr)
};

struct Y : X {
  Y() {}            //ST+(ptr)
  virtual ~Y() {}   //LD+(ptr)
};

void test() {
  X* x = new Y;
  delete x;         //LD+(ptr)
}
}













