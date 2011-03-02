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


// TODO(dvyukov) if an address of a function that needs to be intercepted
// is taken, replace it with the wrapper


static void             dbg                 (relite_context_t* ctx,
                                             char const* format, ...)
                                          __attribute__((format(printf, 2, 3)));

static void             dbg                 (relite_context_t* ctx,
                                             char const* format, ...) {
  if (ctx->opt_debug == 0)
    return;
  va_list argptr;
  va_start(argptr, format);
  vprintf(format, argptr);
  va_end(argptr);
  printf("\n");
}


static char const*      decl_name           (tree decl) {
  if (DECL_P(decl)) {
    tree id = DECL_NAME(decl);
    if (id) {
      char const* name = IDENTIFIER_POINTER(id);
      if (name)
        return name;
    }
  }
  return "<unknown>";
}


static void             dbg_access          (relite_context_t* ctx,
                                             char const* what,
                                             expanded_location const* loc,
                                             tree expr,
                                             tree expr_ssa,
                                             int do_instrument,
                                             char const* reason) {
  dbg(ctx, "%s:%d:%d:%s %s '%s' code=%s%s type=%s: %s %s",
      loc->file, loc->line, loc->column,
      (do_instrument ? "" : " warning:"),
      what, decl_name(expr),
      tree_code_name[TREE_CODE(expr)],
      (expr_ssa ? "(ssa)" : ""),
      tree_code_name[TREE_CODE(TREE_TYPE(expr))],
      (do_instrument ? "INSTRUMENTED" : "IGNORED"),
      reason);
}


static void             setup_rt_funcs      (relite_context_t* ctx) {
  int remain = ctx->rt_func_count;
  tree decl = NAMESPACE_LEVEL(global_namespace)->names;
  for (; decl != 0; decl = TREE_CHAIN(decl)) {
    if (DECL_IS_BUILTIN(decl) /*|| TREE_CODE(decl) != FUNCTION_DECL*/)
      continue;
    char const* name = IDENTIFIER_POINTER(DECL_NAME(decl));
    if (name == 0)
      continue;
    for (int i = 0; i != ctx->rt_func_count; i += 1) {
      if (ctx->rt_funcs[i].fndecl == 0
          && strcmp(name, ctx->rt_funcs[i].rt_name) == 0) {
        ctx->rt_funcs[i].fndecl = decl;
        TREE_NO_WARNING(decl) = 1;
        remain -= 1;
        break;
      }
    }
    if (remain == 0)
      break;
  }

  if (remain != 0) {
    printf("relite: can't find runtime function declarations:\n");
    for (int i = 0; i != ctx->rt_func_count; i += 1) {
      if (ctx->rt_funcs[i].fndecl == 0)
        printf("relite: %s\n", ctx->rt_funcs[i].rt_name);
    }
    exit(1);
  }
}


static tree             find_rt_func        (relite_context_t* ctx,
                                             char const* func_name) {
  for (int i = 0; i != ctx->rt_func_count; i += 1) {
    if (ctx->rt_funcs[i].real_name != 0
        && strcmp(ctx->rt_funcs[i].real_name, func_name) == 0) {
      return ctx->rt_funcs[i].fndecl;
    }
  }
  return 0;
}


static void             process_store       (relite_context_t* ctx,
                                             location_t loc,
                                             expanded_location const* eloc,
                                             gimple_stmt_iterator* gsi,
                                             tree expr) {
  gcc_assert(eloc != 0 && gsi != 0 && expr != 0);
  int do_instrument = 0;
  char const* reason = "";

  tree expr_ssa = 0;
  if (TREE_CODE(expr) == SSA_NAME) {
    expr_ssa = expr;
    expr = SSA_NAME_VAR(expr);
  }

  switch (TREE_CODE(expr)) {
    case RESULT_DECL:
      reason = "function result";
      break;
    case PARM_DECL:
      reason = "function parameter";
      break;

    case ARRAY_REF: {
      tree array = TREE_OPERAND (expr, 0);
      if (DECL_P(array) || TREE_CODE(array) == SSA_NAME) {
        //TREE_ADDRESSABLE(array) = 1;
        //TREE_USED(array) = 1;
        if (TREE_CODE(array) == SSA_NAME) {
          //tree array_ssa = SSA_NAME_VAR(array);
          //TREE_ADDRESSABLE(array_ssa) = 1;
          //TREE_USED(array_ssa) = 1;
        }
      }

      tree index = TREE_OPERAND (expr, 1);
      if (DECL_P(index) || TREE_CODE(index) == SSA_NAME) {
        //TREE_USED(index) = 1;
        if (TREE_CODE(index) == SSA_NAME) {
          //tree index_ssa = SSA_NAME_VAR(index);
          //TREE_USED(index_ssa) = 1;
        }
      }

      if (DECL_P(expr)) {
        //TREE_USED(expr) = 1;
        if (expr_ssa != 0) {
          //TREE_USED(expr_ssa) = 1;
        }
      }

      do_instrument = 1;
      break;
    }

    case VAR_DECL:
    //!!! case FIELD_DECL:
    //!!! case MEM_REF:
    //!!! case ARRAY_RANGE_REF:
    //!!! case TARGET_MEM_REF:
    //!!! case ADDR_EXPR:
    case COMPONENT_REF:
    case INDIRECT_REF:
    {
      if (TREE_CODE(TREE_TYPE(expr)) == RECORD_TYPE) {
        reason = "record type";
        break;
      }

      //if (TREE_USED(lhs) == 0) return;
      if (DECL_ARTIFICIAL(expr) || (expr_ssa && DECL_ARTIFICIAL(expr_ssa))) {
        reason = "artificial";
        break;
      }

      if (TREE_CODE(expr) == VAR_DECL && TREE_ADDRESSABLE(expr) == 0) {
        reason = "non-addressable var";
        break;
      }

      if (TREE_CODE(expr) == COMPONENT_REF) {
        tree field = expr->exp.operands[1];
        if (TREE_CODE(field) == FIELD_DECL) {
          unsigned fld_off = field->field_decl.bit_offset->int_cst.int_cst.low;
          unsigned fld_size = field->decl_common.size->int_cst.int_cst.low;
          if (((fld_off % __CHAR_BIT__) != 0)
              || ((fld_size % __CHAR_BIT__) != 0)){
            //TODO(dvyukov): handle bit-fields
            dbg(ctx, "bit_offset=%u, size=%u", fld_off, fld_size);
            reason = "weird bit field";
            break;
          }
        }
      }

      //TREE_ADDRESSABLE(expr) = 1;
      //TREE_USED(expr) = 1;
      if (expr_ssa != 0) {
        //TREE_ADDRESSABLE(expr_ssa) = 1;
        //TREE_USED(expr_ssa) = 1;
        add_referenced_var(expr_ssa);
      }

      do_instrument = 1;
      break;
    }

    default: {
      reason = "unknown type";
      break;
    }
  }

  if (do_instrument != 0) {
    tree expr_type = TREE_TYPE(expr);
    tree type_decl = TYPE_NAME(expr_type);
    if (type_decl != 0) {
      char const* type_name = decl_name(type_decl);
      if (strncmp(type_name, "Atomic", sizeof("Atomic") - 1) == 0) {
        //TODO(dvyukov): check that it's at least volatile
        // a race on non-volatile int is not all that cool
        reason = "atomic type";
        do_instrument = 0;
      }
    }
  }

  assert(do_instrument != 0 || (reason != 0 && reason[0] != 0));

  ctx->stat_store_total += 1;
  if (do_instrument != 0 && ctx->instr_store) {
    ctx->stat_store_instrumented += 1;
    assert(is_gimple_addressable(expr));
    expr_desc_t desc = {};
    desc.loc = loc;
    desc.eloc = eloc;
    desc.gsi = gsi;
    desc.expr = expr;
    ctx->instr_store(ctx, &desc);
  }

  dbg_access(ctx, "store to", eloc, expr, expr_ssa, do_instrument, reason);
}


static void             process_load        (relite_context_t* ctx,
                                             location_t loc,
                                             expanded_location const* eloc,
                                             gimple_stmt_iterator* gsi,
                                             tree expr) {
  gcc_assert(eloc != 0 && gsi != 0 && expr != 0);
  int do_instrument = 0;
  char const* reason = "";

  tree expr_ssa = 0;
  if (TREE_CODE(expr) == SSA_NAME) {
    expr_ssa = expr;
    expr = SSA_NAME_VAR(expr);
  }

  switch (TREE_CODE(expr)) {
    case CONSTRUCTOR: {
      //!!! handle all elements recursively
      /*
      CONSTRUCTOR
      These nodes represent the brace-enclosed initializers for a structure or array. The first operand is reserved for use by the back end. The second operand is a TREE_LIST. If the TREE_TYPE of the CONSTRUCTOR is a RECORD_TYPE or UNION_TYPE, then the TREE_PURPOSE of each node in the TREE_LIST will be a FIELD_DECL and the TREE_VALUE of each node will be the expression used to initialize that field.
      If the TREE_TYPE of the CONSTRUCTOR is an ARRAY_TYPE, then the TREE_PURPOSE of each element in the TREE_LIST will be an INTEGER_CST or a RANGE_EXPR of two INTEGER_CSTs. A single INTEGER_CST indicates which element of the array (indexed from zero) is being assigned to. A RANGE_EXPR indicates an inclusive range of elements to initialize. In both cases the TREE_VALUE is the corresponding initializer. It is re-evaluated for each element of a RANGE_EXPR. If the TREE_PURPOSE is NULL_TREE, then the initializer is for the next available array element.
      In the front end, you should not depend on the fields appearing in any particular order. However, in the middle end, fields must appear in declaration order. You should not assume that all fields will be represented. Unrepresented fields will be set to zero.
      */
      reason = "constructor expression";
      break;
    }
    case RESULT_DECL:
      reason = "function result";
      break;
    case INTEGER_CST:
      reason = "constant";
      break;
    case PARM_DECL:
      reason = "function parameter";
      break;

    case VAR_DECL:
    //case INDIRECT_REF:
    case COMPONENT_REF:
    //case ARRAY_REF:
      //!!! case FIELD_DECL:
      //!!! case MEM_REF:
      //!!! case ARRAY_REF:
      //!!! case ARRAY_RANGE_REF:
      //!!! case TARGET_MEM_REF:
      //!!! case ADDR_EXPR:
    {
      if (DECL_ARTIFICIAL(expr) || (expr_ssa && DECL_ARTIFICIAL(expr_ssa))) {
        reason = "artificial";
        break;
      }

      if (TREE_CODE(TREE_TYPE(expr)) == RECORD_TYPE) {
        reason = "record type";
        break;
      }

      if (is_gimple_addressable(expr) == 0) {
        reason = "doesn't have an address";
        break;
      }

      if (TREE_CODE(expr) == VAR_DECL && TREE_ADDRESSABLE(expr) == 0) {
        reason = "non-addressable var";
        break;
      }

      if (TREE_CODE(expr) == COMPONENT_REF) {
        tree field = expr->exp.operands[1];
        if (TREE_CODE(field) == FIELD_DECL) {
          unsigned fld_off = field->field_decl.bit_offset->int_cst.int_cst.low;
          unsigned fld_size = field->decl_common.size->int_cst.int_cst.low;
          if (((fld_off % __CHAR_BIT__) != 0)
              || ((fld_size % __CHAR_BIT__) != 0)){
            //TODO(dvyukov): handle bit-fields
            dbg(ctx, "bit_offset=%u, size=%u", fld_off, fld_size);
            reason = "weird bit field";
            break;
          }
        }
      }

      //TREE_ADDRESSABLE(expr) = 1;
      //TREE_USED(expr) = 1;
//      add_referenced_var(expr);
      if (expr_ssa != 0) {
        //TREE_ADDRESSABLE(expr_ssa) = 1;
        //TREE_USED(expr_ssa) = 1;
        //add_referenced_var(expr_ssa);
        //mark_operand_necessary(expr_ssa);
      }

      do_instrument = 1;
      break;
    }

    default: {
      reason = "unknown type";
      break;
    }
  }

  assert(do_instrument != 0 || (reason != 0 && reason[0] != 0));

  ctx->stat_load_total += 1;
  if (do_instrument != 0 && ctx->instr_load != 0) {
    ctx->stat_load_instrumented += 1;
    assert(is_gimple_addressable(expr));
    expr_desc_t desc = {};
    desc.loc = loc;
    desc.eloc = eloc;
    desc.gsi = gsi;
    desc.expr = expr;
    ctx->instr_load(ctx, &desc);
  }

  dbg_access(ctx, "load of", eloc, expr, expr_ssa, do_instrument, reason);
}


static void             instrument          (relite_context_t* ctx,
                                             gimple_stmt_iterator* gsi) {
  gimple stmt = gsi_stmt(*gsi);
  location_t const loc = gimple_location(stmt);
  expanded_location eloc = expand_location(loc);
  ctx->stat_gimple += 1;

  if (gimple_code(stmt) == GIMPLE_BIND) {
    gimple_stmt_iterator gsi2;
    for (gsi2 = gsi_start(gimple_bind_body(stmt));
        !gsi_end_p(gsi2);
        gsi_next(&gsi2)) {
      instrument(ctx, &gsi2);
    }
  } else if (is_gimple_assign(stmt)) {
    for (int i = 1; i != gimple_num_ops(stmt); i += 1) {
      tree rhs = gimple_op(stmt, i);
      process_load(ctx, loc, &eloc, gsi, rhs);
    }
    tree lhs = gimple_assign_lhs(stmt);
    process_store(ctx, loc, &eloc, gsi, lhs);
  } else if (is_gimple_call (stmt)) {
    tree fndecl = gimple_call_fndecl(stmt);
    if (fndecl != 0) {
      char const* func_name = decl_name(fndecl);
      if (func_name != 0) {
        dbg(ctx, "processing function call '%s'", func_name);
        tree rt_fndecl = find_rt_func(ctx, func_name);
        if (rt_fndecl != 0) {
          if ((strcmp(func_name, "operator new") != 0
              && strcmp(func_name, "operator delete") != 0)
              || gimple_call_num_args(stmt) == 1) {
            gimple_call_set_fndecl(stmt, rt_fndecl);
          }
        }
      }
    }
    tree lhs = gimple_call_lhs(stmt);
    if (lhs != 0)
      process_store(ctx, loc, &eloc, gsi, lhs);
  }
}


static void             insert_before_return_impl (gimple_stmt_iterator* gsi,
                                                   gimple collect) {
  gimple stmt = gsi_stmt(*gsi);
  if (gimple_code(stmt) == GIMPLE_BIND) {
    gimple_stmt_iterator gsi2;
    for (gsi2 = gsi_start(gimple_bind_body(stmt));
        !gsi_end_p(gsi2);
        gsi_next(&gsi2)) {
      insert_before_return_impl(&gsi2, collect);
    }
  } else if (gimple_code(stmt) == GIMPLE_RETURN) {
    gsi_insert_before(gsi, collect, GSI_SAME_STMT);
  }
}


static void             insert_before_return(struct function* func,
                                             gimple collect) {
  gimple_stmt_iterator gsi;
  for (gsi = gsi_start(func->gimple_body); !gsi_end_p(gsi); gsi_next(&gsi)) {
    insert_before_return_impl(&gsi, collect);
  }
}


static void             insert_after_enter_impl   (gimple_stmt_iterator* gsi,
                                                   gimple collect,
                                                   int* is_inserted) {
  gimple stmt = gsi_stmt(*gsi);
  if (gimple_code(stmt) == GIMPLE_BIND) {
    gimple_stmt_iterator gsi2;
    for (gsi2 = gsi_start(gimple_bind_body(stmt));
        !gsi_end_p(gsi2) && !*is_inserted;
        gsi_next(&gsi2)) {
      insert_after_enter_impl(&gsi2, collect, is_inserted);
    }
  } else {
    assert(*is_inserted == 0);
    *is_inserted = 1;
    gsi_insert_before(gsi, collect, GSI_SAME_STMT);
  }
}


static void             insert_after_enter  (struct function* func,
                                             gimple collect) {
  int is_inserted = 0;
  gimple_stmt_iterator gsi;
  for (gsi = gsi_start(func->gimple_body); !gsi_end_p(gsi); gsi_next(&gsi)) {
    insert_after_enter_impl(&gsi, collect, &is_inserted);
  }
  assert(is_inserted != 0);
}


void                    relite_pass         (relite_context_t* ctx,
                                             struct function* func) {
  if (ctx->rt_func_setup == 0) {
    setup_rt_funcs(ctx);
    ctx->rt_func_setup = 1;
  }

  char const* func_name = decl_name(func->decl);

  dbg(ctx, "\nPROCESSING FUNCTION '%s'", func_name);
  ctx->stat_func_total += 1;

  tree func_attr = DECL_ATTRIBUTES(func->decl);
  for (; func_attr != NULL_TREE; func_attr = TREE_CHAIN(func_attr)) {
    char const* attr_name = IDENTIFIER_POINTER(TREE_PURPOSE(func_attr));
    if (strcmp(attr_name, "relite_ignore") == 0) {
      dbg(ctx, "IGNORING due to relite_ignore attribute");
      return;
    }
  }

  if (strncmp(func_name, "NoBarrier_", sizeof("NoBarrier_") - 1) == 0) {
    dbg(ctx, "IGNORING relaxed atomic operation");
    return;
  }

  if (strncmp(func_name, "Acquire_", sizeof("Acquire_") - 1) == 0) {
    dbg(ctx, "atomic operation with acquire memory ordering");
    tree arg = DECL_ARGUMENTS(func->decl);
    if (arg == 0) {
      dbg(ctx, "IGNORING no arguments");
      return;
    }
    gimple collect = gimple_build_call(rt_func(rt_acquire), 1, arg);
    insert_before_return(func, collect);
    return;
  }

  if (strncmp(func_name, "Release_", sizeof("Release_") - 1) == 0) {
    dbg(ctx, "atomic operation with release memory ordering");
    tree arg = DECL_ARGUMENTS(func->decl);
    if (arg == 0) {
      dbg(ctx, "IGNORING no arguments");
      return;
    }
    gimple collect = gimple_build_call(rt_func(rt_release), 1, arg);
    insert_after_enter(func, collect);
    return;
  }

  if (strncmp(func_name, "Barrier_", sizeof("Barrier_") - 1) == 0) {
    dbg(ctx, "atomic operation with acquire-release memory ordering");
    tree arg = DECL_ARGUMENTS(func->decl);
    if (arg == 0) {
      dbg(ctx, "IGNORING no arguments");
      return;
    }
    {
      gimple collect = gimple_build_call(rt_func(rt_acquire), 1, arg);
      insert_before_return(func, collect);
    }
    {
      gimple collect = gimple_build_call(rt_func(rt_release), 1, arg);
      insert_after_enter(func, collect);
    }
    return;
  }

  ctx->stat_func_instrumented += 1;

  int is_first_gimple = 0;
  basic_block bb;
  gimple_stmt_iterator gsi;
  FOR_EACH_BB_FN (bb, func) {
    for (gsi = gsi_start_bb (bb); !gsi_end_p (gsi); gsi_next (&gsi)) {
  //for (gsi = gsi_start(func->gimple_body); !gsi_end_p(gsi); gsi_next(&gsi)) {
      gimple stmt = gsi_stmt(gsi);
      location_t loc = gimple_location(stmt);
      expanded_location eloc = expand_location(loc);
      expr_desc_t desc = {};
      desc.loc = loc;
      desc.eloc = &eloc;
      desc.gsi = &gsi;
      desc.expr = func->decl;
      if (is_first_gimple == 0) {
        if (ctx->instr_enter) {
          ctx->instr_enter(ctx, &desc);
        }
        is_first_gimple = 1;
      }
      instrument(ctx, &gsi);
      if (gimple_code(stmt) == GIMPLE_RETURN) {
        if (ctx->instr_leave != 0)
          ctx->instr_leave(ctx, &desc);
      }
    }
  }

  if (ctx->instr_func) {
    expr_desc_t desc = {};
    ctx->instr_func(ctx, &desc);
  }
}


void                    relite_finish       (relite_context_t* ctx) {
  dbg(ctx, "STATS func: %d/%d, gimple: %d, store: %d/%d, load: %d/%d",
      ctx->stat_func_instrumented, ctx->stat_func_total,
      ctx->stat_gimple,
      ctx->stat_store_instrumented, ctx->stat_store_total,
      ctx->stat_load_instrumented, ctx->stat_load_total);
}


