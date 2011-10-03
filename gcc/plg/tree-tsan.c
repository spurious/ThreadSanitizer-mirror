/* Relite: GCC instrumentation plugin for ThreadSanitizer
 * Copyright (c) 2011, Google Inc. All rights reserved.
 * Author: Dmitry Vyukov (dvyukov)
 *
 * Relite is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3, or (at your option) any later
 * version. See http://www.gnu.org/licenses/
 */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tree.h"
#include "intl.h"
#include "tm.h"
#include "basic-block.h"
#include "gimple.h"
#include "function.h"
#include "tree-flow.h"
#include "tree-pass.h"
#include "cfghooks.h"
#include "cp/cp-tree.h"
#include "langhooks.h"
#include "toplev.h"
#include "output.h"
#include "diagnostic.h"
#include "tree-tsan.h"

static enum tsan_ignore_e func_ignore;
static int func_calls;
static int func_mops;
static int ignore_file = -1;

#define SBLOCK_SIZE 5
#define MAX_MOP_BYTES 16

enum bb_state_e
{
  bb_not_visited,
  bb_candidate,
  bb_visited
};

struct bb_data_t
{
  enum bb_state_e       state;
  int                   has_sb;
  char const           *sb_file;
  int                   sb_line_min;
  int                   sb_line_max;
};

struct mop_desc_t
{
  int                   is_call;
  gimple_stmt_iterator  gsi;
  tree                  expr;
  tree                  dtor_vptr_expr;
  int                   is_store;
};

typedef struct mop_desc_t mop_desc_t;
DEF_VEC_O(mop_desc_t);
DEF_VEC_ALLOC_O(mop_desc_t, heap);
static VEC(mop_desc_t, heap) *mop_list;

static tree
shadow_stack_def (void)
{
  static tree def;

  if (def != NULL)
    return def;

  def = build_decl (UNKNOWN_LOCATION, VAR_DECL, 
		    get_identifier ("__tsan_shadow_stack"), 
		    build_pointer_type(ptr_type_node));
  TREE_STATIC (def) = 1;
  TREE_PUBLIC (def) = 0;
  DECL_INITIAL (def) = NULL;
  DECL_TLS_MODEL (def) = decl_default_tls_model (def);
  DECL_ARTIFICIAL (def) = 1;
  varpool_finalize_decl (def);
  varpool_mark_needed_node (varpool_node (def));
  return def;
}

static tree
thread_ignore_def (void)
{
  static tree def;

  if (def != NULL)
    return def;

  def = build_decl (UNKNOWN_LOCATION, VAR_DECL, 
		    get_identifier ("__tsan_thread_ignore"), 
		    integer_type_node);
  TREE_STATIC (def) = 1;
  TREE_PUBLIC (def) = 0;
  DECL_INITIAL (def) = NULL;
  DECL_TLS_MODEL (def) = decl_default_tls_model (def);
  DECL_ARTIFICIAL (def) = 1;
  varpool_finalize_decl (def);
  varpool_mark_needed_node (varpool_node (def));
  return def;
}

static tree
rtl_mop_def (void)
{
  tree fn_type;

  static tree def;

  if (def != NULL)
    return def;

  fn_type = build_function_type_list (void_type_node, ptr_type_node, integer_type_node , NULL_TREE);
  def = build_fn_decl ("__tsan_mop", fn_type);
  TREE_NOTHROW (def) = 1;
  DECL_ATTRIBUTES (def) = tree_cons (get_identifier ("leaf"), NULL, DECL_ATTRIBUTES (def));
  DECL_ASSEMBLER_NAME (def);
  return def;
}

static void
build_stack_op (gimple_seq *seq, bool do_dec)
{
  /* Builds either (ShadowStack += 1) or (ShadowStack -= 1) expression
     depending on 'do_dec' parameter */

  tree op_size;
  double_int op_size_cst;
  unsigned long long size_val;
  unsigned long long size_valhi;
  tree op_expr;
  tree assign;
  tree rtl_stack;

  op_size = TYPE_SIZE(ptr_type_node);
  op_size_cst = tree_to_double_int(op_size);
  /* TODO(dvyukov): use target arch byte size */
  size_val = op_size_cst.low / __CHAR_BIT__;
  size_valhi = 0;
  if (do_dec) {
    size_val = -size_val;
    size_valhi = -1;
  }
  op_size = build_int_cst_wide(sizetype, size_val, size_valhi);
  rtl_stack = shadow_stack_def ();
  op_expr = build2(POINTER_PLUS_EXPR, ptr_type_node, rtl_stack, op_size);
  assign = build2(MODIFY_EXPR, ptr_type_node, rtl_stack, op_expr);
  force_gimple_operand(assign, seq, true, NULL_TREE);
}

static void
build_rec_ignore_op (gimple_seq *seq, enum tree_code op)
{
  /* Builds either (thread_local_ignore += 1)
     or (thread_local_ignore -= 1) expression depending on 'op' parameter */

  tree rec_expr;
  gimple_seq rec_inc;
  gimple rec_assign;
  tree rtl_ignore;

  rtl_ignore = thread_ignore_def ();
  rec_expr = build2(op, integer_type_node, rtl_ignore, integer_one_node);
  rec_inc = 0;
  rec_expr = force_gimple_operand(rec_expr, &rec_inc, true, NULL_TREE);
  rec_assign = gimple_build_assign(rtl_ignore, rec_expr);
  gimple_seq_add_seq(seq, rec_inc);
  gimple_seq_add_stmt(seq, rec_assign);
}

static void
build_stack_assign (gimple_seq *seq)
{
  /* Build the following gimple sequence:
     ShadowStack[-1] = __builtin_return_address(0); */

  tree pc_addr;
  tree op_size;
  double_int op_size_cst;
  unsigned long long size_val;
  tree op_expr;
  tree stack_op;
  tree assign;
  tree rtl_retaddr;

  rtl_retaddr = implicit_built_in_decls[BUILT_IN_RETURN_ADDRESS];
  pc_addr = build_call_expr(rtl_retaddr, 1, integer_zero_node);

  op_size = TYPE_SIZE(ptr_type_node);
  op_size_cst = tree_to_double_int(op_size);
  size_val = op_size_cst.low / __CHAR_BIT__;
  op_size = build_int_cst_wide(sizetype, -size_val, -1);
  op_expr = build2(POINTER_PLUS_EXPR, ptr_type_node,
                        shadow_stack_def (), op_size);

  stack_op = build1(INDIRECT_REF, ptr_type_node, op_expr);
  assign = build2(MODIFY_EXPR, ptr_type_node, stack_op, pc_addr);
  force_gimple_operand(assign, seq, true, NULL_TREE);
}

static void
instr_func (gimple_seq *pre, gimple_seq *post)
{
  /* In this case we need no instrumentation for the function */
  if (func_calls == 0 && func_mops == 0)
    return;

  build_stack_assign(pre);
  build_stack_op(pre, false);
  build_stack_op(post, true);

  if (func_ignore == tsan_ignore_rec && func_calls != 0) {
    build_rec_ignore_op(pre, PLUS_EXPR);
    build_rec_ignore_op(post, MINUS_EXPR);
  }
}

static void
instr_mop (tree expr, location_t loc, int is_store, int is_sblock, gimple_seq *gseq)
{
  /* Builds the following gimple sequence:
     tsan_rtl_mop(&expr, (is_sblock | (is_store << 1) | ((sizeof(expr)-1) << 2) */

  tree addr_expr;
  tree expr_type;
  tree expr_size;
  double_int size;
  unsigned flags;
  tree flags_expr;
  tree call_expr;

  (void)loc;

  gcc_assert(gseq != 0 && *gseq == 0);
  gcc_assert(is_gimple_addressable(expr));

  addr_expr = build_addr(expr, current_function_decl);
  expr_type = TREE_TYPE(expr);
  /* TODO(dvyukov): try to remove that WTF, and see if compiler crashes w/o that */
  while (TREE_CODE(expr_type) == ARRAY_TYPE)
    expr_type = TREE_TYPE(expr_type);
  expr_size = TYPE_SIZE(expr_type);
  /*!!! use:
  //#define TREE_INT_CST_LOW(NODE) (TREE_INT_CST (NODE).low)
  //#define TREE_INT_CST_HIGH(NODE) (TREE_INT_CST (NODE).high) */
  size = tree_to_double_int(expr_size);
  gcc_assert(size.high == 0 && size.low != 0);
  size.low = (size.low / __CHAR_BIT__);
  if (size.low > MAX_MOP_BYTES)
    size.low = MAX_MOP_BYTES;
  size.low = size.low - 1;
  flags = ((!!is_sblock << 0) + (!!is_store << 1) + (size.low << 2));
  flags_expr = build_int_cst(unsigned_type_node, flags);

  call_expr = build_call_expr(rtl_mop_def (), 2, addr_expr, flags_expr);
  force_gimple_operand(call_expr, gseq, true, 0);
}


static void
instr_vptr_store (tree expr, tree rhs, location_t loc, int is_sblock, gimple_seq *gseq)
{
  /* Builds the following gimple sequence:
     int is_store = (expr != rhs);
     tsan_rtl_mop(&expr, (is_sblock | (is_store << 1) | ((sizeof(expr)-1) << 2) */

  tree expr_ptr;
  tree addr_expr;
  tree expr_type;
  tree expr_size;
  double_int size;
  unsigned flags;
  tree flags_expr;
  gimple_seq flags_seq;
  gimple collect;
  tree is_store_expr;

  (void)loc;

  expr_ptr = build_addr(expr, current_function_decl);
  addr_expr = force_gimple_operand(expr_ptr, gseq, true, NULL_TREE);

  expr_type = TREE_TYPE(expr);
  while (TREE_CODE(expr_type) == ARRAY_TYPE)
    expr_type = TREE_TYPE(expr_type);
  expr_size = TYPE_SIZE(expr_type);
  size = tree_to_double_int(expr_size);
  gcc_assert(size.high == 0 && size.low != 0);
  if (size.low > 128)
    size.low = 128;
  size.low = (size.low / 8) - 1;
  flags = ((!!is_sblock << 0) + (size.low << 2));
  flags_expr = build_int_cst(unsigned_type_node, flags);

  is_store_expr = build2(NE_EXPR, integer_type_node,
                              build_c_cast(0, size_type_node, expr),
                              build_c_cast(0, size_type_node, rhs));
  is_store_expr = build2(LSHIFT_EXPR, integer_type_node,
                              is_store_expr, integer_one_node);
  flags_expr = build2(BIT_IOR_EXPR, integer_type_node,
                              is_store_expr, flags_expr);

  flags_seq = 0;
  flags_expr = force_gimple_operand(flags_expr, &flags_seq, true, NULL_TREE);
  gimple_seq_add_seq(gseq, flags_seq);

  collect = gimple_build_call(
      rtl_mop_def (), 2, addr_expr, flags_expr);

  gimple_seq_add_stmt(gseq, collect);
}

static char const *
decl_name (tree decl)
{
  tree id;
  char const *name;

  if (decl != 0 && DECL_P(decl))
    {
      id = DECL_NAME(decl);
      if (id != NULL)
        {
          name = IDENTIFIER_POINTER(id);
          if (name != NULL)
            return name;
        }
    }
  return "<unknown>";
}

static void
set_location (gimple_seq seq, location_t loc)
{
  gimple_seq_node n;
  for (n = gimple_seq_first(seq); n != NULL; n = n->next)
    gimple_set_location(n->stmt, loc);
}

static tree
is_dtor_vptr_store (gimple stmt, tree expr, int is_store)
{
  if (is_store == 1
      && TREE_CODE(expr) == COMPONENT_REF
      && gimple_assign_single_p(stmt)
      && strcmp(decl_name(cfun->decl), "__base_dtor ") == 0) {
    tree comp = expr->exp.operands[0];
    while (TREE_CODE(comp) == COMPONENT_REF)
      comp = comp->exp.operands[0];
    if (TREE_CODE(comp) == INDIRECT_REF || TREE_CODE(comp) == MEM_REF) {
      comp = comp->exp.operands[0];
      if (TREE_CODE(comp) == SSA_NAME)
        comp = SSA_NAME_VAR(comp);
      if (strcmp(decl_name(comp), "this") == 0) {
        tree field = expr->exp.operands[1];
        if (TREE_CODE(field) == FIELD_DECL
            && strncmp(decl_name(field), "_vptr.", sizeof("_vptr.") - 1) == 0) {
          return gimple_assign_rhs1(stmt);
        }
      }
    }
  }
  return 0;
}

static int
is_vtbl_read (tree expr, int is_store)
{
  /* We may not instrument reads from vtbl, because the data is constant.
     vtbl read is of the form:
       gimple_assign <component_ref, D.2133, x->_vptr.X, NULL>
       gimple_assign <indirect_ref, D.2134, *D.2133, NULL>
     or:
       gimple_assign <component_ref, D.2133, x->_vptr.X, NULL>
       gimple_assign <pointer_plus_expr, D.2135, D.2133, 8>
       gimple_assign <indirect_ref, D.2136, *D.2135, NULL> */

  if (is_store == 0
      && TREE_CODE(expr) == INDIRECT_REF) {
    tree ref_target = expr->exp.operands[0];
    if (TREE_CODE(ref_target) == SSA_NAME) {
      gimple ref_stmt = ref_target->ssa_name.def_stmt;
      if (gimple_code(ref_stmt) == GIMPLE_ASSIGN) {
        if (gimple_expr_code(ref_stmt) == POINTER_PLUS_EXPR) {
          tree tmp = ref_stmt->gsmem.op[1];
          if (TREE_CODE(tmp) == SSA_NAME
              && gimple_code(tmp->ssa_name.def_stmt) == GIMPLE_ASSIGN) {
            ref_stmt = tmp->ssa_name.def_stmt;
          }
        }
        if (gimple_expr_code(ref_stmt) == COMPONENT_REF
            && gimple_assign_single_p(ref_stmt)) {
          tree comp_expr = ref_stmt->gsmem.op[1];
          tree field_expr = comp_expr->exp.operands[1];
          if (TREE_CODE(field_expr) == FIELD_DECL
              && strncmp(decl_name(field_expr), "_vptr.", sizeof("_vptr.") - 1) == 0) {
            return 1;
          }
        }
      }
    }
  }

  return 0;
}

static int
is_load_of_const (tree expr, int is_store)
{
  if (is_store == 0)
    {
      if (TREE_CODE(expr) == COMPONENT_REF)
        expr = expr->exp.operands[1];
      if (TREE_CODE(expr) == VAR_DECL
          || TREE_CODE(expr) == PARM_DECL
          || TREE_CODE(expr) == FIELD_DECL)
        {
          if (TREE_READONLY(expr))
            return 1;
        }
    }
  return 0;
}

static int
is_fake_mop (const_tree expr)
{
  /* various constant literals */
  if (TREE_CODE_CLASS(TREE_CODE(expr)) == tcc_constant)
    return 1;

  /* compiler-emitted artificial variables */
  if (TREE_CODE_CLASS(TREE_CODE(expr)) == tcc_declaration
      && DECL_ARTIFICIAL(expr))
    return 1;

  /* store to function result */
  if (TREE_CODE(expr) == RESULT_DECL)
    return 1;

  return 0;
}

static void
instrument_mop(gimple stmt, gimple_stmt_iterator *gsi, location_t loc,
               tree expr, int is_store, VEC(mop_desc_t, heap) **mop_list)
{
  tree expr_ssa;
  char const* reason;
  enum tree_code tcode;
  unsigned fld_off;
  unsigned fld_size;
  tree dtor_vptr_expr;
  struct mop_desc_t mop;

  (void)loc;

  /* map SSA name to real name */
  expr_ssa = 0;
  if (TREE_CODE(expr) == SSA_NAME) {
    expr_ssa = expr;
    expr = SSA_NAME_VAR(expr);
  }

  if (is_fake_mop(expr))
    return;

  reason = 0;
  tcode = TREE_CODE(expr);

  /* Below are things we do NOT want to instrument. */
  if (func_ignore & (tsan_ignore_mop | tsan_ignore_rec)) {
    reason = "ignore file";
  } else if (tcode == VAR_DECL
      && TREE_ADDRESSABLE(expr) == 0
      && TREE_STATIC(expr) == 0) {
    /* the var does not live in memory -> no possibility of races */
    reason = "non-addressable";
  /* } else if (TREE_CODE(TREE_TYPE(expr)) == RECORD_TYPE) {
      // why don't I instrument records?.. perhaps it crashes compilation,
      // and should be handled more carefully
    reason = "record type"; */
  } else if (tcode == CONSTRUCTOR) {
    /* as of now crashes compilation
       TODO(dvyukov): handle it correctly */
    reason = "constructor expression";
  } else if (tcode == PARM_DECL) {
    /* TODO(dvyukov): need to instrument it */
    reason = "function parameter";
  } else if (is_load_of_const(expr, is_store)) {
    reason = "load of a const variable/parameter/field";
  } else if (is_vtbl_read(expr, is_store)) {
    reason = "vtbl read";
  } else if (tcode == COMPONENT_REF) {
    tree field = expr->exp.operands[1];
    if (TREE_CODE(field) == FIELD_DECL) {
      fld_off = field->field_decl.bit_offset->int_cst.int_cst.low;
      fld_size = field->decl_common.size->int_cst.int_cst.low;
      if (((fld_off % __CHAR_BIT__) != 0)
          || ((fld_size % __CHAR_BIT__) != 0)){
        /* as of now it crashes compilation
           TODO(dvyukov): handle bit-fields -> as if touching the whole field */
        reason = "weird bit field";
      }
    }
  }

  if (tcode != ARRAY_REF /*?*/
      && tcode != VAR_DECL
      && tcode != COMPONENT_REF
      && tcode != INDIRECT_REF /*?*/
      && tcode != MEM_REF
      /* TODO(dvyukov): handle those cases
      //&& tcode != FIELD_DECL
      //&& tcode != MEM_REF
      //&& tcode != ARRAY_RANGE_REF
      //&& tcode != TARGET_MEM_REF
      //&& tcode != ADDR_EXPR */
      ) {
    reason = "unknown type";
  }

  dtor_vptr_expr = is_dtor_vptr_store(stmt, expr, is_store);

  if (reason != 0)
    return;

  mop.is_call = 0;
  mop.gsi = *gsi;
  mop.expr = expr;
  mop.dtor_vptr_expr = dtor_vptr_expr;
  mop.is_store = is_store;
  VEC_safe_push(mop_desc_t, heap, *mop_list, &mop);
}

static void
handle_gimple (gimple_stmt_iterator *gsi, VEC(mop_desc_t, heap) **mop_list)
{
  unsigned i;
  struct mop_desc_t mop;
  gimple stmt;
  enum gimple_code gcode;
  location_t loc;
  tree rhs;
  tree lhs;
  tree fndecl;

  stmt = gsi_stmt(*gsi);
  gcode = gimple_code(stmt);
  if (gcode >= LAST_AND_UNUSED_GIMPLE_CODE)
    return;

  loc = gimple_location(stmt);

  switch (gcode) {
    /* TODO(dvyukov): handle GIMPLE_COND */
    case GIMPLE_CALL: {
      func_calls += 1;
      /* Handle call arguments as loads */
      for (i = 0; i != gimple_call_num_args(stmt); i += 1) {
        rhs = gimple_call_arg(stmt, i);
        instrument_mop(stmt, gsi, loc, rhs, 0, mop_list);
      }

      memset(&mop, 0, sizeof(mop));
      mop.is_call = 1;
      VEC_safe_push(mop_desc_t, heap, *mop_list, &mop);

      fndecl = gimple_call_fndecl(stmt);
      gcc_assert(strcmp(decl_name(fndecl), "__tsan_mop") != 0);

      /* Handle assignment lhs as store */
      lhs = gimple_call_lhs(stmt);
      if (lhs != 0)
        instrument_mop(stmt, gsi, loc, lhs, 1, mop_list);

      break;
    }

    case GIMPLE_ASSIGN: {
      /* Handle assignment lhs as store */
      lhs = gimple_assign_lhs(stmt);
      instrument_mop(stmt, gsi, loc, lhs, 1, mop_list);

      /* Handle operands as loads */
      for (i = 1; i != gimple_num_ops(stmt); i += 1) {
        rhs = gimple_op(stmt, i);
        instrument_mop(stmt, gsi, loc, rhs, 0, mop_list);
      }
      break;
    }

    case GIMPLE_BIND: {
      gcc_assert(!"there should be no GIMPLE_BIND on this level");
      break;
    }

    default:
      break;
  }
}

static void
instrument_bblock (struct bb_data_t *bbd, basic_block bb)
{
  int ix;
  int is_sblock;
  gimple_stmt_iterator gsi;
  gimple_stmt_iterator gsinext;
  struct mop_desc_t *mop;
  gimple stmt;
  location_t loc;
  expanded_location eloc;
  gimple_seq instr_seq;

  VEC_free(mop_desc_t, heap, mop_list);

  gsi = gsi_start_bb(bb);
  for (;;) {
    if (gsi_end_p(gsi))
      break;
    gsinext = gsi;
    gsi_next(&gsinext);
    handle_gimple(&gsi, &mop_list);
    gsi = gsinext;
  }

  mop = 0;
  for (ix = 0; VEC_iterate(mop_desc_t, mop_list, ix, mop); ix += 1) {
    if (mop->is_call != 0) {
      /* After a function call we must start a brand new sblock,
         because the call can contain synchronization or whatever. */
      bbd->has_sb = 0;
      continue;
    }

    func_mops += 1;
    stmt = gsi_stmt(mop->gsi);
    loc = gimple_location(stmt);
    eloc = expand_location(loc);

    is_sblock = (bbd->has_sb == 0
        || !(eloc.file != 0
            && bbd->sb_file != 0
            && strcmp(eloc.file, bbd->sb_file) == 0
            && eloc.line >= bbd->sb_line_min
            && eloc.line <= bbd->sb_line_max));

    if (is_sblock == 1 && func_ignore == tsan_ignore_hist)
      is_sblock = 0;

    if (is_sblock) {
      bbd->has_sb = 1;
      bbd->sb_file = eloc.file;
      bbd->sb_line_min = eloc.line;
      bbd->sb_line_max = eloc.line + SBLOCK_SIZE;
    }

    instr_seq = 0;
    if (mop->dtor_vptr_expr == 0)
      instr_mop(mop->expr, loc, mop->is_store, is_sblock, &instr_seq);
    else
      instr_vptr_store(mop->expr, mop->dtor_vptr_expr, loc,
                            is_sblock, &instr_seq);
    gcc_assert(instr_seq != 0);
    set_location(instr_seq, loc);
    if (is_gimple_call(stmt) && mop->is_store == 1)
      gsi_insert_seq_after(&mop->gsi, instr_seq, GSI_NEW_STMT);
    else /* dtor_vptr_expr != 0 */
      gsi_insert_seq_before(&mop->gsi, instr_seq, GSI_SAME_STMT);
  }
}

static void
instrument_function (void)
{
  int sb_line_min;
  int sb_line_max;
  int bb_cnt;
  int eidx;
  basic_block bb;
  basic_block entry_bb;
  basic_block cur_bb;
  basic_block any_bb;
  struct bb_data_t *pred;
  struct bb_data_t *bb_data;
  struct bb_data_t *bbd;
  edge entry_edge;
  edge e;

  bb_cnt = cfun->cfg->x_n_basic_blocks;
  bb_data = (struct bb_data_t*)xcalloc(bb_cnt, sizeof(struct bb_data_t));
  entry_bb = ENTRY_BLOCK_PTR;
  entry_edge = single_succ_edge(entry_bb);
  entry_bb = entry_edge->dest;
  bb = 0;
  FOR_EACH_BB(bb) {
    bb_data[bb->index].state = (bb == entry_bb) ? bb_candidate : bb_not_visited;
  }

  for (;;) {
    cur_bb = 0;
    any_bb = 0;
    FOR_EACH_BB(bb) {
      bbd = &bb_data[bb->index];
      if (bbd->state == bb_candidate) {
        cur_bb = bb;
        any_bb = bb;
        e = 0;
        for (eidx = 0; VEC_iterate(edge, bb->preds, eidx, e); eidx++) {
          pred = &bb_data[e->src->index];
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
    cur_bb = cur_bb ? cur_bb : any_bb;
    bbd = &bb_data[cur_bb->index];
    gcc_assert(bbd->state == bb_candidate);
    bbd->state = bb_visited;

    e = 0;
    for (eidx = 0; VEC_iterate(edge, cur_bb->preds, eidx, e); eidx++) {
      pred = &bb_data[e->src->index];
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
        if (bbd->sb_file != 0
            && pred->sb_file != 0
            && strcmp(bbd->sb_file, pred->sb_file) == 0) {
          sb_line_min = MAX(bbd->sb_line_min, pred->sb_line_min);
          sb_line_max = MIN(bbd->sb_line_max, pred->sb_line_max);
          if (sb_line_min <= sb_line_max) {
            bbd->has_sb = 1;
            bbd->sb_line_min = sb_line_min;
            bbd->sb_line_max = sb_line_max;
          }
        }
      }
    }

    instrument_bblock(bbd, cur_bb);

    for (eidx = 0; VEC_iterate(edge, cur_bb->succs, eidx, e); eidx++) {
      pred = &bb_data[e->dest->index];
      if (pred->state == bb_not_visited)
        pred->state = bb_candidate;
    }
  }
}

static unsigned
tsan_pass_func (void)
{
  char const *asm_name;
  location_t loc;
  gimple_seq pre_func_seq;
  gimple_seq post_func_seq;
  basic_block entry_bb;
  basic_block first_bb;
  basic_block bb;
  edge entry_edge;
  gimple_stmt_iterator first_gsi;
  gimple_stmt_iterator gsi;
  gimple_stmt_iterator gsi2;
  gimple first_stmt;
  gimple stmt;

  /* Check as to whether we need to completely ignore the file or not */
  if (ignore_file == -1)
    ignore_file = tsan_ignore_file (main_input_filename);
  if (ignore_file)
    return 0;

  asm_name = IDENTIFIER_POINTER(DECL_ASSEMBLER_NAME(cfun->decl));
  func_ignore = tsan_ignore_func(asm_name);
  func_calls = 0;
  func_mops = 0;

  instrument_function();

  pre_func_seq = 0;
  post_func_seq = 0;
  instr_func(&pre_func_seq, &post_func_seq);
  if (pre_func_seq != 0 || post_func_seq != 0) {
    if (pre_func_seq != 0) {
      entry_bb = ENTRY_BLOCK_PTR;
      entry_edge = single_succ_edge(entry_bb);
      first_bb = entry_edge->dest;
      first_gsi = gsi_start_bb(first_bb);
      if (!gsi_end_p(first_gsi)) {
        first_stmt = gsi_stmt(first_gsi);
        loc = gimple_location(first_stmt);
        set_location(pre_func_seq, loc);
      }
      entry_bb = split_edge(entry_edge);
      gsi = gsi_start_bb(entry_bb);
      gsi_insert_seq_after(&gsi, pre_func_seq, GSI_NEW_STMT);
    }

    FOR_EACH_BB(bb) {
      gsi2 = gsi_start_bb (bb);
      for (;;) {
        gsi = gsi2;
        if (gsi_end_p(gsi))
          break;
        gsi_next(&gsi2);

        stmt = gsi_stmt(gsi);
        loc = gimple_location(stmt);

        if (gimple_code(stmt) == GIMPLE_RETURN) {
          if (post_func_seq != 0) {
            set_location(post_func_seq, loc);
            gsi_insert_seq_before(&gsi, post_func_seq, GSI_SAME_STMT);
          }
        }
      }
    }
  }

  return 0;
}

struct gimple_opt_pass tsan_pass = {{
  GIMPLE_PASS,
  "tsan",                               /* name */
  tsan_gate,                            /* gate */
  tsan_pass_func,                       /* execute */
  NULL,                                 /* sub */
  NULL,                                 /* next */
  0,                                    /* static_pass_number */
  TV_NONE,                              /* tv_id */
  PROP_trees | PROP_cfg,                /* properties_required */
  0,                                    /* properties_provided */
  0,                                    /* properties_destroyed */
  0,                                    /* todo_flags_start */
  TODO_dump_cgraph | TODO_dump_func | TODO_verify_all
    | TODO_update_ssa | TODO_update_address_taken /* todo_flags_finish */
}};

