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

static void *g_lock;
static int   g_n_created_threads;

struct DrThread {
  int tid;  // A unique 0-based thread id.
};


static void OnEvent_ThreadInit(void *drcontext) {
  DrThread *thr = new DrThread;

  dr_mutex_lock(g_lock);
  thr->tid = g_n_created_threads++;
  dr_mutex_unlock(g_lock);

  dr_set_tls_field(drcontext, thr);

  dr_printf("T%d %s\n", thr->tid, (int)__FUNCTION__+8);
}

static void OnEvent_ThreadExit(void *drcontext) {
  DrThread *thr = (DrThread*)dr_get_tls_field(drcontext);
  dr_printf("T%d %s\n", thr->tid, (int)__FUNCTION__+8);
}

void OnEvent_ModuleLoaded(void *drcontext, const module_data_t *info,
                          bool loaded) {
  // dr_printf("%s: %s\n", __FUNCTION__, dr_module_preferred_name(info));
}

static void OnEvent_Exit(void) {
  dr_printf("ThreadSanitizerDynamoRio: done\n");
  dr_mutex_destroy(g_lock);
}

static void On_Mop(app_pc pc, size_t size, void *a, bool is_w) {
  void *drcontext = dr_get_current_drcontext();
  DrThread *thr = (DrThread*)dr_get_tls_field(drcontext);
//  if (thr->tid > 0) {
    dr_fprintf(STDERR, "T%d pc=%p a=%p size=%ld %s\n", thr->tid, pc, a, size, is_w ? "WRITE" : "READ");
//  }
}

static void On_Read(app_pc pc, size_t size, void *a) {
  On_Mop(pc, size, a, false);
}

static void On_Write(app_pc pc, size_t size, void *a) {
  On_Mop(pc, size, a, true);
}

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

static dr_emit_flags_t OnEvent_BB(void* drcontext, void *tag, instrlist_t *bb,
                                  bool for_trace, bool translating) {
  instr_t *instr, *next_instr;

  for (instr = instrlist_first(bb); instr != NULL; instr = next_instr) {
   next_instr = instr_get_next(instr);
   // instr_disassemble(drcontext, instr, 1);
   // dr_printf("  -- \n");
   int n_reads = 0, n_writes = 0;
    if (!instr_reads_memory(instr) && !instr_writes_memory(instr)) continue;
    // reads:
    for (int a = 0; a < instr_num_srcs(instr); a++) {
      opnd_t curop = instr_get_src(instr, a);
      if (opnd_is_memory_reference(curop)) {
        n_reads++;
        InstrumentOneMop(drcontext, bb, instr, curop, false);
      }
    }
    // writes:
    for (int a = 0; a < instr_num_dsts(instr); a++) {
      opnd_t curop = instr_get_dst(instr, a);
      if (opnd_is_memory_reference(curop)) {
        n_writes++;
        InstrumentOneMop(drcontext, bb, instr, curop, true);
      }
    }
    //dr_printf("reads: %d writes: %d\n", n_reads, n_writes);
  }
  return DR_EMIT_DEFAULT;
}


DR_EXPORT void dr_init(client_id_t id) {
  // Register events.
  dr_register_exit_event(OnEvent_Exit);
  dr_register_bb_event(OnEvent_BB);
  dr_register_thread_init_event(OnEvent_ThreadInit);
  dr_register_thread_exit_event(OnEvent_ThreadExit);
  dr_register_module_load_event(OnEvent_ModuleLoaded);
  g_lock = dr_mutex_create();
}
