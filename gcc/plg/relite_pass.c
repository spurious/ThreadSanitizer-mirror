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
    if (DECL_IS_BUILTIN(decl) || TREE_CODE(decl) != FUNCTION_DECL)
      continue;
    char const* name = IDENTIFIER_POINTER(DECL_NAME(decl));
    if (name == 0)
      continue;
    for (int i = 0; i != ctx->rt_func_count; i += 1) {
      if (ctx->rt_funcs[i].fndecl == 0
          && strcmp(name, ctx->rt_funcs[i].rt_name) == 0) {
        ctx->rt_funcs[i].fndecl = decl;
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
                                             tree fndecl) {
  char const* func_name = decl_name(fndecl);
  if (func_name == 0)
    return 0;
  for (int i = 0; i != ctx->rt_func_count; i += 1) {
    if (ctx->rt_funcs[i].real_name != 0
        && strcmp(ctx->rt_funcs[i].real_name, func_name) == 0) {
      return ctx->rt_funcs[i].fndecl;
    }
  }
  return 0;
}


static void             process_store       (relite_context_t* ctx,
                                             expanded_location const* loc,
                                             gimple_stmt_iterator* gsi,
                                             tree expr) {
  gcc_assert(loc != 0 && gsi != 0 && expr != 0);
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
        TREE_ADDRESSABLE(array) = 1;
        TREE_USED(array) = 1;
        if (TREE_CODE(array) == SSA_NAME) {
          tree array_ssa = SSA_NAME_VAR(array);
          TREE_ADDRESSABLE(array_ssa) = 1;
          TREE_USED(array_ssa) = 1;
        }
      }

      tree index = TREE_OPERAND (expr, 1);
      if (DECL_P(index) || TREE_CODE(index) == SSA_NAME) {
        TREE_USED(index) = 1;
        if (TREE_CODE(index) == SSA_NAME) {
          tree index_ssa = SSA_NAME_VAR(index);
          TREE_USED(index_ssa) = 1;
        }
      }

      if (DECL_P(expr)) {
        TREE_USED(expr) = 1;
        if (expr_ssa != 0) {
          TREE_USED(expr_ssa) = 1;
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
    //case INDIRECT_REF:
    //case ARRAY_REF:
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

      TREE_ADDRESSABLE(expr) = 1;
      TREE_USED(expr) = 1;
      if (expr_ssa != 0) {
        TREE_ADDRESSABLE(expr_ssa) = 1;
        TREE_USED(expr_ssa) = 1;
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

  assert(do_instrument != 0 || (reason != 0 && reason[0] != 0));

  ctx->stat_store_total += 1;
  if (do_instrument != 0 && ctx->instr_store) {
    ctx->stat_store_instrumented += 1;
    assert(is_gimple_addressable(expr));
    ctx->instr_store(ctx, loc, gsi, expr);
  }

  dbg_access(ctx, "store to", loc, expr, expr_ssa, do_instrument, reason);
}


static void             process_load        (relite_context_t* ctx,
                                             expanded_location const* loc,
                                             gimple_stmt_iterator* gsi,
                                             tree expr) {
  gcc_assert(loc != 0 && gsi != 0 && expr != 0);
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
    case INDIRECT_REF:
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

      TREE_ADDRESSABLE(expr) = 1;
      TREE_USED(expr) = 1;
//      add_referenced_var(expr);
      if (expr_ssa != 0) {
        TREE_ADDRESSABLE(expr_ssa) = 1;
        TREE_USED(expr_ssa) = 1;
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
    ctx->instr_load(ctx, loc, gsi, expr);
  }

  dbg_access(ctx, "load of", loc, expr, expr_ssa, do_instrument, reason);
}


static void             instrument          (relite_context_t* ctx,
                                             gimple_stmt_iterator* gsi) {
  gimple stmt = gsi_stmt(*gsi);
  expanded_location loc = expand_location(gimple_location(stmt));
  ctx->stat_gimple += 1;

  if (is_gimple_assign(stmt)) {
    for (int i = 1; i != gimple_num_ops(stmt); i += 1) {
      tree rhs = gimple_op(stmt, i);
      process_load(ctx, &loc, gsi, rhs);
    }
    tree lhs = gimple_assign_lhs(stmt);
    process_store(ctx, &loc, gsi, lhs);
  } else if (is_gimple_call (stmt)) {
    tree fndecl = gimple_call_fndecl(stmt);
    if (fndecl) {
      tree rt_fndecl = find_rt_func(ctx, fndecl);
      if (rt_fndecl != 0)
        gimple_call_set_fndecl(stmt, rt_fndecl);
    }
    tree lhs = gimple_call_lhs(stmt);
    if (lhs != 0)
      process_store(ctx, &loc, gsi, lhs);
  }
}


void                    relite_pass         (relite_context_t* ctx,
                                             struct function* func) {
  if (ctx->rt_func_setup == 0) {
    setup_rt_funcs(ctx);
    ctx->rt_func_setup = 1;
  }

  dbg(ctx, "\nPROCESSING FUNCTION '%s'", decl_name(func->decl));
  ctx->stat_func += 1;

  int is_first_gimple = 0;
  basic_block bb;
  gimple_stmt_iterator gsi;
  FOR_EACH_BB_FN (bb, func) {
    for (gsi = gsi_start_bb (bb); !gsi_end_p (gsi); gsi_next (&gsi)) {
      gimple stmt = gsi_stmt(gsi);
      expanded_location loc = expand_location(gimple_location(stmt));
      if (is_first_gimple == 0) {
        if (ctx->instr_enter) {
          ctx->instr_enter(ctx, &loc, &gsi, func->decl);
        }
        is_first_gimple = 1;
      }
      instrument(ctx, &gsi);
      if (gimple_code(stmt) == GIMPLE_RETURN) {
        if (ctx->instr_leave != 0)
          ctx->instr_leave(ctx, &loc, &gsi, func->decl);
      }
    }
  }
}


void                    relite_finish       (relite_context_t* ctx) {
  dbg(ctx, "STATS func: %d, gimple: %d, store: %d/%d, load: %d/%d",
      ctx->stat_func, ctx->stat_gimple,
      ctx->stat_store_instrumented, ctx->stat_store_total,
      ctx->stat_load_instrumented, ctx->stat_load_total);
}


