#include <gcc-plugin.h>
#include <plugin-version.h>
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

#define GCCTSAN_PAUSE
#define GCCTSAN_DUMP


#ifdef GCCTSAN_DUMP
# define DBG(...) ((void)(/*fprintf(stdout, "gcctsan: "),*/ \
    fprintf(stdout, __VA_ARGS__), \
    fprintf(stdout, "\n")))
#else
# define DBG(...) if (1) {} else fprintf(stdout, __VA_ARGS__)
#endif


// TODO(dvyukov) if an address of a function that needs to be intercepted
// is taken, replace it with the wrapper

// required by gcc plugin machinery
int plugin_is_GPL_compatible;


typedef struct gcctsan_context_t {
  int         fake;
} gcctsan_context_t;


int stat_func;
int stat_gimple;
int stat_store;
int stat_store_instrumented;
int stat_load;
int stat_load_instrumented;


static char const* decl_name(tree decl) {
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


typedef struct rt_func_desc_t {
  char const*                   rt_name;
  char const*                   real_name;
  tree                          fndecl;
} rt_func_desc_t;

static rt_func_desc_t rt_funcs [] = {
    {"gcctsan_enter"},
    {"gcctsan_leave"},
    {"gcctsan_store"},
    {"gcctsan_load"},
    {"gcctsan_pthread_create",          "pthread_create"},
    {"gcctsan_pthread_join",            "pthread_join"},
    {"gcctsan_pthread_mutex_init",      "pthread_mutex_init"},
    {"gcctsan_pthread_mutex_destroy",   "pthread_mutex_destroy"},
    {"gcctsan_pthread_mutex_lock",      "pthread_mutex_lock"},
    {"gcctsan_pthread_mutex_unlock",    "pthread_mutex_unlock"},
    {"gcctsan_pthread_cond_init",       "gcctsan_pthread_cond_init"},
    {"gcctsan_pthread_cond_destroy",    "gcctsan_pthread_cond_destroy"},
    {"gcctsan_pthread_cond_signal",     "gcctsan_pthread_cond_signal"},
    {"gcctsan_pthread_cond_broadcast",  "gcctsan_pthread_cond_broadcast"},
    {"gcctsan_pthread_cond_wait",       "gcctsan_pthread_cond_wait"},
    {"gcctsan_pthread_cond_timedwait",  "gcctsan_pthread_cond_timedwait"},
};


typedef enum rt_func_index {
  rt_enter,
  rt_leave,
  rt_store,
  rt_load,
  //rt_pthread_create,
  //rt_pthread_join,
  //rt_pthread_mutex_init,
  //rt_pthread_mutex_destroy,
  //rt_pthread_mutex_lock,
  //rt_pthread_mutex_unlock,
} rt_func_index;

static tree rt_func(rt_func_index idx) {
  return rt_funcs[idx].fndecl;
}

static void setup_builtin_declarations() {
  if (rt_funcs[rt_enter].fndecl != 0)
    return;
  int remain = sizeof(rt_funcs)/sizeof(rt_funcs[0]);
  tree decl = NAMESPACE_LEVEL(global_namespace)->names;
  for (; decl != 0; decl = TREE_CHAIN(decl)) {
    if (DECL_IS_BUILTIN(decl) || TREE_CODE(decl) != FUNCTION_DECL)
      continue;
    char const* name = IDENTIFIER_POINTER(DECL_NAME(decl));
    if (name == 0)
      continue;
    for (int i = 0; i != sizeof(rt_funcs)/sizeof(rt_funcs[0]); i += 1) {
      if (rt_funcs[i].fndecl == 0 && strcmp(name, rt_funcs[i].rt_name) == 0) {
        rt_funcs[i].fndecl = decl;
        remain -= 1;
        break;
      }
    }
    if (remain == 0)
      return;
  }
  DBG("can't find runtime function declarations");
  exit(1);
}


static void process_store(expanded_location const* loc, gimple_stmt_iterator* gsi, tree expr) {
  gcc_assert(loc != 0 && gsi != 0 && expr != 0);
  int is_instrumented = 0;
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

      is_instrumented = 1;
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

      is_instrumented = 1;
      break;
    }

    default: {
      reason = "unknown type";
      break;
    }
  }

  assert(is_instrumented != 0 || (reason != 0 && reason[0] != 0));

  stat_store += 1;
  if (is_instrumented) {
    stat_store_instrumented += 1;
    assert(is_gimple_addressable(expr));
    tree expr_ptr = build1(ADDR_EXPR, build_pointer_type(TREE_TYPE(expr)), expr);
    expr_ptr = convert(ptr_type_node, expr_ptr);
    gimple collect = gimple_build_call(rt_func(rt_store), 1, expr_ptr);
    gsi_insert_after(gsi, collect, GSI_NEW_STMT);
    //find_new_referenced_vars(collect);
  }

  //if (is_instrumented)
  DBG("%s:%d:%d: %s store to '%s' code=%s%s type=%s: %s %s",
      loc->file, loc->line, loc->column,
      (is_instrumented ? "" : "warning:"),
      decl_name(expr),
      tree_code_name[TREE_CODE(expr)],
      (expr_ssa ? "(ssa)" : ""),
      tree_code_name[TREE_CODE(TREE_TYPE(expr))],
      (is_instrumented ? "INSTRUMENTED" : "IGNORED"),
      reason);
}


static void process_load(expanded_location const* loc, gimple_stmt_iterator* gsi, tree expr) {
  gcc_assert(loc != 0 && gsi != 0 && expr != 0);
  int is_instrumented = 0;
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

      is_instrumented = 1;
      break;
    }

    default: {
      reason = "unknown type";
      break;
    }
  }

  assert(is_instrumented != 0 || (reason != 0 && reason[0] != 0));

  stat_load += 1;
  if (is_instrumented) {
    stat_load_instrumented += 1;
    assert(is_gimple_addressable(expr));
    tree expr_ptr = build1(ADDR_EXPR, build_pointer_type(TREE_TYPE(expr)), expr);
    expr_ptr = convert(ptr_type_node, expr_ptr);
    gimple collect = gimple_build_call(rt_func(rt_load), 1, expr_ptr);
    gsi_insert_after(gsi, collect, GSI_NEW_STMT);
    find_new_referenced_vars(collect);
    is_instrumented = 1;
  }

  //if (is_instrumented)
  DBG("%s:%d:%d: %s load of '%s' code=%s%s type=%s: %s %s",
      loc->file, loc->line, loc->column,
      (is_instrumented ? "" : "warning:"),
      decl_name(expr),
      tree_code_name[TREE_CODE(expr)],
      (expr_ssa ? "(ssa)" : ""),
      tree_code_name[TREE_CODE(TREE_TYPE(expr))],
      (is_instrumented ? "INSTRUMENTED" : "IGNORED"),
      reason);
}





static void instrument(gimple_stmt_iterator* gsi) {
  gimple stmt = gsi_stmt(*gsi);
  expanded_location loc = expand_location(gimple_location(stmt));
  //DBG("processing %s at %s:%d:%d",
  //    gimple_code_name[gimple_code(stmt)], loc.file, loc.line, loc.column);

  stat_gimple += 1;

#if 1
  if (loc.line == 3453 && strcmp(loc.file, "../../unittest/racecheck_unittest.cc") == 0) {
    DBG("HIT");
  }
#endif

  if (is_gimple_assign(stmt)) {
    for (int i = 1; i != gimple_num_ops(stmt); i += 1) {
      tree rhs = gimple_op(stmt, i);
      process_load(&loc, gsi, rhs);
    }

    tree lhs = gimple_assign_lhs(stmt);
    process_store(&loc, gsi, lhs);
  } else if (is_gimple_call (stmt)) {
    tree fndecl = gimple_call_fndecl(stmt);
    if (fndecl) {
      char const* func_name = decl_name(fndecl);
      //DBG("processing function call '%s'", func_name);
      if (func_name != 0) {
        for (int i = 0; i != sizeof(rt_funcs)/sizeof(rt_funcs[0]); i += 1) {
          if (rt_funcs[i].real_name != 0
              && strcmp(func_name, rt_funcs[i].real_name) == 0) {
            DBG("intercepting %s", func_name);
            gimple_call_set_fndecl(stmt, rt_func(i));
            break;
          }
        }
      }
    }

    tree lhs = gimple_call_lhs(stmt);
    if (lhs != 0)
      process_store(&loc, gsi, lhs);
  } /* else if (gimple_code(stmt) == GIMPLE_RETURN) {
    gimple cleanup = gimple_build_call(gcctsan_leave, 0);
    gsi_insert_before(gsi, cleanup, GSI_SAME_STMT);
  }
  */
}


static unsigned instrumentation_pass() {
  //DBG("instrumentation_pass {");
  assert(cfun != 0);

  stat_func += 1;

  /*
  char const* func_name = decl_name(cfun->decl);
  if (strcmp(func_name, "gcctsan_enter")
      * strcmp(func_name, "gcctsan_leave")
      * strcmp(func_name, "gcctsan_store")
      * strcmp(func_name, "gcctsan_load") == 0)
    return 0;
  */

  DBG(" ");
  DBG("PROCESSING FUNCTION '%s'", decl_name(cfun->decl));
  setup_builtin_declarations();

  basic_block bb;
  gimple_stmt_iterator gsi;

  //bool first = false;
  FOR_EACH_BB (bb) {
    for (gsi = gsi_start_bb (bb); !gsi_end_p (gsi); gsi_next (&gsi)) {
      /*
      if (first == false) {
        first = true;
        tree func_ptr = build1(ADDR_EXPR, build_pointer_type(TREE_TYPE(cfun->decl)), cfun->decl);
        func_ptr = convert(ptr_type_node, func_ptr);
        gimple collect = gimple_build_call(gcctsan_enter, 1, func_ptr);
        gsi_insert_before(&gsi, collect, GSI_SAME_STMT);
      }
      */
      instrument(&gsi);
    }
  }

  /*

  FOR_EACH_BB (bb) {
    for (gsi = gsi_start_bb (bb); !gsi_end_p (gsi); gsi_next (&gsi)) {
      gimple cleanup = gimple_build_call(gcctsan_leave, 0);
      gimple_seq cleanup_seq = gimple_seq_alloc_with_stmt(cleanup);
      if (gsi_one_before_end_p (gsi)) {
        gsi_insert_seq_before_without_update (&gsi,
                                              cleanup_seq,
                                              GSI_SAME_STMT);
        gsi_remove (&gsi, true);
      } else {
        gimple_seq seq = gsi_split_seq_after(gsi);
        gimple gtry = gimple_build_try (seq, cleanup_seq, GIMPLE_TRY_FINALLY);
        *gsi_stmt_ptr (&gsi) = gtry;
      }
      break;
    }
  }

  */

  //DBG("}");
  /*
  //struct function
  gimple_
  gimple cleanup = gimple_build_call(gcctsan_leave, 0);
  gimple_seq cleanup_seq = gimple_seq_alloc_with_stmt(cleanup);
  gimple leave_wrapper = gimple_build_try(cfun->gimple_body, cleanup_seq, GIMPLE_TRY_FINALLY);
  cfun->gimple_body = gimple_seq_alloc_with_stmt(leave_wrapper);

  gimple_build_wce
  */


  /*
  bb = ENTRY_BLOCK_PTR_FOR_FUNCTION(cfun);
  gsi = gsi_start_bb (bb);
  gimple collect = gimple_build_call(gcctsan_enter, 0);
  gsi_insert_after(&gsi, collect, GSI_SAME_STMT);
  */

  return 0;
}


static void process_file (void *event_data, void *data)
{
  if (errorcount || sorrycount)
    return;
  instrumentation_pass();
}



static void plugin_finish_unit (void *event_data, void *data)
{
  DBG("STATS func: %d, gimple: %d, store: %d/%d, load: %d/%d",
      stat_func, stat_gimple,
      stat_store_instrumented, stat_store,
      stat_load_instrumented, stat_load);
}




int plugin_init (struct plugin_name_args* info, struct plugin_gcc_version* ver) {
  if (!plugin_default_version_check (ver, &gcc_version))
  {
    DBG("invalid gcc version");
    return 1;
  }

#ifdef GCCTSAN_PAUSE
  DBG("attach a debugger and press ENTER");
  char buf [16];
  scanf("%1c", buf);
#endif

  //gcctsan_context_t ctx = {};

  static struct gimple_opt_pass pass_instrumentation = {{
    GIMPLE_PASS,
    "gcctsan",                            /* name */
    NULL,                                 /* gate */
    instrumentation_pass,                 /* execute */
    NULL,                                 /* sub */
    NULL,                                 /* next */
    0,                                    /* static_pass_number */
    TV_NONE,                              /* tv_id */
    PROP_ssa,                             /* properties_required */
    0,                                    /* properties_provided */
    0,                                    /* properties_destroyed */
    0,                                    /* todo_flags_start */
    TODO_verify_all | TODO_update_address_taken | TODO_update_ssa                  /* todo_flags_finish */
  }};

  struct register_pass_info pass;
  pass.pass = &pass_instrumentation.pass;
  pass.reference_pass_name = "ssa";
  pass.ref_pass_instance_number = 1;
  pass.pos_op = PASS_POS_INSERT_BEFORE;

#if 0
  register_callback(info->base_name, PLUGIN_PASS_MANAGER_SETUP, NULL, &pass);
#else
  register_callback(info->base_name, PLUGIN_ALL_PASSES_START, &process_file, 0);
#endif

  register_callback(info->base_name, PLUGIN_FINISH_UNIT,
                    &plugin_finish_unit, 0);

  return 0;
}


