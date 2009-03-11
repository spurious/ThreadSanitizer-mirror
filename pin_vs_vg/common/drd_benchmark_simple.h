// Copyright 2009 Google Inc. All Rights Reserved.
// Author: timurrrr@google.com (Timur Iskhodzhanov)

#ifndef DRD_BENCHMARK_SIMPLE_H_
#define DRD_BENCHMARK_SIMPLE_H_

class DrdBenchmarkSimple {
  long long dynamic_memory_access_count,
            static_memory_access_count;
  int num_divisions_per_memaccess;
  volatile int a, b;
 public:
  inline void Initialize() {
    dynamic_memory_access_count = 0;
    static_memory_access_count = 0;
    num_divisions_per_memaccess = 5;
    a = 0xDEADBEEF;
    b = 0x1010;
  }

  inline void OnMemAccessInstrumentation(bool is_write) {
    static_memory_access_count++;
  }

  inline void OnMemAccess(bool is_write) {
    dynamic_memory_access_count++;
    for (int i = 0; i < num_divisions_per_memaccess; i++)
      a /= b;
    a = 0xDEADBEEF;
  }

  inline void OnExit(int exit_code) {
    tool_printf("Memory accesses:\n"
                "  instrumented = %lld\n"
                "  executed     = %lld\n"
                "  divisions per instrumentation: %d\n",
                static_memory_access_count,
                dynamic_memory_access_count,
                num_divisions_per_memaccess);
  }

  inline void SetNumDivisionsPerMemAccess(int value) {
    num_divisions_per_memaccess = value;
  }
} benchmark;

#endif  // DRD_BENCHMARK_SIMPLE_H_
