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


static tree             get_rt_mop          () {
  static tree fn = 0;
  if (fn == 0) {
    fn = lookup_name(get_identifier(
        "tsan_rtl_mop"));
    if (fn == 0) {
      printf("relite: can't find tsan_rtl_mop decl\n");
      exit(1);
    }
  }
  return fn;
}


static tree             get_rt_stack        () {
  static tree var = 0;
  if (var == 0) {
    var = lookup_name(get_identifier("ShadowStack"));
    if (var == 0) {
      printf("relite: can't find ShadowStack decl\n");
      exit(1);
    }
  }
  return var;
}


static tree             get_rt_thread       () {
  static tree var = 0;
  if (var == 0) {
    var = lookup_name(get_identifier("INFO"));
    if (var == 0) {
      printf("relite: can't find INFO decl\n");
      exit(1);
    }
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
  if (ctx->func_calls == 0 && ctx->func_mops == 0)
    return;
  build_stack_op(pre, PLUS_EXPR);
  build_stack_op(post, MINUS_EXPR);
}


static void             instr_mop           (struct relite_context_t* ctx,
                                             tree expr,
                                             location_t loc,
                                             int is_store,
                                             int is_sblock,
                                             gimple_seq* pre,
                                             gimple_seq* post) {
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

  dbg(ctx, "instrumenting mop: is_sblock=%d, is_store=%d, flags=%d",
      is_sblock, is_store, flags);

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
  ctx->opt_sblock_size  = 5;
  return ctx;
}



typedef enum bb_state_e {
  bb_not_visited,
  bb_candidate,
  bb_visited,
} bb_state_e;


typedef struct bb_data_t {
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


static void             set_location        (gimple_seq seq,
                                             location_t loc) {
  gimple_seq_node n;
  for (n = gimple_seq_first(seq); n != 0; n = n->next)
    gimple_set_location(n->stmt, loc);
}



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
  instr_mop(ctx->ctx, expr, ctx->loc, is_store, is_sblock,
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
    gimple_seq pre_call = 0;
    gimple_seq post_call = 0;
    instr_call(ctx, fndecl, loc, &pre_call, &post_call);
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

  mop_ctx_t mop_ctx = {ctx, bbd, gsi, gimple_location(stmt)};
  walk_stmt_load_store_ops(stmt, &mop_ctx, instrument_load, instrument_store);
}


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
    bb_data[bb->index].state = (bb == entry_bb) ? bb_candidate : bb_not_visited;
  }

  for (;;) {
    basic_block cur_bb = 0;
    basic_block any_bb = 0;
    FOR_EACH_BB_FN(bb, func) {
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

  gimple_seq pre_func_seq = 0;
  gimple_seq post_func_seq = 0;
  instr_func(ctx, func->decl, &pre_func_seq, &post_func_seq);
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


void                    relite_prepass      (relite_context_t* ctx) {
  if (ctx->setup_completed == 0) {
    ctx->setup_completed = 1;
    setup(ctx);
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

