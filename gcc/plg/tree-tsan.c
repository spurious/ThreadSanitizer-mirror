/* ThreadSanitizer instrumentation pass.
   http://code.google.com/p/data-race-test
   Copyright (C) 2011
   Free Software Foundation, Inc.
   Contributed by Dmitry Vyukov <dvyukov@google.com>

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING3.  If not see
<http://www.gnu.org/licenses/>.  */

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
#include "langhooks.h"
#include "output.h"
#include "options.h"
#include "target.h"

/* The file can be compiled either as compiler pass or plugin.  */
#ifdef GCC_PLG
# include "c-common.h"
#else
# include "c-family/c-common.h"
#endif

#include "diagnostic.h"

#include <stdlib.h>
#include <stdio.h>

/* ThreadSanitizer is a data race detector for C/C++ programs.
   http://code.google.com/p/data-race-test/wiki/ThreadSanitizer

   The tool consists of two parts:
   instrumentation module (this file) and a run-time library.
   The instrumentation module maintains shadow call stacks
   and intercepts interesting memory accesses.
   The instrumentation is enabled with -ftsan flag.

   Instrumentation for shadow stack maintenance is as follows:
   void somefunc ()
   {
     __tsan_shadow_stack [-1] = __builtin_return_address (0);
     __tsan_shadow_stack++;
     // function body
     __tsan_shadow_stack--;
   }

   Interception for memory access interception is as follows:
   *addr = 1;
   __tsan_handle_mop (addr, flags);
   where flags are (is_sblock | (is_store << 1) | ((sizeof (*addr) - 1) << 2).
   is_sblock is used merely for optimization purposes and can always
   be set to 1, see comments in instrument_mops function.

   Ignore files can be used to selectively non instrument some functions.
   Ignore file is specified with -ftsan-ignore=filename flag.
   There are 3 types of ignores: (1) do not instrument memory accesses
   in the function, (2) do not create sblocks in the function
   and (3) recursively ignore memory accesses in the function.
   That last ignore type requires additional instrumentation of the form:
   void somefunc ()
   {
     __tsan_thread_ignore++;
     // function body
     __tsan_thread_ignore--;
   }

   The run-time library provides __tsan_handle_mop function,
   definitions of __tsan_shadow_stack and __tsan_thread_ignore variables,
   and intercepts synchronization related functions.  */

#define TSAN_IGNORE "__tsan_thread_ignore"
#define TSAN_STACK "__tsan_shadow_stack"
#define TSAN_MOP "__tsan_handle_mop"
#define TSAN_PERFIX "__tsan_"
#define MAX_MOP_BYTES 16
#define SBLOCK_SIZE 5

enum tsan_ignore_type
{
  tsan_ignore_none  = 1 << 0, /* Do not ignore.  */
  tsan_ignore_func  = 1 << 1, /* Completely ignore the whole func.  */
  tsan_ignore_mop   = 1 << 2, /* Do not instrument accesses.  */
  tsan_ignore_rec   = 1 << 3, /* Do not instrument accesses recursively.  */
  tsan_ignore_hist  = 1 << 4  /* Do not create superblocks.  */
};

/* Info associated with each basic block.
   Used to determine super-blocks (see instrument_mops ()).  */

struct bb_data
{
  int         is_visited;
  int         has_sb;
  const char *sb_file;
  int         sb_line_min;
  int         sb_line_max;
};

/* Memory access descriptor.  */

struct mop_desc
{
  int                  is_call;
  gimple_stmt_iterator gsi;
  tree                 expr;
  tree                 dtor_vptr_expr;
  int                  is_store;
};

struct tsan_ignore_desc
{
  struct tsan_ignore_desc *next;
  enum tsan_ignore_type    type;
  char                    *name;
};

/* Number of instrumented memory accesses in the current function.  */

static int func_mops;

/* Number of function calls in the current function.  */

static int func_calls;

/* Ignore status for the current function (see tsan_ignore_type).  */

static enum tsan_ignore_type func_ignore;

static int ignore_init = 0;
static struct tsan_ignore_desc *ignore_head;

typedef struct mop_desc mop_desc;
DEF_VEC_O (mop_desc);
DEF_VEC_ALLOC_O (mop_desc, heap);
static VEC (mop_desc, heap) *mop_list;

/* The pass references lookup_name ()
   that is not available in some modules (e.g. lto1).
   lookup_name () is used only for testing purposes - unit tests may declare
   runtime variables and functions themselves, if so we want to use those
   declarations.  So we just define the weak stub that is overridden
   in modules that provide the function (e.g. cc1).  */

tree __attribute__ ((weak))
lookup_name (tree t)
{
  (void)t;
  return NULL_TREE;
}

/* Builds a definition of a runtime variable with type TYP and name NAME.  */

static tree
build_var_decl (tree typ, const char *name)
{
  tree def;
  tree id;

  /* Check if a user has defined it for testing.  */
  id = get_identifier (name);
  def = lookup_name (id);
  if (def != NULL)
    return def;

  def = build_decl (UNKNOWN_LOCATION, VAR_DECL, id, typ);
  TREE_STATIC (def) = 1;
  TREE_PUBLIC (def) = 1;
  DECL_EXTERNAL (def) = 1;
  if (targetm.have_tls)
    DECL_TLS_MODEL (def) = decl_default_tls_model (def);
  TREE_USED (def) = 1;
  TREE_THIS_VOLATILE (def) = 1;
  SET_DECL_ASSEMBLER_NAME (def, id);
  return def;
}

/* Builds the following decl
   extern __thread void **__tsan_shadow_stack;  */

static tree
get_shadow_stack_decl (void)
{
  static tree def;

  if (def == NULL)
    def = build_var_decl (build_pointer_type (ptr_type_node), TSAN_STACK);
  return def;
}

/* Builds the following decl
   extern __thread int __tsan_thread_ignore;  */

static tree
get_thread_ignore_decl (void)
{
  static tree def;

  if (def == NULL)
    def = build_var_decl (integer_type_node, TSAN_IGNORE);
  return def;
}

/* Builds the following decl
   void __tsan_handle_mop (void *addr, unsigned flags);  */

static tree
get_handle_mop_decl (void)
{
  tree fn_type;

  static tree def;

  if (def != NULL)
    return def;

  /* Check if a user has defined it for testing.  */

  def = lookup_name (get_identifier (TSAN_MOP));
  if (def != NULL)
    return def;

  fn_type = build_function_type_list (void_type_node, ptr_type_node,
                                      integer_type_node , NULL_TREE);
  def = build_fn_decl (TSAN_MOP, fn_type);
  TREE_NOTHROW (def) = 1;
  DECL_ATTRIBUTES (def) = tree_cons (get_identifier ("leaf"),
                                     NULL, DECL_ATTRIBUTES (def));
  DECL_ASSEMBLER_NAME (def);
  return def;
}

/* Adds new ignore definition to the global list.
   TYPE is the ignore type (see tsan_ignore_type).
   NAME is the ignore pattern (e.g. "std*string*insert").  */

static void
ignore_append (enum tsan_ignore_type type, char *name)
{
  struct tsan_ignore_desc *desc;

  desc = XCNEW (struct tsan_ignore_desc);
  desc->type = type;
  desc->name = xstrdup (name);
  desc->next = ignore_head;
  ignore_head = desc;
}

/* Checks as to whether identifier STR matches template TEMPL.
   Templates can only contain '*', e.g. 'std*string*insert'.
   Templates implicitly start and end with '*'
   since they are matched against mangled names.
   Returns non-zero if STR is matched against TEMPL.  */

static int
ignore_match (char *templ, const char *str)
{
  char *tpos;
  const char *spos;

  while (templ && templ [0])
    {
      if (templ [0] == '*')
        {
          templ++;
          continue;
        }
      if (str [0] == 0)
        return 0;
      tpos = strchr (templ, '*');
      if (tpos != NULL)
        tpos [0] = 0;
      spos = strstr (str, templ);
      str = spos + strlen (templ);
      templ = tpos;
      if (tpos != NULL)
        tpos [0] = '*';
      if (spos == NULL)
        return 0;
    }
  return 1;
}

/* Loads ignore definitions from the file specified by -ftsan-ignore=filename.
   Ignore files have the following format:

# This is a comment - ignored

# The below line says to not instrument memory accesses
# in all functions that match 'std*string*insert'
fun:std*string*insert

# The below line says to not instrument memory accesses
# in the function called 'foobar' *and* in all functions
# that it calls recursively
fun_r:foobar

# The below line says to not create superblocks
# in the function called 'barbaz'
fun_hist:barbaz

# Ignore all functions in the source file
src:atomic.c

# Everything else is uninteresting for us (e.g. obj:)
*/

static void
ignore_load (void)
{
  FILE *f;
  char *line;
  size_t linesz;
  ssize_t sz;
  char buf [PATH_MAX];

  if (flag_tsan_ignore == NULL || flag_tsan_ignore [0] == 0)
    return;

  f = fopen (flag_tsan_ignore, "r");
  if (f == NULL)
    {
      /* Try to open it relative to main_input_filename.  */
      strncpy (buf, main_input_filename, sizeof (buf));
      buf [sizeof (buf) - 1] = 0;
      line = strrchr (buf, '/');
      if (line != NULL)
        {
          line++;
          strncpy (line, flag_tsan_ignore, sizeof (buf) - (line - buf));
          buf [sizeof (buf) - 1] = 0;
          f = fopen (buf, "r");
        }
    }
  if (f == NULL)
    {
      error ("failed to open ignore file '%s'\n", flag_tsan_ignore);
      return;
    }

  line = 0;
  linesz = 0;
  while ((sz = getline (&line, &linesz, f)) != -1)
    {
      if (sz == 0)
        continue;
      /* Strip line terminator.  */
      if (line [sz - 1] == '\r' || line [sz - 1] == '\n')
        line [sz - 1] = 0;
      if (strncmp (line, "src:", sizeof ("src:") - 1) == 0)
        ignore_append (tsan_ignore_func, line + sizeof ("src:") - 1);
      else if (strncmp (line, "fun:", sizeof ("fun:") - 1) == 0)
        ignore_append (tsan_ignore_mop, line + sizeof ("fun:") - 1);
      else if (strncmp (line, "fun_r:", sizeof ("fun_r:") - 1) == 0)
        ignore_append (tsan_ignore_rec, line + sizeof ("fun_r:") - 1);
      else if (strncmp (line, "fun_hist:", sizeof ("fun_hist:") - 1) == 0)
        ignore_append (tsan_ignore_hist, line + sizeof ("fun_hist:") - 1);
      /* Other lines are not interesting.  */
    }

  free (line);
  fclose (f);
}

/* Returns ignore status for the current function.  */

static enum tsan_ignore_type
tsan_ignore (void)
{
  const char *func_name;
  const char *src_name;
  struct tsan_ignore_desc *desc;

  if (ignore_init == 0)
    {
      ignore_load ();
      ignore_init = 1;
    }

  src_name = expand_location (cfun->function_start_locus).file;
  if (src_name == NULL)
    src_name = "";

  func_name = IDENTIFIER_POINTER (DECL_ASSEMBLER_NAME (cfun->decl));
  /* Ignore all functions starting with __tsan_ - intended for testing.  */
  if (strncmp (func_name, TSAN_PERFIX, sizeof (TSAN_PERFIX) - 1) == 0)
    return tsan_ignore_func;

  for (desc = ignore_head; desc; desc = desc->next)
    {
      if (desc->type == tsan_ignore_func)
        {
          if (ignore_match (desc->name, src_name))
           return desc->type;
        }
      else if (ignore_match (desc->name, func_name))
       return desc->type;
    }
  return tsan_ignore_none;
}

/* Forgiving variant of IDENTIFIER_POINTER (DECL_NAME (DECL)).
   In case of any errors (e.g. not a decl or NULL) returns "<unknown>".  */

static const char *
decl_name (tree decl)
{
  tree id;
  const char *name;

  if (decl != 0 && DECL_P (decl))
    {
      id = DECL_NAME (decl);
      if (id != NULL)
        {
          name = IDENTIFIER_POINTER (id);
          if (name != NULL)
            return name;
        }
    }
  return "<unknown>";
}

/* Builds either (__tsan_shadow_stack += 1) or (__tsan_shadow_stack -= 1)
   expression depending on DO_DEC parameter.  Appends the result to SEQ.  */

static void
build_stack_op (gimple_seq *seq, bool do_dec)
{
  tree op_size;
  double_int op_size_cst;
  unsigned long long size_val;
  unsigned long long size_valhi;
  tree op_expr;
  tree assign;
  tree sstack_decl;
  gimple_seq s;

  op_size = TYPE_SIZE (ptr_type_node);
  op_size_cst = tree_to_double_int (op_size);
  size_val = op_size_cst.low / BITS_PER_UNIT;
  size_valhi = 0;
  if (do_dec)
    {
      size_val = -size_val;
      size_valhi = -1;
    }
  op_size = build_int_cst_wide (sizetype, size_val, size_valhi);
  sstack_decl = get_shadow_stack_decl ();
  op_expr = build2 (POINTER_PLUS_EXPR, ptr_type_node, sstack_decl, op_size);
  assign = build2 (MODIFY_EXPR, ptr_type_node, sstack_decl, op_expr);
  s = NULL;
  force_gimple_operand (assign, &s, true, NULL_TREE);
  gimple_seq_add_seq (seq, s);
}

/* Builds either (__tsan_thread_ignore += 1) or (__tsan_thread_ignore -= 1)
   expression depending on OP parameter.  Stores the result in SEQ.  */

static void
build_rec_ignore_op (gimple_seq *seq, enum tree_code op)
{
  tree rec_expr;
  gimple_seq rec_inc;
  gimple rec_assign;
  tree ignore_decl;

  ignore_decl = get_thread_ignore_decl ();
  rec_expr = build2 (op, integer_type_node, ignore_decl, integer_one_node);
  rec_inc = NULL;
  rec_expr = force_gimple_operand (rec_expr, &rec_inc, true, NULL_TREE);
  rec_assign = gimple_build_assign (ignore_decl, rec_expr);
  gimple_seq_add_seq (seq, rec_inc);
  gimple_seq_add_stmt (seq, rec_assign);
}

/* Build the following gimple sequence:
   __tsan_shadow_stack [-1] = __builtin_return_address (0);
   Stores the result in SEQ.  */

static void
build_stack_assign (gimple_seq *seq)
{
  tree pc_addr;
  tree op_size;
  tree op_expr;
  tree stack_op;
  tree assign;
  tree retaddr_decl;

  retaddr_decl = implicit_built_in_decls [BUILT_IN_RETURN_ADDRESS];
  pc_addr = build_call_expr (retaddr_decl, 1, integer_zero_node);
  op_size = build_int_cst_wide (sizetype, -(POINTER_SIZE / BITS_PER_UNIT), -1);
  op_expr = build2 (POINTER_PLUS_EXPR, ptr_type_node,
                        get_shadow_stack_decl (), op_size);
  stack_op = build1 (INDIRECT_REF, ptr_type_node, op_expr);
  assign = build2 (MODIFY_EXPR, ptr_type_node, stack_op, pc_addr);
  force_gimple_operand (assign, seq, true, NULL_TREE);
}

/* Builds the following gimple sequence:
   __tsan_handle_mop (&EXPR,
                      (IS_SBLOCK | (IS_STORE << 1) | ((sizeof (EXPR) - 1) << 2);
   The result is stored in GSEQ.  */

static void
instr_mop (tree expr, int is_store, int is_sblock, gimple_seq *gseq)
{
  tree addr_expr;
  tree expr_type;
  unsigned size;
  unsigned flags;
  tree flags_expr;
  tree call_expr;

  gcc_assert (gseq != 0 && *gseq == 0);
  gcc_assert (is_gimple_addressable (expr));

  addr_expr = build_addr (expr, current_function_decl);
  expr_type = TREE_TYPE (expr);
  while (TREE_CODE (expr_type) == ARRAY_TYPE)
    expr_type = TREE_TYPE (expr_type);
  size = TREE_INT_CST_LOW (TYPE_SIZE (expr_type));
  size = size / BITS_PER_UNIT;
  if (size > MAX_MOP_BYTES)
    size = MAX_MOP_BYTES;
  size -= 1;
  flags = ((!!is_sblock << 0) + (!!is_store << 1) + (size << 2));
  flags_expr = build_int_cst (unsigned_type_node, flags);
  call_expr = build_call_expr (get_handle_mop_decl (),
                               2, addr_expr, flags_expr);
  force_gimple_operand (call_expr, gseq, true, 0);
}

/* Builds the following gimple sequence:
   int is_store = (EXPR != RHS); // The temp is not actually introduced.
   __tsan_handle_mop (&EXPR,
                      (IS_SBLOCK | (IS_STORE << 1) | ((sizeof (EXPR) - 1) << 2);
   The result is stored in GSEQ.  */

static void
instr_vptr_store (tree expr, tree rhs, int is_sblock, gimple_seq *gseq)
{
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

  expr_ptr = build_addr (expr, current_function_decl);
  addr_expr = force_gimple_operand (expr_ptr, gseq, true, NULL_TREE);
  expr_type = TREE_TYPE (expr);
  while (TREE_CODE (expr_type) == ARRAY_TYPE)
    expr_type = TREE_TYPE (expr_type);
  expr_size = TYPE_SIZE (expr_type);
  size = tree_to_double_int (expr_size);
  gcc_assert (size.high == 0 && size.low != 0);
  if (size.low > 128)
    size.low = 128;
  size.low = (size.low / 8) - 1;
  flags = ((!!is_sblock << 0) + (size.low << 2));
  flags_expr = build_int_cst (unsigned_type_node, flags);
  is_store_expr = build2 (NE_EXPR, unsigned_type_node,
                              build1 (VIEW_CONVERT_EXPR, size_type_node, expr),
                              build1 (VIEW_CONVERT_EXPR, size_type_node, rhs));
  is_store_expr = build2 (LSHIFT_EXPR, unsigned_type_node,
                              is_store_expr, integer_one_node);
  flags_expr = build2 (BIT_IOR_EXPR, unsigned_type_node,
                              is_store_expr, flags_expr);
  flags_seq = 0;
  flags_expr = force_gimple_operand (flags_expr, &flags_seq, true, NULL_TREE);
  gimple_seq_add_seq (gseq, flags_seq);
  collect = gimple_build_call (
      get_handle_mop_decl (), 2, addr_expr, flags_expr);
  gimple_seq_add_stmt (gseq, collect);
}

/* Builds gimple sequences that must be inserted at function entry (PRE)
   and before function exit (POST).  */

static void
instr_func (gimple_seq *pre, gimple_seq *post)
{
  /* In this case we need no instrumentation for the function.  */
  if (func_calls == 0 && func_mops == 0)
    return;

  if (func_ignore != tsan_ignore_rec)
    {
      build_stack_assign (pre);
      build_stack_op (pre, false);
      build_stack_op (post, true);
    }

  if (func_ignore == tsan_ignore_rec && func_calls != 0)
    {
      build_rec_ignore_op (pre, PLUS_EXPR);
      build_rec_ignore_op (post, MINUS_EXPR);
    }
}

/* Sets location LOC for all gimples in the SEQ.  */

static void
set_location (gimple_seq seq, location_t loc)
{
  gimple_seq_node n;

  for (n = gimple_seq_first (seq); n != NULL; n = n->next)
    gimple_set_location (n->stmt, loc);
}

/* Check as to whether EXPR refers to a store to vptr.  */

static tree
is_dtor_vptr_store (gimple stmt, tree expr, int is_store)
{
  if (is_store == 1
      && TREE_CODE (expr) == COMPONENT_REF
      && gimple_assign_single_p (stmt)
      && strcmp (decl_name (cfun->decl), "__base_dtor ") == 0)
    {
      tree comp = TREE_OPERAND (expr, 0);
      while (TREE_CODE (comp) == COMPONENT_REF)
        comp = TREE_OPERAND (comp, 0);
      if (TREE_CODE (comp) == INDIRECT_REF || TREE_CODE (comp) == MEM_REF)
        {
          comp = TREE_OPERAND (comp, 0);
          if (TREE_CODE (comp) == SSA_NAME)
            comp = SSA_NAME_VAR (comp);
          if (strcmp (decl_name (comp), "this") == 0)
            {
              tree field = TREE_OPERAND (expr, 1);
              if (TREE_CODE (field) == FIELD_DECL
                  && strncmp (decl_name (field),
                              "_vptr.", sizeof ("_vptr.") - 1) == 0)
                return gimple_assign_rhs1 (stmt);
            }
        }
    }
  return 0;
}

/* Checks as to whether EXPR refers to a read from vtlb.
   Vtlbs are immutable, so don't bother to instrument them.  */

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
       gimple_assign <indirect_ref, D.2136, *D.2135, NULL>  */

  if (is_store == 0
      && TREE_CODE (expr) == INDIRECT_REF)
    {
      tree ref_target = TREE_OPERAND (expr, 0);
      if (TREE_CODE (ref_target) == SSA_NAME)
        {
          gimple ref_stmt = ref_target->ssa_name.def_stmt;
          if (gimple_code (ref_stmt) == GIMPLE_ASSIGN)
            {
              if (gimple_expr_code (ref_stmt) == POINTER_PLUS_EXPR)
                {
                  tree tmp = ref_stmt->gsmem.op [1];
                  if (TREE_CODE (tmp) == SSA_NAME
                      && gimple_code (tmp->ssa_name.def_stmt) == GIMPLE_ASSIGN)
                    ref_stmt = tmp->ssa_name.def_stmt;
                }
              if (gimple_expr_code (ref_stmt) == COMPONENT_REF
                    && gimple_assign_single_p (ref_stmt))
                {
                  tree comp_expr = ref_stmt->gsmem.op [1];
                  tree field_expr = TREE_OPERAND (comp_expr, 1);
                  if (TREE_CODE (field_expr) == FIELD_DECL
                      && strncmp (decl_name (field_expr),
                                  "_vptr.", sizeof ("_vptr.") - 1) == 0)
                    return 1;
                }
            }
        }
    }

  return 0;
}

/* Checks as to whether EXPR refers to constant var/field/param.
   Don't bother to instrument them.  */

static int
is_load_of_const (tree expr, int is_store)
{
  if (is_store == 0)
    {
      if (TREE_CODE (expr) == COMPONENT_REF)
        expr = TREE_OPERAND (expr, 1);
      if (TREE_CODE (expr) == VAR_DECL
          || TREE_CODE (expr) == PARM_DECL
          || TREE_CODE (expr) == FIELD_DECL)
        {
          if (TREE_READONLY (expr))
            return 1;
        }
    }
  return 0;
}

/* Checks as to whether EXPR needs to be instrumented,
   if so puts it into the MOP_LIST.
   STMT is the gimple from which EXPR was extracted.
   GSI is the iterator from which STMT was extracted.
   IS_STORE says as to whether EXPR refers to a memory store
   or a memory load.  */

static void
handle_expr (gimple stmt, gimple_stmt_iterator gsi,
             tree expr, int is_store, VEC (mop_desc, heap) **mop_list)
{
  enum tree_code tcode;
  struct mop_desc mop;
  unsigned fld_off;
  unsigned fld_size;

  tcode = TREE_CODE (expr);

  /* Below are things we do not instrument
     (no possibility of races or not implemented yet).  */
  if ((func_ignore & (tsan_ignore_mop | tsan_ignore_rec))
      || tcode == SSA_NAME
      /* Store to function result.  */
      || tcode == RESULT_DECL
      /* Various constant literals.  */
      || TREE_CODE_CLASS (tcode) == tcc_constant
      /* Compiler-emitted artificial variables.  */
      || (TREE_CODE_CLASS (tcode) == tcc_declaration && DECL_ARTIFICIAL (expr))
      /* The var does not live in memory -> no possibility of races.  */
      || (tcode == VAR_DECL
          && TREE_ADDRESSABLE (expr) == 0
          && DECL_EXTERNAL (expr) == 0)
      /* TODO (dvyukov): not implemented.  */
      || TREE_CODE (TREE_TYPE (expr)) == RECORD_TYPE
      /* TODO (dvyukov): not implemented.  */
      || tcode == CONSTRUCTOR
      /* TODO (dvyukov): not implemented.  */
      || tcode == PARM_DECL
      /* Load of a const variable/parameter/field.  */
      || is_load_of_const (expr, is_store)
      /* Vtbl read.  */
      || is_vtbl_read (expr, is_store))
    return;

  if (tcode == COMPONENT_REF)
    {
      tree field = TREE_OPERAND (expr, 1);
      if (TREE_CODE (field) == FIELD_DECL)
        {
          fld_off = TREE_INT_CST_LOW (DECL_FIELD_BIT_OFFSET (field));
          fld_size = TREE_INT_CST_LOW (DECL_SIZE (field));
          if (((fld_off % BITS_PER_UNIT) != 0)
              || ((fld_size % BITS_PER_UNIT) != 0))
            {
              /* As of now it crashes compilation.
                 TODO (dvyukov): handle bit-fields as if touching
                 the whole field.  */
              return;
            }
        }
    }

  /* TODO (dvyukov): handle other cases
     (FIELD_DECL, MEM_REF, ARRAY_RANGE_REF, TARGET_MEM_REF, ADDR_EXPR).  */
  if (tcode != ARRAY_REF
      && tcode != VAR_DECL
      && tcode != COMPONENT_REF
      && tcode != INDIRECT_REF
      && tcode != MEM_REF)
    return;

  mop.is_call = 0;
  mop.gsi = gsi;
  mop.expr = expr;
  mop.dtor_vptr_expr = is_dtor_vptr_store (stmt, expr, is_store);
  mop.is_store = is_store;
  VEC_safe_push (mop_desc, heap, *mop_list, &mop);
}

/* Collects all interesting memory accesses from the gimple pointed to by GSI
   into MOP_LIST.  */

static void
handle_gimple (gimple_stmt_iterator gsi, VEC (mop_desc, heap) **mop_list)
{
  unsigned i;
  struct mop_desc mop;
  gimple stmt;
  enum gimple_code gcode;
  tree rhs;
  tree lhs;

  stmt = gsi_stmt (gsi);
  gcode = gimple_code (stmt);
  if (gcode >= LAST_AND_UNUSED_GIMPLE_CODE)
    return;

  switch (gcode)
    {
      /* TODO (dvyukov): handle GIMPLE_COND (can it access memmory?).  */
      case GIMPLE_CALL:
        {
          func_calls += 1;
          memset (&mop, 0, sizeof (mop));
          mop.is_call = 1;
          VEC_safe_push (mop_desc, heap, *mop_list, &mop);
          break;
        }

      case GIMPLE_ASSIGN:
        {
          /* Handle assignment lhs as store.  */
          lhs = gimple_assign_lhs (stmt);
          handle_expr (stmt, gsi, lhs, 1, mop_list);

          /* Handle operands as loads.  */
          for (i = 1; i < gimple_num_ops (stmt); i++)
            {
              rhs = gimple_op (stmt, i);
              handle_expr (stmt, gsi, rhs, 0, mop_list);
            }
          break;
        }

      default:
        break;
    }
}

/* Instruments single basic block BB.  */

static void
instrument_bblock (struct bb_data *bbd, basic_block bb)
{
  int ix;
  int is_sblock;
  gimple_stmt_iterator gsi;
  struct mop_desc *mop;
  gimple stmt;
  location_t loc;
  expanded_location eloc;
  gimple_seq instr_seq;

  /* Iterate over all gimples and collect interesting mops into mop_list.  */
  VEC_free (mop_desc, heap, mop_list);
  for (gsi = gsi_start_bb (bb); !gsi_end_p (gsi); gsi_next (&gsi))
    {
      handle_gimple (gsi, &mop_list);
    }

  mop = 0;
  for (ix = 0; VEC_iterate (mop_desc, mop_list, ix, mop); ix += 1)
    {
      if (mop->is_call != 0)
        {
          /* After a function call we must start a brand new sblock,
             because the function can contain synchronization.  */
          bbd->has_sb = 0;
          continue;
        }

      func_mops += 1;
      stmt = gsi_stmt (mop->gsi);
      loc = gimple_location (stmt);
      eloc = expand_location (loc);

      /* Check as to whether we may not set sblock flag for the access.  */
      is_sblock = (bbd->has_sb == 0
          || !(eloc.file != 0
              && bbd->sb_file != 0
              && strcmp (eloc.file, bbd->sb_file) == 0
              && eloc.line >= bbd->sb_line_min
              && eloc.line <= bbd->sb_line_max));

      if (func_ignore == tsan_ignore_hist)
        is_sblock = 0;

      if (is_sblock)
        {
          /* Start new sblock with new source info.  */
          bbd->has_sb = 1;
          bbd->sb_file = eloc.file;
          bbd->sb_line_min = eloc.line;
          bbd->sb_line_max = eloc.line + SBLOCK_SIZE;
        }

      instr_seq = 0;
      if (mop->dtor_vptr_expr == 0)
        instr_mop (mop->expr, mop->is_store, is_sblock, &instr_seq);
      else
        instr_vptr_store (mop->expr, mop->dtor_vptr_expr,
                          is_sblock, &instr_seq);
      gcc_assert (instr_seq != 0);
      set_location (instr_seq, loc);
      /* Instrumentation for assignment of a function result
         must be inserted after the call.  Instrumentation for
         reads of function arguments must be inserted before the call.
         That's because the call can contain synchronization.  */
      if (is_gimple_call (stmt) && mop->is_store == 1)
        gsi_insert_seq_after (&mop->gsi, instr_seq, GSI_NEW_STMT);
      else
        gsi_insert_seq_before (&mop->gsi, instr_seq, GSI_SAME_STMT);
    }
}

/* Instruments all interesting memory accesses in the function.  */

static void
instrument_mops (void)
{
  basic_block bb;
  int *blocks_inverted;
  struct bb_data *bb_data;
  struct bb_data *pred;
  struct bb_data *bbd;
  edge e;
  int sb_line_min, sb_line_max;
  int cnt, eidx, i;

  /* The function does basic block traversal in reverse top sort order
     of the inverted CFG.  Such order is required to properly mark super-blocks.
     The idea behind super-blocks is as follows.
     If several memory accesses happen within SBLOCK_SIZE source code lines
     from each other, then we only mark the first access as SBLOCK.
     This allows the runtime library to memorize a stack trace
     only for the first access and do not memorize for others.
     This significantly reduces memory consumption in exchange for slightly
     imprecise stack traces for previous accesses.  */

  blocks_inverted = XNEWVEC (int, last_basic_block + NUM_FIXED_BLOCKS);
  bb_data = XCNEWVEC (struct bb_data, last_basic_block + NUM_FIXED_BLOCKS);
  cnt = inverted_post_order_compute (blocks_inverted);
  for (i = 0; i < cnt; i++)
    {
      bb = BASIC_BLOCK (blocks_inverted [i]);
      bbd = &bb_data [bb->index];
      /* Iterate over all predecessors and merge their sblock info.  */
      e = 0;
      for (eidx = 0; VEC_iterate (edge, bb->preds, eidx, e); eidx++)
        {
          pred = &bb_data [e->src->index];
          if (!pred->is_visited || !pred->has_sb || pred == bbd)
            {
              /* If there is a not visited predecessor,
                 or a predecessor with no active sblock info,
                 or a self-loop, then we will have to start
                 a brand new sblock on next memory access.  */
              bbd->has_sb = 0;
              break;
            }
          else if (bbd->has_sb == 0)
            {
              /* If it's a first predecessor, just copy the info.  */
              bbd->has_sb = 1;
              bbd->sb_file = pred->sb_file;
              bbd->sb_line_min = pred->sb_line_min;
              bbd->sb_line_max = pred->sb_line_max;
            }
          else
            {
              /* Otherwise, find the interception
                 between two sblock descriptors.  */
              bbd->has_sb = 0;
              if (bbd->sb_file != 0 && pred->sb_file != 0
                  && strcmp (bbd->sb_file, pred->sb_file) == 0)
                {
                  sb_line_min = MAX (bbd->sb_line_min, pred->sb_line_min);
                  sb_line_max = MIN (bbd->sb_line_max, pred->sb_line_max);
                  if (sb_line_min <= sb_line_max)
                    {
                      bbd->has_sb = 1;
                      bbd->sb_line_min = sb_line_min;
                      bbd->sb_line_max = sb_line_max;
                    }
                }
              /* No interception, have to start new sblock.  */
              if (bbd->has_sb == 0)
                break;
            }
        }

      instrument_bblock (bbd, bb);
      bbd->is_visited = 1;
    }

  free (blocks_inverted);
  free (bb_data);
}

/* Instruments function entry and exit, if necessary.  */

static void
instrument_function (void)
{
  location_t loc;
  gimple_seq pre_func_seq;
  gimple_seq post_func_seq;
  basic_block entry_bb;
  basic_block first_bb;
  basic_block exit_bb;
  edge entry_edge;
  gimple_stmt_iterator first_gsi;
  gimple_stmt_iterator gsi;
  gimple first_stmt;
  gimple stmt;
  edge e;
  int eidx;

  pre_func_seq = 0;
  post_func_seq = 0;
  instr_func (&pre_func_seq, &post_func_seq);

  if (pre_func_seq != 0)
    {
      /* Insert new BB before the first BB.  */
      entry_bb = ENTRY_BLOCK_PTR;
      entry_edge = single_succ_edge (entry_bb);
      first_bb = entry_edge->dest;
      first_gsi = gsi_start_bb (first_bb);
      if (!gsi_end_p (first_gsi))
        {
          first_stmt = gsi_stmt (first_gsi);
          loc = gimple_location (first_stmt);
          set_location (pre_func_seq, loc);
        }
      entry_bb = split_edge (entry_edge);
      gsi = gsi_start_bb (entry_bb);
      gsi_insert_seq_after (&gsi, pre_func_seq, GSI_NEW_STMT);
    }

  if (post_func_seq != 0)
    {
      /* Find all function exits.  */
      exit_bb = EXIT_BLOCK_PTR;
      e = 0;
      for (eidx = 0; VEC_iterate (edge, exit_bb->preds, eidx, e); eidx++)
        {
           gsi = gsi_last_bb (e->src);
           stmt = gsi_stmt (gsi);
           gcc_assert (gimple_code (stmt) == GIMPLE_RETURN);
           loc = gimple_location (stmt);
           set_location (post_func_seq, loc);
           gsi_insert_seq_before (&gsi, post_func_seq, GSI_SAME_STMT);
        }
    }
}

static unsigned
tsan_pass (void)
{
  func_ignore = tsan_ignore ();
  if (func_ignore == tsan_ignore_func)
    return 0;

  func_calls = 0;
  func_mops = 0;

  instrument_mops ();
  instrument_function ();

  return 0;
}

static bool
tsan_gate (void)
{
  return flag_tsan != 0;
}

struct gimple_opt_pass pass_tsan = {{
  GIMPLE_PASS,
  "tsan",                               /* name  */
  tsan_gate,                            /* gate  */
  tsan_pass,                            /* execute  */
  NULL,                                 /* sub  */
  NULL,                                 /* next  */
  0,                                    /* static_pass_number  */
  TV_NONE,                              /* tv_id  */
  PROP_trees | PROP_cfg,                /* properties_required  */
  0,                                    /* properties_provided  */
  0,                                    /* properties_destroyed  */
  0,                                    /* todo_flags_start  */
  TODO_dump_cgraph | TODO_dump_func | TODO_verify_all
    | TODO_update_ssa | TODO_update_address_taken /* todo_flags_finish  */
}};

