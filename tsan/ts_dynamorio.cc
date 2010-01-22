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

// Some parts of the code in this file are taken from the examples
// in DynamoRIO distribution, which have the following copyright.
/* **********************************************************
 * Copyright (c) 2003-2008 VMware, Inc.  All rights reserved.
 * **********************************************************/

/*
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * * Neither the name of VMware, Inc. nor the names of its contributors may be
 *   used to endorse or promote products derived from this software without
 *   specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL VMWARE, INC. OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 */

// Author: Konstantin Serebryany.
// Author: Timur Iskhodzhanov.
//
// ******* WARNING ********
// This code is experimental. Do not expect anything here to work.
// ***** END WARNING ******

#include "dr_api.h"

#include "ts_util.h"


#define Printf dr_printf

static void *g_lock;
static int   g_n_created_threads;

string *g_main_module_path;

//--------------- StackFrame ----------------- {{{1
struct StackFrame {
  uintptr_t pc;
  uintptr_t sp;
  StackFrame(uintptr_t p, uintptr_t s) : pc(p), sp(s) { }
};


//--------------- DrThread ----------------- {{{1
struct DrThread {
  int tid;  // A unique 0-based thread id.
  vector<StackFrame> shadow_stack;
};

static DrThread &GetCurrentThread(void *drcontext) {
  return *(DrThread*)dr_get_tls_field(drcontext);
}

//--------------- ShadowStack ----------------- {{{1
#define DEB_PR (0 && t.tid == 1)

static void PrintShadowStack(DrThread &t) {
  Printf("T%d Shadow stack (%d)\n", t.tid, (int)t.shadow_stack.size());
  for (int i = t.shadow_stack.size() - 1; i >= 0; i--) {
    uintptr_t pc = t.shadow_stack[i].pc;
    Printf("%s[%p]\n", g_main_module_path->c_str(), pc);
  }
  for (int i = t.shadow_stack.size() - 1; i >= 0; i--) {
    uintptr_t pc = t.shadow_stack[i].pc;
    uintptr_t sp = t.shadow_stack[i].sp;
    Printf("  sp=%p pc=%p\n", sp, pc);
  }
}

static void UpdateShadowStack(DrThread &t, uintptr_t sp) {
  while (t.shadow_stack.size() > 0 && sp >= t.shadow_stack.back().sp) {
    t.shadow_stack.pop_back();
    if (DEB_PR) {
      dr_mutex_lock(g_lock);
      Printf("T%d PopShadowStack\n", t.tid);
      PrintShadowStack(t);
      dr_mutex_unlock(g_lock);
    }
  }
}

static void PushShadowStack(DrThread &t, uintptr_t pc, uintptr_t target_pc, uintptr_t sp) {
  if (t.shadow_stack.size() > 0) {
    t.shadow_stack.back().pc = pc;
  }
  t.shadow_stack.push_back(StackFrame(target_pc, sp));
  if (DEB_PR) {
    dr_mutex_lock(g_lock);
    Printf("T%d PushShadowStack %p %p %d\n", t.tid, pc, target_pc, sp);
    PrintShadowStack(t);
    dr_mutex_unlock(g_lock);
  }
}

//--------------- callbacks ----------------- {{{1
static void OnEvent_ThreadInit(void *drcontext) {
  DrThread *t_ptr = new DrThread;
  DrThread &t = *t_ptr;

  dr_mutex_lock(g_lock);
  t.tid = g_n_created_threads++;
  dr_mutex_unlock(g_lock);

  dr_set_tls_field(drcontext, t_ptr);

  dr_printf("T%d %s\n", t.tid, (int)__FUNCTION__+8);
}

static void OnEvent_ThreadExit(void *drcontext) {
  DrThread &t = GetCurrentThread(drcontext);
  dr_printf("T%d %s\n", t.tid, (int)__FUNCTION__+8);
}

void OnEvent_ModuleLoaded(void *drcontext, const module_data_t *info,
                          bool loaded) {
  CHECK(info->full_path);
  dr_printf("%s: %s (%s)\n", __FUNCTION__,
            dr_module_preferred_name(info), info->full_path);
  if (g_main_module_path == NULL) {
    g_main_module_path = new string(info->full_path);
  }
}

static void OnEvent_Exit(void) {
  dr_printf("ThreadSanitizerDynamoRio: done\n");
  dr_mutex_destroy(g_lock);
}

static void On_Mop(uintptr_t pc, size_t size, void *a, bool is_w) {
  void *drcontext = dr_get_current_drcontext();
  DrThread &t = GetCurrentThread(drcontext);
  if (t.tid == 777) {
    dr_fprintf(STDERR, "T%d pc=%p a=%p size=%ld %s\n", t.tid, pc, a, size, is_w ? "WRITE" : "READ");
  }
}

static void On_Read(uintptr_t pc, size_t size, void *a) {
  On_Mop(pc, size, a, false);
}

static void On_Write(uintptr_t pc, size_t size, void *a) {
  On_Mop(pc, size, a, true);
}

static void On_AnyCall(uintptr_t pc, uintptr_t target_pc, uintptr_t sp, bool is_direct) {
  void *drcontext = dr_get_current_drcontext();
  DrThread &t = GetCurrentThread(drcontext);
  // dr_fprintf(STDOUT, "T%d CALL %p => %p; sp=%p\n", t.tid, pc, target_pc, sp);
  PushShadowStack(t, pc, target_pc, sp);
}

static void On_DirectCall(uintptr_t pc, uintptr_t target_pc, uintptr_t sp) {
  On_AnyCall(pc, target_pc, sp, true);
}

static void On_IndirectCall(uintptr_t pc, uintptr_t target_pc, uintptr_t sp) {
  On_AnyCall(pc, target_pc, sp, false);
}

static void On_TraceEnter(uintptr_t pc, uintptr_t sp) {
  void *drcontext = dr_get_current_drcontext();
  DrThread &t = GetCurrentThread(drcontext);
  // dr_fprintf(STDOUT, "T%d TRACE:\n%p\n%p\n", t.tid, pc, sp);
  UpdateShadowStack(t, sp);
}

//--------------- instrumentation ----------------- {{{1
opnd_t opnd_create_base_disp_from_dst(opnd_t dst) {
  return opnd_create_base_disp(opnd_get_base(dst),
                               opnd_get_index(dst),
                               opnd_get_scale(dst),
                               opnd_get_disp(dst),
                               OPSZ_lea);
}

static void InstrumentOneMop(void* drcontext, instrlist_t *bb,
                             instr_t *instr, opnd_t opnd, bool is_w) {
  //   opnd_disassemble(drcontext, opnd, 1);
  //   dr_printf("  -- (%s opnd)\n", is_w ? "write" : "read");
  void *callback = (void*)(is_w ? On_Write : On_Read);
  int size = opnd_size_in_bytes(opnd_get_size(opnd));

  instr_t *tmp_instr = NULL;
  reg_id_t reg = REG_XAX;

  /* save %xax */
  dr_save_reg(drcontext, bb, instr, reg, SPILL_SLOT_2);

  if (opnd_is_base_disp(opnd)) {
    /* lea opnd => %xax */
    opnd_set_size(&opnd, OPSZ_lea);
    tmp_instr = INSTR_CREATE_lea(drcontext,
                                 opnd_create_reg(reg),
                                 opnd);
  } else if(
#ifdef X86_64
      opnd_is_rel_addr(opnd) ||
#endif
      opnd_is_abs_addr(opnd)) {
    tmp_instr = INSTR_CREATE_mov_imm(drcontext,
                                     opnd_create_reg(reg),
                                     OPND_CREATE_INTPTR(opnd_get_addr(opnd)));
  }
  if (tmp_instr) {
    // CHECK(tmp_instr);
    instrlist_meta_preinsert(bb, instr, tmp_instr);

    /* clean call */
    dr_insert_clean_call(drcontext, bb, instr, callback, false,
                         3,
                         OPND_CREATE_INTPTR(instr_get_app_pc(instr)),
                         OPND_CREATE_INT32(size),
                         opnd_create_reg(reg));
    /* restore %xax */
    dr_restore_reg(drcontext, bb, instr, REG_XAX, SPILL_SLOT_2);
  } else {
    dr_printf("%s ????????????????????\n", __FUNCTION__);
  }
}

static void InstrumentMopInstruction(void *drcontext,
                                     instrlist_t *bb, instr_t *instr) {
  // reads:
  for (int a = 0; a < instr_num_srcs(instr); a++) {
    opnd_t curop = instr_get_src(instr, a);
    if (opnd_is_memory_reference(curop)) {
      InstrumentOneMop(drcontext, bb, instr, curop, false);
    }
  }
  // writes:
  for (int a = 0; a < instr_num_dsts(instr); a++) {
    opnd_t curop = instr_get_dst(instr, a);
    if (opnd_is_memory_reference(curop)) {
      InstrumentOneMop(drcontext, bb, instr, curop, true);
    }
  }
  //dr_printf("reads: %d writes: %d\n", n_reads, n_writes);
}

static void InstrumentInstruction(void *drcontext, instrlist_t *bb,
                                  instr_t *instr) {
  // instr_disassemble(drcontext, instr, 1);
  // dr_printf("  -- \n");
  if (instr_is_call_direct(instr)) {
    dr_insert_call_instrumentation(drcontext, bb, instr,
                                   (app_pc)On_DirectCall);
  } else if (instr_is_call_indirect(instr)) {
    dr_insert_mbr_instrumentation(drcontext, bb, instr,
                                  (app_pc)On_IndirectCall, SPILL_SLOT_1);

  } else if (instr_reads_memory(instr) || instr_writes_memory(instr)) {
    InstrumentMopInstruction(drcontext, bb, instr);
  }
}

static dr_emit_flags_t OnEvent_Trace(void *drcontext, void *tag,
                                     instrlist_t *trace, bool translating) {
  instr_t *first_instr = NULL;
  for (instr_t *instr = instrlist_first(trace); instr != NULL;
       instr = instr_get_next(instr)) {
    if (instr_get_app_pc(instr)) {
      first_instr = instr;
      break;
    }
  }
  if (first_instr) {
    // instr_disassemble(drcontext, first_instr, 1);
    // dr_printf("  -- in_trace %p\n", instr_get_app_pc(first_instr));
    dr_insert_clean_call(drcontext, trace, first_instr,
                         (void*)On_TraceEnter, false,
                         2,
                         OPND_CREATE_INTPTR(instr_get_app_pc(first_instr)),
                         opnd_create_reg(REG_ESP)
                         );
  }
  return DR_EMIT_DEFAULT;
}

static dr_emit_flags_t OnEvent_BB(void* drcontext, void *tag, instrlist_t *bb,
                                  bool for_trace, bool translating) {
  instr_t *instr, *next_instr;


  for (instr = instrlist_first(bb); instr != NULL; instr = next_instr) {
    next_instr = instr_get_next(instr);
    InstrumentInstruction(drcontext, bb, instr);
  }


  OnEvent_Trace(drcontext, tag, bb, translating);

  return DR_EMIT_DEFAULT;
}


//--------------- dr_init ----------------- {{{1
DR_EXPORT void dr_init(client_id_t id) {
  // Register events.
  dr_register_exit_event(OnEvent_Exit);
  dr_register_bb_event(OnEvent_BB);
  dr_register_trace_event(OnEvent_Trace);
  dr_register_thread_init_event(OnEvent_ThreadInit);
  dr_register_thread_exit_event(OnEvent_ThreadExit);
  dr_register_module_load_event(OnEvent_ModuleLoaded);
  g_lock = dr_mutex_create();
}
// end. {{{1
// vim:shiftwidth=2:softtabstop=2:expandtab
