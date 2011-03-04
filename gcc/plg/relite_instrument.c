/* Relite
 * Copyright (c) 2011, Google Inc.
 * All rights reserved.
 * Author: Dmitry Vyukov (dvyukov@google.com)
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

#include <gcc-plugin.h>
#include <config.h>
#include <system.h>
#include <coretypes.h>
#include <tree.h>
#include <intl.h>
#include <tm.h>
#include <basic-block.h>
#include <gimple.h>
#include <tree-flow.h>
#include <tree-pass.h>
#include <diagnostic.h>
#include <c-common.h>
#include <c-pragma.h>
#include <cp/cp-tree.h>
#include <toplev.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include "relite_pass.h"

/*
static rt_decl_desc_t rt_decl [] = {
    {"ShadowStack"},
    {"INFO"},
    {"ThreadSanitizerHandleOneMemoryAccess"},
};


typedef enum rt_decl_index {
  rt_stack,
  rt_thread,
  rt_mop,
} rt_decl_index;
*/


static tree             get_rt_mop          () {
  static tree fn = 0;
  if (fn == 0) {
    fn = lookup_name(get_identifier(
        "tsan_rtl_mop"));
    if (fn == 0) {
      //__asm("int3");
      printf("relite: can't find tsan_rtl_mop decl\n");
      exit(1);
    }
    /*
    tree fntype = build_function_type_list(void_type_node, ptr_type_node,
                                           long_long_unsigned_type_node,
                                           ptr_type_node, NULL_TREE);
    tree fnid = get_identifier("ThreadSanitizerHandleOneMemoryAccess");
    tree fn = build_decl(0, FUNCTION_DECL, fnid, fntype);
    DECL_EXTERNAL(fn) = 1;
    TREE_PUBLIC(fn) = 1;

    pushdecl_top_level()
    pushdecl_top_level

    TREE_USED(fn) = 1;
    TREE_ADDRESSABLE(fn) = 1;
    //DECL_ARTIFICIAL(fn) = 1;
    //TREE_READONLY(fn) = 1;
    //make_decl_rtl(fn);
    //mark_decl_referenced(fn);
    //notice_global_symbol(fn);
    //set_user_assembler_name(fn);
    */
  }
  return fn;
}


static tree             get_rt_stack        () {
  static tree var = 0;
  if (var == 0) {
    //__asm("int3");
    var = lookup_name(get_identifier("ShadowStack"));
    if (var == 0) {
      printf("relite: can't find ShadowStack decl\n");
      exit(1);
    }
    /*
    tree varid = get_identifier("ShadowStack");
    tree var = build_decl(0, VAR_DECL, varid,
                          build_pointer_type(ptr_type_node));
    DECL_TLS_MODEL(var) = TLS_MODEL_INITIAL_EXEC;
    DECL_EXTERNAL(var) = 1;

    //TREE_PUBLIC(var) = 1;
    TREE_USED(var) = 1;
    TREE_ADDRESSABLE(var) = 1;
    //DECL_ARTIFICIAL(var) = 1;
    //TREE_READONLY(var) = 1;
    //make_decl_rtl(var);
    */
  }
  return var;
}


static tree             get_rt_thread       () {
  static tree var = 0;
  if (var == 0) {
    var = lookup_name(get_identifier("INFO"));
    if (var == 0) {
      //__asm("int3");
      printf("relite: can't find INFO decl\n");
      exit(1);
    }
    /*
    tree varid = get_identifier("INFO");
    tree var = build_decl(0, VAR_DECL, varid, ptr_type_node);
    DECL_TLS_MODEL(var) = TLS_MODEL_INITIAL_EXEC;
    DECL_EXTERNAL(var) = 1;

    TREE_USED(var) = 1;
    TREE_ADDRESSABLE(var) = 1;

    //TREE_PUBLIC(var) = 1;
    //DECL_ARTIFICIAL(var) = 1;
    //TREE_READONLY(var) = 1;
    //make_decl_rtl(var);
     */
  }
  return var;
}


static void             setup                 (struct relite_context_t* ctx) {
  get_rt_mop();
  get_rt_stack();
  get_rt_thread();
}


static gimple           build_label         (location_t loc, tree* label_ptr) {
  static unsigned label_seq = 0;
  char label_name_buf [32];
  sprintf(label_name_buf, "relite_label_%u", label_seq++);
  tree pc_label_id = get_identifier(label_name_buf);
  tree pc_label = build_decl(loc, LABEL_DECL, pc_label_id, void_type_node);
  DECL_CONTEXT(pc_label) = current_function_decl;
  DECL_MODE(pc_label) = VOIDmode;
  C_DECLARED_LABEL_FLAG(pc_label) = 0;
  DECL_SOURCE_LOCATION(pc_label) = loc;
  SET_IDENTIFIER_LABEL_VALUE(pc_label_id, pc_label);
  *label_ptr = pc_label;
  return gimple_build_label(pc_label);
}


static void             build_stack_op      (gimple_seq* seq,
                                             enum tree_code op) {

  tree stack = get_rt_stack();
  tree op_size = TYPE_SIZE(ptr_type_node);
  double_int op_size_cst = tree_to_double_int(op_size);
  unsigned size_val = op_size_cst.low / __CHAR_BIT__;
  op_size = build_int_cst_wide(long_long_unsigned_type_node, size_val, 0);
  tree op_expr = build2(op, long_long_unsigned_type_node, stack, op_size);
  op_expr = force_gimple_operand(op_expr, seq, true, NULL_TREE);
  gimple assign = gimple_build_assign(stack, op_expr);
  gimple_seq_add_stmt(seq, assign);
}


static void             instr_func            (struct relite_context_t* ctx,
                                             tree func_decl,
                                             gimple_seq* pre,
                                             gimple_seq* post) {
  if (ctx->func_calls != 0) {
    build_stack_op(pre, PLUS_EXPR);
    build_stack_op(post, MINUS_EXPR);
  }
}


static void             instr_mop             (struct relite_context_t* ctx,
                                             tree expr,
                                             location_t loc,
                                             int is_store,
                                             int is_sblock,
                                             gimple_seq* pre,
                                             gimple_seq* post) {
  /*
  tree expr_ptr = build_addr(expr, current_function_decl);
  tree addr_expr = force_gimple_operand(expr_ptr, post, true, NULL_TREE);

  tree expr_size = TYPE_SIZE(TREE_TYPE(expr));
  double_int size = tree_to_double_int(expr_size);
  gcc_assert(size.high == 0 && size.low != 0);
  if (size.low > 128)
    size.low = 128;
  size.low = (size.low / 8) - 1;
  unsigned long flags = (size.low + (!!is_store << 4) + (!!is_sblock << 5));
  flags <<= 48;
  tree flags_expr = build_int_cst_wide(long_long_unsigned_type_node, flags, 0);

  tree pc_label_id = get_identifier("relite_label");
  tree pc_label = build_decl(loc, LABEL_DECL, pc_label_id, void_type_node);
  DECL_CONTEXT(pc_label) = current_function_decl;
  DECL_MODE(pc_label) = VOIDmode;
  C_DECLARED_LABEL_FLAG(pc_label) = 0;
  DECL_SOURCE_LOCATION(pc_label) = loc;
  SET_IDENTIFIER_LABEL_VALUE(pc_label_id, pc_label);

  gimple pc_gimple = gimple_build_label(pc_label);
  gimple_seq_add_stmt(post, pc_gimple);
  tree pc_addr = build1(ADDR_EXPR, ptr_type_node, pc_label);
  pc_addr = build_c_cast(loc, long_long_unsigned_type_node, pc_addr);
  tree decl_expr = build2(
      BIT_IOR_EXPR, long_long_unsigned_type_node, pc_addr, flags_expr);

  gimple_seq desc_seq = 0;
  tree desc_expr = force_gimple_operand(decl_expr, &desc_seq, true, NULL_TREE);
  gimple_seq_add_seq(post, desc_seq);

  tree thread_var = get_rt_thread();
  gimple_seq thread_seq = 0;
  tree thr_expr = force_gimple_operand(thread_var, &thread_seq, true, NULL_TREE);
  gimple_seq_add_seq(post, thread_seq);

  tree call_decl = get_rt_mop();
  gimple collect = gimple_build_call(
      call_decl, 3, thr_expr, desc_expr, addr_expr);

  gimple_seq_add_stmt(post, collect);
  */

  tree expr_ptr = build_addr(expr, current_function_decl);
  tree addr_expr = force_gimple_operand(expr_ptr, post, true, NULL_TREE);

  tree expr_size = TYPE_SIZE(TREE_TYPE(expr));
  double_int size = tree_to_double_int(expr_size);
  gcc_assert(size.high == 0 && size.low != 0);
  if (size.low > 128)
    size.low = 128;
  size.low = (size.low / 8) - 1;
  unsigned flags = ((!!is_sblock << 0) + (!!is_store << 1) + (size.low << 2));
  tree flags_expr = build_int_cst(unsigned_type_node, flags);

  gimple_seq flags_seq = 0;
  flags_expr = force_gimple_operand(flags_expr, &flags_seq, true, NULL_TREE);
  gimple_seq_add_seq(post, flags_seq);

  tree call_decl = get_rt_mop();
  gimple collect = gimple_build_call(
      call_decl, 2, addr_expr, flags_expr);

  gimple_seq_add_stmt(post, collect);
}


static void             instr_call            (struct relite_context_t* ctx,
                                             tree func_decl,
                                             location_t loc,
                                             gimple_seq* pre,
                                             gimple_seq* post) {
  if (func_decl != 0 && DECL_IS_BUILTIN(func_decl))
    return;

  tree pc_label = 0;
  gimple pc_gimple = build_label(loc, &pc_label);
  gimple_seq_add_stmt(pre, pc_gimple);

  tree pc_addr = build1(ADDR_EXPR, ptr_type_node, pc_label);
  gimple_seq seq = 0;
  pc_addr = force_gimple_operand(pc_addr, &seq, true, NULL_TREE);
  gimple_seq_add_seq(pre, seq);

  tree stack = get_rt_stack();
  tree stack_op = build1(
      INDIRECT_REF, build_pointer_type(ptr_type_node), stack);
  gimple assign = gimple_build_assign(stack_op, pc_addr);
  gimple_seq_add_stmt(pre, assign);
}


relite_context_t*       create_context      () {
  static relite_context_t g_ctx;
  relite_context_t* ctx = &g_ctx;

  ctx->setup            = setup;
  ctx->instr_func       = instr_func;
  ctx->instr_mop        = instr_mop;
  ctx->instr_call       = instr_call;

  //ctx->rt_decl          = rt_decl;
  //ctx->rt_decl_count    = sizeof(rt_decl)/sizeof(rt_decl[0]);

  //build_rt_decls();

  return ctx;
}



