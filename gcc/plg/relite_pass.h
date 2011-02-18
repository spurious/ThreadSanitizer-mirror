#ifndef RELITE_PASS_H_INCLUDED
#define RELITE_PASS_H_INCLUDED

#include <config.h>
#include <system.h>
#include <tm.h>
#include <coretypes.h>
#include <function.h>
#include <gimple.h>


typedef struct relite_context_t relite_context_t;
typedef struct rt_func_desc_t rt_func_desc_t;


typedef void            (*instrument_func)  (relite_context_t* ctx,
                                            expanded_location const* loc,
                                            gimple_stmt_iterator* gsi,
                                            tree expr);


struct rt_func_desc_t {
  char const*           rt_name;
  char const*           real_name;
  tree                  fndecl;
};


struct relite_context_t {
  int                   opt_debug;

  instrument_func       instr_enter;
  instrument_func       instr_leave;
  instrument_func       instr_store;
  instrument_func       instr_load;

  rt_func_desc_t*       rt_funcs;
  int                   rt_func_count;
  int                   rt_func_setup;

  int                   stat_func;
  int                   stat_gimple;
  int                   stat_store_total;
  int                   stat_store_instrumented;
  int                   stat_load_total;
  int                   stat_load_instrumented;
};


void                    setup_context       (relite_context_t* ctx);


void                    relite_pass         (relite_context_t* ctx,
                                             struct function* func);


void                    relite_finish       (relite_context_t* ctx);


#endif

