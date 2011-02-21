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

    {"relite_call_desc_t"},
    {"relite_mop_desc_t"},
    {"relite_func_desc_t"},
    {"relite_calls"},
    {"relite_mops"},
    {"relite_func_desc"},

    {"relite_memset",                   "memset"},
    {"relite_memcpy",                   "memcpy"},
    {"relite_memcmp",                   "memcmp"},

    {"relite_pthread_create",           "pthread_create"},
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
};


typedef enum rt_func_index {
  rt_enter,
  rt_leave,
  rt_store,
  rt_load,
  rt_call_desc_t,
  rt_mop_desc_t,
  rt_func_desc_t,
  rt_calls,
  rt_mops,
  rt_func_desc,
} rt_func_index;


static tree rt_func(rt_func_index idx) {
  return rt_funcs[idx].fndecl;
  return 0;
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
                                            expanded_location const* loc,
                                            gimple_stmt_iterator* gsi,
                                            tree expr) {
  tree expr_ptr = build1(ADDR_EXPR, build_pointer_type(TREE_TYPE(expr)), expr);
  expr_ptr = convert(ptr_type_node, expr_ptr);
  gimple collect = gimple_build_call(rt_func(rt_store), 1, expr_ptr);
  gsi_insert_after(gsi, collect, GSI_NEW_STMT);
}


static void             instr_load          (relite_context_t* ctx,
                                            expanded_location const* loc,
                                            gimple_stmt_iterator* gsi,
                                            tree expr) {
  tree expr_ptr = build1(ADDR_EXPR, build_pointer_type(TREE_TYPE(expr)), expr);
  expr_ptr = convert(ptr_type_node, expr_ptr);
  gimple collect = gimple_build_call(rt_func(rt_load), 1, expr_ptr);
  gsi_insert_after(gsi, collect, GSI_NEW_STMT);
}


static void             instr_func          (relite_context_t* ctx,
                                            expanded_location const* loc,
                                            gimple_stmt_iterator* gsi,
                                            tree expr) {
  tree func_desc = copy_node(rt_func(rt_func_desc));
  func_desc->decl_minimal.name = get_identifier("qweqweqwe");
  chainon(TREE_CHAIN(NAMESPACE_LEVEL(global_namespace)->names), func_desc);
}


void                    setup_context       (relite_context_t* ctx) {
  ctx->instr_enter = instr_enter;
  ctx->instr_leave = instr_leave;
  ctx->instr_store = instr_store;
  ctx->instr_load = instr_load;
  ctx->instr_func = instr_func;

  ctx->rt_funcs = rt_funcs;
  ctx->rt_func_count = sizeof(rt_funcs)/sizeof(rt_funcs[0]);
}



