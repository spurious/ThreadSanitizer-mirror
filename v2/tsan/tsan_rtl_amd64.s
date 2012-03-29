.section .text

.globl __tsan_trace_switch_thunk
__tsan_trace_switch_thunk:
  push %rax
  push %rcx
  push %rdx
  push %rsi
  push %rdi
  push %r8
  push %r9
  push %r10
  push %r11
  call __tsan_trace_switch
  pop %r11
  pop %r10
  pop %r9
  pop %r8
  pop %rdi
  pop %rsi
  pop %rdx
  pop %rcx
  pop %rax
  ret

.globl __tsan_report_race_thunk
__tsan_report_race_thunk:
  push %rax
  push %rcx
  push %rdx
  push %rsi
  push %rdi
  push %r8
  push %r9
  push %r10
  push %r11
  call __tsan_report_race
  pop %r11
  pop %r10
  pop %r9
  pop %r8
  pop %rdi
  pop %rsi
  pop %rdx
  pop %rcx
  pop %rax
  ret

