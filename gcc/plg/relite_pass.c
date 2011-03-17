/* Relite: GCC instrumentation plugin for ThreadSanitizer
 * Copyright (c) 2011, Google Inc. All rights reserved.
 * Author: Dmitry Vyukov (dvyukov)
 *
 * Relite is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3, or (at your option) any later
 * version. See http://www.gnu.org/licenses/
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
#include <function.h>
#include <tree-flow.h>
#include <tree-pass.h>
#include <domwalk.h>
#include <cfghooks.h>
#include <diagnostic.h>
#include <c-common.h>
#include <c-pragma.h>
#include <cp/cp-tree.h>
#include <toplev.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include "relite_pass.h"
#include "relite_ignore.h"


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


typedef enum bb_state_e {
  bb_not_visited,
  bb_candidate,
  bb_visited,
} bb_state_e;


typedef struct bb_data_t {
  //int                   idx;
  //basic_block           bb;
  bb_state_e            state;
  int                   has_sb;
  char const*           sb_file;
  int                   sb_line_min;
  int                   sb_line_max;
} bb_data_t;


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


static void             dbg_dump_mop        (relite_context_t* ctx,
                                             char const* what,
                                             location_t loc,
                                             tree expr,
                                             tree expr_ssa,
                                             char const* reason) {
  expanded_location eloc = expand_location(loc);
  dbg(ctx, "%s:%d:%d: %s '%s' code=%s%s type=%s: %s %s",
      eloc.file, eloc.line, eloc.column,
      what, decl_name(expr),
      tree_code_name[TREE_CODE(expr)],
      (expr_ssa ? "(ssa)" : ""),
      tree_code_name[TREE_CODE(TREE_TYPE(expr))],
      (reason == 0 ? "INSTRUMENTED" : "IGNORED"),
      reason ?: "");
}


static void             dump_instr_seq      (relite_context_t* ctx,
                                             char const* what,
                                             gimple_seq* seq,
                                             location_t loc) {
  expanded_location eloc = expand_location(loc);
  dbg(ctx, "inserting %s at %s:%d:%d gimple seq:",
      what, eloc.file, eloc.line, eloc.column);
  gimple_seq_node n = gimple_seq_first(*seq);
  for (; n != 0; n = n->next) {
    enum gimple_code gc = gimple_code(n->stmt);
    dbg(ctx, "  gimple %s", gimple_code_name[gc]);
  }
}

/*
static void             setup_rt_decls      (relite_context_t* ctx) {
  int remain = ctx->rt_decl_count;
  tree decl = NAMESPACE_LEVEL(global_namespace)->names;
  for (; decl != 0; decl = TREE_CHAIN(decl)) {
    if (DECL_IS_BUILTIN(decl))
      continue;
    char const* name = IDENTIFIER_POINTER(DECL_NAME(decl));
    if (name == 0)
      continue;
    for (int i = 0; i != ctx->rt_decl_count; i += 1) {
      if (ctx->rt_decl[i].decl == 0
          && strcmp(name, ctx->rt_decl[i].rt_name) == 0) {
        ctx->rt_decl[i].decl = decl;
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
    for (int i = 0; i != ctx->rt_decl_count; i += 1) {
      if (ctx->rt_decl[i].decl == 0)
        printf("relite: %s\n", ctx->rt_decl[i].rt_name);
    }
    exit(1);
  }
}


static tree             find_rt_decl        (relite_context_t* ctx,
                                             char const* decl_name) {
  for (int i = 0; i != ctx->rt_decl_count; i += 1) {
    if (ctx->rt_decl[i].real_name != 0
        && strcmp(ctx->rt_decl[i].real_name, decl_name) == 0) {
      return ctx->rt_decl[i].decl;
    }
  }
  return 0;
}
  */


static void             set_location        (gimple_seq seq,
                                             location_t loc) {
  gimple_seq_node n;
  for (n = gimple_seq_first(seq); n != 0; n = n->next)
    gimple_set_location(n->stmt, loc);
}


/*
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

      do_instrument = 1;
      break;
    }

    default: {
      reason = "unknown type";
      break;
    }
  }

//  if (do_instrument != 0) {
//    tree expr_type = TREE_TYPE(expr);
//    tree type_decl = TYPE_NAME(expr_type);
//    if (type_decl != 0) {
//      char const* type_name = decl_name(type_decl);
//      if (strncmp(type_name, "Atomic", sizeof("Atomic") - 1) == 0) {
//        //TODO(dvyukov): check that it's at least volatile
//        // a race on non-volatile int is not all that cool
//        reason = "atomic type";
//        do_instrument = 0;
//      }
//    }
//  }

  assert(do_instrument != 0 || (reason != 0 && reason[0] != 0));

  ctx->stat_store_total += 1;
  if (do_instrument != 0 && ctx->instr_mop) {
    ctx->stat_store_instrumented += 1;
    assert(is_gimple_addressable(expr));
    gimple_seq pre_mop = 0;
    gimple_seq post_mop = 0;
    ctx->instr_mop(ctx, expr, 1, 1, loc, &pre_mop, &post_mop);
    if (pre_mop != 0 || post_mop != 0) {
      ctx->func_mops += 1;
      if (pre_mop != 0) {
        dump_instr_seq(ctx, "before store", &post_mop, loc);
        set_location(post_mop, loc);
        gsi_insert_seq_before(gsi, pre_mop, GSI_SAME_STMT);
      }
      if (post_mop != 0) {
        dump_instr_seq(ctx, "after store", &post_mop, loc);
        set_location(post_mop, loc);
        gsi_insert_seq_after(gsi, post_mop, GSI_NEW_STMT);
      }
    }
  }

  //dbg_access(ctx, "store to", eloc, expr, expr_ssa, do_instrument, reason);
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
            //TODO(dvyukov): handle bit-fields correctly
            dbg(ctx, "bit_offset=%u, size=%u", fld_off, fld_size);
            reason = "weird bit field";
            break;
          }
        }
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
  if (do_instrument != 0 && ctx->instr_mop != 0) {
    ctx->stat_load_instrumented += 1;
    assert(is_gimple_addressable(expr));
    gimple_seq pre_mop = 0;
    gimple_seq post_mop = 0;
    ctx->instr_mop(ctx, expr, loc, 0, 1, &pre_mop, &post_mop);
    if (pre_mop != 0 || post_mop != 0) {
      ctx->func_mops += 1;
      if (pre_mop != 0) {
        set_location(post_mop, loc);
        dump_instr_seq(ctx, "before load", &pre_mop, loc);
        gsi_insert_seq_before(gsi, pre_mop, GSI_SAME_STMT);
      }
      if (post_mop != 0) {
        set_location(post_mop, loc);
        dump_instr_seq(ctx, "after load", &post_mop, loc);
        gsi_insert_seq_after(gsi, post_mop, GSI_NEW_STMT);
      }
    }
  }

  //dbg_access(ctx, "load of", eloc, expr, expr_ssa, do_instrument, reason);
}
*/

typedef struct  mop_ctx_t {
  relite_context_t*     ctx;
  bb_data_t*            bbd;
  gimple_stmt_iterator* gsi;
  location_t            loc;
} mop_ctx_t;


static void             instrument_mop      (mop_ctx_t* ctx,
                                             gimple stmt,
                                             tree expr,
                                             int is_store) {
  assert(ctx != 0 && stmt != 0 && expr != 0);

  if (is_store)
    ctx->ctx->stat_store_total += 1;
  else
    ctx->ctx->stat_load_total += 1;

  char const* reason = 0;

  // map SSA name to real name
  tree expr_ssa = 0;
  if (TREE_CODE(expr) == SSA_NAME) {
    expr_ssa = expr;
    expr = SSA_NAME_VAR(expr);
  }
  enum tree_code const tcode = TREE_CODE(expr);

  // Below are things we do NOT want to instrument.
  if (DECL_ARTIFICIAL(expr)/* || (expr_ssa && DECL_ARTIFICIAL(expr_ssa))*/) {
    // artificial variable emitted by the compiler
    reason = "artificial";
  } else if (tcode == RESULT_DECL) {
    reason = "result";
  } else if (tcode == INTEGER_CST) {
    reason = "constant";
  } else if (tcode == VAR_DECL && TREE_ADDRESSABLE(expr) == 0) {
    // the var does not live in memory -> no possibility of races
    reason = "non-addressable";
  } else if (TREE_CODE(TREE_TYPE(expr)) == RECORD_TYPE) {
    // why don't I instrument records?.. perhaps it crashes compilation,
    // and should be handled more carefully
    reason = "record type";
  } else if (tcode == CONSTRUCTOR) {
    // as of now crashes compilation
    //TODO(dvyukov): handle it correctly
    reason = "constructor expression";
  } else if (tcode == CONSTRUCTOR) {
    reason = "constructor expr";
  } else if (tcode == RESULT_DECL) {
    reason = "function result";
  } else if (tcode == PARM_DECL) {
    reason = "function parameter";
  } else if (tcode == COMPONENT_REF) {
    tree field = expr->exp.operands[1];
    if (TREE_CODE(field) == FIELD_DECL) {
      unsigned fld_off = field->field_decl.bit_offset->int_cst.int_cst.low;
      unsigned fld_size = field->decl_common.size->int_cst.int_cst.low;
      if (((fld_off % __CHAR_BIT__) != 0)
          || ((fld_size % __CHAR_BIT__) != 0)){
        // as of now it crashes compilation
        //TODO(dvyukov): handle bit-fields -> as if touching the whole field
        dbg(ctx->ctx, "bit_offset=%u, size=%u", fld_off, fld_size);
        reason = "weird bit field";
      }
    }
  }

  if (tcode != ARRAY_REF //?
      && tcode != VAR_DECL
      && tcode != COMPONENT_REF
      && tcode != INDIRECT_REF //?
      //TODO(dvyukov): handle those cases
      //&& tcode != FIELD_DECL
      //&& tcode != MEM_REF
      //&& tcode != ARRAY_RANGE_REF
      //&& tcode != TARGET_MEM_REF
      //&& tcode != ADDR_EXPR
      ) {
    reason = "unknown type";
  }

  dbg_dump_mop(ctx->ctx, is_store ? "store to" : "load of",
      ctx->loc, expr, expr_ssa, reason);

  if (reason != 0)
    return;

//  if (is_store == 0)
//    return;

  // If a call gimple contains a load,
  // then we must emit instrumentation BEFORE the gimple.
  // As of now all instrumentation is emitted AFTER the gimple.
  assert(!is_gimple_call(stmt) || is_store);

  if (is_store)
    ctx->ctx->stat_store_instrumented += 1;
  else
    ctx->ctx->stat_load_instrumented += 1;

  expanded_location eloc = expand_location(ctx->loc);
  bb_data_t* bbd = ctx->bbd;
  int const is_sblock = (bbd->has_sb == 0
      || !(strcmp(eloc.file, bbd->sb_file) == 0
          && eloc.line >= bbd->sb_line_min
          && eloc.line <= bbd->sb_line_max));

  dbg(ctx->ctx, "sblock: %s (%d/%s:%d-%d)->(%d/%s:%d-%d)",
      (is_sblock ? "YES" : "NO"),
      bbd->has_sb, bbd->sb_file, bbd->sb_line_min, bbd->sb_line_max,
      is_sblock, eloc.file, eloc.line, eloc.line + ctx->ctx->opt_sblock_size);

  if (is_sblock) {
    ctx->ctx->stat_sblock += 1;
    bbd->has_sb = 1;
    bbd->sb_file = eloc.file;
    bbd->sb_line_min = eloc.line;
    bbd->sb_line_max = eloc.line + ctx->ctx->opt_sblock_size;
  }

  gimple_seq pre_mop = 0;
  gimple_seq post_mop = 0;
  ctx->ctx->instr_mop(ctx->ctx, expr, ctx->loc, is_store, is_sblock,
                      &pre_mop, &post_mop);
  if (pre_mop != 0 || post_mop != 0) {
    ctx->ctx->func_mops += 1;
    if (pre_mop != 0) {
      dump_instr_seq(ctx->ctx,
                     (is_store ? "before store" : "before load"),
                     &post_mop, ctx->loc);
      set_location(post_mop, ctx->loc);
      gsi_insert_seq_before(ctx->gsi, pre_mop, GSI_SAME_STMT);
    }
    if (post_mop != 0) {
      dump_instr_seq(ctx->ctx,
                     (is_store ? "after store" : "after load"),
                     &post_mop, ctx->loc);
      set_location(post_mop, ctx->loc);
      gsi_insert_seq_after(ctx->gsi, post_mop, GSI_NEW_STMT);
    }
  }
}


static bool             instrument_load     (gimple stmt,
                                             tree expr,
                                             void* arg) {
  mop_ctx_t* ctx = (mop_ctx_t*)arg;
  instrument_mop(ctx, stmt, expr, 0);
  return 0;
}


static bool             instrument_store    (gimple stmt,
                                             tree expr,
                                             void* arg) {
  mop_ctx_t* ctx = (mop_ctx_t*)arg;
  instrument_mop(ctx, stmt, expr, 1);
  return 0;
}


static void             handle_gimple       (relite_context_t* ctx,
                                             gimple_stmt_iterator* gsi,
                                             bb_data_t* bbd) {
  ctx->stat_gimple += 1;
  gimple stmt = gsi_stmt(*gsi);
  location_t loc = gimple_location(stmt);
  expanded_location eloc = expand_location(loc);
  dbg(ctx, "%s:%d:%d: processing gimple %s",
      eloc.file, eloc.line, eloc.column, gimple_code_name[gimple_code(stmt)]);

  if (is_gimple_call(stmt)) {
    // After a function call we must start a brand new sblock,
    // because the call can contain synchronization or whatever.
    bbd->has_sb = 0;

    tree fndecl = gimple_call_fndecl(stmt);
    if (ctx->instr_call != 0) {
      gimple_seq pre_call = 0;
      gimple_seq post_call = 0;
      ctx->instr_call(ctx, fndecl, loc, &pre_call, &post_call);
      if (pre_call != 0 || post_call != 0) {
        ctx->func_calls += 1;
        if (pre_call != 0) {
          dump_instr_seq(ctx, "before call", &pre_call, loc);
          set_location(pre_call, loc);
          gsi_insert_seq_before(gsi, pre_call, GSI_SAME_STMT);
        }
        if (post_call != 0) {
          dump_instr_seq(ctx, "after call", &post_call, loc);
          set_location(post_call, loc);
          gsi_insert_seq_after(gsi, post_call, GSI_NEW_STMT);
        }
      }
    }
  }

  mop_ctx_t mop_ctx = {ctx, bbd, gsi, gimple_location(stmt)};
  walk_stmt_load_store_ops(stmt, &mop_ctx, instrument_load, instrument_store);


  /*
  gimple stmt = gsi_stmt(*gsi);
  location_t const loc = gimple_location(stmt);
  expanded_location eloc = expand_location(loc);
  ctx->stat_gimple += 1;

  if (gimple_code(stmt) == GIMPLE_BIND) {
    assert(false);
  } else if (is_gimple_assign(stmt)) {
    for (int i = 1; i != gimple_num_ops(stmt); i += 1) {
      tree rhs = gimple_op(stmt, i);
      process_load(ctx, loc, &eloc, gsi, rhs);
    }
    tree lhs = gimple_assign_lhs(stmt);
    process_store(ctx, loc, &eloc, gsi, lhs);
  } else if (is_gimple_call (stmt)) {
    tree fndecl = gimple_call_fndecl(stmt);
    if (ctx->instr_call != 0) {
      gimple_seq pre_call = 0;
      gimple_seq post_call = 0;
      ctx->instr_call(ctx, fndecl, loc, &pre_call, &post_call);
      if (pre_call != 0 || post_call != 0) {
        ctx->func_calls += 1;
        if (pre_call != 0) {
          dump_instr_seq(ctx, "before call", &pre_call, loc);
          set_location(pre_call, loc);
          gsi_insert_seq_before(gsi, pre_call, GSI_SAME_STMT);
        }
        if (post_call != 0) {
          dump_instr_seq(ctx, "after call", &post_call, loc);
          set_location(post_call, loc);
          gsi_insert_seq_after(gsi, post_call, GSI_NEW_STMT);
        }
      }
    }

    //TODO(dvyukov): check call operands, because strictly saying they are loads
    tree lhs = gimple_call_lhs(stmt);
    if (lhs != 0)
      process_store(ctx, loc, &eloc, gsi, lhs);
  }
  */
}


/*
static void             per_block_data_init (struct dom_walk_data* walk_data,
                                             basic_block bb,
                                             bool recycled) {
  //per_block_data_t* data =
  //    (per_block_data_t*)VEC_last(void_p, walk_data->block_data_stack);
  //data->has_sb = 0;
  //if (VEC_length)



}

static void             process_basic_block (struct dom_walk_data* walk_data,
                                             basic_block bb) {
  relite_context_t* ctx = (relite_context_t*)walk_data->global_data;

  VEC(void_p,heap)* stack = walk_data->block_data_stack;
  per_block_data_t* data = (per_block_data_t*)VEC_last(void_p, stack);
  data->has_sb = 0;
  if (VEC_length(void_p, stack) > 1) {
    per_block_data_t* dom_data = (per_block_data_t*)
        VEC_index(void_p, stack, VEC_length(void_p, stack) - 2);
    if (dom_data->has_sb != 0) {
      data->has_sb = 1;
      data->sb_loc = dom_data->sb_loc;
    }
  }

  gimple_stmt_iterator gsi;
  for (gsi = gsi_start_bb(bb); !gsi_end_p(gsi); gsi_next(&gsi)) {
    handle_gimple(ctx, &gsi, data);
  }

}
*/


//#define BB_NOT_VISITED  (1<<28)
//#define BB_CANDIDATE    (1<<29)
//#define BB_VISITED      (1<<30)

static void             instrument_bblock   (relite_context_t* ctx,
                                             bb_data_t* bbd,
                                             basic_block bb) {
  ctx->stat_bb_total += 1;
  gimple_stmt_iterator gsi;
  for (gsi = gsi_start_bb(bb); !gsi_end_p(gsi); gsi_next(&gsi)) {
    handle_gimple(ctx, &gsi, bbd);
  }
}




static void             instrument_function (relite_context_t* ctx,
                                             struct function* func) {
  int const bb_cnt = func->cfg->x_n_basic_blocks;
  dbg(ctx, "%d basic blocks", bb_cnt);
  bb_data_t* bb_data = xcalloc(bb_cnt, sizeof(bb_data_t));
  basic_block entry_bb = ENTRY_BLOCK_PTR_FOR_FUNCTION(func);
  edge entry_edge = single_succ_edge(entry_bb);
  entry_bb = entry_edge->dest;
  basic_block bb = 0;
  FOR_EACH_BB_FN(bb, func) {
    //bb_data[bb->index].idx = bb->index;
    //bb_data[bb->index].bb = bb;
    bb_data[bb->index].state = (bb == entry_bb) ? bb_candidate : bb_not_visited;
    //BASIC_BLOCK
  }

  for (;;) {
    basic_block cur_bb = 0;
    basic_block any_bb = 0;
    FOR_EACH_BB_FN(bb, func) {
    //int i;
    //for (i = 0; i != bb_cnt; i += 1) {
      bb_data_t* bbd = &bb_data[bb->index];
      dbg(ctx, "considering bb %d state %d", bb->index, bbd->state);
      if (bbd->state == bb_candidate) {
        cur_bb = bb;
        any_bb = bb;
        int eidx;
        edge e = 0;
        for (eidx = 0; VEC_iterate(edge, bb->preds, eidx, e); eidx++) {
          bb_data_t* pred = &bb_data[e->src->index];
          if (pred->state != bb_visited) {
            cur_bb = 0;
            break;
          }
        }
      }
      if (cur_bb != 0)
        break;
    }
    if (any_bb == 0)
      break;
    cur_bb = cur_bb ?: any_bb;
    bb_data_t* bbd = &bb_data[cur_bb->index];
    assert(bbd->state == bb_candidate);
    bbd->state = bb_visited;
    dbg(ctx, "processing basic block %d", cur_bb->index);

    int eidx;
    edge e = 0;
    for (eidx = 0; VEC_iterate(edge, cur_bb->preds, eidx, e); eidx++) {
      bb_data_t* pred = &bb_data[e->src->index];
      if ((pred->state != bb_visited)
          || (pred->has_sb == 0)
          || (pred == bbd)) {
        bbd->has_sb = 0;
        break;
      } else if (bbd->has_sb == 0) {
        bbd->has_sb = 1;
        bbd->sb_file = pred->sb_file;
        bbd->sb_line_min = pred->sb_line_min;
        bbd->sb_line_max = pred->sb_line_max;
      } else {
        bbd->has_sb = 0;
        if (strcmp(bbd->sb_file, pred->sb_file) == 0) {
          int const sb_line_min = MAX(bbd->sb_line_min, pred->sb_line_min);
          int const sb_line_max = MIN(bbd->sb_line_max, pred->sb_line_max);
          if (sb_line_min <= sb_line_max) {
            bbd->has_sb = 1;
            bbd->sb_line_min = sb_line_min;
            bbd->sb_line_max = sb_line_max;
          }
        }
      }
    }

    instrument_bblock(ctx, bbd, cur_bb);

    for (eidx = 0; VEC_iterate(edge, cur_bb->succs, eidx, e); eidx++) {
      bb_data_t* pred = &bb_data[e->dest->index];
      if (pred->state == bb_not_visited)
        pred->state = bb_candidate;
    }
  }



  /*
  basic_block bb = 0;
  FOR_EACH_BB_FN(bb, func) {
    assert((bb->flags & (BB_NOT_VISITED | BB_CANDIDATE | BB_VISITED)) == 0);
    bb->flags |= BB_NOT_VISITED;
  }
  bb = ENTRY_BLOCK_PTR_FOR_FUNCTION(func);
  bb->flags = (bb->flags & ~BB_NOT_VISITED) | BB_CANDIDATE;

  for (;;) {
    basic_block cur_bb = 0;
    basic_block any_bb = 0;
    //int finished = 1;
    FOR_EACH_BB_FN(bb, func) {
      assert(!!(bb->flags & BB_NOT_VISITED)
             + !!(bb->flags & BB_CANDIDATE)
             + !!(bb->flags & BB_VISITED) == 1);
      if (bb->flags & BB_CANDIDATE) {
        int eidx;
        edge e = 0;
        cur_bb = bb;
        any_bb = bb;
        for (eidx = 0; VEC_iterate(edge,bb->preds,eidx,e); eidx++) {
          basic_block pred = e->src;
          if ((pred->flags & BB_VISITED) == 0) {
            cur_bb = 0;
            break;
          }
        }
      }
      if (cur_bb != 0)
        break;
    }
    if (any_bb == 0)
      break;
    if (cur_bb == 0)
      cur_bb = any_bb;

  }
  */

  //int const bb_count = func->cfg->x_n_basic_blocks;
  //int remain = bb_count;

  /*
  int entry_bb = ENTRY_BLOCK_PTR_FOR_FUNCTION(func)->index;
  int* candidates = XCNEWVEC(int, remain);
  candidates[entry_bb] = 1;
  while (remain != 0) {
    basic_block bb;
    FOR_EACH_BB_FN (bb, func) {

    int bb_idx;
    for (bb_idx = 0; bb_idx != bb_count; bb_idx += 1) {
      if (candidates[bb_idx] == 1) {
        basic_block bb = func->cfg->
      }
    }
  }
  xfree(candidates);
  */

  /*
  struct dom_walk_data walk_data;
  walk_data.dom_direction = CDI_DOMINATORS;
  walk_data.initialize_block_local_data = per_block_data_init;
  walk_data.before_dom_children = process_basic_block;
  walk_data.after_dom_children = 0;
  walk_data.block_local_data_size = sizeof(per_block_data_t);
  walk_data.global_data = ctx;

  calculate_dominance_info(CDI_DOMINATORS);
  init_walk_dominator_tree(&walk_data);
  walk_dominator_tree(&walk_data, ENTRY_BLOCK_PTR_FOR_FUNCTION(func));
  fini_walk_dominator_tree(&walk_data);
  free_dominance_info(CDI_DOMINATORS);
  */

  /*
  gimple_seq pre_func_seq = 0;
  gimple_seq post_func_seq = 0;
  if (ctx->instr_func) {
    ctx->instr_func(ctx, func->decl, &pre_func_seq, &post_func_seq);
    if (pre_func_seq != 0 || post_func_seq != 0) {
      if (pre_func_seq != 0) {
        basic_block entry = ENTRY_BLOCK_PTR;
        edge entry_edge = single_succ_edge(entry);
        entry = split_edge(entry_edge);
        gimple_stmt_iterator gsi = gsi_start_bb(entry);
        //gimple stmt = gsi_stmt(gsi);
        //location_t loc = gimple_location(stmt);
        //dump_instr_seq(ctx, "at function start", &pre_func_seq, loc);
        //set_location(pre_func_seq, loc);
        gsi_insert_seq_after(&gsi, pre_func_seq, GSI_NEW_STMT);
      }

      //int is_first_gimple = 0;
      basic_block bb;
      FOR_EACH_BB_FN (bb, func) {
        gimple_stmt_iterator gsi;
        gimple_stmt_iterator gsi2 = gsi_start_bb (bb);
        for (;;) {
          gsi = gsi2;
          if (gsi_end_p(gsi))
            break;
          gsi_next(&gsi2);

          gimple stmt = gsi_stmt(gsi);
          location_t loc = gimple_location(stmt);

          if (gimple_code(stmt) == GIMPLE_RETURN) {
            if (post_func_seq != 0) {
              dump_instr_seq(ctx, "at function end", &post_func_seq, loc);
              set_location(post_func_seq, loc);
              gsi_insert_seq_before(&gsi, post_func_seq, GSI_SAME_STMT);
            }
          }
        }
      }
    }
  }
  */
}


void                    relite_pass         (relite_context_t* ctx,
                                             struct function* func) {
  if (ctx->ignore_file != 0)
    return;

  char const* func_name = decl_name(func->decl);
  dbg(ctx, "\nPROCESSING FUNCTION '%s'", func_name);
  ctx->stat_func_total += 1;
  ctx->func_calls = 0;
  ctx->func_mops = 0;

  tree func_attr = DECL_ATTRIBUTES(func->decl);
  for (; func_attr != NULL_TREE; func_attr = TREE_CHAIN(func_attr)) {
    char const* attr_name = IDENTIFIER_POINTER(TREE_PURPOSE(func_attr));
    if (strcmp(attr_name, "relite_ignore") == 0) {
      dbg(ctx, "IGNORING due to relite_ignore attribute");
      return;
    }
  }

  ctx->stat_func_instrumented += 1;

  char const* asm_name = IDENTIFIER_POINTER(DECL_ASSEMBLER_NAME(func->decl));
  ctx->func_ignore = relite_ignore_func(asm_name);

  instrument_function(ctx, func);


  /*
  basic_block bb;
  FOR_EACH_BB_FN (bb, func) {
    ctx->stat_bb_total += 1;
    gimple_stmt_iterator gsi;
    gimple_stmt_iterator gsi2 = gsi_start_bb (bb);
    for (;;) {
      gsi = gsi2;
      if (gsi_end_p(gsi))
        break;
      gsi_next(&gsi2);

      handle_gimple(ctx, &gsi);
    }
  }
  */

  gimple_seq pre_func_seq = 0;
  gimple_seq post_func_seq = 0;
  if (ctx->instr_func) {
    ctx->instr_func(ctx, func->decl, &pre_func_seq, &post_func_seq);
    if (pre_func_seq != 0 || post_func_seq != 0) {
      if (pre_func_seq != 0) {
        basic_block entry = ENTRY_BLOCK_PTR;
        edge entry_edge = single_succ_edge(entry);
        entry = split_edge(entry_edge);
        gimple_stmt_iterator gsi = gsi_start_bb(entry);
        //gimple stmt = gsi_stmt(gsi);
        //location_t loc = gimple_location(stmt);
        //dump_instr_seq(ctx, "at function start", &pre_func_seq, loc);
        //set_location(pre_func_seq, loc);
        gsi_insert_seq_after(&gsi, pre_func_seq, GSI_NEW_STMT);
      }

      //int is_first_gimple = 0;
      basic_block bb;
      FOR_EACH_BB_FN (bb, func) {
        gimple_stmt_iterator gsi;
        gimple_stmt_iterator gsi2 = gsi_start_bb (bb);
        for (;;) {
          gsi = gsi2;
          if (gsi_end_p(gsi))
            break;
          gsi_next(&gsi2);

          gimple stmt = gsi_stmt(gsi);
          location_t loc = gimple_location(stmt);

          if (gimple_code(stmt) == GIMPLE_RETURN) {
            if (post_func_seq != 0) {
              dump_instr_seq(ctx, "at function end", &post_func_seq, loc);
              set_location(post_func_seq, loc);
              gsi_insert_seq_before(&gsi, post_func_seq, GSI_SAME_STMT);
            }
          }
        }
      }
    }
  }
}


void                    relite_prepass      (relite_context_t* ctx) {
  if (ctx->setup_completed == 0) {
    ctx->setup_completed = 1;
    if (ctx->setup != 0) {
      ctx->setup(ctx);
    }
    if (relite_ignore_func(main_input_filename)) {
      dbg(ctx, "IGNORING FILE due to ignore file");
      ctx->ignore_file = 1;
    }
  }
}


void                    relite_finish       (relite_context_t* ctx) {
  int mop_count = ctx->stat_store_instrumented + ctx->stat_load_instrumented;
  dbg(ctx, "STATS func: %d/%d, gimple: %d, store: %d/%d, load: %d/%d",
      ctx->stat_func_instrumented, ctx->stat_func_total,
      ctx->stat_gimple,
      ctx->stat_store_instrumented, ctx->stat_store_total,
      ctx->stat_load_instrumented, ctx->stat_load_total);
  dbg(ctx, "basic blocks: %d",
      ctx->stat_bb_total);
  dbg(ctx, "sblocks/mop: %d/%d",
      ctx->stat_sblock, mop_count);
}






/*
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

*/

