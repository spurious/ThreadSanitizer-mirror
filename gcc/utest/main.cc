/* Relite
 * Copyright (c) 2011, Google Inc.
 * All rights reserved.
 * Author: Dmitry Vyukov (dvyukov)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#if 1

#include <pthread.h>
#include <sys/mman.h>
#include <stdio.h>
#include <stdint.h>
#include <memory.h>
#include <malloc.h>
#include <assert.h>
#include <memory>
#include <utility>
#include <typeinfo>
#include <vector>

#include "../rt/relite_rt.h"

//#define SINGLE_TEST
#define RELITE_IGNORE __attribute__((relite_ignore))
//#define RELITE_IGNORE

typedef int Atomic32;
typedef int64_t Atomic64;

void NoBarrier_Store(Atomic32 volatile* a, Atomic32 v) {
  *a = v;
}

void Release_Store(Atomic32 volatile* a, Atomic32 v) {
  *a = v;
}

Atomic32 NoBarrier_Load(Atomic32 const volatile* a) {
  return *a;
}

Atomic32 Acquire_Load(Atomic32 const volatile* a) {
  return *a;
}


void NoBarrier_Store(Atomic64 volatile* a, Atomic64 v) {
  *a = v;
}

void Release_Store(Atomic64 volatile* a, Atomic64 v) {
  *a = v;
}

Atomic64 NoBarrier_Load(Atomic64 const volatile* a) {
  return *a;
}

Atomic64 Acquire_Load(Atomic64 const volatile* a) {
  return *a;
}



struct test_base {
  virtual int setup() = 0;
  virtual void teardown() {}
  virtual void thread(int) = 0;
  virtual ~test_base() {}

  static test_base* instance;

  template<typename test_t>
  static test_base* create(char const** namep) {
    if (namep != 0) {
      char const* name = typeid(test_t).name();
      char const* pos = strstr(name, "test_");
      if (pos)
        name = pos + strlen("test_");
      *namep = name;
      return 0;
    } else {
      return new test_t;
    }
  }

  static void* thread_func(void* p) {
    test_base* self = static_cast<test_base*>(p);
    int const tid = __sync_fetch_and_add(&self->tid_seq, self->reverse ? -1 : 1);
    self->thread(tid);
    return 0;
  }

  struct race_desc_t {
    void* addr;
    size_t size;
    relite_report_type_e type;
    bool hit;
  };

  race_desc_t races [10];
  size_t race_count;
  bool unexpected_races;
  bool reverse;
  int tid_seq;

  void expect_race(void* addr, relite_report_type_e type) {
    race_desc_t& report = races[race_count];
    report.addr = addr;
    report.size = 0;
    report.type = type;
    report.hit = false;
    race_count += 1;
  }

  int report_hook(relite_report_t const* report) RELITE_IGNORE {
    for (size_t i = 0; i != race_count; i += 1) {
      if (races[i].addr == report->addr
          && races[i].type == report->type
          && races[i].hit == false) {
        //printf("EXPECTED RACE on %p type %d\n", report->addr, report->type);
        races[i].hit = true;
        //races.erase(races.begin() + i);
        return 1;
      }
    }
    printf("UNEXPECTED %s on %p\n",
           relite_report_str(report->type),
           report->addr);
    /*
    for (size_t i = 0; i != races.size(); i += 1) {
      printf(" WAITING FOR %s on %p\n",
          relite_report_str(races[i].type),
          races[i].addr);
    }
    */

    unexpected_races = true;
    return 0;
  }

  int init(bool reverse) {
    assert(instance == 0);
    instance = this;
    //reverse = !reverse;
    unexpected_races = false;
    race_count = 0;
    this->reverse = reverse;
    int thread_count = setup();
    tid_seq = reverse ? thread_count - 1 : 0;
    return thread_count;
  }

  bool finalize() {
    teardown();

    bool missed_races = false;
    for (size_t i = 0; i != race_count; i += 1) {
      if (races[i].hit == false) {
        missed_races = true;
        printf("UNDETECTED %s on %p\n",
            relite_report_str(races[i].type),
            races[i].addr);
      }
    }

    assert(instance == this);
    instance = 0;
    return (unexpected_races == 0 && missed_races == false);
  }
};

test_base* test_base::instance;


//static extern "C" int relite_report_hook(relite_report_t const* report) RELITE_IGNORE;


int report_hook(relite_report_t const* report) {
  if (test_base::instance == 0) {
    printf("NO TEST INSTANCE\n");
    return 0;
  }
  return test_base::instance->report_hook(report);
}


struct test_distinct_norace : test_base {
  int var [2];

  virtual int setup() {
    return 2;
  }

  virtual void thread(int tid) {
    if (tid == 0) {
      var[0] = 1;
    } else {
      var[1] = 1;
    }
  }
};


struct test_load_load_norace : test_base {
  int var;

  virtual int setup() {
    var = 0;
    return 2;
  }

  virtual void thread(int tid) {
    int r = var;
    (void)r;
  }
};


struct test_store_load_race : test_base {
  int var;

  virtual int setup() {
    var = 0;
    expect_race(&var, relite_report_race_load);
    return 2;
  }

  virtual void thread(int tid) {
    if (tid == 0) {
      var = 1;
    } else {
      int r = var;
      (void)r;
    }
  }
};


struct test_store_store_race : test_base {
  int var;

  virtual int setup() {
    expect_race(&var, relite_report_race_store);
    return 2;
  }

  virtual void thread(int tid) {
    var = tid;
  }
};


struct test_call_norace : test_base {
  pthread_mutex_t mtx;
  int var1;
  int var2;

  virtual int setup() {
    var1 = 0;
    var2 = 0;
    pthread_mutex_init(&mtx, 0);
    return 2;
  }

  virtual void teardown() {
    pthread_mutex_destroy(&mtx);
  }

  virtual void thread(int tid) {
    if (tid == 0) {
      foo(var1);
    } else {
      for (;;) {
        pthread_mutex_lock(&mtx);
        int r = var2;
        pthread_mutex_unlock(&mtx);
        if (r != 0)
          break;
        pthread_yield();
      }
      var1 = 2;
    }
  }

  void foo(int val) {
    (void)val;
    pthread_mutex_lock(&mtx);
    var2 = 1;
    pthread_mutex_unlock(&mtx);
  }
};


struct test_mutex_norace : test_base {
  int var;
  pthread_mutex_t mtx;

  virtual int setup() {
    var = 0;
    pthread_mutex_init(&mtx, 0);
    return 2;
  }

  virtual void teardown() {
    pthread_mutex_destroy(&mtx);
  }

  virtual void thread(int tid) {
    pthread_mutex_lock(&mtx);
    var += 1;
    pthread_mutex_unlock(&mtx);
  }
};


struct test_distinct_mutex_race : test_base {
  int var;
  pthread_mutex_t mtx [2];

  virtual int setup() {
    expect_race(&var, relite_report_race_store);
    pthread_mutex_init(&mtx[0], 0);
    pthread_mutex_init(&mtx[1], 0);
    return 2;
  }

  virtual void teardown() {
    pthread_mutex_destroy(&mtx[0]);
    pthread_mutex_destroy(&mtx[1]);
  }

  virtual void thread(int tid) {
    pthread_mutex_lock(&mtx[tid]);
    var = tid;
    pthread_mutex_unlock(&mtx[tid]);
  }
};


struct test_memset_race : test_base {
  int var;

  virtual int setup() {
    var = 0;
    expect_race(&var, relite_report_race_load);
    return 2;
  }

  virtual void thread(int tid) {
    if (tid) {
      memset(&var, 0, sizeof(var));
    } else {
      int r;
      memcpy(&r, &var, sizeof(var));
    }
  }
};


struct test_unitialized_malloc : test_base {
  int* var;

  virtual int setup() {
    var = (int*)malloc(sizeof(int));
    expect_race(var, relite_report_unitialized);
    return 1;
  }

  virtual void teardown() {
    free(var);
  }

  virtual void thread(int tid) {
    int r = *var;
    (void)r;
  }
};


struct test_unitialized_new : test_base {
  int* var;

  virtual int setup() {
    var = new int;
    expect_race(var, relite_report_unitialized);
    return 1;
  }

  virtual void teardown() {
    delete var;
  }

  virtual void thread(int tid) {
    int r = *var;
    (void)r;
  }
};


struct test_buffer_overrun : test_base {
  int* var;

  virtual int setup() {
    var = (int*)malloc(2 * sizeof(int));
    expect_race(var + 2, relite_report_out_of_bounds);
    return 1;
  }

  virtual void teardown() {
    free(var);
  }

  virtual void thread(int tid) {
    for (int i = 0; i != 3; i += 1) {
      int* p = &var[i];
      *p = i;
    }
  }
};


struct test_buffer_underrun : test_base {
  int* var;

  virtual int setup() {
    var = (int*)malloc(2 * sizeof(int));
    expect_race(var - 1, relite_report_out_of_bounds);
    return 1;
  }

  virtual void teardown() {
    free(var);
  }

  virtual void thread(int tid) {
    for (int i = -1; i != 2; i += 1) {
      var[i] = i;
    }
  }
};


struct test_atomic_norace : test_base {
  Atomic32 var;

  virtual int setup() {
    return 2;
  }

  virtual void thread(int tid) {
    var = tid;
  }
};


struct test_atomic_sync : test_base {
  Atomic32 flag;
  int data;

  virtual int setup() {
    flag = 0;
    return 2;
  }

  virtual void thread(int tid) {
    if (tid) {
      data = 1;
      Release_Store(&flag, 1);
    }
    else {
      while (Acquire_Load(&flag) == 0)
        pthread_yield();
      int r = data;
      assert(r == 1);
    }
  }
};


struct test_atomic_race : test_base {
  Atomic32 flag;
  int data;

  virtual int setup() {
    flag = 0;
    expect_race(&data, relite_report_race_load);
    return 2;
  }

  virtual void thread(int tid) {
    if (tid) {
      data = 1;
      NoBarrier_Store(&flag, 1);
    }
    else {
      while (NoBarrier_Load(&flag) == 0)
        pthread_yield();
      int r = data;
      assert(r == 1);
    }
  }
};


struct test_malloc_race : test_base {
  Atomic64 flag;

  virtual int setup() {
    flag = 0;
    return 2;
  }

  virtual void thread(int tid) {
    if (tid) {
      void* data = malloc(sizeof(int));
      expect_race(data, relite_report_race_store);
      NoBarrier_Store(&flag, (int64_t)data);
    }
    else {
      while (NoBarrier_Load(&flag) == 0)
        pthread_yield();
      void* data = (void*)NoBarrier_Load(&flag);
      free(data);
    }
  }
};


struct test_mmap_race : test_base {
  Atomic64 flag;

  virtual int setup() {
    flag = 0;
    return 2;
  }

  virtual void thread(int tid) {
    if (tid) {
      void* data = mmap(0, sizeof(int),
                        PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
      assert(data != 0);
      expect_race(data, relite_report_race_store);
      NoBarrier_Store(&flag, (int64_t)data);
    }
    else {
      while (NoBarrier_Load(&flag) == 0)
        pthread_yield();
      void* data = (void*)NoBarrier_Load(&flag);
      munmap(data, sizeof(int));
    }
  }
};


struct test_mem_reuse : test_base {
  Atomic64 flag;

  virtual int setup() {
    flag = 0;
    return 2;
  }

  virtual void thread(int tid) {
    if (tid) {
      int* data = (int*)malloc(sizeof(int));
      data[0] = tid;
      free(data);
      NoBarrier_Store(&flag, 1);
    }
    else {
      while (NoBarrier_Load(&flag) == 0)
        pthread_yield();
      int* data = (int*)malloc(sizeof(int));
      data[0] = tid;
      free(data);
    }
  }
};


typedef test_base* (*test_ctor) (char const**);
test_ctor tests [] = {
#ifndef SINGLE_TEST
    &test_base::create<test_distinct_norace>,
    &test_base::create<test_load_load_norace>,
    &test_base::create<test_store_load_race>,
    &test_base::create<test_store_store_race>,
    &test_base::create<test_call_norace>,

    &test_base::create<test_mutex_norace>,
    &test_base::create<test_distinct_mutex_race>,

    &test_base::create<test_memset_race>,

    &test_base::create<test_unitialized_malloc>,
    &test_base::create<test_unitialized_new>,
    &test_base::create<test_buffer_overrun>,
    &test_base::create<test_buffer_underrun>,

    &test_base::create<test_atomic_norace>,
    &test_base::create<test_atomic_sync>,
    &test_base::create<test_atomic_race>,

    &test_base::create<test_malloc_race>,
    &test_base::create<test_mmap_race>,
    &test_base::create<test_mem_reuse>,
#else
    &test_base::create<test_call_norace>,
#endif
};


int main()
{
  relite_report_hook(report_hook);
  for (int test = 0; test != sizeof(tests)/sizeof(tests[0]); test += 1) {
    char const* name = 0;
    tests[test](&name);
    printf("%-24s...", name);
    bool success = true;
#ifndef SINGLE_TEST
    int const reverse_count = 2;
    int const repeat_count = 10;
#else
    int const reverse_count = 1;
    int const repeat_count = 1;
#endif
    for (int reverse = 0; reverse != reverse_count && success; reverse += 1) {
      for (int repeat = 0; repeat != repeat_count && success; repeat += 1) {
        std::auto_ptr<test_base> test_ptr (tests[test](0));
        int thread_count = test_ptr->init(reverse != 0);
        std::vector<pthread_t> threads (thread_count);
        for (int i = 0; i != thread_count; i += 1)
          pthread_create(&threads[i], 0, &test_base::thread_func, test_ptr.get());
        void* res;
        for (int i = 0; i != thread_count; i += 1)
          pthread_join(threads[i], &res);
        success = test_ptr->finalize();
      }
    }
    if (success)
      printf("OK\n");
  }

  char buf [16];
  printf("attach a debugger and press ENTER");
  scanf("%1c", buf);

  return 0;
}



#else

#include <stdlib.h>

struct QWER {
  int ttt;
  QWER() {
    ttt = rand();
  }
};

QWER qwerty;

int main() {
}

#endif




