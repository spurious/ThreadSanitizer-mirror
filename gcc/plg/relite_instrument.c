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


static func_desc_t rt_funcs [] = {
    {"relite_enter"},
    {"relite_leave"},
    {"relite_store"},
    {"relite_load"},
    {"relite_acquire"},
    {"relite_release"},

    /*
    {"relite_call_desc_t"},
    {"relite_mop_desc_t"},
    {"relite_func_desc_t"},
    {"relite_calls"},
    {"relite_mops"},
    {"relite_func_desc"},

    {"relite_malloc",                   "malloc"},
    {"relite_new",                      "operator new"},
    {"relite_calloc",                   "calloc"},
    {"relite_realloc",                  "realloc"},
    {"relite_free",                     "free"},
    {"relite_delete",                   "operator delete"},
    {"relite_memset",                   "memset"},
    {"relite_memcpy",                   "memcpy"},
    {"relite_memcmp",                   "memcmp"},

    //{"relite_pthread_create",           "pthread_create"},
    {"relite_pthread_join",             "pthread_join"},
    {"relite_pthread_mutex_init",       "pthread_mutex_init"},
    {"relite_pthread_mutex_destroy",    "pthread_mutex_destroy"},
    {"relite_pthread_mutex_lock",       "pthread_mutex_lock"},
    {"relite_pthread_mutex_unlock",     "pthread_mutex_unlock"},
    {"relite_pthread_cond_init",        "pthread_cond_init"},
    {"relite_pthread_cond_destroy",     "pthread_cond_destroy"},
    {"relite_pthread_cond_signal",      "pthread_cond_signal"},
    {"relite_pthread_cond_broadcast",   "pthread_cond_broadcast"},
    {"relite_pthread_cond_wait",        "pthread_cond_wait"},
    {"relite_pthread_cond_timedwait",   "pthread_cond_timedwait"},
*/
};


tree rt_func(rt_func_index idx) {
  return rt_funcs[idx].fndecl;
  return 0;
}


static tree             build_flags         (tree expr) {
  tree expr_size = TYPE_SIZE(TREE_TYPE(expr));
  double_int size = tree_to_double_int(expr_size);
  gcc_assert(size.high == 0);
  if (size.low == 8)
    return build_int_cstu(unsigned_type_node, 3);
  else if (size.low == 16)
    return build_int_cstu(unsigned_type_node, 2);
  else if (size.low == 32)
    return build_int_cstu(unsigned_type_node, 1);
  else if (size.low == 64)
    return build_int_cstu(unsigned_type_node, 0);
  else
    // let's treat it as 1-byte access
    return build_int_cstu(unsigned_type_node, 3);
}


static void             instr_enter         (relite_context_t* ctx,
                                            expanded_location const* loc,
                                            gimple_stmt_iterator* gsi,
                                            tree expr) {
  tree func_ptr = build1(ADDR_EXPR,
                         build_pointer_type(TREE_TYPE(expr)), expr);
  func_ptr = convert(ptr_type_node, func_ptr);
  gimple collect = gimple_build_call(rt_func(rt_enter), 1, func_ptr);
  gsi_insert_before(gsi, collect, GSI_SAME_STMT);
}


static void             instr_leave         (relite_context_t* ctx,
                                            expanded_location const* loc,
                                            gimple_stmt_iterator* gsi,
                                            tree expr) {
  gimple func_leave = gimple_build_call(rt_func(rt_leave), 0);
  gsi_insert_before(gsi, func_leave, GSI_SAME_STMT);
}


static void             instr_store         (relite_context_t* ctx,
                                             expr_desc_t const* expr) {
  tree expr_ptr = build_addr(expr->expr, current_function_decl);
  tree addr_expr = force_gimple_operand_gsi
      (expr->gsi, expr_ptr, true, NULL_TREE, false, GSI_NEW_STMT);

  tree expr_size = build_flags(expr->expr);
  gimple collect = gimple_build_call
      (rt_func(rt_store), 2, addr_expr, expr_size);
  gimple_set_location(collect, expr->loc);
  gsi_insert_after(expr->gsi, collect, GSI_NEW_STMT);

  /*
  //expr_ptr = convert(ptr_type_node, expr_ptr);

  tree tmp_var = create_tmp_var(ptr_type_node, "relite_tmp");
  if (gimple_referenced_vars (cfun)) {
    add_referenced_var(tmp_var);
    mark_sym_for_renaming(tmp_var);
  }
  gimple tmp_assign = gimple_build_assign_stat(tmp_var, expr_ptr);

  //tree tmp_ssa = make_ssa_name(tmp_var, tmp_assign);
  //gimple_assign_set_lhs(tmp_assign, tmp_ssa);

  //tmp_var = make_ssa_name(tmp_var, tmp_assign);
  //TREE_OPERAND(tmp_assign, 0) = tmp_var;

  gsi_insert_after(gsi, tmp_assign, GSI_NEW_STMT);

  tree expr_size = build_flags(expr);
  gimple collect = gimple_build_call(rt_func(rt_store), 2, tmp_var, expr_size);
  //update_stmt(collect);
  gsi_insert_after(gsi, collect, GSI_NEW_STMT);
  */
}


static void             instr_load          (relite_context_t* ctx,
                                             expr_desc_t const* expr) {
  tree expr_ptr = build_addr(expr->expr, current_function_decl);
  tree addr_expr = force_gimple_operand_gsi
      (expr->gsi, expr_ptr, true, NULL_TREE, false, GSI_NEW_STMT);

  tree expr_size = build_flags(expr->expr);
  gimple collect = gimple_build_call
      (rt_func(rt_load), 2, addr_expr, expr_size);
  gimple_set_location(collect, expr->loc);
  gsi_insert_after(expr->gsi, collect, GSI_NEW_STMT);


  /*
  tree expr_ptr = build1(ADDR_EXPR, build_pointer_type(TREE_TYPE(expr)), expr);
  expr_ptr = convert(ptr_type_node, expr_ptr);
  tree expr_size = build_flags(expr);
  gimple collect = gimple_build_call(rt_func(rt_load), 2, expr_ptr, expr_size);
  //update_stmt(collect);
  gsi_insert_after(gsi, collect, GSI_NEW_STMT);
  */
}


static void             instr_func          (relite_context_t* ctx,
                                            expanded_location const* loc,
                                            gimple_stmt_iterator* gsi,
                                            tree expr) {
  //tree func_desc = copy_node(rt_func(rt_func_desc));
  //func_desc->decl_minimal.name = get_identifier("qweqweqwe");
  //chainon(TREE_CHAIN(NAMESPACE_LEVEL(global_namespace)->names), func_desc);
}


void                    setup_context       (relite_context_t* ctx) {
  (void)instr_enter;
  (void)instr_leave;
  (void)instr_func;

  //ctx->instr_enter = instr_enter;
  //ctx->instr_leave = instr_leave;
  ctx->instr_store = instr_store;
  ctx->instr_load = instr_load;
  //ctx->instr_func = instr_func;

  ctx->rt_funcs = rt_funcs;
  ctx->rt_func_count = sizeof(rt_funcs)/sizeof(rt_funcs[0]);
}



