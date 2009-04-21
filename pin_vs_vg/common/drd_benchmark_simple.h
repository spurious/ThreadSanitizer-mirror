// Copyright 2009 Google Inc. All Rights Reserved.
// Author: timurrrr@google.com (Timur Iskhodzhanov)

#ifndef DRD_BENCHMARK_SIMPLE_H_
#define DRD_BENCHMARK_SIMPLE_H_

long GetTimeInMilliseconds(void);

struct DrdBenchmarkSimple {
  long long dynamic_memory_access_count,
            static_memory_access_count;
  int num_divisions_per_memaccess;
  volatile int a, b;
  long start_time;
} benchmark;

inline void Benchmark_Initialize(void);
inline void Benchmark_Initialize(void) {
  benchmark.dynamic_memory_access_count = 0;
  benchmark.static_memory_access_count = 0;
  benchmark.num_divisions_per_memaccess = 5;
  benchmark.a = 0xDEADBEEF;
  benchmark.b = 0x1010;
  benchmark.start_time = GetTimeInMilliseconds();
}

inline void Benchmark_OnMemAccessInstrumentation(bool is_write);
inline void Benchmark_OnMemAccessInstrumentation(bool is_write) {
  benchmark.static_memory_access_count++;
}

inline void Benchmark_OnMemAccess(bool is_write);
inline void Benchmark_OnMemAccess(bool is_write) {
  int i;
  benchmark.dynamic_memory_access_count++;
  for (i = 0; i < benchmark.num_divisions_per_memaccess; i++)
    benchmark.a /= benchmark.b;
  benchmark.a = 0xDEADBEEF;
}

inline void Benchmark_OnExit(int exit_code);
inline void Benchmark_OnExit(int exit_code) {
  long elapsed_time = GetTimeInMilliseconds() - benchmark.start_time;
  tool_printf("Memory accesses:\n"
              "  instrumented = %lld\n"
              "  executed     = %lld\n"
              "  divisions per instrumentation: %d\n",
              benchmark.static_memory_access_count,
              benchmark.dynamic_memory_access_count,
              benchmark.num_divisions_per_memaccess);
  tool_printf("Elapsed time: %ldms\n", elapsed_time);
}

inline void Benchmark_SetNumDivisionsPerMemAccess(int value);
inline void Benchmark_SetNumDivisionsPerMemAccess(int value) {
  benchmark.num_divisions_per_memaccess = value;
}

#endif  // DRD_BENCHMARK_SIMPLE_H_
